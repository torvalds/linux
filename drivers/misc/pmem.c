/* drivers/android/pmem.c
 *
 * Copyright (C) 2007 Google, Inc.
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
#include <linux/android_pmem.h>
#include <linux/mempolicy.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>

#define PMEM_MAX_DEVICES 10
#define PMEM_MAX_ORDER 128
#define PMEM_MIN_ALLOC PAGE_SIZE

#define PMEM_DEBUG 1

/* indicates that a refernce to this file has been taken via get_pmem_file,
 * the file should not be released until put_pmem_file is called */
#define PMEM_FLAGS_BUSY 0x1
/* indicates that this is a suballocation of a larger master range */
#define PMEM_FLAGS_CONNECTED 0x1 << 1
/* indicates this is a master and not a sub allocation and that it is mmaped */
#define PMEM_FLAGS_MASTERMAP 0x1 << 2
/* submap and unsubmap flags indicate:
 * 00: subregion has never been mmaped
 * 10: subregion has been mmaped, reference to the mm was taken
 * 11: subretion has ben released, refernece to the mm still held
 * 01: subretion has been released, reference to the mm has been released
 */
#define PMEM_FLAGS_SUBMAP 0x1 << 3
#define PMEM_FLAGS_UNSUBMAP 0x1 << 4


struct pmem_data {
	/* in alloc mode: an index into the bitmap
	 * in no_alloc mode: the size of the allocation */
	int index;
	/* see flags above for descriptions */
	unsigned int flags;
	/* protects this data field, if the mm_mmap sem will be held at the
	 * same time as this sem, the mm sem must be taken first (as this is
	 * the order for vma_open and vma_close ops */
	struct rw_semaphore sem;
	/* info about the mmaping process */
	struct vm_area_struct *vma;
	/* task struct of the mapping process */
	struct task_struct *task;
	/* process id of teh mapping process */
	pid_t pid;
	/* file descriptor of the master */
	int master_fd;
	/* file struct of the master */
	struct file *master_file;
	/* a list of currently available regions if this is a suballocation */
	struct list_head region_list;
	/* a linked list of data so we can access them for debugging */
	struct list_head list;
#if PMEM_DEBUG
	int ref;
#endif
};

struct pmem_bits {
	unsigned allocated:1;		/* 1 if allocated, 0 if free */
	unsigned order:7;		/* size of the region in pmem space */
};

struct pmem_region_node {
	struct pmem_region region;
	struct list_head list;
};

#define PMEM_DEBUG_MSGS 0
#if PMEM_DEBUG_MSGS
#define DLOG(fmt,args...) \
	do { printk(KERN_INFO "[%s:%s:%d] "fmt, __FILE__, __func__, __LINE__, \
		    ##args); } \
	while (0)
#else
#define DLOG(x...) do {} while (0)
#endif

struct pmem_info {
	struct miscdevice dev;
	/* physical start address of the remaped pmem space */
	unsigned long base;
	/* vitual start address of the remaped pmem space */
	unsigned char __iomem *vbase;
	/* total size of the pmem space */
	unsigned long size;
	/* number of entries in the pmem space */
	unsigned long num_entries;
	/* pfn of the garbage page in memory */
	unsigned long garbage_pfn;
	/* index of the garbage page in the pmem space */
	int garbage_index;
	/* the bitmap for the region indicating which entries are allocated
	 * and which are free */
	struct pmem_bits *bitmap;
	/* indicates the region should not be managed with an allocator */
	unsigned no_allocator;
	/* indicates maps of this region should be cached, if a mix of
	 * cached and uncached is desired, set this and open the device with
	 * O_SYNC to get an uncached region */
	unsigned cached;
	unsigned buffered;
	/* in no_allocator mode the first mapper gets the whole space and sets
	 * this flag */
	unsigned allocated;
	/* for debugging, creates a list of pmem file structs, the
	 * data_list_sem should be taken before pmem_data->sem if both are
	 * needed */
	struct semaphore data_list_sem;
	struct list_head data_list;
	/* pmem_sem protects the bitmap array
	 * a write lock should be held when modifying entries in bitmap
	 * a read lock should be held when reading data from bits or
	 * dereferencing a pointer into bitmap
	 *
	 * pmem_data->sem protects the pmem data of a particular file
	 * Many of the function that require the pmem_data->sem have a non-
	 * locking version for when the caller is already holding that sem.
	 *
	 * IF YOU TAKE BOTH LOCKS TAKE THEM IN THIS ORDER:
	 * down(pmem_data->sem) => down(bitmap_sem)
	 */
	struct rw_semaphore bitmap_sem;

	long (*ioctl)(struct file *, unsigned int, unsigned long);
	int (*release)(struct inode *, struct file *);
};

static struct pmem_info pmem[PMEM_MAX_DEVICES];
static int id_count;

#define PMEM_IS_FREE(id, index) !(pmem[id].bitmap[index].allocated)
#define PMEM_ORDER(id, index) pmem[id].bitmap[index].order
#define PMEM_BUDDY_INDEX(id, index) (index ^ (1 << PMEM_ORDER(id, index)))
#define PMEM_NEXT_INDEX(id, index) (index + (1 << PMEM_ORDER(id, index)))
#define PMEM_OFFSET(index) (index * PMEM_MIN_ALLOC)
#define PMEM_START_ADDR(id, index) (PMEM_OFFSET(index) + pmem[id].base)
#define PMEM_LEN(id, index) ((1 << PMEM_ORDER(id, index)) * PMEM_MIN_ALLOC)
#define PMEM_END_ADDR(id, index) (PMEM_START_ADDR(id, index) + \
	PMEM_LEN(id, index))
#define PMEM_START_VADDR(id, index) (PMEM_OFFSET(id, index) + pmem[id].vbase)
#define PMEM_END_VADDR(id, index) (PMEM_START_VADDR(id, index) + \
	PMEM_LEN(id, index))
