/******************************************************************************
 * gntdev.c
 *
 * Device for accessing (in user-space) pages that have been granted by other
 * domains.
 *
 * Copyright (c) 2006-2007, D G Murray.
 *           (c) 2009 Gerd Hoffmann <kraxel@redhat.com>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#undef DEBUG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/mmu_notifier.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#include <xen/xen.h>
#include <xen/grant_table.h>
#include <xen/gntdev.h>
#include <asm/xen/hypervisor.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/page.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Derek G. Murray <Derek.Murray@cl.cam.ac.uk>, "
	      "Gerd Hoffmann <kraxel@redhat.com>");
MODULE_DESCRIPTION("User-space granted page access driver");

static int limit = 1024;
module_param(limit, int, 0644);
MODULE_PARM_DESC(limit, "Maximum number of grants that may be mapped at "
		"once by a gntdev instance");

struct gntdev_priv {
	struct list_head maps;
	uint32_t used;
	uint32_t limit;
	/* lock protects maps from concurrent changes */
	spinlock_t lock;
	struct mm_struct *mm;
	struct mmu_notifier mn;
};

struct grant_map {
	struct list_head next;
	struct gntdev_priv *priv;
	struct vm_area_struct *vma;
	int index;
	int count;
	int flags;
	int is_mapped;
	struct ioctl_gntdev_grant_ref *grants;
	struct gnttab_map_grant_ref   *map_ops;
	struct gnttab_unmap_grant_ref *unmap_ops;
};

/* ------------------------------------------------------------------ */

static void gntdev_print_maps(struct gntdev_priv *priv,
			      char *text, int text_index)
{
#ifdef DEBUG
	struct grant_map *map;

	pr_debug("maps list (priv %p, usage %d/%d)\n",
	       priv, priv->used, priv->limit);

	list_for_each_entry(map, &priv->maps, next)
		pr_debug("  index %2d, count %2d %s\n",
		       map->index, map->count,
		       map->index == text_index && text ? text : "");
#endif
}

static struct grant_map *gntdev_alloc_map(struct gntdev_priv *priv, int count)
{
	struct grant_map *add;

	add = kzalloc(sizeof(struct grant_map), GFP_KERNEL);
	if (NULL == add)
		return NULL;

	add->grants    = kzalloc(sizeof(add->grants[0])    * count, GFP_KERNEL);
	add->map_ops   = kzalloc(sizeof(add->map_ops[0])   * count, GFP_KERNEL);
	add->unmap_ops = kzalloc(sizeof(add->unmap_ops[0]) * count, GFP_KERNEL);
	if (NULL == add->grants  ||
	    NULL == add->map_ops ||
	    NULL == add->unmap_ops)
		goto err;

	add->index = 0;
	add->count = count;
	add->priv  = priv;

	if (add->count + priv->used > priv->limit)
		goto err;

	return add;

err:
	kfree(add->grants);
	kfree(add->map_ops);
	kfree(add->unmap_ops);
	kfree(add);
	return NULL;
}

static void gntdev_add_map(struct gntdev_priv *priv, struct grant_map *add)
{
	struct grant_map *map;

	list_for_each_entry(map, &priv->maps, next) {
		if (add->index + add->count < map->index) {
			list_add_tail(&add->next, &map->next);
			goto done;
		}
		add->index = map->index + map->count;
	}
	list_add_tail(&add->next, &priv->maps);

done:
	priv->used += add->count;
	gntdev_print_maps(priv, "[new]", add->index);
}

static struct grant_map *gntdev_find_map_index(struct gntdev_priv *priv,
		int index, int count)
{
	struct grant_map *map;

	list_for_each_entry(map, &priv->maps, next) {
		if (map->index != index)
			continue;
		if (map->count != count)
			continue;
		return map;
	}
	return NULL;
}

static struct grant_map *gntdev_find_map_vaddr(struct gntdev_priv *priv,
					       unsigned long vaddr)
{
	struct grant_map *map;

	list_for_each_entry(map, &priv->maps, next) {
		if (!map->vma)
			continue;
		if (vaddr < map->vma->vm_start)
			continue;
		if (vaddr >= map->vma->vm_end)
			continue;
		return map;
	}
	return NULL;
}

static int gntdev_del_map(struct grant_map *map)
{
	int i;

	if (map->vma)
		return -EBUSY;
	for (i = 0; i < map->count; i++)
		if (map->unmap_ops[i].handle)
			return -EBUSY;

	map->priv->used -= map->count;
	list_del(&map->next);
	return 0;
}

