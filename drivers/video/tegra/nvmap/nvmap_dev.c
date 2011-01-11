/*
 * drivers/video/tegra/nvmap/nvmap_dev.c
 *
 * User-space interface to nvmap
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/backing-dev.h>
#include <linux/bitmap.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/oom.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

#include <mach/iovmm.h>
#include <mach/nvmap.h>

#include "nvmap.h"
#include "nvmap_ioctl.h"
#include "nvmap_mru.h"

#define NVMAP_NUM_PTES		64
#define NVMAP_CARVEOUT_KILLER_RETRY_TIME 100 /* msecs */

struct nvmap_carveout_node {
	unsigned int		heap_bit;
	struct nvmap_heap	*carveout;
	int			index;
	struct list_head	clients;
	spinlock_t 		clients_lock;
};

struct nvmap_device {
	struct vm_struct *vm_rgn;
	pte_t		*ptes[NVMAP_NUM_PTES];
	unsigned long	ptebits[NVMAP_NUM_PTES / BITS_PER_LONG];
	unsigned int	lastpte;
	spinlock_t	ptelock;

	struct rb_root	handles;
	spinlock_t	handle_lock;
	wait_queue_head_t pte_wait;
	struct miscdevice dev_super;
	struct miscdevice dev_user;
	struct nvmap_carveout_node *heaps;
	int nr_carveouts;
	struct nvmap_share iovmm_master;
};

struct nvmap_device *nvmap_dev;

static struct backing_dev_info nvmap_bdi = {
	.ra_pages	= 0,
	.capabilities	= (BDI_CAP_NO_ACCT_AND_WRITEBACK |
			   BDI_CAP_READ_MAP | BDI_CAP_WRITE_MAP),
};

static int nvmap_open(struct inode *inode, struct file *filp);
static int nvmap_release(struct inode *inode, struct file *filp);
static long nvmap_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static int nvmap_map(struct file *filp, struct vm_area_struct *vma);
static void nvmap_vma_open(struct vm_area_struct *vma);
static void nvmap_vma_close(struct vm_area_struct *vma);
static int nvmap_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf);

static const struct file_operations nvmap_user_fops = {
	.owner		= THIS_MODULE,
	.open		= nvmap_open,
	.release	= nvmap_release,
	.unlocked_ioctl	= nvmap_ioctl,
	.mmap		= nvmap_map,
};

static const struct file_operations nvmap_super_fops = {
	.owner		= THIS_MODULE,
	.open		= nvmap_open,
	.release	= nvmap_release,
	.unlocked_ioctl	= nvmap_ioctl,
	.mmap		= nvmap_map,
};

static struct vm_operations_struct nvmap_vma_ops = {
	.open		= nvmap_vma_open,
	.close		= nvmap_vma_close,
	.fault		= nvmap_vma_fault,
};

int is_nvmap_vma(struct vm_area_struct *vma)
{
	return vma->vm_ops == &nvmap_vma_ops;
}

struct device *nvmap_client_to_device(struct nvmap_client *client)
{
	if (client->super)
		return client->dev->dev_super.this_device;
	else
		return client->dev->dev_user.this_device;
}

struct nvmap_share *nvmap_get_share_from_dev(struct nvmap_device *dev)
{
	return &dev->iovmm_master;
}

/* allocates a PTE for the caller's use; returns the PTE pointer or
 * a negative errno. may be called from IRQs */
pte_t **nvmap_alloc_pte_irq(struct nvmap_device *dev, void **vaddr)
{
	unsigned long flags;
	unsigned long bit;

	spin_lock_irqsave(&dev->ptelock, flags);
	bit = find_next_zero_bit(dev->ptebits, NVMAP_NUM_PTES, dev->lastpte);
	if (bit == NVMAP_NUM_PTES) {
		bit = find_first_zero_bit(dev->ptebits, dev->lastpte);
		if (bit == dev->lastpte)
			bit = NVMAP_NUM_PTES;
	}

	if (bit == NVMAP_NUM_PTES) {
		spin_unlock_irqrestore(&dev->ptelock, flags);
		return ERR_PTR(-ENOMEM);
	}

	dev->lastpte = bit;
	set_bit(bit, dev->ptebits);
	spin_unlock_irqrestore(&dev->ptelock, flags);

	*vaddr = dev->vm_rgn->addr + bit * PAGE_SIZE;
	return &(dev->ptes[bit]);
}

/* allocates a PTE for the caller's use; returns the PTE pointer or
 * a negative errno. must be called from sleepable contexts */