#define PMEM_REVOKED(data) (data->flags & PMEM_FLAGS_REVOKED)
#define PMEM_IS_PAGE_ALIGNED(addr) (!((addr) & (~PAGE_MASK)))
#define PMEM_IS_SUBMAP(data) ((data->flags & PMEM_FLAGS_SUBMAP) && \
	(!(data->flags & PMEM_FLAGS_UNSUBMAP)))

static int pmem_release(struct inode *, struct file *);
static int pmem_mmap(struct file *, struct vm_area_struct *);
static int pmem_open(struct inode *, struct file *);
static long pmem_ioctl(struct file *, unsigned int, unsigned long);

struct file_operations pmem_fops = {
	.release = pmem_release,
	.mmap = pmem_mmap,
	.open = pmem_open,
	.unlocked_ioctl = pmem_ioctl,
};

static int get_id(struct file *file)
{
	return MINOR(file->f_dentry->d_inode->i_rdev);
}

int is_pmem_file(struct file *file)
{
	int id;

	if (unlikely(!file || !file->f_dentry || !file->f_dentry->d_inode))
		return 0;
	id = get_id(file);
	if (unlikely(id >= PMEM_MAX_DEVICES))
		return 0;
	if (unlikely(file->f_dentry->d_inode->i_rdev !=
	     MKDEV(MISC_MAJOR, pmem[id].dev.minor)))
		return 0;
	return 1;
}

static int has_allocation(struct file *file)
{
	struct pmem_data *data;
	/* check is_pmem_file first if not accessed via pmem_file_ops */

	if (unlikely(!file->private_data))
		return 0;
	data = (struct pmem_data *)file->private_data;
	if (unlikely(data->index < 0))
		return 0;
	return 1;
}

static int is_master_owner(struct file *file)
{
	struct file *master_file;
	struct pmem_data *data;
	int put_needed, ret = 0;

	if (!is_pmem_file(file) || !has_allocation(file))
		return 0;
	data = (struct pmem_data *)file->private_data;
	if (PMEM_FLAGS_MASTERMAP & data->flags)
		return 1;
	master_file = fget_light(data->master_fd, &put_needed);
	if (master_file && data->master_file == master_file)
		ret = 1;
	fput_light(master_file, put_needed);
	return ret;
}

static int pmem_free(int id, int index)
{
	/* caller should hold the write lock on pmem_sem! */
	int buddy, curr = index;
	DLOG("index %d\n", index);

	if (pmem[id].no_allocator) {
		pmem[id].allocated = 0;
		return 0;
	}
	/* clean up the bitmap, merging any buddies */
	pmem[id].bitmap[curr].allocated = 0;
	/* find a slots buddy Buddy# = Slot# ^ (1 << order)
	 * if the buddy is also free merge them
	 * repeat until the buddy is not free or end of the bitmap is reached
	 */
	do {
		buddy = PMEM_BUDDY_INDEX(id, curr);
		if (PMEM_IS_FREE(id, buddy) &&
				PMEM_ORDER(id, buddy) == PMEM_ORDER(id, curr)) {
			PMEM_ORDER(id, buddy)++;
			PMEM_ORDER(id, curr)++;
			curr = min(buddy, curr);
		} else {
			break;
		}
	} while (curr < pmem[id].num_entries);

	return 0;
}

static void pmem_revoke(struct file *file, struct pmem_data *data);

static int pmem_release(struct inode *inode, struct file *file)
{
	struct pmem_data *data = (struct pmem_data *)file->private_data;
	struct pmem_region_node *region_node;
	struct list_head *elt, *elt2;
	int id = get_id(file), ret = 0;


	down(&pmem[id].data_list_sem);
	/* if this file is a master, revoke all the memory in the connected
	 *  files */
	if (PMEM_FLAGS_MASTERMAP & data->flags) {
		struct pmem_data *sub_data;
		list_for_each(elt, &pmem[id].data_list) {
			sub_data = list_entry(elt, struct pmem_data, list);
			down_read(&sub_data->sem);
			if (PMEM_IS_SUBMAP(sub_data) &&
			    file == sub_data->master_file) {
				up_read(&sub_data->sem);
				pmem_revoke(file, sub_data);
			}  else
				up_read(&sub_data->sem);
		}
	}
	list_del(&data->list);
	up(&pmem[id].data_list_sem);


	down_write(&data->sem);

	/* if its not a conencted file and it has an allocation, free it */
	if (!(PMEM_FLAGS_CONNECTED & data->flags) && has_allocation(file)) {
		down_write(&pmem[id].bitmap_sem);
		ret = pmem_free(id, data->index);
		up_write(&pmem[id].bitmap_sem);
	}

	/* if this file is a submap (mapped, connected file), downref the
	 * task struct */
	if (PMEM_FLAGS_SUBMAP & data->flags)
		if (data->task) {
			put_task_struct(data->task);
			data->task = NULL;
		}

	file->private_data = NULL;

	list_for_each_safe(elt, elt2, &data->region_list) {
		region_node = list_entry(elt, struct pmem_region_node, list);
		list_del(elt);
		kfree(region_node);
	}
	BUG_ON(!list_empty(&data->region_list));

	up_write(&data->sem);
	kfree(data);
	if (pmem[id].release)
		ret = pmem[id].release(inode, file);

	return ret;
}