static void gntdev_free_map(struct grant_map *map)
{
	if (!map)
		return;
	kfree(map->grants);
	kfree(map->map_ops);
	kfree(map->unmap_ops);
	kfree(map);
}

/* ------------------------------------------------------------------ */

static int find_grant_ptes(pte_t *pte, pgtable_t token,
		unsigned long addr, void *data)
{
	struct grant_map *map = data;
	unsigned int pgnr = (addr - map->vma->vm_start) >> PAGE_SHIFT;
	u64 pte_maddr;

	BUG_ON(pgnr >= map->count);
	pte_maddr  = (u64)pfn_to_mfn(page_to_pfn(token)) << PAGE_SHIFT;
	pte_maddr += (unsigned long)pte & ~PAGE_MASK;
	gnttab_set_map_op(&map->map_ops[pgnr], pte_maddr, map->flags,
			  map->grants[pgnr].ref,
			  map->grants[pgnr].domid);
	gnttab_set_unmap_op(&map->unmap_ops[pgnr], pte_maddr, map->flags,
			    0 /* handle */);
	return 0;
}

static int map_grant_pages(struct grant_map *map)
{
	int i, err = 0;

	pr_debug("map %d+%d\n", map->index, map->count);
	err = HYPERVISOR_grant_table_op(GNTTABOP_map_grant_ref,
					map->map_ops, map->count);
	if (err)
		return err;

	for (i = 0; i < map->count; i++) {
		if (map->map_ops[i].status)
			err = -EINVAL;
		map->unmap_ops[i].handle = map->map_ops[i].handle;
	}
	return err;
}

static int unmap_grant_pages(struct grant_map *map, int offset, int pages)
{
	int i, err = 0;

	pr_debug("map %d+%d [%d+%d]\n", map->index, map->count, offset, pages);
	err = HYPERVISOR_grant_table_op(GNTTABOP_unmap_grant_ref,
					map->unmap_ops + offset, pages);
	if (err)
		return err;

	for (i = 0; i < pages; i++) {
		if (map->unmap_ops[offset+i].status)
			err = -EINVAL;
		map->unmap_ops[offset+i].handle = 0;
	}
	return err;
}

/* ------------------------------------------------------------------ */

static void gntdev_vma_close(struct vm_area_struct *vma)
{
	struct grant_map *map = vma->vm_private_data;

	pr_debug("close %p\n", vma);
	map->is_mapped = 0;
	map->vma = NULL;
	vma->vm_private_data = NULL;
}

static int gntdev_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	pr_debug("vaddr %p, pgoff %ld (shouldn't happen)\n",
			vmf->virtual_address, vmf->pgoff);
	vmf->flags = VM_FAULT_ERROR;
	return 0;
}

static struct vm_operations_struct gntdev_vmops = {
	.close = gntdev_vma_close,
	.fault = gntdev_vma_fault,
};

/* ------------------------------------------------------------------ */

static void mn_invl_range_start(struct mmu_notifier *mn,
				struct mm_struct *mm,
				unsigned long start, unsigned long end)
{
	struct gntdev_priv *priv = container_of(mn, struct gntdev_priv, mn);
	struct grant_map *map;
	unsigned long mstart, mend;
	int err;

	spin_lock(&priv->lock);
	list_for_each_entry(map, &priv->maps, next) {
		if (!map->vma)
			continue;
		if (!map->is_mapped)
			continue;
		if (map->vma->vm_start >= end)
			continue;
		if (map->vma->vm_end <= start)
			continue;
		mstart = max(start, map->vma->vm_start);
		mend   = min(end,   map->vma->vm_end);
		pr_debug("map %d+%d (%lx %lx), range %lx %lx, mrange %lx %lx\n",
				map->index, map->count,
				map->vma->vm_start, map->vma->vm_end,
				start, end, mstart, mend);
		err = unmap_grant_pages(map,
					(mstart - map->vma->vm_start) >> PAGE_SHIFT,
					(mend - mstart) >> PAGE_SHIFT);
		WARN_ON(err);
	}
	spin_unlock(&priv->lock);
}

static void mn_invl_page(struct mmu_notifier *mn,
			 struct mm_struct *mm,
			 unsigned long address)
{
	mn_invl_range_start(mn, mm, address, address + PAGE_SIZE);
}

static void mn_release(struct mmu_notifier *mn,
		       struct mm_struct *mm)
{
	struct gntdev_priv *priv = container_of(mn, struct gntdev_priv, mn);
	struct grant_map *map;
	int err;

