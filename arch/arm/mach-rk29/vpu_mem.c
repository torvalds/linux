/* arch/arm/mach-rk29/vpu_mem.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/debugfs.h>
#include <linux/mempolicy.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>

#include <mach/vpu_mem.h>


#define VPU_MEM_MAX_ORDER 128
#define VPU_MEM_MIN_ALLOC PAGE_SIZE

#define VPU_MEM_DEBUG 1

#define VPU_MEM_BITMAP_ERR              (-1)
#define VPU_MEM_ERR_FREE_REFN_ERR       (-5)

struct vpu_mem_data {
	/* protects this data field, if the mm_mmap sem will be held at the
	 * same time as this sem, the mm sem must be taken first (as this is
	 * the order for vma_open and vma_close ops */
	struct rw_semaphore sem;
	/* process id of teh mapping process */
	pid_t pid;
	/* a list of currently available regions if this is a suballocation */
	struct list_head region_list;
	/* a linked list of data so we can access them for debugging */
	struct list_head list;
};

struct vpu_mem_bits {
	unsigned short pfn;             /* page frame number - vpu_mem space max 256M */
	signed   refrn:7;               /* reference number */
	unsigned first:1;               /* 1 if first, 0 if not first */
	signed   avail:7;               /* available link number */
	unsigned allocated:1;           /* 1 if allocated, 0 if free */
};

struct vpu_mem_region {
	unsigned long offset;
	unsigned long len;
};

struct vpu_mem_region_node {
	struct vpu_mem_region region;
	struct list_head list;
};