static int pmem_open(struct inode *inode, struct file *file)
{
	struct pmem_data *data;
	int id = get_id(file);
	int ret = 0;

	DLOG("current %u file %p(%d)\n", current->pid, file, file_count(file));
	/* setup file->private_data to indicate its unmapped */
	/*  you can only open a pmem device one time */
	if (file->private_data != NULL)
		return -1;
	data = kmalloc(sizeof(struct pmem_data), GFP_KERNEL);
	if (!data) {
		printk("pmem: unable to allocate memory for pmem metadata.");
		return -1;
	}
	data->flags = 0;
	data->index = -1;
	data->task = NULL;
	data->vma = NULL;
	data->pid = 0;
	data->master_file = NULL;
#if PMEM_DEBUG
	data->ref = 0;
#endif
	INIT_LIST_HEAD(&data->region_list);
	init_rwsem(&data->sem);

	file->private_data = data;
	INIT_LIST_HEAD(&data->list);

	down(&pmem[id].data_list_sem);
	list_add(&data->list, &pmem[id].data_list);
	up(&pmem[id].data_list_sem);
	return ret;
}

static unsigned long pmem_order(unsigned long len)
{
	int i;

	len = (len + PMEM_MIN_ALLOC - 1)/PMEM_MIN_ALLOC;
	len--;
	for (i = 0; i < sizeof(len)*8; i++)
		if (len >> i == 0)
			break;
	return i;
}

static int pmem_allocate(int id, unsigned long len)
{
	/* caller should hold the write lock on pmem_sem! */
	/* return the corresponding pdata[] entry */
	int curr = 0;
	int end = pmem[id].num_entries;
	int best_fit = -1;
	unsigned long order = pmem_order(len);

	if (pmem[id].no_allocator) {
		DLOG("no allocator");
		if ((len > pmem[id].size) || pmem[id].allocated)
			return -1;
		pmem[id].allocated = 1;
		return len;
	}

	if (order > PMEM_MAX_ORDER)
		return -1;
	DLOG("order %lx\n", order);

	/* look through the bitmap:
	 * 	if you find a free slot of the correct order use it
	 * 	otherwise, use the best fit (smallest with size > order) slot
	 */
	while (curr < end) {
		if (PMEM_IS_FREE(id, curr)) {
			if (PMEM_ORDER(id, curr) == (unsigned char)order) {
				/* set the not free bit and clear others */
				best_fit = curr;
				break;
			}
			if (PMEM_ORDER(id, curr) > (unsigned char)order &&
			    (best_fit < 0 ||
			     PMEM_ORDER(id, curr) < PMEM_ORDER(id, best_fit)))
				best_fit = curr;
		}
		curr = PMEM_NEXT_INDEX(id, curr);
	}

	/* if best_fit < 0, there are no suitable slots,
	 * return an error
	 */
	if (best_fit < 0) {
		printk("pmem: no space left to allocate!\n");
		return -1;
	}

	/* now partition the best fit:
	 * 	split the slot into 2 buddies of order - 1
	 * 	repeat until the slot is of the correct order
	 */
	while (PMEM_ORDER(id, best_fit) > (unsigned char)order) {
		int buddy;
		PMEM_ORDER(id, best_fit) -= 1;
		buddy = PMEM_BUDDY_INDEX(id, best_fit);
		PMEM_ORDER(id, buddy) = PMEM_ORDER(id, best_fit);
	}
	pmem[id].bitmap[best_fit].allocated = 1;
	return best_fit;
}

static pgprot_t phys_mem_access_prot(struct file *file, pgprot_t vma_prot)
{
	int id = get_id(file);
#ifdef pgprot_noncached
	if (pmem[id].cached == 0 || file->f_flags & O_SYNC)
		return pgprot_noncached(vma_prot);
#endif
#ifdef pgprot_ext_buffered
	else if (pmem[id].buffered)
		return pgprot_ext_buffered(vma_prot);
#endif
	return vma_prot;
}

static unsigned long pmem_start_addr(int id, struct pmem_data *data)
{
	if (pmem[id].no_allocator)
		return PMEM_START_ADDR(id, 0);
	else
		return PMEM_START_ADDR(id, data->index);

}

static void *pmem_start_vaddr(int id, struct pmem_data *data)
{
	return pmem_start_addr(id, data) - pmem[id].base + pmem[id].vbase;
}

static unsigned long pmem_len(int id, struct pmem_data *data)
{
	if (pmem[id].no_allocator)
		return data->index;
	else
		return PMEM_LEN(id, data->index);
}

static int pmem_map_garbage(int id, struct vm_area_struct *vma,
			    struct pmem_data *data, unsigned long offset,
			    unsigned long len)
{
	int i, garbage_pages = len >> PAGE_SHIFT;

	vma->vm_flags |= VM_IO | VM_RESERVED | VM_PFNMAP | VM_SHARED | VM_WRITE;
	for (i = 0; i < garbage_pages; i++) {
		if (vm_insert_pfn(vma, vma->vm_start + offset + (i * PAGE_SIZE),
		    pmem[id].garbage_pfn))
			return -EAGAIN;
	}
	return 0;
}

