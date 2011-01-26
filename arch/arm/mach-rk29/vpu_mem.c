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

#define VPU_MEM_DEBUG 0

#define VPU_MEM_SPLIT_ALLOC             0
#define VPU_MEM_SPLIT_LINK              1

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
	int pfn:16;                 /* page frame number - vpu_mem space max 256M */
	int refrc:7;                /* reference number */
	int first:1;                /* 1 if first, 0 if not first */
	int avail:7;                /* available link number */
	int last:1;                 /* 1 if last, 0 if no last */
};

struct vpu_mem_region {
	int index;
	int ref_count;
};

struct vpu_mem_region_node {
	struct vpu_mem_region region;
	struct list_head list;
};

#define NODE_REGION_INDEX(p)     (p->region.index)
#define NODE_REGION_REFC(p)      (p->region.ref_count)

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

#define VPU_MEM_IS_FREE(index) !(vpu_mem.bitmap[index].avail)
#define VPU_MEM_FIRST(index) (vpu_mem.bitmap[index].first)
#define VPU_MEM_LAST(index) (vpu_mem.bitmap[index].last)
#define VPU_MEM_REFC(index) (vpu_mem.bitmap[index].refrc)
#define VPU_MEM_AVAIL(index) (vpu_mem.bitmap[index].avail)
#define VPU_MEM_BIT(index) (&vpu_mem.bitmap[index])
#define VPU_MEM_PFN(index) (vpu_mem.bitmap[index].pfn)
#define VPU_MEM_LAST_INDEX(index) (index - VPU_MEM_PFN(index - 1))
#define VPU_MEM_NEXT_INDEX(index) (index + VPU_MEM_PFN(index))
#define VPU_MEM_END_INDEX(index) (VPU_MEM_NEXT_INDEX(index) - 1)
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

static void region_set(int index, int pfn)
{
    WARN(pfn <= 0, "vpu_mem: region_set non-positive pfn\n");
    if (pfn > 0) {
        int first = index;
        int last  = index + pfn - 1;

        DLOG("region_set: first %d, last %d, size %d\n", first, last, pfn);

        VPU_MEM_FIRST(first) = 1;
        VPU_MEM_PFN(first) = pfn;

        VPU_MEM_LAST(last) = 1;
        VPU_MEM_PFN(last) = pfn;
    }
}

static void region_unset(int index, int pfn)
{
    WARN(pfn <= 0, "vpu_mem: region_unset non-positive pfn\n");
    if (pfn > 0) {
        int first = index;
        int last  = index + pfn - 1;

        DLOG("region_unset: first %d, last %d, size %d\n", first, last, pfn);

        VPU_MEM_FIRST(first) = 0;
        VPU_MEM_LAST(first) = 0;
        VPU_MEM_PFN(first) = 0;

        VPU_MEM_FIRST(last) = 0;
        VPU_MEM_LAST(last) = 0;
        VPU_MEM_PFN(last) = 0;
    }
}

static void region_set_ref_count(int index, int ref_count)
{
    DLOG("region_set_ref_count: index %d, ref_count %d\n", index, ref_count);

    VPU_MEM_REFC(index) = ref_count;
    VPU_MEM_REFC(VPU_MEM_END_INDEX(index)) = ref_count;
}

static void region_set_avail(int index, int avail)
{
    DLOG("region_set_avail: index %d, avail %d\n", index, avail);

    VPU_MEM_AVAIL(index) = avail;
    VPU_MEM_AVAIL(VPU_MEM_END_INDEX(index)) = avail;
}

static int index_avail(int index)
{
    return ((0 <= index) && (index < vpu_mem.num_entries));
}