#define VPU_MEM_DEBUG_MSGS 0
#if VPU_MEM_DEBUG_MSGS
#define DLOG(fmt,args...) \
	do { printk(KERN_INFO "[%s:%s:%d] "fmt, __FILE__, __func__, __LINE__, \
		    ##args); } \
	while (0)
#else
#define DLOG(x...) do {} while (0)
#endif

struct vpu_mem_info {
	struct miscdevice dev;
	/* physical start address of the remaped vpu_mem space */
	unsigned long base;
	/* vitual start address of the remaped vpu_mem space */
	unsigned char __iomem *vbase;
	/* total size of the vpu_mem space */
	unsigned long size;
	/* number of entries in the vpu_mem space */
	unsigned long num_entries;
	/* indicates maps of this region should be cached, if a mix of
	 * cached and uncached is desired, set this and open the device with
	 * O_SYNC to get an uncached region */
	unsigned cached;
	unsigned buffered;

    /* no data_list is needed and no master mode is needed */
	/* for debugging, creates a list of vpu_mem file structs, the
	 * data_list_sem should be taken before vpu_mem_data->sem if both are
	 * needed */
	struct semaphore data_list_sem;
	struct list_head data_list;

	/* the bitmap for the region indicating which entries are allocated
	 * and which are free */
	struct vpu_mem_bits *bitmap;
	/* vpu_mem_sem protects the bitmap array
	 * a write lock should be held when modifying entries in bitmap
	 * a read lock should be held when reading data from bits or
	 * dereferencing a pointer into bitmap
	 *
	 * vpu_mem_data->sem protects the vpu_mem data of a particular file
	 * Many of the function that require the vpu_mem_data->sem have a non-
	 * locking version for when the caller is already holding that sem.
	 *
	 * IF YOU TAKE BOTH LOCKS TAKE THEM IN THIS ORDER:
	 * down(vpu_mem_data->sem) => down(bitmap_sem)
	 */
	struct rw_semaphore bitmap_sem;
};

static struct vpu_mem_info vpu_mem;
static int vpu_mem_count;

#define VPU_MEM_IS_FREE(index) !(vpu_mem.bitmap[index].allocated)
#define VPU_MEM_IS_FIRST(index) (vpu_mem.bitmap[index].first)
#define VPU_MEM_BIT(index) &vpu_mem.bitmap[index]
#define VPU_MEM_PFN(index) vpu_mem.bitmap[index].pfn
#define VPU_MEM_NEXT_INDEX(index) (index + VPU_MEM_PFN(index))
#define VPU_MEM_OFFSET(index) (index * VPU_MEM_MIN_ALLOC)
#define VPU_MEM_START_ADDR(index) (VPU_MEM_OFFSET(index) + vpu_mem.base)
#define VPU_MEM_SIZE(index) ((VPU_MEM_PFN(index)) * VPU_MEM_MIN_ALLOC)
#define VPU_MEM_END_ADDR(index) (VPU_MEM_START_ADDR(index) + VPU_MEM_SIZE(index))
#define VPU_MEM_START_VADDR(index) (VPU_MEM_OFFSET(index) + vpu_mem.vbase)
#define VPU_MEM_END_VADDR(index) (VPU_MEM_START_VADDR(index) + VPU_MEM_SIZE(index))
#define VPU_MEM_IS_PAGE_ALIGNED(addr) (!((addr) & (~PAGE_MASK)))

static int vpu_mem_release(struct inode *, struct file *);
static int vpu_mem_mmap(struct file *, struct vm_area_struct *);
static int vpu_mem_open(struct inode *, struct file *);
static long vpu_mem_ioctl(struct file *, unsigned int, unsigned long);

struct file_operations vpu_mem_fops = {
	.open = vpu_mem_open,
    .mmap = vpu_mem_mmap,
    .unlocked_ioctl = vpu_mem_ioctl,
	.release = vpu_mem_release,
};

int is_vpu_mem_file(struct file *file)
{
	if (unlikely(!file || !file->f_dentry || !file->f_dentry->d_inode))
		return 0;
	if (unlikely(file->f_dentry->d_inode->i_rdev !=
	     MKDEV(MISC_MAJOR, vpu_mem.dev.minor)))
		return 0;
	return 1;
}


static void vpu_mem_get_region(struct vpu_mem_data *data,
                               struct vpu_mem_region_node *region_node,
                               int index, int pfn)
{
    int curr, next = index + pfn;
    struct vpu_mem_bits *pbits;

    if (VPU_MEM_IS_FREE(next)) {
        pbits        = VPU_MEM_BIT(next);
        pbits->first = 1;
        pbits->pfn   = VPU_MEM_PFN(index) - pfn;
    } else {
        if (!VPU_MEM_IS_FIRST(next))
            DLOG("something wrong when get_region pfn %d at index %d\n", pfn, index);
    }

    pbits = VPU_MEM_BIT(index);

    pbits->first = 1;
    pbits->pfn = pfn;
    pbits->refrn++;
    pbits->avail++;

    for (curr = 0; curr < pfn; curr++)
        pbits[curr].allocated = 1;

    region_node->region.offset = index;
    region_node->region.len = pfn;

    down_write(&data->sem);
    list_add(&region_node->list, &data->region_list);
    up_write(&data->sem);

    return ;
}

static int vpu_mem_put_region_by_index(struct vpu_mem_data *data, int index)
{
    struct vpu_mem_bits *pbits = VPU_MEM_BIT(index);
    pbits->refrn--;
    pbits->avail--;

    if (!pbits->avail)
    {
        int i;
        for (i = 0; i < pbits->pfn; i++)
            pbits[i].allocated = 0;

        down_write(&data->sem);
        {
            struct vpu_mem_region_node *region_node;
            struct list_head *elt, *elt2;
            list_for_each_safe(elt, elt2, &data->region_list) {
                region_node = list_entry(elt, struct vpu_mem_region_node, list);
                if (region_node->region.offset == index)
                {
                    if (pbits->pfn != region_node->region.len)
                        DLOG("something wrong when put_region at index %d\n", index);
                    list_del(elt);
                    kfree(region_node);
                    break;
                }
            }
        }
        up_write(&data->sem);
    }
    return 0;
}

static int vpu_mem_put_region_by_region(struct vpu_mem_region_node *region_node)
{
    int index = region_node->region.offset;
    struct vpu_mem_bits *pbits = VPU_MEM_BIT(index);
    pbits->refrn--;
    pbits->avail--;

    if (!pbits->avail)
    {
        int i;
        for (i = 0; i < pbits->pfn; i++)
            pbits[i].allocated = 0;

        list_del(&region_node->list);
        kfree(region_node);
    }

    return 0;
}

static long vpu_mem_allocate(struct file *file, unsigned int len)
{
    /* caller should hold the write lock on vpu_mem_sem! */
	/* return the corresponding pdata[] entry */
	int curr = 0;
	int end = vpu_mem.num_entries;
	int best_fit = -1;
	unsigned int pfn = (len + VPU_MEM_MIN_ALLOC - 1)/VPU_MEM_MIN_ALLOC;
    struct vpu_mem_data *data = (struct vpu_mem_data *)file->private_data;
    struct vpu_mem_region_node *region_node;

    if (!is_vpu_mem_file(file)) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "allocate vpu_mem data from invalid file.\n");
#endif
        return -ENODEV;
    }

	DLOG("vpu_mem_allocate pfn %x\n", pfn);

    region_node = kmalloc(sizeof(struct vpu_mem_region_node),
              GFP_KERNEL);
    if (!region_node) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "No space to allocate metadata!");