pte_t **nvmap_alloc_pte(struct nvmap_device *dev, void **vaddr)
{
	int ret;
	pte_t **pte;
	ret = wait_event_interruptible(dev->pte_wait,
			!IS_ERR(pte = nvmap_alloc_pte_irq(dev, vaddr)));

	if (ret == -ERESTARTSYS)
		return ERR_PTR(-EINTR);

	return pte;
}

/* frees a PTE */
void nvmap_free_pte(struct nvmap_device *dev, pte_t **pte)
{
	unsigned long addr;
	unsigned int bit = pte - dev->ptes;
	unsigned long flags;

	if (WARN_ON(bit >= NVMAP_NUM_PTES))
		return;

	addr = (unsigned long)dev->vm_rgn->addr + bit * PAGE_SIZE;
	set_pte_at(&init_mm, addr, *pte, 0);

	spin_lock_irqsave(&dev->ptelock, flags);
	clear_bit(bit, dev->ptebits);
	spin_unlock_irqrestore(&dev->ptelock, flags);
	wake_up(&dev->pte_wait);
}

/* verifies that the handle ref value "ref" is a valid handle ref for the
 * file. caller must hold the file's ref_lock prior to calling this function */
struct nvmap_handle_ref *_nvmap_validate_id_locked(struct nvmap_client *c,
						   unsigned long id)
{
	struct rb_node *n = c->handle_refs.rb_node;

	while (n) {
		struct nvmap_handle_ref *ref;
		ref = rb_entry(n, struct nvmap_handle_ref, node);
		if ((unsigned long)ref->handle == id)
			return ref;
		else if (id > (unsigned long)ref->handle)
			n = n->rb_right;
		else
			n = n->rb_left;
	}

	return NULL;
}

struct nvmap_handle *nvmap_get_handle_id(struct nvmap_client *client,
					 unsigned long id)
{
	struct nvmap_handle_ref *ref;
	struct nvmap_handle *h = NULL;

	nvmap_ref_lock(client);
	ref = _nvmap_validate_id_locked(client, id);
	if (ref)
		h = ref->handle;
	if (h)
		h = nvmap_handle_get(h);
	nvmap_ref_unlock(client);
	return h;
}

unsigned long nvmap_carveout_usage(struct nvmap_client *c,
				   struct nvmap_heap_block *b)
{
	struct nvmap_heap *h = nvmap_block_to_heap(b);
	struct nvmap_carveout_node *n;
	int i;

	for (i = 0; i < c->dev->nr_carveouts; i++) {
		n = &c->dev->heaps[i];
		if (n->carveout == h)
			return n->heap_bit;
	}
	return 0;
}

static int nvmap_flush_heap_block(struct nvmap_client *client,
				  struct nvmap_heap_block *block, size_t len)
{
	pte_t **pte;
	void *addr;
	unsigned long kaddr;
	unsigned long phys = block->base;
	unsigned long end = block->base + len;

	pte = nvmap_alloc_pte(client->dev, &addr);
	if (IS_ERR(pte))
		return PTR_ERR(pte);

	kaddr = (unsigned long)addr;

	while (phys < end) {
		unsigned long next = (phys + PAGE_SIZE) & PAGE_MASK;
		unsigned long pfn = __phys_to_pfn(phys);
		void *base = (void *)kaddr + (phys & ~PAGE_MASK);

		next = min(next, end);
		set_pte_at(&init_mm, kaddr, *pte, pfn_pte(pfn, pgprot_kernel));
		flush_tlb_kernel_page(kaddr);
		__cpuc_flush_dcache_area(base, next - phys);
		phys = next;
	}

	outer_flush_range(block->base, block->base + len);

	nvmap_free_pte(client->dev, pte);
	return 0;
}

void nvmap_carveout_commit_add(struct nvmap_client *client,
			       struct nvmap_carveout_node *node,
			       size_t len)
{
	unsigned long flags;

	nvmap_ref_lock(client);
	spin_lock_irqsave(&node->clients_lock, flags);
	BUG_ON(list_empty(&client->carveout_commit[node->index].list) &&
	       client->carveout_commit[node->index].commit != 0);

	client->carveout_commit[node->index].commit += len;
	/* if this client isn't already on the list of nodes for this heap,
	   add it */
	if (list_empty(&client->carveout_commit[node->index].list)) {
		list_add(&client->carveout_commit[node->index].list,
			 &node->clients);
	}
	spin_unlock_irqrestore(&node->clients_lock, flags);
	nvmap_ref_unlock(client);
}