static int region_check(int index)
{
    int end = VPU_MEM_END_INDEX(index);

    DLOG("region_check: index %d val 0x%.8x, end %d val 0x%.8x\n",
        index, *((unsigned int *)VPU_MEM_BIT(index)),
        end, *((unsigned int *)VPU_MEM_BIT(end)));

    WARN(index <  0,
        "vpu_mem: region_check fail: negative first %d\n", index);
    WARN(index >= vpu_mem.num_entries,
        "vpu_mem: region_check fail: too large first %d\n", index);
    WARN(end   <  0,
        "vpu_mem: region_check fail: negative end %d\n", end);
    WARN(end   >= vpu_mem.num_entries,
        "vpu_mem: region_check fail: too large end %d\n", end);
    WARN(!VPU_MEM_FIRST(index),
        "vpu_mem: region_check fail: index %d is not first\n", index);
    WARN(!VPU_MEM_LAST(end),
        "vpu_mem: region_check fail: index %d is not end\n", end);
    WARN((VPU_MEM_PFN(index) != VPU_MEM_PFN(end)),
        "vpu_mem: region_check fail: first %d and end %d pfn is not equal\n", index, end);
    WARN(VPU_MEM_REFC(index) != VPU_MEM_REFC(end),
        "vpu_mem: region_check fail: first %d and end %d ref count is not equal\n", index, end);
    WARN(VPU_MEM_AVAIL(index) != VPU_MEM_AVAIL(end),
        "vpu_mem: region_check fail: first %d and end %d avail count is not equal\n", index, end);
    return 0;
}

/*
 * split new allocated block from free block
 * the bitmap_sem and region_list_sem must be hold together
 * the pnode is a ouput region node
 */
static int region_new(struct list_head *region_list, int index, int pfn)
{
    int pfn_free = VPU_MEM_PFN(index);
    // check pfn is smaller then target index region
    if ((pfn > pfn_free) || (pfn <= 0)) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "unable to split region %d of size %d, while is smaller than %d!", index, pfn_free, pfn);
#endif
        return -1;
    }
    // check region data coherence
    if (region_check(index)) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "region %d unable to pass coherence check!", index);
#endif
        return -EINVAL;
    }

    {
        struct list_head *last;
        struct vpu_mem_region_node *node;
        // check target index region first
        if (!VPU_MEM_IS_FREE(index)) {
#if VPU_MEM_DEBUG
            printk(KERN_INFO "try to split not free region %d!", index);
#endif
            return -2;
        }
        // malloc vpu_mem_region_node
        node = kmalloc(sizeof(struct vpu_mem_region_node), GFP_KERNEL);
        if (NULL == node) {
#if VPU_MEM_DEBUG
            printk(KERN_INFO "No space to allocate struct vpu_mem_region_node!");
#endif
            return -ENOMEM;
        }

        // search the last node
        DLOG("search the last node\n");
        for (last = region_list; !list_is_last(last, region_list);)
            last = last->next;

        DLOG("list_add_tail\n");
        list_add_tail(&node->list, last);

        DLOG("start region_set index %d pfn %u\n", index, pfn);
        region_set(index,       pfn);

        if (pfn_free - pfn) {
            DLOG("start region_set index %d pfn %u\n", index + pfn, pfn_free - pfn);
            region_set(index + pfn, pfn_free - pfn);
        }

        region_set_avail(index, VPU_MEM_AVAIL(index) + 1);
        region_set_ref_count(index, VPU_MEM_REFC(index) + 1);
        node->region.index = index;
        node->region.ref_count = 1;
    }

    return 0;
}

/*
 * link allocated block from free block
 * the bitmap_sem and region_list_sem must be hold together
 * the pnode is a ouput region node
 */
static int region_link(struct list_head *region_list, int index)
{
    struct vpu_mem_region_node *node = NULL;
    struct list_head *list, *tmp;
    list_for_each_safe(list, tmp, region_list) {
        struct vpu_mem_region_node *p = list_entry(list, struct vpu_mem_region_node, list);
        if (index == NODE_REGION_INDEX(p)) {
            node = p;
            break;
        }
    }

    if (NULL == node) {
        struct list_head *last;
        DLOG("link non-exists index %d\n", index);

        // malloc vpu_mem_region_node
        node = kmalloc(sizeof(struct vpu_mem_region_node), GFP_KERNEL);
        if (NULL == node) {
#if VPU_MEM_DEBUG
            printk(KERN_INFO "No space to allocate struct vpu_mem_region_node!");
#endif
            return -ENOMEM;
        }

        // search the last node
        DLOG("search the last node\n");
        for (last = region_list; !list_is_last(last, region_list);)
            last = last->next;

        DLOG("list_add_tail\n");
        list_add_tail(&node->list, last);

        node->region.index = index;
        node->region.ref_count = 1;
    } else {
        DLOG("link existed index %d\n", index);
        node->region.ref_count++;
    }
    region_set_ref_count(index, VPU_MEM_REFC(index) + 1);

    return 0;
}