#endif
        return -ENOMEM;
    }

	/* look through the bitmap:
	 * 	if you find a free slot of the correct order use it
	 * 	otherwise, use the best fit (smallest with size > order) slot
	 */
	while (curr < end) {
		if (VPU_MEM_IS_FREE(curr)) {
			if (VPU_MEM_PFN(curr) >= (unsigned char)pfn) {
				/* set the not free bit and clear others */
				best_fit = curr;
                printk("find fit size at index %d\n", curr);
				break;
			}
		}
		curr = VPU_MEM_NEXT_INDEX(curr);
	}

	/* if best_fit < 0, there are no suitable slots,
	 * return an error
	 */
	if (best_fit < 0) {
		printk("vpu_mem: no space left to allocate!\n");
		return -1;
	}

	DLOG("best_fit: %d next: %u\n", best_fit, best_fit + pfn);

    vpu_mem_get_region(data, region_node, best_fit, pfn);

	return best_fit;
}

static int vpu_mem_free(struct file *file, int index)
{
    /* caller should hold the write lock on vpu_mem_sem! */
    struct vpu_mem_bits *pbits = VPU_MEM_BIT(index);
    struct vpu_mem_data *data = (struct vpu_mem_data *)file->private_data;

    if (!is_vpu_mem_file(file)) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "free vpu_mem data from invalid file.\n");
#endif
        return -ENODEV;
    }

	DLOG("free index %d\n", index);

    if ((!pbits->first) ||
        (!pbits->allocated) ||
        ((pbits->refrn - 1) < 0) ||
        ((pbits->avail - 1) < 0))
    {
        DLOG("VPM ERR: found error in vpu_mem_free :\nvpu_mem.bitmap[%d].first %d, allocated %d, avail %d, refrn %d\n",
            index, pbits->first, pbits->allocated, pbits->avail, pbits->refrn);
        return VPU_MEM_BITMAP_ERR;
    }

	return vpu_mem_put_region_by_index(data, index);
}

static long vpu_mem_duplicate(struct file *file, int index)
{
	/* caller should hold the write lock on vpu_mem_sem! */
    struct vpu_mem_bits *pbits = VPU_MEM_BIT(index);

    if (!is_vpu_mem_file(file)) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "duplicate vpu_mem data from invalid file.\n");
#endif
        return -ENODEV;
    }

	DLOG("duplicate index %d\n", index);

    if ((!pbits->first) ||
        (!pbits->allocated) ||
        (!pbits->avail))
    {
        DLOG("VPM ERR: found error in vpu_mem_duplicate :\nvpu_mem.bitmap[%d].first %d, allocated %d, avail %d, refrn %d\n",
            index, pbits->first, pbits->allocated, pbits->avail, pbits->refrn);
        return VPU_MEM_BITMAP_ERR;
    }

    pbits->avail++;

	return 0;
}

static long vpu_mem_link(struct file *file, int index)
{
    struct vpu_mem_bits *pbits = VPU_MEM_BIT(index);
    struct vpu_mem_data *data = (struct vpu_mem_data *)file->private_data;
    struct vpu_mem_region_node *region_node;

	if (!is_vpu_mem_file(file)) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "link vpu_mem data from invalid file.\n");
#endif
        return -ENODEV;
	}

    region_node = kmalloc(sizeof(struct vpu_mem_region_node),
              GFP_KERNEL);
    if (!region_node) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "No space to allocate metadata!");
#endif
        return -ENOMEM;
    }

	/* caller should hold the write lock on vpu_mem_sem! */
	DLOG("link index %d\n", index);

    if ((!pbits->first) ||
        (!pbits->allocated) ||
        (!pbits->avail) ||
        (pbits->avail <= pbits->refrn))
    {
        DLOG("VPM ERR: found error in vpu_mem_duplicate :\nvpu_mem.bitmap[%d].first %d, allocated %d, avail %d, refrn %d\n",
            index, pbits->first, pbits->allocated, pbits->avail, pbits->refrn);
        return VPU_MEM_BITMAP_ERR;
    }

    pbits->refrn++;

    region_node->region.offset = index;
    region_node->region.len = pbits->pfn;

    down_write(&data->sem);
    list_add(&region_node->list, &data->region_list);
    up_write(&data->sem);

	return 0;
}