void nvmap_carveout_commit_subtract(struct nvmap_client *client,
				    struct nvmap_carveout_node *node,
				    size_t len)
{
	unsigned long flags;

	if (!client)
		return;

	spin_lock_irqsave(&node->clients_lock, flags);
	client->carveout_commit[node->index].commit -= len;
	BUG_ON(client->carveout_commit[node->index].commit < 0);
	/* if no more allocation in this carveout for this node, delete it */
	if (!client->carveout_commit[node->index].commit)
		list_del_init(&client->carveout_commit[node->index].list);
	spin_unlock_irqrestore(&node->clients_lock, flags);
}

static struct nvmap_client* get_client_from_carveout_commit(
	struct nvmap_carveout_node *node, struct nvmap_carveout_commit *commit)
{
	struct nvmap_carveout_commit *first_commit = commit - node->index;
	return (void *)first_commit - offsetof(struct nvmap_client,
					       carveout_commit);
}

#ifdef CONFIG_NVMAP_CARVEOUT_KILLER
bool nvmap_shrink_carveout(struct nvmap_carveout_node *node)
{
	struct nvmap_carveout_commit *commit;
	size_t selected_size = 0;
	int selected_oom_adj = OOM_ADJUST_MIN;
	struct task_struct *selected_task = NULL;
	unsigned long flags;
	bool death_pending = false;

	spin_lock_irqsave(&node->clients_lock, flags);
	/* find the task with the smallest oom_adj (lowest priority)
	 * and largest carveout allocation -- ignore kernel allocations,
	 * there's no way to handle them */
	list_for_each_entry(commit, &node->clients, list) {
		struct nvmap_client *client =
			get_client_from_carveout_commit(node, commit);
		size_t size = commit->commit;
		struct task_struct *task = client->task;
		struct signal_struct *sig = task->signal;

		if (!task->mm || !sig)
			continue;
		if (sig->oom_adj < selected_oom_adj)
			continue;
		if (sig->oom_adj == selected_oom_adj &&
		    size <= selected_size)
			continue;
		selected_oom_adj = sig->oom_adj;
		selected_size = size;
		selected_task = task;
	}
	if (selected_task) {
		death_pending = selected_task == current;
		if (fatal_signal_pending(selected_task)) {
			pr_warning("carveout_killer: process %d dying "
				   "slowly\n", selected_task->pid);
			goto out;
		}
		pr_info("carveout_killer: killing process %d with oom_adj %d "
			"to reclaim %d\n", selected_task->pid, selected_oom_adj,
			selected_size);
		force_sig(SIGKILL, selected_task);
	}
out:
	spin_unlock_irqrestore(&node->clients_lock, flags);
	return death_pending;
}
#endif

struct nvmap_heap_block *do_nvmap_carveout_alloc(struct nvmap_client *client,
						 size_t len, size_t align,
						 unsigned long usage,
						 unsigned int prot)
{
	struct nvmap_carveout_node *co_heap;
	struct nvmap_device *dev = client->dev;
	int i;

	for (i = 0; i < dev->nr_carveouts; i++) {
		struct nvmap_heap_block *block;
		co_heap = &dev->heaps[i];

		if (!(co_heap->heap_bit & usage))
			continue;

		block = nvmap_heap_alloc(co_heap->carveout, len, align, prot);
		if (block) {
			/* flush any stale data that may be left in the
			 * cache at the block's address, since the new
			 * block may be mapped uncached */
			if (nvmap_flush_heap_block(client, block, len)) {
				nvmap_heap_free(block);
				return NULL;
			} else
				return block;
		}
	}
	return NULL;
}

struct nvmap_heap_block *nvmap_carveout_alloc(struct nvmap_client *client,
					      size_t len, size_t align,
					      unsigned long usage,
					      unsigned int prot)
{
	struct nvmap_heap_block *block;
#ifdef CONFIG_NVMAP_CARVEOUT_KILLER
	struct nvmap_carveout_node *co_heap;
	struct nvmap_device *dev = client->dev;
	int i;
	unsigned long end = jiffies +
		msecs_to_jiffies(NVMAP_CARVEOUT_KILLER_RETRY_TIME);

	do {
		block = do_nvmap_carveout_alloc(client, len, align, usage,
						prot);
		if (block)
			return block;

		/* shrink carveouts that matter and try again */
		for (i = 0; i < dev->nr_carveouts; i++) {
			co_heap = &dev->heaps[i];

			if (!(co_heap->heap_bit & usage))
				continue;

			/* indicates we just delivered a sigkill to current,
			   might as well stop trying so the process can exit */
			if (nvmap_shrink_carveout(co_heap))
				return NULL;
		}
		yield();
	} while (time_is_after_jiffies(end));

	return NULL;
#else
	block = do_nvmap_carveout_alloc(client, len, align, usage, prot);
	return block;
#endif
}