	spin_lock(&priv->lock);
	list_for_each_entry(map, &priv->maps, next) {
		if (!map->vma)
			continue;
		pr_debug("map %d+%d (%lx %lx)\n",
				map->index, map->count,
				map->vma->vm_start, map->vma->vm_end);
		err = unmap_grant_pages(map, /* offset */ 0, map->count);
		WARN_ON(err);
	}
	spin_unlock(&priv->lock);
}

struct mmu_notifier_ops gntdev_mmu_ops = {
	.release                = mn_release,
	.invalidate_page        = mn_invl_page,
	.invalidate_range_start = mn_invl_range_start,
};

/* ------------------------------------------------------------------ */

static int gntdev_open(struct inode *inode, struct file *flip)
{
	struct gntdev_priv *priv;
	int ret = 0;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	INIT_LIST_HEAD(&priv->maps);
	spin_lock_init(&priv->lock);
	priv->limit = limit;

	priv->mm = get_task_mm(current);
	if (!priv->mm) {
		kfree(priv);
		return -ENOMEM;
	}
	priv->mn.ops = &gntdev_mmu_ops;
	ret = mmu_notifier_register(&priv->mn, priv->mm);
	mmput(priv->mm);

	if (ret) {
		kfree(priv);
		return ret;
	}

	flip->private_data = priv;
	pr_debug("priv %p\n", priv);

	return 0;
}

static int gntdev_release(struct inode *inode, struct file *flip)
{
	struct gntdev_priv *priv = flip->private_data;
	struct grant_map *map;
	int err;

	pr_debug("priv %p\n", priv);

	spin_lock(&priv->lock);
	while (!list_empty(&priv->maps)) {
		map = list_entry(priv->maps.next, struct grant_map, next);
		err = gntdev_del_map(map);
		if (WARN_ON(err))
			gntdev_free_map(map);

	}
	spin_unlock(&priv->lock);

	mmu_notifier_unregister(&priv->mn, priv->mm);
	kfree(priv);
	return 0;
}

static long gntdev_ioctl_map_grant_ref(struct gntdev_priv *priv,
				       struct ioctl_gntdev_map_grant_ref __user *u)
{
	struct ioctl_gntdev_map_grant_ref op;
	struct grant_map *map;
	int err;

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;
	pr_debug("priv %p, add %d\n", priv, op.count);
	if (unlikely(op.count <= 0))
		return -EINVAL;
	if (unlikely(op.count > priv->limit))
		return -EINVAL;

	err = -ENOMEM;
	map = gntdev_alloc_map(priv, op.count);
	if (!map)
		return err;
	if (copy_from_user(map->grants, &u->refs,
			   sizeof(map->grants[0]) * op.count) != 0) {
		gntdev_free_map(map);
		return err;
	}

	spin_lock(&priv->lock);
	gntdev_add_map(priv, map);
	op.index = map->index << PAGE_SHIFT;
	spin_unlock(&priv->lock);

	if (copy_to_user(u, &op, sizeof(op)) != 0) {
		spin_lock(&priv->lock);
		gntdev_del_map(map);
		spin_unlock(&priv->lock);
		gntdev_free_map(map);
		return err;
	}
	return 0;
}

static long gntdev_ioctl_unmap_grant_ref(struct gntdev_priv *priv,
					 struct ioctl_gntdev_unmap_grant_ref __user *u)
{
	struct ioctl_gntdev_unmap_grant_ref op;
	struct grant_map *map;
	int err = -ENOENT;

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;
	pr_debug("priv %p, del %d+%d\n", priv, (int)op.index, (int)op.count);

	spin_lock(&priv->lock);
	map = gntdev_find_map_index(priv, op.index >> PAGE_SHIFT, op.count);
	if (map)
		err = gntdev_del_map(map);
	spin_unlock(&priv->lock);
	if (!err)
		gntdev_free_map(map);
	return err;
}

static long gntdev_ioctl_get_offset_for_vaddr(struct gntdev_priv *priv,
					      struct ioctl_gntdev_get_offset_for_vaddr __user *u)
{
	struct ioctl_gntdev_get_offset_for_vaddr op;
	struct grant_map *map;

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;
	pr_debug("priv %p, offset for vaddr %lx\n", priv, (unsigned long)op.vaddr);

	spin_lock(&priv->lock);
	map = gntdev_find_map_vaddr(priv, op.vaddr);
	if (map == NULL ||
	    map->vma->vm_start != op.vaddr) {
		spin_unlock(&priv->lock);
		return -EINVAL;
	}
	op.offset = map->index << PAGE_SHIFT;
	op.count = map->count;
	spin_unlock(&priv->lock);