void vpu_mem_flush(struct file *file, long index)
{
	struct vpu_mem_data *data;
	void *flush_start, *flush_end;

	if (!is_vpu_mem_file(file)) {
		return;
	}

	data = (struct vpu_mem_data *)file->private_data;
	if (!vpu_mem.cached || file->f_flags & O_SYNC)
		return;

	down_read(&data->sem);
    flush_start = VPU_MEM_START_VADDR(index);
    flush_end   = VPU_MEM_END_VADDR(index);
    dmac_flush_range(flush_start, flush_end);
	up_read(&data->sem);
}

static pgprot_t phys_mem_access_prot(struct file *file, pgprot_t vma_prot)
{
#ifdef pgprot_noncached
	if (vpu_mem.cached == 0 || file->f_flags & O_SYNC)
		return pgprot_noncached(vma_prot);
#endif
#ifdef pgprot_ext_buffered
	else if (vpu_mem.buffered)
		return pgprot_ext_buffered(vma_prot);
#endif
	return vma_prot;
}

static int vpu_mem_map_pfn_range(struct vm_area_struct *vma, unsigned long len)
{
	DLOG("map len %lx\n", len);
	BUG_ON(!VPU_MEM_IS_PAGE_ALIGNED(vma->vm_start));
	BUG_ON(!VPU_MEM_IS_PAGE_ALIGNED(vma->vm_end));
	BUG_ON(!VPU_MEM_IS_PAGE_ALIGNED(len));
	if (io_remap_pfn_range(vma, vma->vm_start,
		vpu_mem.base >> PAGE_SHIFT,
		len, vma->vm_page_prot)) {
		return -EAGAIN;
	}
	return 0;
}

static int vpu_mem_open(struct inode *inode, struct file *file)
{
	struct vpu_mem_data *data;
	int ret = 0;

	DLOG("current %u file %p(%d)\n", current->pid, file, (int)file_count(file));
	/* setup file->private_data to indicate its unmapped */
	/*  you can only open a vpu_mem device one time */
	if (file->private_data != NULL)
		return -1;
	data = kmalloc(sizeof(struct vpu_mem_data), GFP_KERNEL);
	if (!data) {
		printk("vpu_mem: unable to allocate memory for vpu_mem metadata.");
		return -1;
	}
	data->pid = 0;

	INIT_LIST_HEAD(&data->region_list);
	init_rwsem(&data->sem);

	file->private_data = data;
	INIT_LIST_HEAD(&data->list);

	down(&vpu_mem.data_list_sem);
	list_add(&data->list, &vpu_mem.data_list);
	up(&vpu_mem.data_list_sem);
	return ret;
}

static int vpu_mem_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct vpu_mem_data *data;
	unsigned long vma_size =  vma->vm_end - vma->vm_start;
	int ret = 0;

	if (vma->vm_pgoff || !VPU_MEM_IS_PAGE_ALIGNED(vma_size)) {
#if VPU_MEM_DEBUG
		printk(KERN_ERR "vpu_mem: mmaps must be at offset zero, aligned"
				" and a multiple of pages_size.\n");
#endif
		return -EINVAL;
	}

	data = (struct vpu_mem_data *)file->private_data;

    printk(KERN_ALERT "file->private_data : 0x%x\n", (unsigned int)data);

	down_write(&data->sem);

    /* assert: vma_size must be the total size of the vpu_mem */
	if (vpu_mem.size != vma_size) {
#if VPU_MEM_DEBUG
		printk(KERN_WARNING "vpu_mem: mmap size [%lu] does not match"
		       "size of backing region [%lu].\n", vma_size, vpu_mem.size);
#endif
		ret = -EINVAL;
		goto error;
	}

	vma->vm_pgoff = vpu_mem.base >> PAGE_SHIFT;
	vma->vm_page_prot = phys_mem_access_prot(file, vma->vm_page_prot);

	if (vpu_mem_map_pfn_range(vma, vma_size)) {
		printk(KERN_INFO "vpu_mem: mmap failed in kernel!\n");
		ret = -EAGAIN;
		goto error;
	}

	data->pid = current->pid;

error:
	up_write(&data->sem);
	return ret;
}