/* remove a handle from the device's tree of all handles; called
 * when freeing handles. */
int nvmap_handle_remove(struct nvmap_device *dev, struct nvmap_handle *h)
{
	spin_lock(&dev->handle_lock);

	/* re-test inside the spinlock if the handle really has no clients;
	 * only remove the handle if it is unreferenced */
	if (atomic_add_return(0, &h->ref) > 0) {
		spin_unlock(&dev->handle_lock);
		return -EBUSY;
	}
	smp_rmb();
	BUG_ON(atomic_read(&h->ref) < 0);
	BUG_ON(atomic_read(&h->pin) != 0);

	rb_erase(&h->node, &dev->handles);

	spin_unlock(&dev->handle_lock);
	return 0;
}

/* adds a newly-created handle to the device master tree */
void nvmap_handle_add(struct nvmap_device *dev, struct nvmap_handle *h)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;

	spin_lock(&dev->handle_lock);
	p = &dev->handles.rb_node;
	while (*p) {
		struct nvmap_handle *b;

		parent = *p;
		b = rb_entry(parent, struct nvmap_handle, node);
		if (h > b)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}
	rb_link_node(&h->node, parent, p);
	rb_insert_color(&h->node, &dev->handles);
	spin_unlock(&dev->handle_lock);
}

/* validates that a handle is in the device master tree, and that the
 * client has permission to access it */
struct nvmap_handle *nvmap_validate_get(struct nvmap_client *client,
					unsigned long id)
{
	struct nvmap_handle *h = NULL;
	struct rb_node *n;

	spin_lock(&client->dev->handle_lock);

	n = client->dev->handles.rb_node;

	while (n) {
		h = rb_entry(n, struct nvmap_handle, node);
		if ((unsigned long)h == id) {
			if (client->super || h->global || (h->owner == client))
				h = nvmap_handle_get(h);
			else
				h = NULL;
			spin_unlock(&client->dev->handle_lock);
			return h;
		}
		if (id > (unsigned long)h)
			n = n->rb_right;
		else
			n = n->rb_left;
	}
	spin_unlock(&client->dev->handle_lock);
	return NULL;
}

struct nvmap_client *nvmap_create_client(struct nvmap_device *dev,
					 const char *name)
{
	struct nvmap_client *client;
	int i;

	if (WARN_ON(!dev))
		return NULL;

	client = kzalloc(sizeof(*client) + (sizeof(struct nvmap_carveout_commit)
			 * dev->nr_carveouts), GFP_KERNEL);
	if (!client)
		return NULL;

	client->name = name;
	client->super = true;
	client->dev = dev;
	/* TODO: allocate unique IOVMM client for each nvmap client */
	client->share = &dev->iovmm_master;
	client->handle_refs = RB_ROOT;

	atomic_set(&client->iovm_commit, 0);

	client->iovm_limit = nvmap_mru_vm_size(client->share->iovmm);

	for (i = 0; i < dev->nr_carveouts; i++) {
		INIT_LIST_HEAD(&client->carveout_commit[i].list);
		client->carveout_commit[i].commit = 0;
	}

	get_task_struct(current);
	client->task = current;

	spin_lock_init(&client->ref_lock);
	atomic_set(&client->count, 1);

	return client;
}

static void destroy_client(struct nvmap_client *client)
{
	struct rb_node *n;
	int i;

	if (!client)
		return;

	while ((n = rb_first(&client->handle_refs))) {
		struct nvmap_handle_ref *ref;
		int pins, dupes;

		ref = rb_entry(n, struct nvmap_handle_ref, node);
		rb_erase(&ref->node, &client->handle_refs);

		smp_rmb();
		pins = atomic_read(&ref->pin);

		mutex_lock(&ref->handle->lock);
		if (ref->handle->owner == client)
		    ref->handle->owner = NULL;
		mutex_unlock(&ref->handle->lock);

		while (pins--)
			nvmap_unpin_handles(client, &ref->handle, 1);

		dupes = atomic_read(&ref->dupes);
		while (dupes--)
			nvmap_handle_put(ref->handle);

		kfree(ref);
	}

	for (i = 0; i < client->dev->nr_carveouts; i++)
		list_del(&client->carveout_commit[i].list);

	if (client->task)
		put_task_struct(client->task);

	kfree(client);
}