static int pmem_unmap_pfn_range(int id, struct vm_area_struct *vma,
				struct pmem_data *data, unsigned long offset,
				unsigned long len)
{
	int garbage_pages;
	DLOG("unmap offset %lx len %lx\n", offset, len);

	BUG_ON(!PMEM_IS_PAGE_ALIGNED(len));

	garbage_pages = len >> PAGE_SHIFT;
	zap_page_range(vma, vma->vm_start + offset, len, NULL);
	pmem_map_garbage(id, vma, data, offset, len);
	return 0;
}

static int pmem_map_pfn_range(int id, struct vm_area_struct *vma,
			      struct pmem_data *data, unsigned long offset,
			      unsigned long len)
{
	DLOG("map offset %lx len %lx\n", offset, len);
	BUG_ON(!PMEM_IS_PAGE_ALIGNED(vma->vm_start));
	BUG_ON(!PMEM_IS_PAGE_ALIGNED(vma->vm_end));
	BUG_ON(!PMEM_IS_PAGE_ALIGNED(len));
	BUG_ON(!PMEM_IS_PAGE_ALIGNED(offset));

	if (io_remap_pfn_range(vma, vma->vm_start + offset,
		(pmem_start_addr(id, data) + offset) >> PAGE_SHIFT,
		len, vma->vm_page_prot)) {
		return -EAGAIN;
	}
	return 0;
}

static int pmem_remap_pfn_range(int id, struct vm_area_struct *vma,
			      struct pmem_data *data, unsigned long offset,
			      unsigned long len)
{
	/* hold the mm semp for the vma you are modifying when you call this */
	BUG_ON(!vma);
	zap_page_range(vma, vma->vm_start + offset, len, NULL);
	return pmem_map_pfn_range(id, vma, data, offset, len);
}

static void pmem_vma_open(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;
	struct pmem_data *data = file->private_data;
	int id = get_id(file);
	/* this should never be called as we don't support copying pmem
	 * ranges via fork */
	BUG_ON(!has_allocation(file));
	down_write(&data->sem);
	/* remap the garbage pages, forkers don't get access to the data */
	pmem_unmap_pfn_range(id, vma, data, 0, vma->vm_start - vma->vm_end);
	up_write(&data->sem);
}

static void pmem_vma_close(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;
	struct pmem_data *data = file->private_data;

	DLOG("current %u ppid %u file %p count %d\n", current->pid,
	     current->parent->pid, file, file_count(file));
	if (unlikely(!is_pmem_file(file) || !has_allocation(file))) {
		printk(KERN_WARNING "pmem: something is very wrong, you are "
		       "closing a vm backing an allocation that doesn't "
		       "exist!\n");
		return;
	}
	down_write(&data->sem);
	if (data->vma == vma) {
		data->vma = NULL;
		if ((data->flags & PMEM_FLAGS_CONNECTED) &&
		    (data->flags & PMEM_FLAGS_SUBMAP))
			data->flags |= PMEM_FLAGS_UNSUBMAP;
	}
	/* the kernel is going to free this vma now anyway */
	up_write(&data->sem);
}

static struct vm_operations_struct vm_ops = {
	.open = pmem_vma_open,
	.close = pmem_vma_close,
};

static int pmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct pmem_data *data;
	int index;
	unsigned long vma_size =  vma->vm_end - vma->vm_start;
	int ret = 0, id = get_id(file);

	if (vma->vm_pgoff || !PMEM_IS_PAGE_ALIGNED(vma_size)) {
#if PMEM_DEBUG
		printk(KERN_ERR "pmem: mmaps must be at offset zero, aligned"
				" and a multiple of pages_size.\n");
#endif
		return -EINVAL;
	}

	data = (struct pmem_data *)file->private_data;
	down_write(&data->sem);
	/* check this file isn't already mmaped, for submaps check this file
	 * has never been mmaped */
	if ((data->flags & PMEM_FLAGS_MASTERMAP) ||
	    (data->flags & PMEM_FLAGS_SUBMAP) ||
	    (data->flags & PMEM_FLAGS_UNSUBMAP)) {
#if PMEM_DEBUG
		printk(KERN_ERR "pmem: you can only mmap a pmem file once, "
		       "this file is already mmaped. %x\n", data->flags);
#endif
		ret = -EINVAL;
		goto error;
	}
	/* if file->private_data == unalloced, alloc*/
	if (data && data->index == -1) {
		down_write(&pmem[id].bitmap_sem);
		index = pmem_allocate(id, vma->vm_end - vma->vm_start);
		up_write(&pmem[id].bitmap_sem);
		data->index = index;
	}
	/* either no space was available or an error occured */
	if (!has_allocation(file)) {
		ret = -EINVAL;
		printk("pmem: could not find allocation for map.\n");
		goto error;
	}

	if (pmem_len(id, data) < vma_size) {
#if PMEM_DEBUG
		printk(KERN_WARNING "pmem: mmap size [%lu] does not match"
		       "size of backing region [%lu].\n", vma_size,
		       pmem_len(id, data));
#endif
		ret = -EINVAL;
		goto error;
	}

	vma->vm_pgoff = pmem_start_addr(id, data) >> PAGE_SHIFT;
	vma->vm_page_prot = phys_mem_access_prot(file, vma->vm_page_prot);

	if (data->flags & PMEM_FLAGS_CONNECTED) {
		struct pmem_region_node *region_node;
		struct list_head *elt;
		if (pmem_map_garbage(id, vma, data, 0, vma_size)) {
			printk("pmem: mmap failed in kernel!\n");
			ret = -EAGAIN;
			goto error;
		}
		list_for_each(elt, &data->region_list) {
			region_node = list_entry(elt, struct pmem_region_node,
						 list);
			DLOG("remapping file: %p %lx %lx\n", file,
				region_node->region.offset,
				region_node->region.len);
			if (pmem_remap_pfn_range(id, vma, data,
						 region_node->region.offset,
						 region_node->region.len)) {
				ret = -EAGAIN;
				goto error;
			}
		}
		data->flags |= PMEM_FLAGS_SUBMAP;
		get_task_struct(current->group_leader);
		data->task = current->group_leader;
		data->vma = vma;
#if PMEM_DEBUG
		data->pid = current->pid;
#endif
		DLOG("submmapped file %p vma %p pid %u\n", file, vma,
		     current->pid);
	} else {
		if (pmem_map_pfn_range(id, vma, data, 0, vma_size)) {
			printk(KERN_INFO "pmem: mmap failed in kernel!\n");
			ret = -EAGAIN;
			goto error;
		}
		data->flags |= PMEM_FLAGS_MASTERMAP;
		data->pid = current->pid;
	}
	vma->vm_ops = &vm_ops;