static int vpu_mem_release(struct inode *inode, struct file *file)
{
	struct vpu_mem_data *data = (struct vpu_mem_data *)file->private_data;
	struct list_head *elt, *elt2;

	down(&vpu_mem.data_list_sem);
	list_del(&data->list);
	up(&vpu_mem.data_list_sem);

	down_write(&data->sem);
	file->private_data = NULL;
	list_for_each_safe(elt, elt2, &data->region_list) {
		struct vpu_mem_region_node *region_node = list_entry(elt, struct vpu_mem_region_node, list);
        vpu_mem_put_region_by_region(region_node);
	}
	BUG_ON(!list_empty(&data->region_list));
	up_write(&data->sem);
	kfree(data);

	return 0;
}

static long vpu_mem_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    long index, ret = 0;

	switch (cmd) {
	case VPU_MEM_GET_PHYS:
		DLOG("get_phys\n");
		printk(KERN_INFO "vpu_mem: request for physical address of vpu_mem region "
				"from process %d.\n", current->pid);
		if (copy_to_user((void __user *)arg, &vpu_mem.base, sizeof(vpu_mem.base)))
			return -EFAULT;
		break;
	case VPU_MEM_GET_TOTAL_SIZE:
		DLOG("get total size\n");
		if (copy_to_user((void __user *)arg, &vpu_mem.size, sizeof(vpu_mem.size)))
			return -EFAULT;
		break;
    case VPU_MEM_ALLOCATE:
		DLOG("allocate\n");
        {
            unsigned int size;
            if (copy_from_user(&size, (void __user *)arg, sizeof(size)))
                return -EFAULT;
            down_write(&vpu_mem.bitmap_sem);
            index = vpu_mem_allocate(file, size);
            up_write(&vpu_mem.bitmap_sem);
            DLOG("allocate at index %ld\n", index);
            return index;
            break;
        }
    case VPU_MEM_FREE:
        {
            DLOG("mem free\n");
            if (copy_from_user(&index, (void __user *)arg, sizeof(index)))
                return -EFAULT;
            if (index >= vpu_mem.size)
                return -EACCES;
            down_write(&vpu_mem.bitmap_sem);
            ret = vpu_mem_free(file, index);
            up_write(&vpu_mem.bitmap_sem);
            return ret;
            break;
        }
	case VPU_MEM_CACHE_FLUSH:
		{
			DLOG("flush\n");
			if (copy_from_user(&index, (void __user *)arg, sizeof(index)))
				return -EFAULT;

            down_write(&vpu_mem.bitmap_sem);
			vpu_mem_flush(file, index);
            up_write(&vpu_mem.bitmap_sem);
			break;
		}
	case VPU_MEM_DUPLICATE:
		{
			DLOG("duplicate\n");
			if (copy_from_user(&index, (void __user *)arg, sizeof(index)))
				return -EFAULT;
            down_write(&vpu_mem.bitmap_sem);
			ret = vpu_mem_duplicate(file, index);
            up_write(&vpu_mem.bitmap_sem);
            return ret;
			break;
		}
	case VPU_MEM_LINK:
		{
			DLOG("link\n");
			if (copy_from_user(&index, (void __user *)arg, sizeof(index)))
				return -EFAULT;
            down_write(&vpu_mem.bitmap_sem);
			ret = vpu_mem_link(file, index);
            up_write(&vpu_mem.bitmap_sem);
			break;
		}
	case VPU_MEM_MAP:
        DLOG("map\n");
		break;
	case VPU_MEM_CONNECT:
		DLOG("connect\n");
		break;
    case VPU_MEM_GET_SIZE:
        DLOG("get_size\n");
        break;
    case VPU_MEM_UNMAP:
        DLOG("unmap\n");
        break;
	default:
		return -EINVAL;
	}
	return ret;
}

#if VPU_MEM_DEBUG
static ssize_t debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t debug_read(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos)
{
	struct list_head *elt, *elt2;
	struct vpu_mem_data *data;
	struct vpu_mem_region_node *region_node;
	const int debug_bufmax = 4096;
	static char buffer[4096];
	int n = 0;

	DLOG("debug open\n");
	n = scnprintf(buffer, debug_bufmax,
		      "pid #: mapped regions (offset, len) (offset,len)...\n");

	down(&vpu_mem.data_list_sem);
	list_for_each(elt, &vpu_mem.data_list) {
		data = list_entry(elt, struct vpu_mem_data, list);
		down_read(&data->sem);
		n += scnprintf(buffer + n, debug_bufmax - n, "pid %u:",
				data->pid);
		list_for_each(elt2, &data->region_list) {
			region_node = list_entry(elt2, struct vpu_mem_region_node,
				      list);
			n += scnprintf(buffer + n, debug_bufmax - n,
					"(%lx,%lx) ",
					region_node->region.offset,
					region_node->region.len);
		}
		n += scnprintf(buffer + n, debug_bufmax - n, "\n");
		up_read(&data->sem);
	}
	up(&vpu_mem.data_list_sem);

	n++;
	buffer[n] = 0;
	return simple_read_from_buffer(buf, count, ppos, buffer, n);
}