struct nvmap_client *nvmap_client_get(struct nvmap_client *client)
{
	if (WARN_ON(!client))
		return NULL;

	if (WARN_ON(!atomic_add_unless(&client->count, 1, 0)))
		return NULL;

	return client;
}

struct nvmap_client *nvmap_client_get_file(int fd)
{
	struct nvmap_client *client = ERR_PTR(-EFAULT);
	struct file *f = fget(fd);
	if (!f)
		return ERR_PTR(-EINVAL);

	if ((f->f_op == &nvmap_user_fops) || (f->f_op == &nvmap_super_fops)) {
		client = f->private_data;
		atomic_inc(&client->count);
	}

	fput(f);
	return client;
}

void nvmap_client_put(struct nvmap_client *client)
{
	if (!client)
		return;

	if (!atomic_dec_return(&client->count))
		destroy_client(client);
}

static int nvmap_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *miscdev = filp->private_data;
	struct nvmap_device *dev = dev_get_drvdata(miscdev->parent);
	struct nvmap_client *priv;
	int ret;

	ret = nonseekable_open(inode, filp);
	if (unlikely(ret))
		return ret;

	BUG_ON(dev != nvmap_dev);
	priv = nvmap_create_client(dev, "user");
	if (!priv)
		return -ENOMEM;

	priv->super = (filp->f_op == &nvmap_super_fops);

	filp->f_mapping->backing_dev_info = &nvmap_bdi;

	filp->private_data = priv;
	return 0;
}

static int nvmap_release(struct inode *inode, struct file *filp)
{
	nvmap_client_put(filp->private_data);
	return 0;
}

static int nvmap_map(struct file *filp, struct vm_area_struct *vma)
{
	struct nvmap_vma_priv *priv;

	/* after NVMAP_IOC_MMAP, the handle that is mapped by this VMA
	 * will be stored in vm_private_data and faulted in. until the
	 * ioctl is made, the VMA is mapped no-access */
	vma->vm_private_data = NULL;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->offs = 0;
	priv->handle = NULL;
	atomic_set(&priv->count, 1);

	vma->vm_flags |= VM_SHARED;
	vma->vm_flags |= (VM_IO | VM_DONTEXPAND | VM_MIXEDMAP | VM_RESERVED);
	vma->vm_ops = &nvmap_vma_ops;
	vma->vm_private_data = priv;

	return 0;
}

static long nvmap_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int err = 0;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != NVMAP_IOC_MAGIC)
		return -ENOTTY;

	if (_IOC_NR(cmd) > NVMAP_IOC_MAXNR)
		return -ENOTTY;

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, uarg, _IOC_SIZE(cmd));
	if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ, uarg, _IOC_SIZE(cmd));

	if (err)
		return -EFAULT;

	switch (cmd) {
	case NVMAP_IOC_CLAIM:
		nvmap_warn(filp->private_data, "preserved handles not"
			   "supported\n");
		err = -ENODEV;
		break;
	case NVMAP_IOC_CREATE:
	case NVMAP_IOC_FROM_ID:
		err = nvmap_ioctl_create(filp, cmd, uarg);
		break;

	case NVMAP_IOC_GET_ID:
		err = nvmap_ioctl_getid(filp, uarg);
		break;

	case NVMAP_IOC_PARAM:
		err = nvmap_ioctl_get_param(filp, uarg);
		break;

	case NVMAP_IOC_UNPIN_MULT:
	case NVMAP_IOC_PIN_MULT:
		err = nvmap_ioctl_pinop(filp, cmd == NVMAP_IOC_PIN_MULT, uarg);
		break;

	case NVMAP_IOC_ALLOC:
		err = nvmap_ioctl_alloc(filp, uarg);
		break;

	case NVMAP_IOC_FREE:
		err = nvmap_ioctl_free(filp, arg);
		break;

	case NVMAP_IOC_MMAP:
		err = nvmap_map_into_caller_ptr(filp, uarg);
		break;

	case NVMAP_IOC_WRITE:
	case NVMAP_IOC_READ:
		err = nvmap_ioctl_rw_handle(filp, cmd == NVMAP_IOC_READ, uarg);
		break;

	case NVMAP_IOC_CACHE:
		err = nvmap_ioctl_cache_maint(filp, uarg);
		break;

	default:
		return -ENOTTY;
	}
	return err;
}

/* to ensure that the backing store for the VMA isn't freed while a fork'd
 * reference still exists, nvmap_vma_open increments the reference count on
 * the handle, and nvmap_vma_close decrements it. alternatively, we could
 * disallow copying of the vma, or behave like pmem and zap the pages. FIXME.
*/
static void nvmap_vma_open(struct vm_area_struct *vma)
{
	struct nvmap_vma_priv *priv;

	priv = vma->vm_private_data;

	BUG_ON(!priv);

	atomic_inc(&priv->count);
}