static int region_merge(struct list_head *node)
{
    struct vpu_mem_region_node *pnode = list_entry(node, struct vpu_mem_region_node, list);
    int index = pnode->region.index;
    int target;

    if (VPU_MEM_AVAIL(index))
        return 0;
    if (region_check(index))
        return -EINVAL;

    target = VPU_MEM_NEXT_INDEX(index);
    if (index_avail(target) && VPU_MEM_IS_FREE(target)) {
        int pfn_target  = VPU_MEM_PFN(target);
        int pfn_index   = VPU_MEM_PFN(index);
        int pfn_total   = pfn_target + pfn_index;
        region_unset(index,  pfn_index);
        region_unset(target, pfn_target);
        region_set(index, pfn_total);
    } else {
        DLOG("region_merge: merge NEXT_INDEX fail index_avail(%d) = %d IS_FREE = %d\n",
            target, index_avail(target), VPU_MEM_IS_FREE(target));
    }
    target = index - 1;
    if (index_avail(target) && VPU_MEM_IS_FREE(target)) {
        int pfn_target  = VPU_MEM_PFN(target);
        int pfn_index   = VPU_MEM_PFN(index);
        int pfn_total   = pfn_target + pfn_index;
        target = VPU_MEM_LAST_INDEX(index);
        region_unset(index,  pfn_index);
        region_unset(target, pfn_target);
        region_set(target, pfn_total);
    } else {
        DLOG("region_merge: merge LAST_INDEX fail index_avail(%d) = %d IS_FREE = %d\n",
            target, index_avail(target), VPU_MEM_IS_FREE(target));
    }
    return 0;
}

static long vpu_mem_allocate(struct file *file, unsigned int len)
{
    /* caller should hold the write lock on vpu_mem_sem! */
	/* return the corresponding pdata[] entry */
	int curr = 0;
	int best_fit = -1;
	unsigned int pfn = (len + VPU_MEM_MIN_ALLOC - 1)/VPU_MEM_MIN_ALLOC;
    struct vpu_mem_data *data = (struct vpu_mem_data *)file->private_data;

    if (!is_vpu_mem_file(file)) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "allocate vpu_mem data from invalid file.\n");
#endif
        return -ENODEV;
    }

	/* look through the bitmap:
	 * 	if you find a free slot of the correct order use it
	 * 	otherwise, use the best fit (smallest with size > order) slot
	 */
	while (curr < vpu_mem.num_entries) {
		if (VPU_MEM_IS_FREE(curr)) {
			if (VPU_MEM_PFN(curr) >= pfn) {
				/* set the not free bit and clear others */
				best_fit = curr;
#if VPU_MEM_DEBUG
                    printk("vpu_mem: find fit size at index %d\n", curr);
#endif
				break;
			}
		}
#if VPU_MEM_DEBUG
        //printk(KERN_INFO "vpu_mem: search curr %d\n!", curr);
#endif
		curr = VPU_MEM_NEXT_INDEX(curr);
#if VPU_MEM_DEBUG
        //printk(KERN_INFO "vpu_mem: search next %d\n!", curr);
#endif
	}

	/* if best_fit < 0, there are no suitable slots,
	 * return an error
	 */
	if (best_fit < 0) {
#if VPU_MEM_DEBUG
		printk("vpu_mem: no space left to allocate!\n");
#endif
		return -1;
	}

	DLOG("best_fit: %d next: %u\n", best_fit, best_fit + pfn);

    down_write(&data->sem);
    {
        int ret = region_new(&data->region_list, best_fit, pfn);
        if (ret)
            best_fit = -1;
    }
    up_write(&data->sem);

	DLOG("best_fit result: %d next: %u\n", best_fit, best_fit + pfn);

	return best_fit;
}