	if (copy_to_user(u, &op, sizeof(op)) != 0)
		return -EFAULT;
	return 0;
}

static long gntdev_ioctl_set_max_grants(struct gntdev_priv *priv,
					struct ioctl_gntdev_set_max_grants __user *u)
{
	struct ioctl_gntdev_set_max_grants op;

	if (copy_from_user(&op, u, sizeof(op)) != 0)
		return -EFAULT;
	pr_debug("priv %p, limit %d\n", priv, op.count);
	if (op.count > limit)
		return -E2BIG;

	spin_lock(&priv->lock);
	priv->limit = op.count;
	spin_unlock(&priv->lock);
	return 0;
}

static long gntdev_ioctl(struct file *flip,
			 unsigned int cmd, unsigned long arg)
{
	struct gntdev_priv *priv = flip->private_data;
	void __user *ptr = (void __user *)arg;

	switch (cmd) {
	case IOCTL_GNTDEV_MAP_GRANT_REF:
		return gntdev_ioctl_map_grant_ref(priv, ptr);

	case IOCTL_GNTDEV_UNMAP_GRANT_REF:
		return gntdev_ioctl_unmap_grant_ref(priv, ptr);

	case IOCTL_GNTDEV_GET_OFFSET_FOR_VADDR:
		return gntdev_ioctl_get_offset_for_vaddr(priv, ptr);

	case IOCTL_GNTDEV_SET_MAX_GRANTS:
		return gntdev_ioctl_set_max_grants(priv, ptr);

	default:
		pr_debug("priv %p, unknown cmd %x\n", priv, cmd);
		return -ENOIOCTLCMD;
	}

	return 0;
}

static int gntdev_mmap(struct file *flip, struct vm_area_struct *vma)
{
	struct gntdev_priv *priv = flip->private_data;
	int index = vma->vm_pgoff;
	int count = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;
	struct grant_map *map;
	int err = -EINVAL;

	if ((vma->vm_flags & VM_WRITE) && !(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	pr_debug("map %d+%d at %lx (pgoff %lx)\n",
			index, count, vma->vm_start, vma->vm_pgoff);

	spin_lock(&priv->lock);
	map = gntdev_find_map_index(priv, index, count);
	if (!map)
		goto unlock_out;
	if (map->vma)
		goto unlock_out;
	if (priv->mm != vma->vm_mm) {
		printk(KERN_WARNING "Huh? Other mm?\n");
		goto unlock_out;
	}

	vma->vm_ops = &gntdev_vmops;

	vma->vm_flags |= VM_RESERVED|VM_DONTCOPY|VM_DONTEXPAND|VM_PFNMAP;

	vma->vm_private_data = map;
	map->vma = vma;

	map->flags = GNTMAP_host_map | GNTMAP_application_map | GNTMAP_contains_pte;
	if (!(vma->vm_flags & VM_WRITE))
		map->flags |= GNTMAP_readonly;

	err = apply_to_page_range(vma->vm_mm, vma->vm_start,
				  vma->vm_end - vma->vm_start,
				  find_grant_ptes, map);
	if (err) {
		printk(KERN_WARNING "find_grant_ptes() failure.\n");
		goto unlock_out;
	}

	err = map_grant_pages(map);
	if (err) {
		printk(KERN_WARNING "map_grant_pages() failure.\n");
		goto unlock_out;
	}
	map->is_mapped = 1;

unlock_out:
	spin_unlock(&priv->lock);
	return err;
}

static const struct file_operations gntdev_fops = {
	.owner = THIS_MODULE,
	.open = gntdev_open,
	.release = gntdev_release,
	.mmap = gntdev_mmap,
	.unlocked_ioctl = gntdev_ioctl
};

static struct miscdevice gntdev_miscdev = {
	.minor        = MISC_DYNAMIC_MINOR,
	.name         = "xen/gntdev",
	.fops         = &gntdev_fops,
};

/* ------------------------------------------------------------------ */

static int __init gntdev_init(void)
{
	int err;

	if (!xen_domain())
		return -ENODEV;

	err = misc_register(&gntdev_miscdev);
	if (err != 0) {
		printk(KERN_ERR "Could not register gntdev device\n");
		return err;
	}
	return 0;
}

static void __exit gntdev_exit(void)
{
	misc_deregister(&gntdev_miscdev);
}

module_init(gntdev_init);
module_exit(gntdev_exit);

/* ------------------------------------------------------------------ */