static void nvmap_vma_close(struct vm_area_struct *vma)
{
	struct nvmap_vma_priv *priv = vma->vm_private_data;

	if (priv && !atomic_dec_return(&priv->count)) {
		if (priv->handle)
			nvmap_handle_put(priv->handle);
		kfree(priv);
	}

	vma->vm_private_data = NULL;
}

static int nvmap_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct nvmap_vma_priv *priv;
	unsigned long offs;

	offs = (unsigned long)(vmf->virtual_address - vma->vm_start);
	priv = vma->vm_private_data;
	if (!priv || !priv->handle || !priv->handle->alloc)
		return VM_FAULT_SIGBUS;

	offs += priv->offs;
	/* if the VMA was split for some reason, vm_pgoff will be the VMA's
	 * offset from the original VMA */
	offs += (vma->vm_pgoff << PAGE_SHIFT);

	if (offs >= priv->handle->size)
		return VM_FAULT_SIGBUS;

	if (!priv->handle->heap_pgalloc) {
		unsigned long pfn;
		BUG_ON(priv->handle->carveout->base & ~PAGE_MASK);
		pfn = ((priv->handle->carveout->base + offs) >> PAGE_SHIFT);
		vm_insert_pfn(vma, (unsigned long)vmf->virtual_address, pfn);
		return VM_FAULT_NOPAGE;
	} else {
		struct page *page;
		offs >>= PAGE_SHIFT;
		page = priv->handle->pgalloc.pages[offs];
		if (page)
			get_page(page);
		vmf->page = page;
		return (page) ? 0 : VM_FAULT_SIGBUS;
	}
}

static ssize_t attr_show_usage(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct nvmap_carveout_node *node = nvmap_heap_device_to_arg(dev);

	return sprintf(buf, "%08x\n", node->heap_bit);
}

static struct device_attribute heap_attr_show_usage =
	__ATTR(usage, S_IRUGO, attr_show_usage, NULL);

static struct attribute *heap_extra_attrs[] = {
	&heap_attr_show_usage.attr,
	NULL,
};

static struct attribute_group heap_extra_attr_group = {
	.attrs = heap_extra_attrs,
};

static void client_stringify(struct nvmap_client *client, struct seq_file *s)
{
	char task_comm[sizeof(client->task->comm)];
	get_task_comm(task_comm, client->task);
	seq_printf(s, "%8s %16s %8u", client->name, task_comm,
		   client->task->pid);
}

static void allocations_stringify(struct nvmap_client *client,
				  struct seq_file *s)
{
	struct rb_node *n = rb_first(&client->handle_refs);
	unsigned long long total = 0;

	for (; n != NULL; n = rb_next(n)) {
		struct nvmap_handle_ref *ref =
			rb_entry(n, struct nvmap_handle_ref, node);
		struct nvmap_handle *handle = ref->handle;
		if (handle->alloc && !handle->heap_pgalloc) {
			seq_printf(s, " %8u@%8lx ", handle->size,
				   handle->carveout->base);
			total += handle->size;
		}
	}
	seq_printf(s, " total: %llu\n", total);
}

static int nvmap_debug_allocations_show(struct seq_file *s, void *unused)
{
	struct nvmap_carveout_node *node = s->private;
	struct nvmap_carveout_commit *commit;
	unsigned long flags;

	spin_lock_irqsave(&node->clients_lock, flags);
	list_for_each_entry(commit, &node->clients, list) {
		struct nvmap_client *client =
			get_client_from_carveout_commit(node, commit);
		client_stringify(client, s);
		allocations_stringify(client, s);
	}
	spin_unlock_irqrestore(&node->clients_lock, flags);

	return 0;
}

static int nvmap_debug_allocations_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvmap_debug_allocations_show,
			   inode->i_private);
}

static struct file_operations debug_allocations_fops = {
	.open = nvmap_debug_allocations_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int nvmap_debug_clients_show(struct seq_file *s, void *unused)
{
	struct nvmap_carveout_node *node = s->private;
	struct nvmap_carveout_commit *commit;
	unsigned long flags;

	spin_lock_irqsave(&node->clients_lock, flags);
	list_for_each_entry(commit, &node->clients, list) {
		struct nvmap_client *client =
			get_client_from_carveout_commit(node, commit);
		client_stringify(client, s);
		seq_printf(s, " %8u\n", commit->commit);
	}
	spin_unlock_irqrestore(&node->clients_lock, flags);

	return 0;
}

static int nvmap_debug_clients_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvmap_debug_clients_show, inode->i_private);
}