static int vpu_mem_free_by_region(struct vpu_mem_region_node *node)
{
    int ret = 0;
    int index = node->region.index;
    int avail = VPU_MEM_AVAIL(index);
    int refc  = VPU_MEM_REFC(index);

    WARN((NODE_REGION_REFC(node) <= 0),
        "vpu_mem: vpu_mem_free: non-positive ref count\n");
    WARN((!VPU_MEM_FIRST(index)),
        "vpu_mem: vpu_mem_free: index %d is not first\n", index);
    WARN((avail <= 0),
        "vpu_mem: vpu_mem_free: avail of index %d is non-positive\n", index);
    WARN((refc  <= 0),
        "vpu_mem: vpu_mem_free: refc of index %d is non-positive\n", index);

    NODE_REGION_REFC(node) -= 1;
    region_set_avail(index, avail - 1);
    region_set_ref_count(index, refc - 1);
    if (0 == NODE_REGION_REFC(node))
    {
        avail = VPU_MEM_AVAIL(index);
        if (0 == avail)
        {
            refc  = VPU_MEM_REFC(index);
            WARN((0 != refc),
                "vpu_mem: vpu_mem_free: refc of index %d after free is non-zero\n", index);
            ret = region_merge(&node->list);
        }
        list_del(&node->list);
        kfree(node);
    }
    return ret;
}

static int vpu_mem_free(struct file *file, int index)
{
    /* caller should hold the write lock on vpu_mem_sem! */
    struct vpu_mem_data *data = (struct vpu_mem_data *)file->private_data;

    if (!is_vpu_mem_file(file)) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "free vpu_mem data from invalid file.\n");
#endif
        return -ENODEV;
    }

	DLOG("search for index %d\n", index);

	down_write(&data->sem);
    {
    	struct list_head *list, *tmp;
        list_for_each_safe(list, tmp, &data->region_list) {
    		struct vpu_mem_region_node *node = list_entry(list, struct vpu_mem_region_node, list);
            if (index == NODE_REGION_INDEX(node)) {
                int ret = vpu_mem_free_by_region(node);
                up_write(&data->sem);
                return ret;
            }
        }
	}
	up_write(&data->sem);

	DLOG("no region of index %d searched\n", index);

	return -1;
}

static int vpu_mem_duplicate(struct file *file, int index)
{
	/* caller should hold the write lock on vpu_mem_sem! */
    if (!is_vpu_mem_file(file)) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "duplicate vpu_mem data from invalid file.\n");
#endif
        return -ENODEV;
    }

	DLOG("duplicate index %d\n", index);

    if (region_check(index)) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "region %d unable to pass coherence check!", index);
#endif
        return -EINVAL;
    }

    region_set_avail(index, VPU_MEM_AVAIL(index) + 1);

	return 0;
}

static int vpu_mem_link(struct file *file, int index)
{
    int err;
    struct vpu_mem_data *data = (struct vpu_mem_data *)file->private_data;

	if (!is_vpu_mem_file(file)) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "link vpu_mem data from invalid file.\n");
#endif
        return -ENODEV;
	}

    if (region_check(index)) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "region %d unable to pass coherence check!", index);
#endif
        return -EINVAL;
    }

    // check target index region first
    if (VPU_MEM_IS_FREE(index)) {
#if VPU_MEM_DEBUG
        printk(KERN_INFO "try to link free region %d!", index);
#endif
        return -1;
    }

	/* caller should hold the write lock on vpu_mem_sem! */
	down_write(&data->sem);
    err = region_link(&data->region_list, index);
	up_write(&data->sem);
    DLOG("link index %d ret %d\n", index, err);

	return err;
}