error:
	up_write(&data->sem);
	return ret;
}

/* the following are the api for accessing pmem regions by other drivers
 * from inside the kernel */
int get_pmem_user_addr(struct file *file, unsigned long *start,
		   unsigned long *len)
{
	struct pmem_data *data;
	if (!is_pmem_file(file) || !has_allocation(file)) {
#if PMEM_DEBUG
		printk(KERN_INFO "pmem: requested pmem data from invalid"
				  "file.\n");
#endif
		return -1;
	}
	data = (struct pmem_data *)file->private_data;
	down_read(&data->sem);
	if (data->vma) {
		*start = data->vma->vm_start;
		*len = data->vma->vm_end - data->vma->vm_start;
	} else {
		*start = 0;
		*len = 0;
	}
	up_read(&data->sem);
	return 0;
}

int get_pmem_addr(struct file *file, unsigned long *start,
		  unsigned long *vstart, unsigned long *len)
{
	struct pmem_data *data;
	int id;

	if (!is_pmem_file(file) || !has_allocation(file)) {
		return -1;
	}

	data = (struct pmem_data *)file->private_data;
	if (data->index == -1) {
#if PMEM_DEBUG
		printk(KERN_INFO "pmem: requested pmem data from file with no "
		       "allocation.\n");
		return -1;
#endif
	}
	id = get_id(file);

	down_read(&data->sem);
	*start = pmem_start_addr(id, data);
	*len = pmem_len(id, data);
	*vstart = (unsigned long)pmem_start_vaddr(id, data);
	up_read(&data->sem);
#if PMEM_DEBUG
	down_write(&data->sem);
	data->ref++;
	up_write(&data->sem);
#endif
	return 0;
}

int get_pmem_file(int fd, unsigned long *start, unsigned long *vstart,
		  unsigned long *len, struct file **filp)
{
	struct file *file;

	file = fget(fd);
	if (unlikely(file == NULL)) {
		printk(KERN_INFO "pmem: requested data from file descriptor "
		       "that doesn't exist.");
		return -1;
	}

	if (get_pmem_addr(file, start, vstart, len))
		goto end;

	if (filp)
		*filp = file;
	return 0;
end:
	fput(file);
	return -1;
}

void put_pmem_file(struct file *file)
{
	struct pmem_data *data;
	int id;

	if (!is_pmem_file(file))
		return;
	id = get_id(file);
	data = (struct pmem_data *)file->private_data;
#if PMEM_DEBUG
	down_write(&data->sem);
	if (data->ref == 0) {
		printk("pmem: pmem_put > pmem_get %s (pid %d)\n",
		       pmem[id].dev.name, data->pid);
		BUG();
	}
	data->ref--;
	up_write(&data->sem);
#endif
	fput(file);
}

void flush_pmem_file(struct file *file, unsigned long offset, unsigned long len)
{
	struct pmem_data *data;
	int id;
	void *vaddr;
	struct pmem_region_node *region_node;
	struct list_head *elt;
	void *flush_start, *flush_end;

	if (!is_pmem_file(file) || !has_allocation(file)) {
		return;
	}

	id = get_id(file);
	data = (struct pmem_data *)file->private_data;
	if (!pmem[id].cached || file->f_flags & O_SYNC)
		return;

	down_read(&data->sem);
	vaddr = pmem_start_vaddr(id, data);
	/* if this isn't a submmapped file, flush the whole thing */
	if (unlikely(!(data->flags & PMEM_FLAGS_CONNECTED))) {
		dmac_flush_range(vaddr, vaddr + pmem_len(id, data));
		goto end;
	}
	/* otherwise, flush the region of the file we are drawing */
	list_for_each(elt, &data->region_list) {
		region_node = list_entry(elt, struct pmem_region_node, list);
		if ((offset >= region_node->region.offset) &&
		    ((offset + len) <= (region_node->region.offset +
			region_node->region.len))) {
			flush_start = vaddr + region_node->region.offset;
			flush_end = flush_start + region_node->region.len;
			dmac_flush_range(flush_start, flush_end);
			break;
		}
	}
end:
	up_read(&data->sem);
}