static struct file_operations debug_clients_fops = {
	.open = nvmap_debug_clients_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int nvmap_probe(struct platform_device *pdev)
{
	struct nvmap_platform_data *plat = pdev->dev.platform_data;
	struct nvmap_device *dev;
	struct dentry *nvmap_debug_root;
	unsigned int i;
	int e;

	if (!plat) {
		dev_err(&pdev->dev, "no platform data?\n");
		return -ENODEV;
	}

	if (WARN_ON(nvmap_dev != NULL)) {
		dev_err(&pdev->dev, "only one nvmap device may be present\n");
		return -ENODEV;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&pdev->dev, "out of memory for device\n");
		return -ENOMEM;
	}

	dev->dev_user.minor = MISC_DYNAMIC_MINOR;
	dev->dev_user.name = "nvmap";
	dev->dev_user.fops = &nvmap_user_fops;
	dev->dev_user.parent = &pdev->dev;

	dev->dev_super.minor = MISC_DYNAMIC_MINOR;
	dev->dev_super.name = "knvmap";
	dev->dev_super.fops = &nvmap_super_fops;
	dev->dev_super.parent = &pdev->dev;

	dev->handles = RB_ROOT;

	init_waitqueue_head(&dev->pte_wait);

	init_waitqueue_head(&dev->iovmm_master.pin_wait);
	mutex_init(&dev->iovmm_master.pin_lock);
	dev->iovmm_master.iovmm =
		tegra_iovmm_alloc_client(dev_name(&pdev->dev), NULL);
	if (IS_ERR(dev->iovmm_master.iovmm)) {
		e = PTR_ERR(dev->iovmm_master.iovmm);
		dev_err(&pdev->dev, "couldn't create iovmm client\n");
		goto fail;
	}
	dev->vm_rgn = alloc_vm_area(NVMAP_NUM_PTES * PAGE_SIZE);
	if (!dev->vm_rgn) {
		e = -ENOMEM;
		dev_err(&pdev->dev, "couldn't allocate remapping region\n");
		goto fail;
	}
	e = nvmap_mru_init(&dev->iovmm_master);
	if (e) {
		dev_err(&pdev->dev, "couldn't initialize MRU lists\n");
		goto fail;
	}

	spin_lock_init(&dev->ptelock);
	spin_lock_init(&dev->handle_lock);

	for (i = 0; i < NVMAP_NUM_PTES; i++) {
		unsigned long addr;
		pgd_t *pgd;
		pud_t *pud;
		pmd_t *pmd;

		addr = (unsigned long)dev->vm_rgn->addr + (i * PAGE_SIZE);
		pgd = pgd_offset_k(addr);
		pud = pud_alloc(&init_mm, pgd, addr);
		if (!pud) {
			e = -ENOMEM;
			dev_err(&pdev->dev, "couldn't allocate page tables\n");
			goto fail;
		}
		pmd = pmd_alloc(&init_mm, pud, addr);
		if (!pmd) {
			e = -ENOMEM;
			dev_err(&pdev->dev, "couldn't allocate page tables\n");
			goto fail;
		}
		dev->ptes[i] = pte_alloc_kernel(pmd, addr);
		if (!dev->ptes[i]) {
			e = -ENOMEM;
			dev_err(&pdev->dev, "couldn't allocate page tables\n");
			goto fail;
		}
	}

	e = misc_register(&dev->dev_user);
	if (e) {
		dev_err(&pdev->dev, "unable to register miscdevice %s\n",
			dev->dev_user.name);
		goto fail;
	}

	e = misc_register(&dev->dev_super);
	if (e) {
		dev_err(&pdev->dev, "unable to register miscdevice %s\n",
			dev->dev_super.name);
		goto fail;
	}

	dev->nr_carveouts = 0;
	dev->heaps = kzalloc(sizeof(struct nvmap_carveout_node) *
			     plat->nr_carveouts, GFP_KERNEL);
	if (!dev->heaps) {
		e = -ENOMEM;
		dev_err(&pdev->dev, "couldn't allocate carveout memory\n");
		goto fail;
	}

	nvmap_debug_root = debugfs_create_dir("nvmap", NULL);
	if (IS_ERR_OR_NULL(nvmap_debug_root))
		dev_err(&pdev->dev, "couldn't create debug files\n");