void vpu_mem_cache_opt(struct file *file, long index, unsigned int cmd)
{
	struct vpu_mem_data *data;
	void *start, *end;

	if (!is_vpu_mem_file(file)) {
		return;
	}

	data = (struct vpu_mem_data *)file->private_data;
	if (!vpu_mem.cached || file->f_flags & O_SYNC)
		return;

	down_read(&data->sem);
    start = VPU_MEM_START_VADDR(index);
    end   = VPU_MEM_END_VADDR(index);
    switch (cmd) {
    case VPU_MEM_CACHE_FLUSH : {
        dmac_flush_range(start, end);
        break;
    }
    case VPU_MEM_CACHE_CLEAN : {
        dmac_clean_range(start, end);
        break;
    }
    case VPU_MEM_CACHE_INVALID : {
        dmac_inv_range(start, end);
        break;
    }
    default :
        break;
    }
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
#if VPU_MEM_DEBUG
		printk("vpu_mem: unable to allocate memory for vpu_mem metadata.");
#endif
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

#if VPU_MEM_DEBUG
    printk(KERN_ALERT "vpu_mem: file->private_data : 0x%x\n", (unsigned int)data);
#endif

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

    // TODO: 最后一个文件 release 的时候
	down_write(&data->sem);
	file->private_data = NULL;
	list_for_each_safe(elt, elt2, &data->region_list) {
		struct vpu_mem_region_node *node = list_entry(elt, struct vpu_mem_region_node, list);
        if (vpu_mem_free_by_region(node))
            printk(KERN_INFO "vpu_mem: err on vpu_mem_free_by_region when vpu_mem_release\n");
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
    case VPU_MEM_CACHE_CLEAN:
    case VPU_MEM_CACHE_INVALID:
		{
			DLOG("flush\n");
			if (copy_from_user(&index, (void __user *)arg, sizeof(index)))
				return -EFAULT;

            down_write(&vpu_mem.bitmap_sem);
			vpu_mem_cache_opt(file, index, cmd);
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
					"(%d,%d) ",
					region_node->region.index,
					region_node->region.ref_count);
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
	vpu_mem_count++;

	vpu_mem.num_entries = vpu_mem.size / VPU_MEM_MIN_ALLOC;
	vpu_mem.bitmap = kzalloc(vpu_mem.num_entries *
				  sizeof(struct vpu_mem_bits), GFP_KERNEL);
	if (!vpu_mem.bitmap)
		goto err_no_mem_for_metadata;

    region_set(0, vpu_mem.num_entries);

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
	printk("%s: %d initialized\n", pdata->name, vpu_mem.dev.minor);
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
        if (vpu_mem.bitmap) {
            kfree(vpu_mem.bitmap);
            vpu_mem.bitmap = NULL;
        }
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

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static int proc_vpu_mem_show(struct seq_file *s, void *v)
{
	unsigned int i;

	if (vpu_mem.bitmap) {
		seq_printf(s, "vpu mem opened\n");
	} else {
		seq_printf(s, "vpu mem closed\n");
        return 0;
	}

    down_read(&vpu_mem.bitmap_sem);
    {
        // 打印 bitmap 中的全部 region
        for (i = 0; i < vpu_mem.num_entries; i = VPU_MEM_NEXT_INDEX(i)) {
            region_check(i);
            seq_printf(s, "vpu_mem: idx %6d pfn %6d refc %3d avail %3d\n",
                i, VPU_MEM_PFN(i), VPU_MEM_REFC(i), VPU_MEM_AVAIL(i));
        }

        // 打印 vpu_mem_data 中的全部 region
        down(&vpu_mem.data_list_sem);
        {   // search exists index
            struct list_head *list, *tmp_list;
            list_for_each_safe(list, tmp_list, &vpu_mem.data_list) {
                struct list_head *region, *tmp_data;
                struct vpu_mem_data *data = list_entry(list, struct vpu_mem_data, list);

                seq_printf(s, "pid: %d\n", data->pid);

                down_read(&data->sem);
                list_for_each_safe(region, tmp_data, &data->region_list) {
                    struct vpu_mem_region_node *node = list_entry(region, struct vpu_mem_region_node, list);
                    i = node->region.index;
                    seq_printf(s, "    region: idx %6d pfn %6d refc %3d avail %3d ref by %d\n",
                        i, VPU_MEM_PFN(i), VPU_MEM_REFC(i), VPU_MEM_AVAIL(i), node->region.ref_count);
                }
                up_read(&data->sem);
            }
        }
        up(&vpu_mem.data_list_sem);
    }
    up_read(&vpu_mem.bitmap_sem);
	return 0;
}

static int proc_vpu_mem_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_vpu_mem_show, NULL);
}

static const struct file_operations proc_vpu_mem_fops = {
	.open		= proc_vpu_mem_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init vpu_mem_proc_init(void)
{
	proc_create("vpu_mem", 0, NULL, &proc_vpu_mem_fops);
	return 0;

}
late_initcall(vpu_mem_proc_init);
#endif /* CONFIG_PROC_FS */