static int pmem_connect(unsigned long connect, struct file *file)
{
	struct pmem_data *data = (struct pmem_data *)file->private_data;
	struct pmem_data *src_data;
	struct file *src_file;
	int ret = 0, put_needed;

	down_write(&data->sem);
	/* retrieve the src file and check it is a pmem file with an alloc */
	src_file = fget_light(connect, &put_needed);
	DLOG("connect %p to %p\n", file, src_file);
	if (!src_file) {
		printk("pmem: src file not found!\n");
		ret = -EINVAL;
		goto err_no_file;
	}
	if (unlikely(!is_pmem_file(src_file) || !has_allocation(src_file))) {
		printk(KERN_INFO "pmem: src file is not a pmem file or has no "
		       "alloc!\n");
		ret = -EINVAL;
		goto err_bad_file;
	}
	src_data = (struct pmem_data *)src_file->private_data;

	if (has_allocation(file) && (data->index != src_data->index)) {
		printk("pmem: file is already mapped but doesn't match this"
		       " src_file!\n");
		ret = -EINVAL;
		goto err_bad_file;
	}
	data->index = src_data->index;
	data->flags |= PMEM_FLAGS_CONNECTED;
	data->master_fd = connect;
	data->master_file = src_file;

err_bad_file:
	fput_light(src_file, put_needed);
err_no_file:
	up_write(&data->sem);
	return ret;
}

static void pmem_unlock_data_and_mm(struct pmem_data *data,
				    struct mm_struct *mm)
{
	up_write(&data->sem);
	if (mm != NULL) {
		up_write(&mm->mmap_sem);
		mmput(mm);
	}
}

static int pmem_lock_data_and_mm(struct file *file, struct pmem_data *data,
				 struct mm_struct **locked_mm)
{
	int ret = 0;
	struct mm_struct *mm = NULL;
	*locked_mm = NULL;
lock_mm:
	down_read(&data->sem);
	if (PMEM_IS_SUBMAP(data)) {
		mm = get_task_mm(data->task);
		if (!mm) {
#if PMEM_DEBUG
			printk("pmem: can't remap task is gone!\n");
#endif
			up_read(&data->sem);
			return -1;
		}
	}
	up_read(&data->sem);

	if (mm)
		down_write(&mm->mmap_sem);

	down_write(&data->sem);
	/* check that the file didn't get mmaped before we could take the
	 * data sem, this should be safe b/c you can only submap each file
	 * once */
	if (PMEM_IS_SUBMAP(data) && !mm) {
		pmem_unlock_data_and_mm(data, mm);
		up_write(&data->sem);
		goto lock_mm;
	}
	/* now check that vma.mm is still there, it could have been
	 * deleted by vma_close before we could get the data->sem */
	if ((data->flags & PMEM_FLAGS_UNSUBMAP) && (mm != NULL)) {
		/* might as well release this */
		if (data->flags & PMEM_FLAGS_SUBMAP) {
			put_task_struct(data->task);
			data->task = NULL;
			/* lower the submap flag to show the mm is gone */
			data->flags &= ~(PMEM_FLAGS_SUBMAP);
		}
		pmem_unlock_data_and_mm(data, mm);
		return -1;
	}
	*locked_mm = mm;
	return ret;
}

int pmem_remap(struct pmem_region *region, struct file *file,
		      unsigned operation)
{
	int ret;
	struct pmem_region_node *region_node;
	struct mm_struct *mm = NULL;
	struct list_head *elt, *elt2;
	int id = get_id(file);
	struct pmem_data *data = (struct pmem_data *)file->private_data;

	/* pmem region must be aligned on a page boundry */
	if (unlikely(!PMEM_IS_PAGE_ALIGNED(region->offset) ||
		 !PMEM_IS_PAGE_ALIGNED(region->len))) {
#if PMEM_DEBUG
		printk("pmem: request for unaligned pmem suballocation "
		       "%lx %lx\n", region->offset, region->len);
#endif
		return -EINVAL;
	}

	/* if userspace requests a region of len 0, there's nothing to do */
	if (region->len == 0)
		return 0;

	/* lock the mm and data */
	ret = pmem_lock_data_and_mm(file, data, &mm);
	if (ret)
		return 0;

	/* only the owner of the master file can remap the client fds
	 * that back in it */
	if (!is_master_owner(file)) {
#if PMEM_DEBUG
		printk("pmem: remap requested from non-master process\n");
#endif
		ret = -EINVAL;
		goto err;
	}

	/* check that the requested range is within the src allocation */
	if (unlikely((region->offset > pmem_len(id, data)) ||
		     (region->len > pmem_len(id, data)) ||
		     (region->offset + region->len > pmem_len(id, data)))) {
#if PMEM_DEBUG
		printk(KERN_INFO "pmem: suballoc doesn't fit in src_file!\n");
#endif
		ret = -EINVAL;
		goto err;
	}

	if (operation == PMEM_MAP) {
		region_node = kmalloc(sizeof(struct pmem_region_node),
			      GFP_KERNEL);
		if (!region_node) {
			ret = -ENOMEM;
#if PMEM_DEBUG
			printk(KERN_INFO "No space to allocate metadata!");
#endif
			goto err;
		}
		region_node->region = *region;
		list_add(&region_node->list, &data->region_list);
	} else if (operation == PMEM_UNMAP) {
		int found = 0;
		list_for_each_safe(elt, elt2, &data->region_list) {
			region_node = list_entry(elt, struct pmem_region_node,
				      list);
			if (region->len == 0 ||
			    (region_node->region.offset == region->offset &&
			    region_node->region.len == region->len)) {
				list_del(elt);
				kfree(region_node);
				found = 1;
			}
		}
		if (!found) {
#if PMEM_DEBUG
			printk("pmem: Unmap region does not map any mapped "
				"region!");
#endif
			ret = -EINVAL;
			goto err;
		}
	}

	if (data->vma && PMEM_IS_SUBMAP(data)) {
		if (operation == PMEM_MAP)
			ret = pmem_remap_pfn_range(id, data->vma, data,
						   region->offset, region->len);
		else if (operation == PMEM_UNMAP)
			ret = pmem_unmap_pfn_range(id, data->vma, data,
						   region->offset, region->len);
	}

err:
	pmem_unlock_data_and_mm(data, mm);
	return ret;
}