static struct file_operations debug_fops = {
	.read = debug_read,
	.open = debug_open,
};
#endif

int vpu_mem_setup(struct vpu_mem_platform_data *pdata)
{
	int err = 0;

    if (vpu_mem_count)
    {
		printk(KERN_ALERT "Only one vpu_mem driver can be register!\n");
        goto err_cant_register_device;
    }

    memset(&vpu_mem, 0, sizeof(struct vpu_mem_info));

	vpu_mem.cached = pdata->cached;
	vpu_mem.buffered = pdata->buffered;
	vpu_mem.base = pdata->start;
	vpu_mem.size = pdata->size;
	init_rwsem(&vpu_mem.bitmap_sem);
	init_MUTEX(&vpu_mem.data_list_sem);
	INIT_LIST_HEAD(&vpu_mem.data_list);
	vpu_mem.dev.name = pdata->name;
	vpu_mem.dev.minor = MISC_DYNAMIC_MINOR;
	vpu_mem.dev.fops = &vpu_mem_fops;

	err = misc_register(&vpu_mem.dev);
	if (err) {
		printk(KERN_ALERT "Unable to register vpu_mem driver!\n");
		goto err_cant_register_device;
	}
	printk(KERN_ALERT "%s: %d init\n", pdata->name, vpu_mem.dev.minor);
	vpu_mem_count++;

	vpu_mem.num_entries = vpu_mem.size / VPU_MEM_MIN_ALLOC;
	vpu_mem.bitmap = kmalloc(vpu_mem.num_entries *
				  sizeof(struct vpu_mem_bits), GFP_KERNEL);
	if (!vpu_mem.bitmap)
		goto err_no_mem_for_metadata;

	memset(vpu_mem.bitmap, 0, sizeof(struct vpu_mem_bits) *
					  vpu_mem.num_entries);

    /* record the total page number */
    vpu_mem.bitmap[0].pfn = vpu_mem.num_entries;

	if (vpu_mem.cached)
		vpu_mem.vbase = ioremap_cached(vpu_mem.base,
						vpu_mem.size);
#ifdef ioremap_ext_buffered
	else if (vpu_mem.buffered)
		vpu_mem.vbase = ioremap_ext_buffered(vpu_mem.base,
						      vpu_mem.size);
#endif
	else
		vpu_mem.vbase = ioremap(vpu_mem.base, vpu_mem.size);

	if (vpu_mem.vbase == 0)
		goto error_cant_remap;

#if VPU_MEM_DEBUG
	debugfs_create_file(pdata->name, S_IFREG | S_IRUGO, NULL, (void *)vpu_mem.dev.minor,
			    &debug_fops);
#endif
	return 0;
error_cant_remap:
	kfree(vpu_mem.bitmap);
err_no_mem_for_metadata:
	misc_deregister(&vpu_mem.dev);
err_cant_register_device:
	return -1;
}

static int vpu_mem_probe(struct platform_device *pdev)
{
	struct vpu_mem_platform_data *pdata;

	if (!pdev || !pdev->dev.platform_data) {
		printk(KERN_ALERT "Unable to probe vpu_mem!\n");
		return -1;
	}
	pdata = pdev->dev.platform_data;
	return vpu_mem_setup(pdata);
}

static int vpu_mem_remove(struct platform_device *pdev)
{
	if (!pdev || !pdev->dev.platform_data) {
		printk(KERN_ALERT "Unable to remove vpu_mem!\n");
		return -1;
	}
    if (vpu_mem_count) {
	    misc_deregister(&vpu_mem.dev);
        vpu_mem_count--;
    } else {
		printk(KERN_ALERT "no vpu_mem to remove!\n");
    }
	return 0;
}

static struct platform_driver vpu_mem_driver = {
	.probe  = vpu_mem_probe,
	.remove = vpu_mem_remove,
	.driver = { .name = "vpu_mem" }
};


static int __init vpu_mem_init(void)
{
	return platform_driver_register(&vpu_mem_driver);
}

static void __exit vpu_mem_exit(void)
{
	platform_driver_unregister(&vpu_mem_driver);
}

module_init(vpu_mem_init);
module_exit(vpu_mem_exit);