	for (i = 0; i < plat->nr_carveouts; i++) {
		struct nvmap_carveout_node *node = &dev->heaps[i];
		const struct nvmap_platform_carveout *co = &plat->carveouts[i];
		node->carveout = nvmap_heap_create(dev->dev_user.this_device,
				   co->name, co->base, co->size,
				   co->buddy_size, node);
		if (!node->carveout) {
			e = -ENOMEM;
			dev_err(&pdev->dev, "couldn't create %s\n", co->name);
			goto fail_heaps;
		}
		dev->nr_carveouts++;
		spin_lock_init(&node->clients_lock);
		node->index = i;
		INIT_LIST_HEAD(&node->clients);
		node->heap_bit = co->usage_mask;
		if (nvmap_heap_create_group(node->carveout,
					    &heap_extra_attr_group))
			dev_warn(&pdev->dev, "couldn't add extra attributes\n");

		dev_info(&pdev->dev, "created carveout %s (%uKiB)\n",
			 co->name, co->size / 1024);

		if (!IS_ERR_OR_NULL(nvmap_debug_root)) {
			struct dentry *heap_root =
				debugfs_create_dir(co->name, nvmap_debug_root);
			if (!IS_ERR_OR_NULL(heap_root)) {
				debugfs_create_file("clients", 0664, heap_root,
				    node, &debug_clients_fops);
				debugfs_create_file("allocations", 0664,
				    heap_root, node, &debug_allocations_fops);
			}
		}
	}

	platform_set_drvdata(pdev, dev);
	nvmap_dev = dev;
	return 0;
fail_heaps:
	for (i = 0; i < dev->nr_carveouts; i++) {
		struct nvmap_carveout_node *node = &dev->heaps[i];
		nvmap_heap_remove_group(node->carveout, &heap_extra_attr_group);
		nvmap_heap_destroy(node->carveout);
	}
fail:
	kfree(dev->heaps);
	nvmap_mru_destroy(&dev->iovmm_master);
	if (dev->dev_super.minor != MISC_DYNAMIC_MINOR)
		misc_deregister(&dev->dev_super);
	if (dev->dev_user.minor != MISC_DYNAMIC_MINOR)
		misc_deregister(&dev->dev_user);
	if (!IS_ERR_OR_NULL(dev->iovmm_master.iovmm))
		tegra_iovmm_free_client(dev->iovmm_master.iovmm);
	if (dev->vm_rgn)
		free_vm_area(dev->vm_rgn);
	kfree(dev);
	nvmap_dev = NULL;
	return e;
}

static int nvmap_remove(struct platform_device *pdev)
{
	struct nvmap_device *dev = platform_get_drvdata(pdev);
	struct rb_node *n;
	struct nvmap_handle *h;
	int i;

	misc_deregister(&dev->dev_super);
	misc_deregister(&dev->dev_user);

	while ((n = rb_first(&dev->handles))) {
		h = rb_entry(n, struct nvmap_handle, node);
		rb_erase(&h->node, &dev->handles);
		kfree(h);
	}

	if (!IS_ERR_OR_NULL(dev->iovmm_master.iovmm))
		tegra_iovmm_free_client(dev->iovmm_master.iovmm);

	nvmap_mru_destroy(&dev->iovmm_master);

	for (i = 0; i < dev->nr_carveouts; i++) {
		struct nvmap_carveout_node *node = &dev->heaps[i];
		nvmap_heap_remove_group(node->carveout, &heap_extra_attr_group);
		nvmap_heap_destroy(node->carveout);
	}
	kfree(dev->heaps);

	free_vm_area(dev->vm_rgn);
	kfree(dev);
	nvmap_dev = NULL;
	return 0;
}

static int nvmap_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}

static int nvmap_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver nvmap_driver = {
	.probe		= nvmap_probe,
	.remove		= nvmap_remove,
	.suspend	= nvmap_suspend,
	.resume		= nvmap_resume,

	.driver = {
		.name	= "tegra-nvmap",
		.owner	= THIS_MODULE,
	},
};

static int __init nvmap_init_driver(void)
{
	int e;

	nvmap_dev = NULL;

	e = nvmap_heap_init();
	if (e)
		goto fail;

	e = platform_driver_register(&nvmap_driver);
	if (e) {
		nvmap_heap_deinit();
		goto fail;
	}

fail:
	return e;
}
fs_initcall(nvmap_init_driver);

static void __exit nvmap_exit_driver(void)
{
	platform_driver_unregister(&nvmap_driver);
	nvmap_heap_deinit();
	nvmap_dev = NULL;
}
module_exit(nvmap_exit_driver);