static void pmem_revoke(struct file *file, struct pmem_data *data)
{
	struct pmem_region_node *region_node;
	struct list_head *elt, *elt2;
	struct mm_struct *mm = NULL;
	int id = get_id(file);
	int ret = 0;

	data->master_file = NULL;
	ret = pmem_lock_data_and_mm(file, data, &mm);
	/* if lock_data_and_mm fails either the task that mapped the fd, or
	 * the vma that mapped it have already gone away, nothing more
	 * needs to be done */
	if (ret)
		return;
	/* unmap everything */
	/* delete the regions and region list nothing is mapped any more */
	if (data->vma)
		list_for_each_safe(elt, elt2, &data->region_list) {
			region_node = list_entry(elt, struct pmem_region_node,
						 list);
			pmem_unmap_pfn_range(id, data->vma, data,
					     region_node->region.offset,
					     region_node->region.len);
			list_del(elt);
			kfree(region_node);
	}
	/* delete the master file */
	pmem_unlock_data_and_mm(data, mm);
}

static void pmem_get_size(struct pmem_region *region, struct file *file)
{
	struct pmem_data *data = (struct pmem_data *)file->private_data;
	int id = get_id(file);

	if (!has_allocation(file)) {
		region->offset = 0;
		region->len = 0;
		return;
	} else {
		region->offset = pmem_start_addr(id, data);
		region->len = pmem_len(id, data);
	}
	DLOG("offset %lx len %lx\n", region->offset, region->len);
}


static long pmem_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pmem_data *data;
	int id = get_id(file);

	switch (cmd) {
	case PMEM_GET_PHYS:
		{
			struct pmem_region region;
			DLOG("get_phys\n");
			if (!has_allocation(file)) {
				region.offset = 0;
				region.len = 0;
			} else {
				data = (struct pmem_data *)file->private_data;
				region.offset = pmem_start_addr(id, data);
				region.len = pmem_len(id, data);
			}
			printk(KERN_INFO "pmem: request for physical address of pmem region "
					"from process %d.\n", current->pid);
			if (copy_to_user((void __user *)arg, &region,
						sizeof(struct pmem_region)))
				return -EFAULT;
			break;
		}
	case PMEM_MAP:
		{
			struct pmem_region region;
			if (copy_from_user(&region, (void __user *)arg,
						sizeof(struct pmem_region)))
				return -EFAULT;
			data = (struct pmem_data *)file->private_data;
			return pmem_remap(&region, file, PMEM_MAP);
		}
		break;
	case PMEM_UNMAP:
		{
			struct pmem_region region;
			if (copy_from_user(&region, (void __user *)arg,
						sizeof(struct pmem_region)))
				return -EFAULT;
			data = (struct pmem_data *)file->private_data;
			return pmem_remap(&region, file, PMEM_UNMAP);
			break;
		}
	case PMEM_GET_SIZE:
		{
			struct pmem_region region;
			DLOG("get_size\n");
			pmem_get_size(&region, file);
			if (copy_to_user((void __user *)arg, &region,
						sizeof(struct pmem_region)))
				return -EFAULT;
			break;
		}
	case PMEM_GET_TOTAL_SIZE:
		{
			struct pmem_region region;
			DLOG("get total size\n");
			region.offset = 0;
			get_id(file);
			region.len = pmem[id].size;
			if (copy_to_user((void __user *)arg, &region,
						sizeof(struct pmem_region)))
				return -EFAULT;
			break;
		}
	case PMEM_ALLOCATE:
		{
			if (has_allocation(file))
				return -EINVAL;
			data = (struct pmem_data *)file->private_data;
			data->index = pmem_allocate(id, arg);
			break;
		}
	case PMEM_CONNECT:
		DLOG("connect\n");
		return pmem_connect(arg, file);
		break;
	case PMEM_CACHE_FLUSH:
		{
			struct pmem_region region;
			DLOG("flush\n");
			if (copy_from_user(&region, (void __user *)arg,
					   sizeof(struct pmem_region)))
				return -EFAULT;
			flush_pmem_file(file, region.offset, region.len);
			break;
		}
	default:
		if (pmem[id].ioctl)
			return pmem[id].ioctl(file, cmd, arg);
		return -EINVAL;
	}
	return 0;
}

#if PMEM_DEBUG
static ssize_t debug_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t debug_read(struct file *file, char __user *buf, size_t count,
			  loff_t *ppos)
{
	struct list_head *elt, *elt2;
	struct pmem_data *data;
	struct pmem_region_node *region_node;
	int id = (int)file->private_data;
	const int debug_bufmax = 4096;
	static char buffer[4096];
	int n = 0;

	DLOG("debug open\n");
	n = scnprintf(buffer, debug_bufmax,
		      "pid #: mapped regions (offset, len) (offset,len)...\n");

	down(&pmem[id].data_list_sem);
	list_for_each(elt, &pmem[id].data_list) {
		data = list_entry(elt, struct pmem_data, list);
		down_read(&data->sem);
		n += scnprintf(buffer + n, debug_bufmax - n, "pid %u:",
				data->pid);
		list_for_each(elt2, &data->region_list) {
			region_node = list_entry(elt2, struct pmem_region_node,
				      list);
			n += scnprintf(buffer + n, debug_bufmax - n,
					"(%lx,%lx) ",
					region_node->region.offset,
					region_node->region.len);
		}
		n += scnprintf(buffer + n, debug_bufmax - n, "\n");
		up_read(&data->sem);
	}
	up(&pmem[id].data_list_sem);

	n++;
	buffer[n] = 0;
	return simple_read_from_buffer(buf, count, ppos, buffer, n);
}

static struct file_operations debug_fops = {
	.read = debug_read,
	.open = debug_open,
};
#endif

#if 0
static struct miscdevice pmem_dev = {
	.name = "pmem",
	.fops = &pmem_fops,
};
#endif

int pmem_setup(struct android_pmem_platform_data *pdata,
	       long (*ioctl)(struct file *, unsigned int, unsigned long),
	       int (*release)(struct inode *, struct file *))
{
	int err = 0;
	int i, index = 0;
	int id = id_count;
	id_count++;

	pmem[id].no_allocator = pdata->no_allocator;
	pmem[id].cached = pdata->cached;
	pmem[id].buffered = pdata->buffered;
	pmem[id].base = pdata->start;
	pmem[id].size = pdata->size;
	pmem[id].ioctl = ioctl;
	pmem[id].release = release;
	init_rwsem(&pmem[id].bitmap_sem);
	init_MUTEX(&pmem[id].data_list_sem);
	INIT_LIST_HEAD(&pmem[id].data_list);
	pmem[id].dev.name = pdata->name;
	pmem[id].dev.minor = id;
	pmem[id].dev.fops = &pmem_fops;
	printk(KERN_INFO "%s: %d init\n", pdata->name, pdata->cached);

	err = misc_register(&pmem[id].dev);
	if (err) {
		printk(KERN_ALERT "Unable to register pmem driver!\n");
		goto err_cant_register_device;
	}
	pmem[id].num_entries = pmem[id].size / PMEM_MIN_ALLOC;

	pmem[id].bitmap = kmalloc(pmem[id].num_entries *
				  sizeof(struct pmem_bits), GFP_KERNEL);
	if (!pmem[id].bitmap)
		goto err_no_mem_for_metadata;

	memset(pmem[id].bitmap, 0, sizeof(struct pmem_bits) *
					  pmem[id].num_entries);

	for (i = sizeof(pmem[id].num_entries) * 8 - 1; i >= 0; i--) {
		if ((pmem[id].num_entries) &  1<<i) {
			PMEM_ORDER(id, index) = i;
			index = PMEM_NEXT_INDEX(id, index);
		}
	}

	if (pmem[id].cached)
		pmem[id].vbase = ioremap_cached(pmem[id].base,
						pmem[id].size);
#ifdef ioremap_ext_buffered
	else if (pmem[id].buffered)
		pmem[id].vbase = ioremap_ext_buffered(pmem[id].base,
						      pmem[id].size);
#endif
	else
		pmem[id].vbase = ioremap(pmem[id].base, pmem[id].size);

	if (pmem[id].vbase == 0)
		goto error_cant_remap;

	pmem[id].garbage_pfn = page_to_pfn(alloc_page(GFP_KERNEL));
	if (pmem[id].no_allocator)
		pmem[id].allocated = 0;

#if PMEM_DEBUG
	debugfs_create_file(pdata->name, S_IFREG | S_IRUGO, NULL, (void *)id,
			    &debug_fops);
#endif
	return 0;
error_cant_remap:
	kfree(pmem[id].bitmap);
err_no_mem_for_metadata:
	misc_deregister(&pmem[id].dev);
err_cant_register_device:
	return -1;
}

static int pmem_probe(struct platform_device *pdev)
{
	struct android_pmem_platform_data *pdata;

	if (!pdev || !pdev->dev.platform_data) {
		printk(KERN_ALERT "Unable to probe pmem!\n");
		return -1;
	}
	pdata = pdev->dev.platform_data;
	return pmem_setup(pdata, NULL, NULL);
}


static int pmem_remove(struct platform_device *pdev)
{
	int id = pdev->id;
	__free_page(pfn_to_page(pmem[id].garbage_pfn));
	misc_deregister(&pmem[id].dev);
	return 0;
}

static struct platform_driver pmem_driver = {
	.probe = pmem_probe,
	.remove = pmem_remove,
	.driver = { .name = "android_pmem" }
};


static int __init pmem_init(void)
{
	return platform_driver_register(&pmem_driver);
}

static void __exit pmem_exit(void)
{
	platform_driver_unregister(&pmem_driver);
}

module_init(pmem_init);
module_exit(pmem_exit);

