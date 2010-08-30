/*
 * drivers/char/nvmap.c
 *
 * Memory manager for Tegra GPU memory handles
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
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

#define NV_DEBUG 0

#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/bitmap.h>
#include <linux/wait.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/uaccess.h>
#include <linux/backing-dev.h>
#include <linux/device.h>
#include <linux/highmem.h>
#include <linux/smp_lock.h>
#include <linux/pagemap.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/rbtree.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/nvmap.h>
#include <asm/tlbflush.h>
#include <linux/dma-mapping.h>
#include <asm/cacheflush.h>
#include <mach/iovmm.h>
#include "nvcommon.h"
#include "nvrm_memmgr.h"
#include "nvbootargs.h"


#ifndef NVMAP_BASE
#define NVMAP_BASE 0xFEE00000
#define NVMAP_SIZE SZ_2M
#endif

#define L_PTE_MT_INNER_WB       (0x05 << 2)     /* 0101 (armv6, armv7) */
#define pgprot_inner_writeback(prot) \
	__pgprot((pgprot_val(prot) & ~L_PTE_MT_MASK) | L_PTE_MT_INNER_WB)
static void smp_dma_clean_range(const void *start, const void *end)
{
	dmac_map_area(start, end - start, DMA_TO_DEVICE);
}

static void smp_dma_inv_range(const void *start, const void *end)
{
	dmac_unmap_area(start, end - start, DMA_FROM_DEVICE);
}

static void smp_dma_flush_range(const void *start, const void *end)
{
	dmac_flush_range(start, end);
}

static void nvmap_vma_open(struct vm_area_struct *vma);

static void nvmap_vma_close(struct vm_area_struct *vma);

static int nvmap_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf);

static int nvmap_open(struct inode *inode, struct file *filp);

static int nvmap_release(struct inode *inode, struct file *file);

static int nvmap_mmap(struct file *filp, struct vm_area_struct *vma);

static long nvmap_ioctl(struct file *filp,
	unsigned int cmd, unsigned long arg);

static int nvmap_ioctl_getid(struct file *filp, void __user *arg);

static int nvmap_ioctl_get_param(struct file *filp, void __user* arg);

static int nvmap_ioctl_alloc(struct file *filp, void __user *arg);

static int nvmap_ioctl_free(struct file *filp, unsigned long arg);

static int nvmap_ioctl_create(struct file *filp,
	unsigned int cmd, void __user *arg);

static int nvmap_ioctl_pinop(struct file *filp,
	bool is_pin, void __user *arg);

static int nvmap_ioctl_cache_maint(struct file *filp, void __user *arg);

static int nvmap_map_into_caller_ptr(struct file *filp, void __user *arg);

static int nvmap_ioctl_rw_handle(struct file *filp, int is_read,
	void __user* arg);

static struct backing_dev_info nvmap_bdi = {
	.ra_pages	= 0,
	.capabilities	= (BDI_CAP_NO_ACCT_AND_WRITEBACK |
			   BDI_CAP_READ_MAP | BDI_CAP_WRITE_MAP),
};

#define NVMAP_PTE_OFFSET(x) (((unsigned long)(x) - NVMAP_BASE) >> PAGE_SHIFT)
#define NVMAP_PTE_INDEX(x) (((unsigned long)(x) - NVMAP_BASE)>>PGDIR_SHIFT)
#define NUM_NVMAP_PTES (NVMAP_SIZE >> PGDIR_SHIFT)
#define NVMAP_END (NVMAP_BASE + NVMAP_SIZE)
#define NVMAP_PAGES (NVMAP_SIZE >> PAGE_SHIFT)

/* private nvmap_handle flag for pinning duplicate detection */
#define NVMEM_HANDLE_VISITED (0x1ul << 31)

/* Heaps to use for kernel allocs when no heap list supplied */
#define NVMAP_KERNEL_DEFAULT_HEAPS (NVMEM_HEAP_SYSMEM | NVMEM_HEAP_CARVEOUT_GENERIC)

/* Heaps for which secure allocations are allowed */
#define NVMAP_SECURE_HEAPS (NVMEM_HEAP_CARVEOUT_IRAM | NVMEM_HEAP_IOVMM)

static pte_t *nvmap_pte[NUM_NVMAP_PTES];
static unsigned long nvmap_ptebits[NVMAP_PAGES/BITS_PER_LONG];

static DEFINE_SPINLOCK(nvmap_ptelock);
static DECLARE_WAIT_QUEUE_HEAD(nvmap_ptefull);

/* used to lost the master tree of memory handles */
static DEFINE_SPINLOCK(nvmap_handle_lock);

/* only one task may be performing pin / unpin operations at once, to
 * prevent deadlocks caused by interleaved IOVMM re-allocations */
static DEFINE_MUTEX(nvmap_pin_lock);

/* queue of tasks which are blocking on pin, for IOVMM room */
static DECLARE_WAIT_QUEUE_HEAD(nvmap_pin_wait);
static struct rb_root nvmap_handles = RB_ROOT;

static struct tegra_iovmm_client *nvmap_vm_client = NULL;

/* default heap order policy */
static unsigned int _nvmap_heap_policy (unsigned int heaps, int numpages)
{
	static const unsigned int multipage_order[] = {
		NVMEM_HEAP_CARVEOUT_MASK,
		NVMEM_HEAP_SYSMEM,
		NVMEM_HEAP_IOVMM,
		0
	};
	static const unsigned int singlepage_order[] = {
		NVMEM_HEAP_SYSMEM,
		NVMEM_HEAP_CARVEOUT_MASK,
		NVMEM_HEAP_IOVMM,
		0
	};
	const unsigned int* order;

	if (numpages == 1)
		order = singlepage_order;
	else
		order = multipage_order;

	while (*order) {
		unsigned int h = (*order & heaps);
		if (h) return h;
		order++;
	}
	return 0;
};

/* first-fit linear allocator carveout heap manager */
struct nvmap_mem_block {
	unsigned long	base;
	size_t		size;
	short		next; /* next absolute (address-order) block */
	short		prev; /* previous absolute (address-order) block */
	short		next_free;
	short		prev_free;
};

struct nvmap_carveout {
	unsigned short		num_blocks;
	short			spare_index;
	short			free_index;
	short			block_index;
	spinlock_t		lock;
	const char		*name;
	struct nvmap_mem_block	*blocks;
};

enum {
	CARVEOUT_STAT_TOTAL_SIZE,
	CARVEOUT_STAT_FREE_SIZE,
	CARVEOUT_STAT_NUM_BLOCKS,
	CARVEOUT_STAT_FREE_BLOCKS,
	CARVEOUT_STAT_LARGEST_BLOCK,
	CARVEOUT_STAT_LARGEST_FREE,
	CARVEOUT_STAT_BASE,
};

static inline pgprot_t _nvmap_flag_to_pgprot(unsigned long flag, pgprot_t base)
{
	switch (flag) {
	case NVMEM_HANDLE_UNCACHEABLE:
		base = pgprot_noncached(base);
		break;
	case NVMEM_HANDLE_WRITE_COMBINE:
		base = pgprot_writecombine(base);
		break;
	case NVMEM_HANDLE_INNER_CACHEABLE:
		base = pgprot_inner_writeback(base);
		break;
	}
	return base;
}

static unsigned long _nvmap_carveout_blockstat(struct nvmap_carveout *co,
	int stat)
{
	unsigned long val = 0;
	short idx;
	spin_lock(&co->lock);

	if (stat==CARVEOUT_STAT_BASE) {
		if (co->block_index==-1)
			val = ~0;
		else
			val = co->blocks[co->block_index].base;
		spin_unlock(&co->lock);
		return val;
	}

	if (stat==CARVEOUT_STAT_TOTAL_SIZE ||
	    stat==CARVEOUT_STAT_NUM_BLOCKS ||
	    stat==CARVEOUT_STAT_LARGEST_BLOCK)
		idx = co->block_index;
	else
		idx = co->free_index;

	while (idx!=-1) {
		switch (stat) {
		case CARVEOUT_STAT_TOTAL_SIZE:
			val += co->blocks[idx].size;
			idx = co->blocks[idx].next;
			break;
		case CARVEOUT_STAT_NUM_BLOCKS:
			val++;
			idx = co->blocks[idx].next;
			break;
		case CARVEOUT_STAT_LARGEST_BLOCK:
			val = max_t(unsigned long, val, co->blocks[idx].size);
			idx = co->blocks[idx].next;
			break;
		case CARVEOUT_STAT_FREE_SIZE:
			val += co->blocks[idx].size;
			idx = co->blocks[idx].next_free;
			break;
		case CARVEOUT_STAT_FREE_BLOCKS:
			val ++;
			idx = co->blocks[idx].next_free;
			break;
		case CARVEOUT_STAT_LARGEST_FREE:
			val = max_t(unsigned long, val, co->blocks[idx].size);
			idx = co->blocks[idx].next_free;
			break;
	    }
	}

	spin_unlock(&co->lock);
	return val;
}

#define co_is_free(_co, _idx) \
	((_co)->free_index==(_idx) || ((_co)->blocks[(_idx)].prev_free!=-1))

static int _nvmap_init_carveout(struct nvmap_carveout *co,
	const char *name, unsigned long base_address, size_t len)
{
	unsigned int num_blocks;
	struct nvmap_mem_block *blocks = NULL;
	int i;

	num_blocks = min_t(unsigned int, len/1024, 1024);
	blocks = vmalloc(sizeof(*blocks)*num_blocks);

	if (!blocks) goto fail;
	co->name = kstrdup(name, GFP_KERNEL);
	if (!co->name) goto fail;

	for (i=1; i<num_blocks; i++) {
		blocks[i].next = i+1;
		blocks[i].prev = i-1;
		blocks[i].next_free = -1;
		blocks[i].prev_free = -1;
	}
	blocks[i-1].next = -1;
	blocks[1].prev = -1;

	blocks[0].next = blocks[0].prev = -1;
	blocks[0].next_free = blocks[0].prev_free = -1;
	blocks[0].base = base_address;
	blocks[0].size = len;
	co->blocks = blocks;
	co->num_blocks = num_blocks;
	spin_lock_init(&co->lock);
	co->block_index = 0;
	co->spare_index = 1;
	co->free_index = 0;
	return 0;

fail:
	if (blocks) kfree(blocks);
	return -ENOMEM;
}

static int nvmap_get_spare(struct nvmap_carveout *co) {
	int idx;

	if (co->spare_index == -1)
		return -1;

	idx = co->spare_index;
	co->spare_index = co->blocks[idx].next;
	co->blocks[idx].next = -1;
	co->blocks[idx].prev = -1;
	co->blocks[idx].next_free = -1;
	co->blocks[idx].prev_free = -1;
	return idx;
}

#define BLOCK(_co, _idx) ((_idx)==-1 ? NULL : &(_co)->blocks[(_idx)])

static void nvmap_zap_free(struct nvmap_carveout *co, int idx)
{
	struct nvmap_mem_block *block;

	block = BLOCK(co, idx);
	if (block->prev_free != -1)
		BLOCK(co, block->prev_free)->next_free = block->next_free;
	else
		co->free_index = block->next_free;

	if (block->next_free != -1)
		BLOCK(co, block->next_free)->prev_free = block->prev_free;

	block->prev_free = -1;
	block->next_free = -1;
}

static void nvmap_split_block(struct nvmap_carveout *co,
	int idx, size_t start, size_t size)
{
	if (BLOCK(co, idx)->base < start) {
		int spare_idx = nvmap_get_spare(co);
		struct nvmap_mem_block *spare = BLOCK(co, spare_idx);
		struct nvmap_mem_block *block = BLOCK(co, idx);
		if (spare) {
			spare->size = start - block->base;
			spare->base = block->base;
			block->size -= (start - block->base);
			block->base = start;
			spare->next = idx;
			spare->prev = block->prev;
			block->prev = spare_idx;
			if (spare->prev != -1)
				co->blocks[spare->prev].next = spare_idx;
			else
				co->block_index = spare_idx;
			spare->prev_free = -1;
			spare->next_free = co->free_index;
			if (co->free_index != -1)
				co->blocks[co->free_index].prev_free = spare_idx;
			co->free_index = spare_idx;
		} else {
			if (block->prev != -1) {
				spare = BLOCK(co, block->prev);
				spare->size += start - block->base;
				block->base = start;
			}
		}
	}

	if (BLOCK(co, idx)->size > size) {
		int spare_idx = nvmap_get_spare(co);
		struct nvmap_mem_block *spare = BLOCK(co, spare_idx);
		struct nvmap_mem_block *block = BLOCK(co, idx);
		if (spare) {
			spare->base = block->base + size;
			spare->size = block->size - size;
			block->size = size;
			spare->prev = idx;
			spare->next = block->next;
			block->next = spare_idx;
			if (spare->next != -1)
				co->blocks[spare->next].prev = spare_idx;
			spare->prev_free = -1;
			spare->next_free = co->free_index;
			if (co->free_index != -1)
				co->blocks[co->free_index].prev_free = spare_idx;
			co->free_index = spare_idx;
		}
	}

	nvmap_zap_free(co, idx);
}

#define next_spare next
#define prev_spare prev

#define nvmap_insert_block(_list, _co, _idx)				\
	do {								\
		struct nvmap_mem_block *b = BLOCK((_co), (_idx));	\
		struct nvmap_mem_block *s = BLOCK((_co), (_co)->_list##_index);\
		if (s) s->prev_##_list = (_idx);			\
		b->prev_##_list = -1;					\
		b->next_##_list = (_co)->_list##_index;			\
		(_co)->_list##_index = (_idx);				\
	} while (0);

static void nvmap_carveout_free(struct nvmap_carveout *co, int idx)
{
	struct nvmap_mem_block *b;

	spin_lock(&co->lock);

	b = BLOCK(co, idx);

	if (b->next!=-1 && co_is_free(co, b->next)) {
		int zap = b->next;
		struct nvmap_mem_block *n = BLOCK(co, zap);
		b->size += n->size;

		b->next = n->next;
		if (n->next != -1) co->blocks[n->next].prev = idx;

		nvmap_zap_free(co, zap);
		nvmap_insert_block(spare, co, zap);
	}

	if (b->prev!=-1 && co_is_free(co, b->prev)) {
		int zap = b->prev;
		struct nvmap_mem_block *p = BLOCK(co, zap);

		b->base = p->base;
		b->size += p->size;

		b->prev = p->prev;

		if (p->prev != -1) co->blocks[p->prev].next = idx;
		else co->block_index = idx;

		nvmap_zap_free(co, zap);
		nvmap_insert_block(spare, co, zap);
	}

	nvmap_insert_block(free, co, idx);
	spin_unlock(&co->lock);
}

static int nvmap_carveout_alloc(struct nvmap_carveout *co,
	size_t align, size_t size)
{
	short idx;

	spin_lock(&co->lock);

	idx = co->free_index;

	while (idx != -1) {
		struct nvmap_mem_block *b = BLOCK(co, idx);
		/* try to be a bit more clever about generating block-
		 * droppings by comparing the results of a left-justified vs
		 * right-justified block split, and choosing the
		 * justification style which yields the largest remaining
		 * block */
		size_t end = b->base + b->size;
		size_t ljust = (b->base + align - 1) & ~(align-1);
		size_t rjust = (end - size) & ~(align-1);
		size_t l_max, r_max;

		if (rjust < b->base) rjust = ljust;
		l_max = max_t(size_t, ljust - b->base, end - (ljust + size));
		r_max = max_t(size_t, rjust - b->base, end - (rjust + size));

		if (b->base + b->size >= ljust + size) {
			if (l_max >= r_max)
				nvmap_split_block(co, idx, ljust, size);
			else
				nvmap_split_block(co, idx, rjust, size);
			break;
		}
		idx = b->next_free;
	}

	spin_unlock(&co->lock);
	return idx;
}

#undef next_spare
#undef prev_spare

#define NVDA_POISON (('n'<<24) | ('v'<<16) | ('d'<<8) | ('a'))

struct nvmap_handle {
	struct rb_node node;
	atomic_t ref;
	atomic_t pin;
	unsigned long flags;
	size_t size;
	size_t orig_size;
	struct task_struct *owner;
	unsigned int poison;
	union {
		struct {
			struct page **pages;
			struct tegra_iovmm_area *area;
			struct list_head mru_list;
			bool contig;
			bool dirty; /* IOVMM area allocated since last pin */
		} pgalloc;
		struct {
			struct nvmap_carveout *co_heap;
			int block_idx;
			unsigned long base;
			unsigned int key; /* preserved by bootloader */
		} carveout;
	};
	bool global;
	bool secure; /* only allocated in IOVM space, zapped on unpin */
	bool heap_pgalloc;
	bool alloc;
	void *kern_map; /* used for RM memmgr backwards compat */
};

/* handle_ref objects are file-descriptor-local references to nvmap_handle
 * objects. they track the number of references and pins performed by
 * the specific caller (since nvmap_handle objects may be global), so that
 * a client which terminates without properly unwinding all handles (or
 * all nested pins) can be unwound by nvmap. */
struct nvmap_handle_ref {
	struct nvmap_handle *h;
	struct rb_node node;
	atomic_t refs;
	atomic_t pin;
};

struct nvmap_file_priv {
	struct rb_root handle_refs;
	atomic_t iovm_commit;
	size_t iovm_limit;
	spinlock_t ref_lock;
	bool su;
};

struct nvmap_carveout_node {
	struct device		dev;
	struct list_head	heap_list;
	unsigned int		heap_bit;
	struct nvmap_carveout	carveout;
};

/* the master structure for all nvmap-managed carveouts and all handle_ref
 * objects allocated inside the kernel. heaps are sorted by their heap_bit
 * (highest heap_bit first) so that carveout allocation will be first
 * attempted by the heap with the highest heap_bit set in the allocation's
 * heap mask */
static struct {
	struct nvmap_file_priv init_data;
	struct rw_semaphore list_sem;
	struct list_head heaps;
} nvmap_context;

static struct vm_operations_struct nvmap_vma_ops = {
	.open	= nvmap_vma_open,
	.close	= nvmap_vma_close,
	.fault	= nvmap_vma_fault,
};

const struct file_operations nvmap_fops = {
	.owner		= THIS_MODULE,
	.open		= nvmap_open,
	.release	= nvmap_release,
	.unlocked_ioctl = nvmap_ioctl,
	.mmap		= nvmap_mmap
};

const struct file_operations knvmap_fops = {
	.owner		= THIS_MODULE,
	.open		= nvmap_open,
	.release	= nvmap_release,
	.unlocked_ioctl = nvmap_ioctl,
	.mmap		= nvmap_mmap
};

struct nvmap_vma_priv {
	struct nvmap_handle	*h;
	size_t			offs;
	atomic_t		ref;
};

static struct proc_dir_entry *nvmap_procfs_root;
static struct proc_dir_entry *nvmap_procfs_proc;

static void _nvmap_handle_free(struct nvmap_handle *h);

#define NVMAP_CARVEOUT_ATTR_RO(_name)					\
	struct device_attribute nvmap_heap_attr_##_name =		\
		__ATTR(_name, S_IRUGO, _nvmap_sysfs_show_heap_##_name, NULL)

#define NVMAP_CARVEOUT_ATTR_WO(_name, _mode)				\
	struct device_attribute nvmap_heap_attr_##_name =		\
		__ATTR(_name, _mode, NULL, _nvmap_sysfs_set_heap_##_name)


static ssize_t _nvmap_sysfs_show_heap_usage(struct device *d,
	struct device_attribute *attr, char *buf)
{
	struct nvmap_carveout_node *c = container_of(d,
		struct nvmap_carveout_node, dev);
	return sprintf(buf, "%08x\n", c->heap_bit);
}

static ssize_t _nvmap_sysfs_show_heap_name(struct device *d,
	struct device_attribute *attr, char *buf)
{
	struct nvmap_carveout_node *c = container_of(d,
		struct nvmap_carveout_node, dev);
	return sprintf(buf, "%s\n", c->carveout.name);
}

static ssize_t _nvmap_sysfs_show_heap_base(struct device *d,
	struct device_attribute *attr, char *buf)
{
	struct nvmap_carveout_node *c = container_of(d,
		struct nvmap_carveout_node, dev);
	return sprintf(buf, "%08lx\n",
		_nvmap_carveout_blockstat(&c->carveout, CARVEOUT_STAT_BASE));
}

static ssize_t _nvmap_sysfs_show_heap_free_size(struct device *d,
	struct device_attribute *attr, char *buf)
{
	struct nvmap_carveout_node *c = container_of(d,
		struct nvmap_carveout_node, dev);
	return sprintf(buf, "%lu\n",
		_nvmap_carveout_blockstat(&c->carveout,
			CARVEOUT_STAT_FREE_SIZE));
}

static ssize_t _nvmap_sysfs_show_heap_free_count(struct device *d,
	struct device_attribute *attr, char *buf)
{
	struct nvmap_carveout_node *c = container_of(d,
		struct nvmap_carveout_node, dev);
	return sprintf(buf, "%lu\n",
		_nvmap_carveout_blockstat(&c->carveout,
			CARVEOUT_STAT_FREE_BLOCKS));
}

static ssize_t _nvmap_sysfs_show_heap_free_max(struct device *d,
	struct device_attribute *attr, char *buf)
{
	struct nvmap_carveout_node *c = container_of(d,
		struct nvmap_carveout_node, dev);
	return sprintf(buf, "%lu\n",
		_nvmap_carveout_blockstat(&c->carveout,
			CARVEOUT_STAT_LARGEST_FREE));
}

static ssize_t _nvmap_sysfs_show_heap_total_count(struct device *d,
	struct device_attribute *attr, char *buf)
{
	struct nvmap_carveout_node *c = container_of(d,
		struct nvmap_carveout_node, dev);
	return sprintf(buf, "%lu\n",
		_nvmap_carveout_blockstat(&c->carveout,
			CARVEOUT_STAT_NUM_BLOCKS));
}

static ssize_t _nvmap_sysfs_show_heap_total_max(struct device *d,
	struct device_attribute *attr, char *buf)
{
	struct nvmap_carveout_node *c = container_of(d,
		struct nvmap_carveout_node, dev);
	return sprintf(buf, "%lu\n",
		_nvmap_carveout_blockstat(&c->carveout,
			CARVEOUT_STAT_LARGEST_BLOCK));
}

static ssize_t _nvmap_sysfs_show_heap_total_size(struct device *d,
	struct device_attribute *attr, char *buf)
{
	struct nvmap_carveout_node *c = container_of(d,
		struct nvmap_carveout_node, dev);
	return sprintf(buf, "%lu\n",
		_nvmap_carveout_blockstat(&c->carveout,
			CARVEOUT_STAT_TOTAL_SIZE));
}

static int nvmap_split_carveout_heap(struct nvmap_carveout *co, size_t size,
	const char *name, unsigned int new_bitmask);

static ssize_t _nvmap_sysfs_set_heap_split(struct device *d,
	struct device_attribute *attr, const char * buf, size_t count)
{
	struct nvmap_carveout_node *c = container_of(d,
		struct nvmap_carveout_node, dev);
	char *tmp, *local = kzalloc(count+1, GFP_KERNEL);
	char *sizestr = NULL, *bitmaskstr = NULL, *name = NULL;
	char **format[] = { &sizestr, &bitmaskstr, &name };
	char ***f_iter = format;
	unsigned int i;
	unsigned long size, bitmask;
	int err;

	if (!local) {
		pr_err("%s: unable to read string\n", __func__);
		return -ENOMEM;
	}

	memcpy(local, buf, count);
	tmp = local;
	for (i=0, **f_iter = local; i<count &&
	    (f_iter - format)<ARRAY_SIZE(format)-1; i++) {
		if (local[i]==',') {
			local[i] = '\0';
			f_iter++;
			**f_iter = &local[i+1];
		}
	}

	if (!sizestr || !bitmaskstr || !name) {
		pr_err("%s: format error\n", __func__);
		kfree(tmp);
		return -EINVAL;
	}

	for (local=name; !isspace(*local); local++);

	if (local==name) {
		pr_err("%s: invalid name %s\n", __func__, name);
		kfree(tmp);
		return -EINVAL;
	}

	*local=0;

	size = memparse(sizestr, &sizestr);
	if (!size) {
		kfree(tmp);
		return -EINVAL;
	}

	if (strict_strtoul(bitmaskstr, 0, &bitmask)==-EINVAL) {
		kfree(tmp);
		return -EINVAL;
	}

	err = nvmap_split_carveout_heap(&c->carveout, size, name, bitmask);

	if (err) pr_err("%s: failed to create split heap %s\n", __func__, name);
	kfree(tmp);
	return err ? err : count;
}

static NVMAP_CARVEOUT_ATTR_RO(usage);
static NVMAP_CARVEOUT_ATTR_RO(name);
static NVMAP_CARVEOUT_ATTR_RO(base);
static NVMAP_CARVEOUT_ATTR_RO(free_size);
static NVMAP_CARVEOUT_ATTR_RO(free_count);
static NVMAP_CARVEOUT_ATTR_RO(free_max);
static NVMAP_CARVEOUT_ATTR_RO(total_size);
static NVMAP_CARVEOUT_ATTR_RO(total_count);
static NVMAP_CARVEOUT_ATTR_RO(total_max);
static NVMAP_CARVEOUT_ATTR_WO(split, (S_IWUSR | S_IWGRP));

static struct attribute *nvmap_heap_default_attrs[] = {
	&nvmap_heap_attr_usage.attr,
	&nvmap_heap_attr_name.attr,
	&nvmap_heap_attr_split.attr,
	&nvmap_heap_attr_base.attr,
	&nvmap_heap_attr_total_size.attr,
	&nvmap_heap_attr_free_size.attr,
	&nvmap_heap_attr_total_count.attr,
	&nvmap_heap_attr_free_count.attr,
	&nvmap_heap_attr_total_max.attr,
	&nvmap_heap_attr_free_max.attr,
	NULL
};

static struct attribute_group nvmap_heap_defattr_group = {
	.attrs = nvmap_heap_default_attrs
};

static struct device *__nvmap_heap_parent_dev(void);
#define _nvmap_heap_parent_dev __nvmap_heap_parent_dev()

/* unpinned I/O VMM areas may be reclaimed by nvmap to make room for
 * new surfaces. unpinned surfaces are stored in segregated linked-lists
 * sorted in most-recently-unpinned order (i.e., head insertion, head
 * removal */
#ifdef CONFIG_DEVNVMAP_RECLAIM_UNPINNED_VM
static DEFINE_SPINLOCK(nvmap_mru_vma_lock);
static const size_t nvmap_mru_cutoff[] = {
	262144, 393216, 786432, 1048576, 1572864
};

static struct list_head nvmap_mru_vma_lists[ARRAY_SIZE(nvmap_mru_cutoff)];

static inline struct list_head *_nvmap_list(size_t size)
{
	unsigned int i;

	for (i=0; i<ARRAY_SIZE(nvmap_mru_cutoff); i++)
		if (size <= nvmap_mru_cutoff[i]) return &nvmap_mru_vma_lists[i];

	return &nvmap_mru_vma_lists[ARRAY_SIZE(nvmap_mru_cutoff)-1];
}
#endif

static inline struct nvmap_handle *_nvmap_handle_get(struct nvmap_handle *h)
{
	if (unlikely(h->poison!=NVDA_POISON)) {
		pr_err("%s: %s getting poisoned handle\n", __func__,
			current->group_leader->comm);
		return NULL;
	} else if (unlikely(atomic_inc_return(&h->ref)<=1)) {
		pr_err("%s: %s getting a freed handle\n",
			__func__, current->group_leader->comm);
		return NULL;
	}
	return h;
}

static inline void _nvmap_handle_put(struct nvmap_handle *h)
{
	int cnt = atomic_dec_return(&h->ref);

	if (unlikely(cnt<0)) {
		pr_err("%s: %s put to negative references\n",
			__func__, current->comm);
		dump_stack();
	} else if (!cnt) _nvmap_handle_free(h);
}

static struct nvmap_handle *_nvmap_claim_preserved(
	struct task_struct *new_owner, unsigned long key)
{
	struct rb_node *n;
	struct nvmap_handle *b = NULL;

	if (!key) return NULL;

	spin_lock(&nvmap_handle_lock);
	n = rb_first(&nvmap_handles);

	while (n) {
		b = rb_entry(n, struct nvmap_handle, node);
		if (b->alloc && !b->heap_pgalloc && b->carveout.key == key) {
			b->carveout.key = 0;
			b->owner = new_owner;
			break;
		}
		b = NULL;
		n = rb_next(n);
	}

	spin_unlock(&nvmap_handle_lock);
	return b;
}

static struct nvmap_handle *_nvmap_validate_get(unsigned long handle, bool su)
{
	struct nvmap_handle *b = NULL;

#ifdef CONFIG_DEVNVMAP_PARANOID
	struct rb_node *n;

	spin_lock(&nvmap_handle_lock);

	n = nvmap_handles.rb_node;

	while (n) {
		b = rb_entry(n, struct nvmap_handle, node);
		if ((unsigned long)b == handle) {
			if (su || b->global || b->owner==current->group_leader)
				b = _nvmap_handle_get(b);
			else
				b = NULL;
			spin_unlock(&nvmap_handle_lock);
			return b;
		}
		if (handle > (unsigned long)b) n = n->rb_right;
		else n = n->rb_left;
	}
	spin_unlock(&nvmap_handle_lock);
	return NULL;
#else
	if (!handle) return NULL;
	b = _nvmap_handle_get((struct nvmap_handle *)handle);
	return b;
#endif
}

/*  nvmap_mru_vma_lock should be acquired by the caller before calling this */
static inline void _nvmap_insert_mru_vma(struct nvmap_handle *h)
{
#ifdef CONFIG_DEVNVMAP_RECLAIM_UNPINNED_VM
	list_add(&h->pgalloc.mru_list, _nvmap_list(h->pgalloc.area->iovm_length));
#endif
}

static void _nvmap_remove_mru_vma(struct nvmap_handle *h)
{
#ifdef CONFIG_DEVNVMAP_RECLAIM_UNPINNED_VM
	spin_lock(&nvmap_mru_vma_lock);
	if (!list_empty(&h->pgalloc.mru_list))
		list_del(&h->pgalloc.mru_list);
	spin_unlock(&nvmap_mru_vma_lock);
	INIT_LIST_HEAD(&h->pgalloc.mru_list);
#endif
}

static struct tegra_iovmm_area *_nvmap_get_vm(struct nvmap_handle *h)
{
#ifndef CONFIG_DEVNVMAP_RECLAIM_UNPINNED_VM
	BUG_ON(!h->pgalloc.area);
	BUG_ON(h->size > h->pgalloc.area->iovm_length);
	BUG_ON((h->size | h->pgalloc.area->iovm_length) & ~PAGE_MASK);
	return h->pgalloc.area;
#else
	struct list_head *mru;
	struct nvmap_handle *evict = NULL;
	struct tegra_iovmm_area *vm = NULL;
	unsigned int i, idx;

	if (h->pgalloc.area) {
		spin_lock(&nvmap_mru_vma_lock);
		BUG_ON(list_empty(&h->pgalloc.mru_list));
		list_del(&h->pgalloc.mru_list);
		INIT_LIST_HEAD(&h->pgalloc.mru_list);
		spin_unlock(&nvmap_mru_vma_lock);
		return h->pgalloc.area;
	}

	vm = tegra_iovmm_create_vm(nvmap_vm_client, NULL, h->size,
		_nvmap_flag_to_pgprot(h->flags, pgprot_kernel));

	if (vm) {
		INIT_LIST_HEAD(&h->pgalloc.mru_list);
		return vm;
	}
	/* attempt to re-use the most recently unpinned IOVMM area in the
	 * same size bin as the current handle. If that fails, iteratively
	 * evict handles (starting from the current bin) until an allocation
	 * succeeds or no more areas can be evicted */

	spin_lock(&nvmap_mru_vma_lock);
	mru = _nvmap_list(h->size);
	if (!list_empty(mru))
		evict = list_first_entry(mru, struct nvmap_handle,
			pgalloc.mru_list);
	if (evict && evict->pgalloc.area->iovm_length >= h->size) {
		list_del(&evict->pgalloc.mru_list);
		vm = evict->pgalloc.area;
		evict->pgalloc.area = NULL;
		INIT_LIST_HEAD(&evict->pgalloc.mru_list);
		spin_unlock(&nvmap_mru_vma_lock);
		return vm;
	}

	idx = mru - nvmap_mru_vma_lists;

	for (i=0; i<ARRAY_SIZE(nvmap_mru_vma_lists) && !vm; i++, idx++) {
		if (idx >= ARRAY_SIZE(nvmap_mru_vma_lists))
			idx -= ARRAY_SIZE(nvmap_mru_vma_lists);
		mru = &nvmap_mru_vma_lists[idx];
		while (!list_empty(mru) && !vm) {
			evict = list_first_entry(mru, struct nvmap_handle,
				pgalloc.mru_list);

			BUG_ON(atomic_add_return(0, &evict->pin)!=0);
			BUG_ON(!evict->pgalloc.area);
			list_del(&evict->pgalloc.mru_list);
			INIT_LIST_HEAD(&evict->pgalloc.mru_list);
			spin_unlock(&nvmap_mru_vma_lock);
			tegra_iovmm_free_vm(evict->pgalloc.area);
			evict->pgalloc.area = NULL;
			vm = tegra_iovmm_create_vm(nvmap_vm_client,
				NULL, h->size,
				_nvmap_flag_to_pgprot(h->flags, pgprot_kernel));
			spin_lock(&nvmap_mru_vma_lock);
		}
	}
	spin_unlock(&nvmap_mru_vma_lock);
	return vm;
#endif
}

static int _nvmap_do_cache_maint(struct nvmap_handle *h,
	unsigned long start, unsigned long end, unsigned long op, bool get);

void _nvmap_handle_free(struct nvmap_handle *h)
{
	int e;
	spin_lock(&nvmap_handle_lock);

	/* if 2 contexts call _get and _put simultaneously, the reference
	 * count may drop to 0 and then increase to 1 before the handle
	 * can be freed. */
	if (atomic_add_return(0, &h->ref)>0) {
		spin_unlock(&nvmap_handle_lock);
		return;
	}
	smp_rmb();
	BUG_ON(atomic_read(&h->ref)<0);
	BUG_ON(atomic_read(&h->pin)!=0);

	rb_erase(&h->node, &nvmap_handles);

	spin_unlock(&nvmap_handle_lock);

	if (h->owner) put_task_struct(h->owner);

	/* remove when NvRmMemMgr compatibility is eliminated */
	if (h->kern_map) {
		BUG_ON(!h->alloc);
		if (h->heap_pgalloc)
			vm_unmap_ram(h->kern_map, h->size>>PAGE_SHIFT);
		else {
			unsigned long addr = (unsigned long)h->kern_map;
			addr &= ~PAGE_MASK;
			iounmap((void *)addr);
		}
	}

	/* ensure that no stale data remains in the cache for this handle */
	e = _nvmap_do_cache_maint(h, 0, h->size, NVMEM_CACHE_OP_WB_INV, false);

	if (h->alloc && !h->heap_pgalloc)
		nvmap_carveout_free(h->carveout.co_heap, h->carveout.block_idx);
	else if (h->alloc) {
		unsigned int i;
		BUG_ON(h->size & ~PAGE_MASK);
		BUG_ON(!h->pgalloc.pages);
		_nvmap_remove_mru_vma(h);
		if (h->pgalloc.area) tegra_iovmm_free_vm(h->pgalloc.area);
		for (i=0; i<h->size>>PAGE_SHIFT; i++) {
			ClearPageReserved(h->pgalloc.pages[i]);
			__free_page(h->pgalloc.pages[i]);
		}
		if ((h->size>>PAGE_SHIFT)*sizeof(struct page*)>=PAGE_SIZE)
			vfree(h->pgalloc.pages);
		else
			kfree(h->pgalloc.pages);
	}
	h->poison = 0xa5a5a5a5;
	kfree(h);
}

#define nvmap_gfp (GFP_KERNEL | __GFP_HIGHMEM | __GFP_NOWARN)

/* map the backing pages for a heap_pgalloc handle into its IOVMM area */
static void _nvmap_handle_iovmm_map(struct nvmap_handle *h)
{
	tegra_iovmm_addr_t va;
	unsigned long i;

	BUG_ON(!h->heap_pgalloc || !h->pgalloc.area);
	BUG_ON(h->size & ~PAGE_MASK);
	WARN_ON(!h->pgalloc.dirty);

	for (va = h->pgalloc.area->iovm_start, i=0;
	    va < (h->pgalloc.area->iovm_start + h->size);
	    i++, va+=PAGE_SIZE) {
		BUG_ON(!pfn_valid(page_to_pfn(h->pgalloc.pages[i])));
		tegra_iovmm_vm_insert_pfn(h->pgalloc.area, va,
			page_to_pfn(h->pgalloc.pages[i]));
	}
	h->pgalloc.dirty = false;
}

static int nvmap_pagealloc(struct nvmap_handle *h, bool contiguous)
{
	unsigned int i = 0, cnt = (h->size + PAGE_SIZE - 1) >> PAGE_SHIFT;
	struct page **pages;

	if (cnt*sizeof(*pages)>=PAGE_SIZE)
		pages = vmalloc(cnt*sizeof(*pages));
	else
		pages = kzalloc(sizeof(*pages)*cnt, GFP_KERNEL);

	if (!pages) return -ENOMEM;

	if (contiguous) {
		size_t order = get_order(h->size);
		struct page *compound_page;
		compound_page = alloc_pages(nvmap_gfp, order);
		if (!compound_page) goto fail;
		split_page(compound_page, order);
		for (i=0; i<cnt; i++)
			pages[i] = nth_page(compound_page, i);
		for (; i<(1<<order); i++)
			__free_page(nth_page(compound_page, i));
	} else {
		for (i=0; i<cnt; i++) {
			pages[i] = alloc_page(nvmap_gfp);
			if (!pages[i]) {
			    pr_err("failed to allocate %u pages after %u entries\n",
				   cnt, i);
			    goto fail;
			}
		}
	}

	h->pgalloc.area = NULL;
#ifndef CONFIG_DEVNVMAP_RECLAIM_UNPINNED_VM
	if (!contiguous) {
		h->pgalloc.area = tegra_iovmm_create_vm(nvmap_vm_client,
			NULL, cnt << PAGE_SHIFT,
			_nvmap_flag_to_pgprot(h->flags, pgprot_kernel));
		if (!h->pgalloc.area) goto fail;
		h->pgalloc.dirty = true;
	}
#endif

	for (i=0; i<cnt; i++) {
		void *km;
		SetPageReserved(pages[i]);
		km = kmap(pages[i]);
		if (km) __cpuc_flush_dcache_area(km, PAGE_SIZE);
		outer_flush_range(page_to_phys(pages[i]),
			page_to_phys(pages[i])+PAGE_SIZE);
		kunmap(pages[i]);
	}

	h->size = cnt<<PAGE_SHIFT;
	h->pgalloc.pages = pages;
	h->pgalloc.contig = contiguous;
	INIT_LIST_HEAD(&h->pgalloc.mru_list);
	return 0;

fail:
	while (i--) __free_page(pages[i]);
	if (pages && (cnt*sizeof(*pages)>=PAGE_SIZE)) vfree(pages);
	else if (pages) kfree(pages);
	return -ENOMEM;
}

static struct nvmap_handle *_nvmap_handle_create(
	struct task_struct *owner, size_t size)
{
	struct nvmap_handle *h = kzalloc(sizeof(*h), GFP_KERNEL);
	struct nvmap_handle *b;
	struct rb_node **p;
	struct rb_node *parent = NULL;

	if (!h) return NULL;
	atomic_set(&h->ref, 1);
	atomic_set(&h->pin, 0);
	h->owner = owner;
	h->size = h->orig_size = size;
	h->flags = NVMEM_HANDLE_WRITE_COMBINE;
	h->poison = NVDA_POISON;

	spin_lock(&nvmap_handle_lock);
	p = &nvmap_handles.rb_node;
	while (*p) {
		parent = *p;
		b = rb_entry(parent, struct nvmap_handle, node);
		if (h > b) p = &parent->rb_right;
		else p = &parent->rb_left;
	}
	rb_link_node(&h->node, parent, p);
	rb_insert_color(&h->node, &nvmap_handles);
	spin_unlock(&nvmap_handle_lock);
	if (owner) get_task_struct(owner);
	return h;
}

/* nvmap pte manager */

static void _nvmap_set_pte_at(unsigned long addr, unsigned long pfn,
	pgprot_t prot)
{
	u32 off;
	int idx;
	pte_t *pte;

	BUG_ON(!addr);
	idx = NVMAP_PTE_INDEX(addr);
	off = NVMAP_PTE_OFFSET(addr) & (PTRS_PER_PTE-1);

	pte = nvmap_pte[idx] + off;
	set_pte_ext(pte, pfn_pte(pfn, prot), 0);
	flush_tlb_kernel_page(addr);
}

static int _nvmap_map_pte(unsigned long pfn, pgprot_t prot, void **vaddr)
{
	static unsigned int last_bit = 0;
	unsigned long bit;
	unsigned long addr;
	unsigned long flags;

	spin_lock_irqsave(&nvmap_ptelock, flags);

	bit = find_next_zero_bit(nvmap_ptebits, NVMAP_PAGES, last_bit);
	if (bit==NVMAP_PAGES) {
		bit = find_first_zero_bit(nvmap_ptebits, last_bit);
		if (bit == last_bit) bit = NVMAP_PAGES;
	}

	if (bit==NVMAP_PAGES) {
		spin_unlock_irqrestore(&nvmap_ptelock, flags);
		return -ENOMEM;
	}

	last_bit = bit;
	set_bit(bit, nvmap_ptebits);
	spin_unlock_irqrestore(&nvmap_ptelock, flags);

	addr = NVMAP_BASE + bit*PAGE_SIZE;

	_nvmap_set_pte_at(addr, pfn, prot);
	*vaddr = (void *)addr;
	return 0;
}

static int nvmap_map_pte(unsigned long pfn, pgprot_t prot, void **addr)
{
	int ret;
	ret = wait_event_interruptible(nvmap_ptefull,
		!_nvmap_map_pte(pfn, prot, addr));

	if (ret==-ERESTARTSYS) return -EINTR;
	return ret;
}

static void nvmap_unmap_pte(void *addr)
{
	unsigned long bit = NVMAP_PTE_OFFSET(addr);
	unsigned long flags;

	/* the ptes aren't cleared in this function, since the address isn't
	 * re-used until it is allocated again by nvmap_map_pte. */
	BUG_ON(bit >= NVMAP_PAGES);
	spin_lock_irqsave(&nvmap_ptelock, flags);
	clear_bit(bit, nvmap_ptebits);
	spin_unlock_irqrestore(&nvmap_ptelock, flags);
	wake_up(&nvmap_ptefull);
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

	atomic_inc(&priv->ref);
}

static void nvmap_vma_close(struct vm_area_struct *vma) {
	struct nvmap_vma_priv *priv = vma->vm_private_data;

	if (priv && !atomic_dec_return(&priv->ref)) {
		if (priv->h) _nvmap_handle_put(priv->h);
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
	if (!priv || !priv->h || !priv->h->alloc)
		return VM_FAULT_SIGBUS;

	offs += priv->offs;
	/* if the VMA was split for some reason, vm_pgoff will be the VMA's
	 * offset from the original VMA */
	offs += (vma->vm_pgoff << PAGE_SHIFT);

	if (offs >= priv->h->size)
		return VM_FAULT_SIGBUS;

	if (!priv->h->heap_pgalloc) {
		unsigned long pfn;
		BUG_ON(priv->h->carveout.base & ~PAGE_MASK);
		pfn = ((priv->h->carveout.base + offs) >> PAGE_SHIFT);
		vm_insert_pfn(vma, (unsigned long)vmf->virtual_address, pfn);
		return VM_FAULT_NOPAGE;
	} else {
		struct page *page;
		offs >>= PAGE_SHIFT;
		page = priv->h->pgalloc.pages[offs];
		if (page) get_page(page);
		vmf->page = page;
		return (page) ? 0 : VM_FAULT_SIGBUS;
	}
}

static long nvmap_ioctl(struct file *filp,
	unsigned int cmd, unsigned long arg)
{
	int err = 0;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != NVMEM_IOC_MAGIC)
		return -ENOTTY;

	if (_IOC_NR(cmd) > NVMEM_IOC_MAXNR)
		return -ENOTTY;

	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, uarg, _IOC_SIZE(cmd));
	if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ, uarg, _IOC_SIZE(cmd));

	if (err)
		return -EFAULT;

	switch (cmd) {
	case NVMEM_IOC_CREATE:
	case NVMEM_IOC_CLAIM:
	case NVMEM_IOC_FROM_ID:
		err = nvmap_ioctl_create(filp, cmd, uarg);
		break;

	case NVMEM_IOC_GET_ID:
		err = nvmap_ioctl_getid(filp, uarg);
		break;

	case NVMEM_IOC_PARAM:
		err = nvmap_ioctl_get_param(filp, uarg);
		break;

	case NVMEM_IOC_UNPIN_MULT:
	case NVMEM_IOC_PIN_MULT:
		err = nvmap_ioctl_pinop(filp, cmd==NVMEM_IOC_PIN_MULT, uarg);
		break;

	case NVMEM_IOC_ALLOC:
		err = nvmap_ioctl_alloc(filp, uarg);
		break;

	case NVMEM_IOC_FREE:
		err = nvmap_ioctl_free(filp, arg);
		break;

	case NVMEM_IOC_MMAP:
		err = nvmap_map_into_caller_ptr(filp, uarg);
		break;

	case NVMEM_IOC_WRITE:
	case NVMEM_IOC_READ:
		err = nvmap_ioctl_rw_handle(filp, cmd==NVMEM_IOC_READ, uarg);
		break;

	case NVMEM_IOC_CACHE:
		err = nvmap_ioctl_cache_maint(filp, uarg);
		break;

	default:
		return -ENOTTY;
	}
	return err;
}

/* must be called with the ref_lock held - given a user-space handle ID
 * ref, validate that the handle_ref object may be used by the caller */
struct nvmap_handle_ref *_nvmap_ref_lookup_locked(
	struct nvmap_file_priv *priv, unsigned long ref)
{
	struct rb_node *n = priv->handle_refs.rb_node;
	struct nvmap_handle *h = (struct nvmap_handle *)ref;

	if (unlikely(h->poison != NVDA_POISON)) {
		pr_err("%s: handle is poisoned\n", __func__);
		return NULL;
	}

	while (n) {
		struct nvmap_handle_ref *r;
		r = rb_entry(n, struct nvmap_handle_ref, node);
		if ((unsigned long)r->h == ref) return r;
		else if (ref > (unsigned long)r->h) n = n->rb_right;
		else n = n->rb_left;
	}

	return NULL;
}

/* must be called inside nvmap_pin_lock, to ensure that an entire stream
 * of pins will complete without competition from a second stream. returns
 * 0 if the pin was successful, -ENOMEM on failure */
static int _nvmap_handle_pin_locked(struct nvmap_handle *h)
{
	struct tegra_iovmm_area *area;
	BUG_ON(!h->alloc);

	h = _nvmap_handle_get(h);
	if (!h) return -ENOMEM;

	if (atomic_inc_return(&h->pin)==1) {
		if (h->heap_pgalloc && !h->pgalloc.contig) {
			area = _nvmap_get_vm(h);
			if (!area) {
				/* no race here, inside the pin mutex */
				atomic_dec(&h->pin);
				_nvmap_handle_put(h);
				return -ENOMEM;
			}
			if (area != h->pgalloc.area)
				h->pgalloc.dirty = true;
			h->pgalloc.area = area;
		}
	}
	return 0;
}

/* doesn't need to be called inside nvmap_pin_lock, since this will only
 * expand the available VM area */
static int _nvmap_handle_unpin(struct nvmap_handle *h)
{
	int ret = 0;

	if (atomic_add_return(0, &h->pin)==0) {
		pr_err("%s: %s attempting to unpin an unpinned handle\n",
			__func__, current->comm);
		dump_stack();
		return 0;
	}

	BUG_ON(!h->alloc);
#ifdef CONFIG_DEVNVMAP_RECLAIM_UNPINNED_VM
	spin_lock(&nvmap_mru_vma_lock);
#endif
	if (!atomic_dec_return(&h->pin)) {
		if (h->heap_pgalloc && h->pgalloc.area) {
			/* if a secure handle is clean (i.e., mapped into
			 * IOVMM, it needs to be zapped on unpin. */
			if (h->secure && !h->pgalloc.dirty) {
				tegra_iovmm_zap_vm(h->pgalloc.area);
				h->pgalloc.dirty = true;
			}
			_nvmap_insert_mru_vma(h);
			ret=1;
		}
	}
#ifdef CONFIG_DEVNVMAP_RECLAIM_UNPINNED_VM
	spin_unlock(&nvmap_mru_vma_lock);
#endif
	_nvmap_handle_put(h);
	return ret;
}

/* pin a list of handles, mapping IOVMM areas if needed. may sleep, if
 * a handle's IOVMM area has been reclaimed and insufficient IOVMM space
 * is available to complete the list pin. no intervening pin operations
 * will interrupt this, and no validation is performed on the handles
 * that are provided. */
static int _nvmap_handle_pin_fast(unsigned int nr, struct nvmap_handle **h)
{
	unsigned int i;
	int ret = 0;

	mutex_lock(&nvmap_pin_lock);
	for (i=0; i<nr && !ret; i++) {
		ret = wait_event_interruptible(nvmap_pin_wait,
			!_nvmap_handle_pin_locked(h[i]));
	}
	mutex_unlock(&nvmap_pin_lock);

	if (ret) {
		int do_wake = 0;
		while (i--) do_wake |= _nvmap_handle_unpin(h[i]);
		if (do_wake) wake_up(&nvmap_pin_wait);
		return -EINTR;
	} else {
		for (i=0; i<nr; i++)
			if (h[i]->heap_pgalloc && h[i]->pgalloc.dirty)
				_nvmap_handle_iovmm_map(h[i]);
	}

	return 0;
}

static int _nvmap_do_global_unpin(unsigned long ref)
{
	struct nvmap_handle *h;
	int w;

	h = _nvmap_validate_get(ref, true);
	if (unlikely(!h)) {
		pr_err("%s: %s attempting to unpin non-existent handle\n",
			__func__, current->group_leader->comm);
		return 0;
	}

	pr_err("%s: %s unpinning %s's %uB %s handle without local context\n",
		__func__, current->group_leader->comm,
		(h->owner) ? h->owner->comm : "kernel", h->orig_size,
		(h->heap_pgalloc && !h->pgalloc.contig) ? "iovmm" :
		(h->heap_pgalloc) ? "sysmem" : "carveout");

	w = _nvmap_handle_unpin(h);
	_nvmap_handle_put(h);
	return w;
}

static void _nvmap_do_unpin(struct nvmap_file_priv *priv,
	unsigned int nr, unsigned long *refs)
{
	struct nvmap_handle_ref *r;
	unsigned int i;
	int do_wake = 0;

	spin_lock(&priv->ref_lock);
	for (i=0; i<nr; i++) {
		if (!refs[i]) continue;
		r = _nvmap_ref_lookup_locked(priv, refs[i]);
		if (unlikely(!r)) {
			if (priv->su)
				do_wake |= _nvmap_do_global_unpin(refs[i]);
			else
				pr_err("%s: %s unpinning invalid handle\n",
					__func__, current->comm);
		} else if (unlikely(!atomic_add_unless(&r->pin, -1, 0)))
			pr_err("%s: %s unpinning unpinned handle\n",
				__func__, current->comm);
		else
			do_wake |= _nvmap_handle_unpin(r->h);
	}
	spin_unlock(&priv->ref_lock);
	if (do_wake) wake_up(&nvmap_pin_wait);
}

/* pins a list of handle_ref objects; same conditions apply as to
 * _nvmap_handle_pin, but also bumps the pin count of each handle_ref. */
static int _nvmap_do_pin(struct nvmap_file_priv *priv,
	unsigned int nr, unsigned long *refs)
{
	int ret = 0;
	unsigned int i;
	struct nvmap_handle **h = (struct nvmap_handle **)refs;
	struct nvmap_handle_ref *r;

	/* to optimize for the common case (client provided valid handle
	 * references and the pin succeeds), increment the handle_ref pin
	 * count during validation. in error cases, the tree will need to
	 * be re-walked, since the handle_ref is discarded so that an
	 * allocation isn't required. if a handle_ref is not found,
	 * locally validate that the caller has permission to pin the handle;
	 * handle_refs are not created in this case, so it is possible that
	 * if the caller crashes after pinning a global handle, the handle
	 * will be permanently leaked. */
	spin_lock(&priv->ref_lock);
	for (i=0; i<nr && !ret; i++) {
		r = _nvmap_ref_lookup_locked(priv, refs[i]);
		if (r) atomic_inc(&r->pin);
		else {
			if ((h[i]->poison != NVDA_POISON) ||
			    (!(priv->su || h[i]->global ||
			    current->group_leader == h[i]->owner)))
				ret = -EPERM;
			else {
				pr_err("%s: %s pinning %s's %uB handle without "
					"local context\n", __func__,
					current->group_leader->comm,
					h[i]->owner->comm, h[i]->orig_size);
			}
		}
	}

	while (ret && i--) {
		r = _nvmap_ref_lookup_locked(priv, refs[i]);
		if (r) atomic_dec(&r->pin);
	}
	spin_unlock(&priv->ref_lock);

	if (ret) return ret;

	mutex_lock(&nvmap_pin_lock);
	for (i=0; i<nr && !ret; i++) {
		ret = wait_event_interruptible(nvmap_pin_wait,
			!_nvmap_handle_pin_locked(h[i]));
	}
	mutex_unlock(&nvmap_pin_lock);

	if (ret) {
		int do_wake = 0;
		spin_lock(&priv->ref_lock);
		while (i--) {
			r = _nvmap_ref_lookup_locked(priv, refs[i]);
			do_wake |= _nvmap_handle_unpin(r->h);
			if (r) atomic_dec(&r->pin);
		}
		spin_unlock(&priv->ref_lock);
		if (do_wake) wake_up(&nvmap_pin_wait);
		return -EINTR;
	} else {
		for (i=0; i<nr; i++) {
			if (h[i]->heap_pgalloc && h[i]->pgalloc.dirty)
				_nvmap_handle_iovmm_map(h[i]);
		}
	}

	return 0;
}

static int nvmap_ioctl_pinop(struct file *filp,
	bool is_pin, void __user *arg)
{
	struct nvmem_pin_handle op;
	struct nvmap_handle *h;
	unsigned long on_stack[16];
	unsigned long *refs;
	unsigned long __user *output;
	unsigned int i;
	int err;

	err = copy_from_user(&op, arg, sizeof(op));
	if (err) return err;

	if (!op.count) return -EINVAL;

	if (op.count > 1) {
		size_t bytes = op.count * sizeof(unsigned long *);
		if (!access_ok(VERIFY_READ, (void *)op.handles, bytes))
			return -EPERM;
		if (is_pin && op.addr &&
		    !access_ok(VERIFY_WRITE, (void *)op.addr, bytes))
			return -EPERM;

		if (op.count <= ARRAY_SIZE(on_stack)) refs = on_stack;
		else refs = kzalloc(bytes, GFP_KERNEL);

		if (!refs) return -ENOMEM;
		err = copy_from_user(refs, (void*)op.handles, bytes);
		if (err) goto out;
	} else {
		refs = on_stack;
		on_stack[0] = (unsigned long)op.handles;
	}

	if (is_pin)
		err = _nvmap_do_pin(filp->private_data, op.count, refs);
	else
		_nvmap_do_unpin(filp->private_data, op.count, refs);

	/* skip the output stage on unpin */
	if (err || !is_pin) goto out;

	/* it is guaranteed that if _nvmap_do_pin returns 0 that
	 * all of the handle_ref objects are valid, so dereferencing directly
	 * here is safe */
	if (op.count > 1)
		output = (unsigned long __user *)op.addr;
	else {
		struct nvmem_pin_handle __user *tmp = arg;
		output = (unsigned long __user *)&(tmp->addr);
	}

	if (!output) goto out;

	for (i=0; i<op.count; i++) {
		unsigned long addr;
		h = (struct nvmap_handle *)refs[i];
		if (h->heap_pgalloc && h->pgalloc.contig)
			addr = page_to_phys(h->pgalloc.pages[0]);
		else if (h->heap_pgalloc)
			addr = h->pgalloc.area->iovm_start;
		else
			addr = h->carveout.base;

		__put_user(addr, &output[i]);
	}

out:
	if (refs != on_stack) kfree(refs);
	return err;
}

static int nvmap_release(struct inode *inode, struct file *filp)
{
	struct nvmap_file_priv *priv = filp->private_data;
	struct rb_node *n;
	struct nvmap_handle_ref *r;
	int refs;
	int do_wake = 0;
	int pins;

	if (!priv) return 0;

	while ((n = rb_first(&priv->handle_refs))) {
		r = rb_entry(n, struct nvmap_handle_ref, node);
		rb_erase(&r->node, &priv->handle_refs);
		smp_rmb();
		pins = atomic_read(&r->pin);
		atomic_set(&r->pin, 0);
		while (pins--) do_wake |= _nvmap_handle_unpin(r->h);
		refs = atomic_read(&r->refs);
		if (r->h->alloc && r->h->heap_pgalloc && !r->h->pgalloc.contig)
			atomic_sub(r->h->size, &priv->iovm_commit);
		while (refs--) _nvmap_handle_put(r->h);
		kfree(r);
	}
	if (do_wake) wake_up(&nvmap_pin_wait);
	kfree(priv);
	return 0;
}

static int nvmap_open(struct inode *inode, struct file *filp)
{
	/* eliminate read, write and llseek support on this node */
	struct nvmap_file_priv *priv;
	int ret;

	/* nvmap doesn't track total number of pinned references, so its
	 * IOVMM client is always locked. */
	if (!nvmap_vm_client) {
		mutex_lock(&nvmap_pin_lock);
		if (!nvmap_vm_client) {
			nvmap_vm_client = tegra_iovmm_alloc_client("gpu", NULL);
			if (nvmap_vm_client)
				tegra_iovmm_client_lock(nvmap_vm_client);
		}
		mutex_unlock(&nvmap_pin_lock);
	}

	ret = nonseekable_open(inode, filp);
	if (unlikely(ret))
		return ret;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) return -ENOMEM;
	priv->handle_refs = RB_ROOT;
	priv->su = (filp->f_op == &knvmap_fops);

	atomic_set(&priv->iovm_commit, 0);

	if (nvmap_vm_client)
		priv->iovm_limit = tegra_iovmm_get_vm_size(nvmap_vm_client);
#ifdef CONFIG_DEVNVMAP_RECLAIM_UNPINNED_VM
	/* to prevent fragmentation-caused deadlocks, derate the size of
	 * the IOVM space to 75% */
	priv->iovm_limit >>= 2;
	priv->iovm_limit *= 3;
#endif

	spin_lock_init(&priv->ref_lock);

	filp->f_mapping->backing_dev_info = &nvmap_bdi;

	filp->private_data = priv;
	return 0;
}

static int nvmap_ioctl_getid(struct file *filp, void __user *arg)
{
	struct nvmem_create_handle op;
	struct nvmap_handle *h = NULL;
	int err;

	err = copy_from_user(&op, arg, sizeof(op));
	if (err) return err;

	if (!op.handle) return -EINVAL;

	h = _nvmap_validate_get((unsigned long)op.handle,
		filp->f_op==&knvmap_fops);

	if (h) {
		op.id = (__u32)h;
		/* when the owner of a handle gets its ID, this is treated
		 * as a granting of the handle for use by other processes.
		 * however, the super-user is not capable of promoting a
		 * handle to global status if it was created in another
		 * process. */
		if (current->group_leader == h->owner) h->global = true;

		/* getid is not supposed to result in a ref count increase */
		_nvmap_handle_put(h);

		return copy_to_user(arg, &op, sizeof(op));
	}
	return -EPERM;
}

static int _nvmap_do_alloc(struct nvmap_file_priv *priv,
	unsigned long href, unsigned int heap_mask, size_t align,
	unsigned int flags)
{
	struct nvmap_handle_ref *r;
	struct nvmap_handle *h;
	int numpages;

	align = max_t(size_t, align, L1_CACHE_BYTES);

	if (!href) return -EINVAL;

	spin_lock(&priv->ref_lock);
	r = _nvmap_ref_lookup_locked(priv, href);
	spin_unlock(&priv->ref_lock);

	if (!r) return -EPERM;

	h = r->h;
	if (h->alloc) return 0;

	numpages = ((h->size + PAGE_SIZE - 1) >> PAGE_SHIFT);
	h->secure = (flags & NVMEM_HANDLE_SECURE);
	h->flags = (flags & 0x3);

	BUG_ON(!numpages);

	/* secure allocations can only be served from secure heaps */
	if (h->secure) {
		heap_mask &= NVMAP_SECURE_HEAPS;
		if (!heap_mask) return -EINVAL;
	}
	/* can't do greater than page size alignment with page alloc */
	if (align > PAGE_SIZE)
		heap_mask &= NVMEM_HEAP_CARVEOUT_MASK;

	while (heap_mask && !h->alloc) {
		unsigned int heap_type = _nvmap_heap_policy(heap_mask, numpages);

		if (heap_type & NVMEM_HEAP_CARVEOUT_MASK) {
			struct nvmap_carveout_node *n;

			down_read(&nvmap_context.list_sem);
			list_for_each_entry(n, &nvmap_context.heaps, heap_list) {
				if (heap_type & n->heap_bit) {
					struct nvmap_carveout* co = &n->carveout;
					int idx = nvmap_carveout_alloc(co, align, h->size);
					if (idx != -1) {
						h->carveout.co_heap = co;
						h->carveout.block_idx = idx;
						spin_lock(&co->lock);
						h->carveout.base = co->blocks[idx].base;
						spin_unlock(&co->lock);
						h->heap_pgalloc = false;
						h->alloc = true;
						break;
					}
				}
			}
			up_read(&nvmap_context.list_sem);
		}
		else if (heap_type & NVMEM_HEAP_IOVMM) {
			int ret;

			BUG_ON(align > PAGE_SIZE);

			/* increment the committed IOVM space prior to
			 * allocation, to avoid race conditions with other
			 * threads simultaneously allocating. this is
			 * conservative, but guaranteed to work */
			if (atomic_add_return(numpages << PAGE_SHIFT, &priv->iovm_commit)
				< priv->iovm_limit) {
				ret = nvmap_pagealloc(h, false);
			}
			else ret = -ENOMEM;

			if (ret) {
				atomic_sub(numpages << PAGE_SHIFT, &priv->iovm_commit);
			}
			else {
				BUG_ON(h->pgalloc.contig);
				h->heap_pgalloc = true;
				h->alloc = true;
			}
		}
		else if (heap_type & NVMEM_HEAP_SYSMEM) {
			if (nvmap_pagealloc(h, true) == 0) {
				BUG_ON(!h->pgalloc.contig);
				h->heap_pgalloc = true;
				h->alloc = true;
			}
		}
		else break;

		heap_mask &= ~heap_type;
	}

	return (h->alloc ? 0 : -ENOMEM);
}

static int nvmap_ioctl_alloc(struct file *filp, void __user *arg)
{
	struct nvmem_alloc_handle op;
	struct nvmap_file_priv *priv = filp->private_data;
	int err;

	err = copy_from_user(&op, arg, sizeof(op));
	if (err) return err;

	if (op.align & (op.align-1)) return -EINVAL;

	/* user-space handles are aligned to page boundaries, to prevent
	 * data leakage. */
	op.align = max_t(size_t, op.align, PAGE_SIZE);

	return _nvmap_do_alloc(priv, op.handle, op.heap_mask, op.align, op.flags);
}

static int _nvmap_do_free(struct nvmap_file_priv *priv, unsigned long href)
{
	struct nvmap_handle_ref *r;
	struct nvmap_handle *h;
	int do_wake = 0;

	if (!href) return 0;

	spin_lock(&priv->ref_lock);
	r = _nvmap_ref_lookup_locked(priv, href);

	if (!r) {
		spin_unlock(&priv->ref_lock);
		pr_err("%s attempting to free unrealized handle\n",
			current->group_leader->comm);
		return -EPERM;
	}

	h = r->h;

	smp_rmb();
	if (!atomic_dec_return(&r->refs)) {
		int pins = atomic_read(&r->pin);
		rb_erase(&r->node, &priv->handle_refs);
		spin_unlock(&priv->ref_lock);
		if (pins) pr_err("%s: %s freeing %s's pinned %s %s %uB handle\n",
			__func__, current->comm,
			(r->h->owner) ? r->h->owner->comm : "kernel",
			(r->h->global) ? "global" : "private",
			(r->h->alloc && r->h->heap_pgalloc) ? "page-alloc" :
			(r->h->alloc) ? "carveout" : "unallocated",
			r->h->orig_size);
		while (pins--) do_wake |= _nvmap_handle_unpin(r->h);
		kfree(r);
		if (h->alloc && h->heap_pgalloc && !h->pgalloc.contig)
			atomic_sub(h->size, &priv->iovm_commit);
		if (do_wake) wake_up(&nvmap_pin_wait);
	} else
		spin_unlock(&priv->ref_lock);

	BUG_ON(!atomic_read(&h->ref));
	_nvmap_handle_put(h);
	return 0;
}

static int nvmap_ioctl_free(struct file *filp, unsigned long arg)
{
	return _nvmap_do_free(filp->private_data, arg);
}

/* given a size, pre-existing handle ID, or a preserved handle key, create
 * a handle and a reference to the handle in the per-context data */
static int _nvmap_do_create(struct nvmap_file_priv *priv,
	unsigned int cmd, unsigned long key, bool su,
	struct nvmap_handle_ref **ref)
{
	struct nvmap_handle_ref *r = NULL;
	struct nvmap_handle *h = NULL;
	struct rb_node **p, *parent = NULL;

	if (cmd == NVMEM_IOC_FROM_ID) {
		/* only ugly corner case to handle with from ID:
		 *
		 * normally, if the handle that is being duplicated is IOVMM-
		 * backed, the handle should fail to duplicate if duping it
		 * would over-commit IOVMM space.  however, if the handle is
		 * already duplicated in the client process (or the client
		 * is duplicating a handle it created originally), IOVMM space
		 * should not be doubly-reserved.
		 */
		h = _nvmap_validate_get(key, priv->su);

		if (!h) {
			pr_err("%s: %s duplicate handle failed\n", __func__,
				current->group_leader->comm);
			return -EPERM;
		}

		if (!h->alloc) {
			pr_err("%s: attempting to clone unallocated "
				"handle\n", __func__);
			_nvmap_handle_put(h);
			h = NULL;
			return -EINVAL;
		}

		spin_lock(&priv->ref_lock);
		r = _nvmap_ref_lookup_locked(priv, (unsigned long)h);
		spin_unlock(&priv->ref_lock);
		if (r) {
			/* if the client does something strange, like calling CreateFromId
			 * when it was the original creator, avoid creating two handle refs
			 * for the same handle */
			atomic_inc(&r->refs);
			*ref = r;
			return 0;
		}

		/* verify that adding this handle to the process' access list
		 * won't exceed the IOVM limit */
		/* TODO: [ahatala 2010-04-20] let the kernel over-commit for now */
		if (h->heap_pgalloc && !h->pgalloc.contig && !su) {
			int oc = atomic_add_return(h->size, &priv->iovm_commit);
			if (oc > priv->iovm_limit) {
				atomic_sub(h->size, &priv->iovm_commit);
				_nvmap_handle_put(h);
				h = NULL;
				pr_err("%s: %s duplicating handle would "
					"over-commit iovmm space (%dB / %dB)\n",
					__func__, current->group_leader->comm,
					oc, priv->iovm_limit);
				return -ENOMEM;
			}
		}
	} else if (cmd == NVMEM_IOC_CREATE) {
		h = _nvmap_handle_create(current->group_leader, key);
		if (!h) return -ENOMEM;
	} else {
		h = _nvmap_claim_preserved(current->group_leader, key);
		if (!h) return -EINVAL;
	}

	BUG_ON(!h);

	r = kzalloc(sizeof(*r), GFP_KERNEL);
	if (!r) {
		if (h) _nvmap_handle_put(h);
		return -ENOMEM;
	}

	atomic_set(&r->refs, 1);
	r->h = h;
	atomic_set(&r->pin, 0);

	spin_lock(&priv->ref_lock);
	p = &priv->handle_refs.rb_node;
	while (*p) {
		struct nvmap_handle_ref *l;
		parent = *p;
		l = rb_entry(parent, struct nvmap_handle_ref, node);
		if (r->h > l->h) p = &parent->rb_right;
		else p = &parent->rb_left;
	}
	rb_link_node(&r->node, parent, p);
	rb_insert_color(&r->node, &priv->handle_refs);

	spin_unlock(&priv->ref_lock);
	*ref = r;
	return 0;
}

static int nvmap_ioctl_create(struct file *filp,
	unsigned int cmd, void __user *arg)
{
	struct nvmem_create_handle op;
	struct nvmap_handle_ref *r = NULL;
	struct nvmap_file_priv *priv = filp->private_data;
	unsigned long key;
	int err = 0;

	err = copy_from_user(&op, arg, sizeof(op));
	if (err) return err;

	if (!priv) return -ENODEV;

	/* user-space-created handles are expanded to be page-aligned,
	 * so that mmap() will not accidentally leak a different allocation */
	if (cmd==NVMEM_IOC_CREATE)
		key = (op.size + PAGE_SIZE - 1) & ~(PAGE_SIZE-1);
	else if (cmd==NVMEM_IOC_CLAIM)
		key = op.key;
	else if (cmd==NVMEM_IOC_FROM_ID)
		key = op.id;

	err = _nvmap_do_create(priv, cmd, key, (filp->f_op==&knvmap_fops), &r);

	if (!err) {
		op.handle = (uintptr_t)r->h;
		/* since the size is spoofed to a page-multiple above,
		 * clobber the orig_size field back to the requested value for
		 * debugging. */
		if (cmd == NVMEM_IOC_CREATE) r->h->orig_size = op.size;
		err = copy_to_user(arg, &op, sizeof(op));
		if (err) _nvmap_do_free(priv, op.handle);
	}

	return err;
}

static int nvmap_map_into_caller_ptr(struct file *filp, void __user *arg)
{
	struct nvmem_map_caller op;
	struct nvmap_vma_priv *vpriv;
	struct vm_area_struct *vma;
	struct nvmap_handle *h;
	int err = 0;

	err = copy_from_user(&op, arg, sizeof(op));
	if (err) return err;

	if (!op.handle) return -EINVAL;

	h = _nvmap_validate_get(op.handle, (filp->f_op==&knvmap_fops));
	if (!h) return -EINVAL;

	down_read(&current->mm->mmap_sem);

	vma = find_vma(current->mm, op.addr);
	if (!vma || !vma->vm_private_data) {
		err = -ENOMEM;
		goto out;
	}

	if (op.offset & ~PAGE_MASK) {
		err = -EFAULT;
		goto out;
	}

	if ((op.offset + op.length) > h->size) {
		err = -EADDRNOTAVAIL;
		goto out;
	}

	vpriv = vma->vm_private_data;
	BUG_ON(!vpriv);

	/* the VMA must exactly match the requested mapping operation, and the
	 * VMA that is targetted must have been created originally by /dev/nvmap
	 */
	if ((vma->vm_start != op.addr) || (vma->vm_ops != &nvmap_vma_ops) ||
	    (vma->vm_end-vma->vm_start != op.length)) {
		err = -EPERM;
		goto out;
	}

	/* verify that each mmap() system call creates a unique VMA */

	if (vpriv->h && h==vpriv->h)
		goto out;
	else if (vpriv->h) {
		err = -EADDRNOTAVAIL;
		goto out;
	}

	if (!h->heap_pgalloc && (h->carveout.base & ~PAGE_MASK)) {
		err = -EFAULT;
		goto out;
	}

	vpriv->h = h;
	vpriv->offs = op.offset;

	/* if the hmem is not writeback-cacheable, drop back to a page mapping
	 * which will guarantee DMA coherency
	 */
	vma->vm_page_prot = _nvmap_flag_to_pgprot(h->flags,
		vma->vm_page_prot);

out:
	up_read(&current->mm->mmap_sem);
	if (err) _nvmap_handle_put(h);
	return err;
}
/* Initially, the nvmap mmap system call is used to allocate an inaccessible
 * region of virtual-address space in the client.  A subsequent
 * NVMAP_IOC_MMAP ioctl will associate each
 */
static int nvmap_mmap(struct file *filp, struct vm_area_struct *vma)
{
	/* FIXME: drivers which do not support cow seem to be split down the
	 * middle whether to force the VM_SHARED flag, or to return an error
	 * when this flag isn't already set (i.e., MAP_PRIVATE).
	 */
	struct nvmap_vma_priv *priv;

	vma->vm_private_data = NULL;

	priv = kzalloc(sizeof(*priv),GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->offs = 0;
	priv->h = NULL;
	atomic_set(&priv->ref, 1);

	vma->vm_flags |= VM_SHARED;
	vma->vm_flags |= (VM_IO | VM_DONTEXPAND | VM_MIXEDMAP | VM_RESERVED);
	vma->vm_ops = &nvmap_vma_ops;
	vma->vm_private_data = priv;

	return 0;
}

/* perform cache maintenance on a handle; caller's handle must be pre-
 * validated. */
static int _nvmap_do_cache_maint(struct nvmap_handle *h,
	unsigned long start, unsigned long end, unsigned long op, bool get)
{
	pgprot_t prot;
	void *addr = NULL;
	void (*inner_maint)(const void*, const void*);
	void (*outer_maint)(unsigned long, unsigned long);
	int err = 0;

	if (get) h = _nvmap_handle_get(h);

	if (!h) return -EINVAL;

	/* don't waste time on cache maintenance if the handle isn't cached */
	if (h->flags == NVMEM_HANDLE_UNCACHEABLE ||
	    h->flags == NVMEM_HANDLE_WRITE_COMBINE)
		goto out;

	if (op == NVMEM_CACHE_OP_WB) {
		inner_maint = smp_dma_clean_range;
		if (h->flags == NVMEM_HANDLE_CACHEABLE)
			outer_maint = outer_clean_range;
		else
			outer_maint = NULL;
	} else if (op == NVMEM_CACHE_OP_WB_INV) {
		inner_maint = dmac_flush_range;
		if (h->flags == NVMEM_HANDLE_CACHEABLE)
			outer_maint = outer_flush_range;
		else
			outer_maint = NULL;
	} else {
		inner_maint = smp_dma_inv_range;
		if (h->flags == NVMEM_HANDLE_CACHEABLE)
			outer_maint = outer_inv_range;
		else
			outer_maint = NULL;
	}

	prot = _nvmap_flag_to_pgprot(h->flags, pgprot_kernel);

	while (start < end) {
		struct page *page = NULL;
		unsigned long phys;
		void *src;
		size_t count;

		if (h->heap_pgalloc) {
			page = h->pgalloc.pages[start>>PAGE_SHIFT];
			BUG_ON(!page);
			get_page(page);
			phys = page_to_phys(page) + (start & ~PAGE_MASK);
		} else {
			phys = h->carveout.base + start;
		}

		if (!addr) {
			err = nvmap_map_pte(__phys_to_pfn(phys), prot, &addr);
			if (err) {
				if (page) put_page(page);
				break;
			}
		} else {
			_nvmap_set_pte_at((unsigned long)addr,
				__phys_to_pfn(phys), prot);
		}

		src = addr + (phys & ~PAGE_MASK);
		count = min_t(size_t, end-start, PAGE_SIZE-(phys&~PAGE_MASK));

		inner_maint(src, src+count);
		if (outer_maint) outer_maint(phys, phys+count);
		start += count;
		if (page) put_page(page);
	}

out:
	if (h->flags == NVMEM_HANDLE_INNER_CACHEABLE) outer_sync();
	if (addr) nvmap_unmap_pte(addr);
	if (get) _nvmap_handle_put(h);
	return err;
}

static int nvmap_ioctl_cache_maint(struct file *filp, void __user *arg)
{
	struct nvmem_cache_op	op;
	int			err = 0;
	struct vm_area_struct	*vma;
	struct nvmap_vma_priv	*vpriv;
	unsigned long		start;
	unsigned long		end;

	err = copy_from_user(&op, arg, sizeof(op));
	if (err) return err;

	if (!op.handle || !op.addr || op.op<NVMEM_CACHE_OP_WB ||
	    op.op>NVMEM_CACHE_OP_WB_INV)
		return -EINVAL;

	vma = find_vma(current->active_mm, (unsigned long)op.addr);
	if (!vma || vma->vm_ops!=&nvmap_vma_ops ||
	    (unsigned long)op.addr + op.len > vma->vm_end)
		return -EADDRNOTAVAIL;

	vpriv = (struct nvmap_vma_priv *)vma->vm_private_data;

	if ((unsigned long)vpriv->h != op.handle)
		return -EFAULT;

	start = (unsigned long)op.addr - vma->vm_start;
	end  = start + op.len;

	return _nvmap_do_cache_maint(vpriv->h, start, end, op.op, true);
}

/* copies a single element from the pre-get()'ed handle h, returns
 * the number of bytes copied, and the address in the nvmap mapping range
 * which was used (to eliminate re-allocation when copying multiple
 * elements */
static ssize_t _nvmap_do_one_rw_handle(struct nvmap_handle *h, int is_read,
	int is_user, unsigned long start, unsigned long rw_addr,
	unsigned long bytes, void **nvmap_addr)
{
	pgprot_t prot = _nvmap_flag_to_pgprot(h->flags, pgprot_kernel);
	unsigned long end = start + bytes;
	unsigned long orig_start = start;

	if (is_user) {
		if (is_read && !access_ok(VERIFY_WRITE, (void*)rw_addr, bytes))
			return -EPERM;
		if (!is_read && !access_ok(VERIFY_READ, (void*)rw_addr, bytes))
			return -EPERM;
	}

	while (start < end) {
		struct page *page = NULL;
		unsigned long phys;
		size_t count;
		void *src;

		if (h->heap_pgalloc) {
			page = h->pgalloc.pages[start >> PAGE_SHIFT];
			BUG_ON(!page);
			get_page(page);
			phys = page_to_phys(page) + (start & ~PAGE_MASK);
		} else {
			phys = h->carveout.base + start;
		}

		if (!*nvmap_addr) {
			int err = nvmap_map_pte(__phys_to_pfn(phys),
				prot, nvmap_addr);
			if (err) {
				if (page) put_page(page);
				count = start - orig_start;
				return (count) ? count : err;
			}
		} else {
			_nvmap_set_pte_at((unsigned long)*nvmap_addr,
				__phys_to_pfn(phys), prot);

		}

		src = *nvmap_addr + (phys & ~PAGE_MASK);
		count = min_t(size_t, end-start, PAGE_SIZE-(phys&~PAGE_MASK));

		if (is_user && is_read)
			copy_to_user((void*)rw_addr, src, count);
		else if (is_user)
			copy_from_user(src, (void*)rw_addr, count);
		else if (is_read)
			memcpy((void*)rw_addr, src, count);
		else
			memcpy(src, (void*)rw_addr, count);

		rw_addr += count;
		start += count;
		if (page) put_page(page);
	}

	return (ssize_t)start - orig_start;
}

static ssize_t _nvmap_do_rw_handle(struct nvmap_handle *h, int is_read,
	int is_user, unsigned long h_offs, unsigned long sys_addr,
	unsigned long h_stride, unsigned long sys_stride,
	unsigned long elem_size, unsigned long count)
{
	ssize_t bytes_copied = 0;
	void *addr = NULL;

	h = _nvmap_handle_get(h);
	if (!h) return -EINVAL;

	if (elem_size == h_stride &&
	    elem_size == sys_stride) {
		elem_size *= count;
		h_stride = elem_size;
		sys_stride = elem_size;
		count = 1;
	}

	while (count--) {
		size_t ret = _nvmap_do_one_rw_handle(h, is_read,
			is_user, h_offs, sys_addr, elem_size, &addr);
		if (ret < 0) {
			if (!bytes_copied) bytes_copied = ret;
			break;
		}
		bytes_copied += ret;
		if (ret < elem_size) break;
		sys_addr += sys_stride;
		h_offs += h_stride;
	}

	if (addr) nvmap_unmap_pte(addr);
	_nvmap_handle_put(h);
	return bytes_copied;
}

static int nvmap_ioctl_rw_handle(struct file *filp,
	int is_read, void __user* arg)
{
	struct nvmem_rw_handle __user *uarg = arg;
	struct nvmem_rw_handle op;
	struct nvmap_handle *h;
	ssize_t copied;
	int err = 0;

	err = copy_from_user(&op, arg, sizeof(op));
	if (err) return err;

	if (!op.handle || !op.addr || !op.count || !op.elem_size)
		return -EINVAL;

	h = _nvmap_validate_get(op.handle, (filp->f_op == &knvmap_fops));
	if (!h) return -EINVAL; /* -EPERM? */

	copied = _nvmap_do_rw_handle(h, is_read, 1, op.offset,
		(unsigned long)op.addr, op.hmem_stride,
		op.user_stride, op.elem_size, op.count);

	if (copied < 0) { err = copied; copied = 0; }
	else if (copied < (op.count*op.elem_size)) err = -EINTR;

	__put_user(copied, &uarg->count);

	_nvmap_handle_put(h);

	return err;
}

static unsigned int _nvmap_do_get_param(struct nvmap_handle *h,
	unsigned int param)
{
	if (param==NVMEM_HANDLE_PARAM_SIZE)
		return h->orig_size;

	else if (param==NVMEM_HANDLE_PARAM_ALIGNMENT) {
		if (!h->alloc) return 0;

		if (h->heap_pgalloc) return PAGE_SIZE;
		else {
			unsigned int i=1;
			if (!h->carveout.base) return SZ_4M;
			while (!(i & h->carveout.base)) i<<=1;
			return i;
		}
	} else if (param==NVMEM_HANDLE_PARAM_BASE) {

		if (!h->alloc || !atomic_add_return(0, &h->pin)){
			WARN_ON(1);
			return ~0ul;
		}

		if (!h->heap_pgalloc)
			return h->carveout.base;

		if (h->pgalloc.contig)
			return page_to_phys(h->pgalloc.pages[0]);

		if (h->pgalloc.area)
			return h->pgalloc.area->iovm_start;

		return ~0ul;
	} else if (param==NVMEM_HANDLE_PARAM_HEAP) {

		if (!h->alloc) return 0;

		if (!h->heap_pgalloc) {
			/* FIXME: hard-coded physical address */
			if ((h->carveout.base & 0xf0000000ul)==0x40000000ul)
				return NVMEM_HEAP_CARVEOUT_IRAM;
			else
				return NVMEM_HEAP_CARVEOUT_GENERIC;
		}

		if (!h->pgalloc.contig)
			return NVMEM_HEAP_IOVMM;

		return NVMEM_HEAP_SYSMEM;
	}

	return 0;
}

static int nvmap_ioctl_get_param(struct file *filp, void __user* arg)
{
	struct nvmem_handle_param op;
	struct nvmap_handle *h;
	int err;

	err = copy_from_user(&op, arg, sizeof(op));
	if (err) return err;

	if (op.param < NVMEM_HANDLE_PARAM_SIZE ||
	    op.param > NVMEM_HANDLE_PARAM_HEAP)
		return -EINVAL;

	h = _nvmap_validate_get(op.handle, (filp->f_op==&knvmap_fops));
	if (!h) return -EINVAL;

	op.result = _nvmap_do_get_param(h, op.param);
	err = copy_to_user(arg, &op, sizeof(op));

	_nvmap_handle_put(h);
	return err;
}

static struct miscdevice misc_nvmap_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "nvmap",
	.fops = &nvmap_fops
};

static struct miscdevice misc_knvmap_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "knvmap",
	.fops = &knvmap_fops
};

static struct device *__nvmap_heap_parent_dev(void)
{
	return misc_nvmap_dev.this_device;
}

/* creates the sysfs attribute files for a carveout heap; if called
 * before fs initialization, silently returns.
 */
static void _nvmap_create_heap_attrs(struct nvmap_carveout_node *n)
{
	if (!_nvmap_heap_parent_dev) return;
	dev_set_name(&n->dev, "heap-%s", n->carveout.name);
	n->dev.parent = _nvmap_heap_parent_dev;
	n->dev.driver = NULL;
	n->dev.release = NULL;
	if (device_register(&n->dev)) {
		pr_err("%s: failed to create heap-%s device\n",
			__func__, n->carveout.name);
		return;
	}
	if (sysfs_create_group(&n->dev.kobj, &nvmap_heap_defattr_group))
		pr_err("%s: failed to create attribute group for heap-%s "
			"device\n", __func__, n->carveout.name);
}

static int __init nvmap_dev_init(void)
{
	struct nvmap_carveout_node *n;

	if (misc_register(&misc_nvmap_dev))
		pr_err("%s error registering %s\n", __func__,
			misc_nvmap_dev.name);

	if (misc_register(&misc_knvmap_dev))
		pr_err("%s error registering %s\n", __func__,
			misc_knvmap_dev.name);

	/* create sysfs attribute entries for all the heaps which were
	 * created prior to nvmap_dev_init */
	down_read(&nvmap_context.list_sem);
	list_for_each_entry(n, &nvmap_context.heaps, heap_list) {
		_nvmap_create_heap_attrs(n);
	}
	up_read(&nvmap_context.list_sem);

	nvmap_procfs_root = proc_mkdir("nvmap", NULL);
	if (nvmap_procfs_root) {
		nvmap_procfs_proc = proc_mkdir("proc", nvmap_procfs_root);
	}
	return 0;
}
fs_initcall(nvmap_dev_init);

/* initialization of core data structures split out to earlier in the
 * init sequence, to allow kernel drivers access to nvmap before devfs
 * is initialized */
#define NR_CARVEOUTS 2
static unsigned int nvmap_carveout_cmds = 0;
static unsigned long nvmap_carveout_cmd_base[NR_CARVEOUTS];
static unsigned long nvmap_carveout_cmd_size[NR_CARVEOUTS];

static int __init nvmap_core_init(void)
{
	u32 base = NVMAP_BASE;
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	unsigned int i;

	init_rwsem(&nvmap_context.list_sem);
	nvmap_context.init_data.handle_refs = RB_ROOT;
	atomic_set(&nvmap_context.init_data.iovm_commit, 0);
	/* no IOVMM allocations for kernel-created handles */
	spin_lock_init(&nvmap_context.init_data.ref_lock);
	nvmap_context.init_data.su = true;
	nvmap_context.init_data.iovm_limit = 0;
	INIT_LIST_HEAD(&nvmap_context.heaps);

#ifdef CONFIG_DEVNVMAP_RECLAIM_UNPINNED_VM
	for (i=0; i<ARRAY_SIZE(nvmap_mru_cutoff); i++)
		INIT_LIST_HEAD(&nvmap_mru_vma_lists[i]);
#endif

	i = 0;
	do {
		pgd = pgd_offset(&init_mm, base);
		pmd = pmd_alloc(&init_mm, pgd, base);
		if (!pmd) {
			pr_err("%s: no pmd tables\n", __func__);
			return -ENOMEM;
		}
		pte = pte_alloc_kernel(pmd, base);
		if (!pte) {
			pr_err("%s: no pte tables\n", __func__);
			return -ENOMEM;
		}
		nvmap_pte[i++] = pte;
		base += (1<<PGDIR_SHIFT);
	} while (base < NVMAP_END);

	for (i=0; i<nvmap_carveout_cmds; i++) {
		char tmp[16];
		snprintf(tmp, sizeof(tmp), "generic-%u", i);
		nvmap_add_carveout_heap(nvmap_carveout_cmd_base[i],
			nvmap_carveout_cmd_size[i], tmp, 0x1);
	}

	return 0;
}
core_initcall(nvmap_core_init);

static int __init nvmap_heap_arg(char *options)
{
	unsigned long start, size;
	char *p = options;

	start = -1;
	size = memparse(p, &p);
	if (*p == '@')
		start = memparse(p + 1, &p);

	if (nvmap_carveout_cmds < ARRAY_SIZE(nvmap_carveout_cmd_size)) {
		nvmap_carveout_cmd_base[nvmap_carveout_cmds] = start;
		nvmap_carveout_cmd_size[nvmap_carveout_cmds] = size;
		nvmap_carveout_cmds++;
	}
	return 0;
}
__setup("nvmem=", nvmap_heap_arg);

static int _nvmap_try_create_preserved(struct nvmap_carveout *co,
	struct nvmap_handle *h, unsigned long base,
	size_t size, unsigned int key)
{
	unsigned long end = base + size;
	short idx;

	h->carveout.base = ~0;
	h->carveout.key = key;
	h->carveout.co_heap = NULL;

	spin_lock(&co->lock);
	idx = co->free_index;
	while (idx != -1) {
		struct nvmap_mem_block *b = BLOCK(co, idx);
		unsigned long blk_end = b->base + b->size;
		if (b->base <= base && blk_end >= end) {
			nvmap_split_block(co, idx, base, size);
			h->carveout.block_idx = idx;
			h->carveout.base = co->blocks[idx].base;
			h->carveout.co_heap = co;
			h->alloc = true;
			break;
		}
		idx = b->next_free;
	}
	spin_unlock(&co->lock);

	return (h->carveout.co_heap == NULL) ? -ENXIO : 0;
}

static void _nvmap_create_nvos_preserved(struct nvmap_carveout *co)
{
#ifdef CONFIG_TEGRA_NVOS
	unsigned int i, key;
	NvBootArgsPreservedMemHandle mem;
	static int was_created[NvBootArgKey_PreservedMemHandle_Num -
		NvBootArgKey_PreservedMemHandle_0] = { 0 };

	for (i=0, key=NvBootArgKey_PreservedMemHandle_0;
	     i<ARRAY_SIZE(was_created); i++, key++) {
		struct nvmap_handle *h;

		if (was_created[i]) continue;

		if (NvOsBootArgGet(key, &mem, sizeof(mem))!=NvSuccess) continue;
		if (!mem.Address || !mem.Size) continue;

		h = _nvmap_handle_create(NULL, mem.Size);
		if (!h) continue;

		if (!_nvmap_try_create_preserved(co, h, mem.Address,
		    mem.Size, key))
			was_created[i] = 1;
		else
			_nvmap_handle_put(h);
	}
#endif
}

int nvmap_add_carveout_heap(unsigned long base, size_t size,
	const char *name, unsigned int bitmask)
{
	struct nvmap_carveout_node *n;
	struct nvmap_carveout_node *l;


	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n) return -ENOMEM;

	BUG_ON(bitmask & ~NVMEM_HEAP_CARVEOUT_MASK);
	n->heap_bit = bitmask;

	if (_nvmap_init_carveout(&n->carveout, name, base, size)) {
		kfree(n);
		return -ENOMEM;
	}

	down_write(&nvmap_context.list_sem);

	/* called inside the list_sem lock to ensure that the was_created
	 * array is protected against simultaneous access */
	_nvmap_create_nvos_preserved(&n->carveout);
	_nvmap_create_heap_attrs(n);

	list_for_each_entry(l, &nvmap_context.heaps, heap_list) {
		if (n->heap_bit > l->heap_bit) {
			list_add_tail(&n->heap_list, &l->heap_list);
			up_write(&nvmap_context.list_sem);
			return 0;
		}
	}
	list_add_tail(&n->heap_list, &nvmap_context.heaps);
	up_write(&nvmap_context.list_sem);
	return 0;
}

int nvmap_create_preserved_handle(unsigned long base, size_t size,
	unsigned int key)
{
	struct nvmap_carveout_node *i;
	struct nvmap_handle *h;

	h = _nvmap_handle_create(NULL, size);
	if (!h) return -ENOMEM;

	down_read(&nvmap_context.list_sem);
	list_for_each_entry(i, &nvmap_context.heaps, heap_list) {
		struct nvmap_carveout *co = &i->carveout;
		if (!_nvmap_try_create_preserved(co, h, base, size, key))
			break;
	}
	up_read(&nvmap_context.list_sem);

	/* the base may not be correct if block splitting fails */
	if (!h->carveout.co_heap || h->carveout.base != base) {
		_nvmap_handle_put(h);
		return -ENOMEM;
	}

	return 0;
}

/* attempts to create a new carveout heap with a new usage bitmask by
 * taking an allocation from a previous carveout with a different bitmask */
static int nvmap_split_carveout_heap(struct nvmap_carveout *co, size_t size,
	const char *name, unsigned int new_bitmask)
{
	struct nvmap_carveout_node *i, *n;
	int idx = -1;
	unsigned int blkbase, blksize;


	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n) return -ENOMEM;
	n->heap_bit = new_bitmask;

	/* align split carveouts to 1M */
	idx = nvmap_carveout_alloc(co, SZ_1M, size);
	if (idx != -1) {
		/* take the spin lock to avoid race conditions with
		 * intervening allocations triggering grow_block operations */
		spin_lock(&co->lock);
		blkbase = co->blocks[idx].base;
		blksize = co->blocks[idx].size;
		spin_unlock(&co->lock);

		if (_nvmap_init_carveout(&n->carveout,name, blkbase, blksize)) {
			nvmap_carveout_free(co, idx);
			idx = -1;
		} else {
			spin_lock(&co->lock);
			if (co->blocks[idx].prev) {
				co->blocks[co->blocks[idx].prev].next =
					co->blocks[idx].next;
			}
			if (co->blocks[idx].next) {
				co->blocks[co->blocks[idx].next].prev =
					co->blocks[idx].prev;
			}
			if (co->block_index==idx)
				co->block_index = co->blocks[idx].next;
			co->blocks[idx].next_free = -1;
			co->blocks[idx].prev_free = -1;
			co->blocks[idx].next = co->spare_index;
			if (co->spare_index!=-1)
				co->blocks[co->spare_index].prev = idx;
			co->spare_index = idx;
			spin_unlock(&co->lock);
		}
	}

	if (idx==-1) {
		kfree(n);
		return -ENOMEM;
	}

	down_write(&nvmap_context.list_sem);
	_nvmap_create_heap_attrs(n);
	list_for_each_entry(i, &nvmap_context.heaps, heap_list) {
		if (n->heap_bit > i->heap_bit) {
			list_add_tail(&n->heap_list, &i->heap_list);
			up_write(&nvmap_context.list_sem);
			return 0;
		}
	}
	list_add_tail(&n->heap_list, &nvmap_context.heaps);
	up_write(&nvmap_context.list_sem);
	return 0;
}

/* NvRmMemMgr APIs implemented on top of nvmap */

#if defined(CONFIG_TEGRA_NVRM)
#include <linux/freezer.h>

NvU32 NvRmMemGetAddress(NvRmMemHandle hMem, NvU32 Offset)
{
	struct nvmap_handle *h = (struct nvmap_handle *)hMem;
	unsigned long addr;

	if (unlikely(!atomic_add_return(0, &h->pin) || !h->alloc ||
	    Offset >= h->orig_size)) {
		WARN_ON(1);
		return ~0ul;
	}

	if (h->heap_pgalloc && h->pgalloc.contig)
		addr = page_to_phys(h->pgalloc.pages[0]);
	else if (h->heap_pgalloc) {
		BUG_ON(!h->pgalloc.area);
		addr = h->pgalloc.area->iovm_start;
	} else
		addr = h->carveout.base;

	return (NvU32)addr+Offset;

}

void NvRmMemPinMult(NvRmMemHandle *hMems, NvU32 *addrs, NvU32 Count)
{
	struct nvmap_handle **h = (struct nvmap_handle **)hMems;
	unsigned int i;
	int ret;

	do {
		ret = _nvmap_handle_pin_fast(Count, h);
		if (ret && !try_to_freeze()) {
			pr_err("%s: failed to pin handles\n", __func__);
			dump_stack();
		}
	} while (ret);

	for (i=0; i<Count; i++) {
		addrs[i] = NvRmMemGetAddress(hMems[i], 0);
		BUG_ON(addrs[i]==~0ul);
	}
}

void NvRmMemUnpinMult(NvRmMemHandle *hMems, NvU32 Count)
{
	int do_wake = 0;
	unsigned int i;

	for (i=0; i<Count; i++) {
		struct nvmap_handle *h = (struct nvmap_handle *)hMems[i];
		if (h) {
			BUG_ON(atomic_add_return(0, &h->pin)==0);
			do_wake |= _nvmap_handle_unpin(h);
		}
	}

	if (do_wake) wake_up(&nvmap_pin_wait);
}

NvU32 NvRmMemPin(NvRmMemHandle hMem)
{
	NvU32 addr;
	NvRmMemPinMult(&hMem, &addr, 1);
	return addr;
}

void NvRmMemUnpin(NvRmMemHandle hMem)
{
	NvRmMemUnpinMult(&hMem, 1);
}

void NvRmMemHandleFree(NvRmMemHandle hMem)
{
	_nvmap_do_free(&nvmap_context.init_data, (unsigned long)hMem);
}

NvError NvRmMemMap(NvRmMemHandle hMem, NvU32 Offset, NvU32 Size,
	NvU32 Flags, void **pVirtAddr)
{
	struct nvmap_handle *h = (struct nvmap_handle *)hMem;
	pgprot_t prot = _nvmap_flag_to_pgprot(h->flags, pgprot_kernel);

	BUG_ON(!h->alloc);

	if (Offset+Size > h->size)
		return NvError_BadParameter;

	if (!h->kern_map && h->heap_pgalloc) {
		BUG_ON(h->size & ~PAGE_MASK);
		h->kern_map = vm_map_ram(h->pgalloc.pages,
			h->size>>PAGE_SHIFT, -1, prot);
	} else if (!h->kern_map) {
		unsigned int size;
		unsigned long addr;

		addr = h->carveout.base;
		size = h->size + (addr & ~PAGE_MASK);
		addr &= PAGE_MASK;
		size = (size + PAGE_SIZE - 1) & PAGE_MASK;

		h->kern_map = ioremap_wc(addr, size);
		if (h->kern_map) {
			addr = h->carveout.base - addr;
			h->kern_map += addr;
		}
	}

	if (h->kern_map) {
		*pVirtAddr = (h->kern_map + Offset);
		return NvSuccess;
	}

	return NvError_InsufficientMemory;
}

void NvRmMemUnmap(NvRmMemHandle hMem, void *pVirtAddr, NvU32 Size)
{
	return;
}

NvU32 NvRmMemGetId(NvRmMemHandle hMem)
{
	struct nvmap_handle *h = (struct nvmap_handle *)hMem;
	if (!h->owner) h->global = true;
	return (NvU32)h;
}

NvError NvRmMemHandleFromId(NvU32 id, NvRmMemHandle *hMem)
{
	struct nvmap_handle_ref *r;

	int err = _nvmap_do_create(&nvmap_context.init_data,
		NVMEM_IOC_FROM_ID, id, true, &r);

	if (err || !r) return NvError_NotInitialized;

	*hMem = (NvRmMemHandle)r->h;
	return NvSuccess;
}

NvError NvRmMemHandleClaimPreservedHandle(NvRmDeviceHandle hRm,
	NvU32 Key, NvRmMemHandle *hMem)
{
	struct nvmap_handle_ref *r;

	int err = _nvmap_do_create(&nvmap_context.init_data,
		NVMEM_IOC_CLAIM, (unsigned long)Key, true, &r);

	if (err || !r) return NvError_NotInitialized;

	*hMem = (NvRmMemHandle)r->h;
	return NvSuccess;
}

NvError NvRmMemHandleCreate(NvRmDeviceHandle hRm,
	NvRmMemHandle *hMem, NvU32 Size)
{
	struct nvmap_handle_ref *r;
	int err = _nvmap_do_create(&nvmap_context.init_data,
		NVMEM_IOC_CREATE, (unsigned long)Size, true, &r);

	if (err || !r) return NvError_InsufficientMemory;
	*hMem = (NvRmMemHandle)r->h;
	return NvSuccess;
}

NvError NvRmMemAlloc(NvRmMemHandle hMem, const NvRmHeap *Heaps,
	NvU32 NumHeaps, NvU32 Alignment, NvOsMemAttribute Coherency)
{
	unsigned int flags = pgprot_kernel;
	int err = -ENOMEM;

	BUG_ON(Alignment & (Alignment-1));

	if (Coherency == NvOsMemAttribute_WriteBack)
		flags = NVMEM_HANDLE_INNER_CACHEABLE;
	else
		flags = NVMEM_HANDLE_WRITE_COMBINE;

	if (!NumHeaps || !Heaps) {
		err = _nvmap_do_alloc(&nvmap_context.init_data,
				  (unsigned long)hMem, NVMAP_KERNEL_DEFAULT_HEAPS,
				  (size_t)Alignment, flags);
	}
	else {
		unsigned int i;
		for (i = 0; i < NumHeaps; i++) {
			unsigned int heap;
			switch (Heaps[i]) {
			case NvRmHeap_GART:
				heap = NVMEM_HEAP_IOVMM;
				break;
			case NvRmHeap_External:
				heap = NVMEM_HEAP_SYSMEM;
				break;
			case NvRmHeap_ExternalCarveOut:
				heap = NVMEM_HEAP_CARVEOUT_GENERIC;
				break;
			case NvRmHeap_IRam:
				heap = NVMEM_HEAP_CARVEOUT_IRAM;
				break;
			default:
				heap = 0;
				break;
			}
			if (heap) {
				err = _nvmap_do_alloc(&nvmap_context.init_data,
						  (unsigned long)hMem, heap,
						  (size_t)Alignment, flags);
				if (!err) break;
			}
		}
	}

	return (err ? NvError_InsufficientMemory : NvSuccess);
}

void NvRmMemReadStrided(NvRmMemHandle hMem, NvU32 Offset, NvU32 SrcStride,
	void *pDst, NvU32 DstStride, NvU32 ElementSize, NvU32 Count)
{
	ssize_t bytes = 0;

	bytes = _nvmap_do_rw_handle((struct nvmap_handle *)hMem, true,
		false, Offset, (unsigned long)pDst, SrcStride,
		DstStride, ElementSize, Count);

	BUG_ON(bytes != (ssize_t)(Count*ElementSize));
}

void NvRmMemWriteStrided(NvRmMemHandle hMem, NvU32 Offset, NvU32 DstStride,
	const void *pSrc, NvU32 SrcStride, NvU32 ElementSize, NvU32 Count)
{
	ssize_t bytes = 0;

	bytes = _nvmap_do_rw_handle((struct nvmap_handle *)hMem, false,
		false, Offset, (unsigned long)pSrc, DstStride,
		SrcStride, ElementSize, Count);

	BUG_ON(bytes != (ssize_t)(Count*ElementSize));
}

NvU32 NvRmMemGetSize(NvRmMemHandle hMem)
{
	struct nvmap_handle *h = (struct nvmap_handle *)hMem;
	return h->orig_size;
}

NvRmHeap NvRmMemGetHeapType(NvRmMemHandle hMem, NvU32 *BaseAddr)
{
	struct nvmap_handle *h = (struct nvmap_handle *)hMem;
	NvRmHeap heap;

	if (!h->alloc) {
		*BaseAddr = ~0ul;
		return (NvRmHeap)0;
	}

	if (h->heap_pgalloc && !h->pgalloc.contig)
		heap = NvRmHeap_GART;
	else if (h->heap_pgalloc)
		heap = NvRmHeap_External;
	else if ((h->carveout.base & 0xf0000000ul) == 0x40000000ul)
		heap = NvRmHeap_IRam;
	else
		heap = NvRmHeap_ExternalCarveOut;

	if (h->heap_pgalloc && h->pgalloc.contig)
		*BaseAddr = (NvU32)page_to_phys(h->pgalloc.pages[0]);
	else if (h->heap_pgalloc && atomic_add_return(0, &h->pin))
		*BaseAddr = h->pgalloc.area->iovm_start;
	else if (h->heap_pgalloc)
		*BaseAddr = ~0ul;
	else
		*BaseAddr = (NvU32)h->carveout.base;

	return heap;
}

void NvRmMemCacheMaint(NvRmMemHandle hMem, void *pMapping,
	NvU32 Size, NvBool Writeback, NvBool Inv)
{
	struct nvmap_handle *h = (struct nvmap_handle *)hMem;
	unsigned long start;
	unsigned int op;

	if (!h->kern_map || h->flags==NVMEM_HANDLE_UNCACHEABLE ||
		h->flags==NVMEM_HANDLE_WRITE_COMBINE) return;

	if (!Writeback && !Inv) return;

	if (Writeback && Inv) op = NVMEM_CACHE_OP_WB_INV;
	else if (Writeback) op = NVMEM_CACHE_OP_WB;
	else op = NVMEM_CACHE_OP_INV;

	start = (unsigned long)pMapping - (unsigned long)h->kern_map;

	_nvmap_do_cache_maint(h, start, start+Size, op, true);
	return;
}

NvU32 NvRmMemGetAlignment(NvRmMemHandle hMem)
{
	struct nvmap_handle *h = (struct nvmap_handle *)hMem;
	return _nvmap_do_get_param(h, NVMEM_HANDLE_PARAM_ALIGNMENT);
}

NvError NvRmMemGetStat(NvRmMemStat Stat, NvS32 *Result)
{
	unsigned long total_co = 0;
	unsigned long free_co = 0;
	unsigned long max_free = 0;
	struct nvmap_carveout_node *n;

	down_read(&nvmap_context.list_sem);
	list_for_each_entry(n, &nvmap_context.heaps, heap_list) {

		if (!(n->heap_bit & NVMEM_HEAP_CARVEOUT_GENERIC)) continue;
			total_co += _nvmap_carveout_blockstat(&n->carveout,
					CARVEOUT_STAT_TOTAL_SIZE);
			free_co += _nvmap_carveout_blockstat(&n->carveout,
					CARVEOUT_STAT_FREE_SIZE);
			max_free = max(max_free,
				_nvmap_carveout_blockstat(&n->carveout,
				    CARVEOUT_STAT_LARGEST_FREE));
	}
	up_read(&nvmap_context.list_sem);

	if (Stat==NvRmMemStat_TotalCarveout) {
		*Result = (NvU32)total_co;
		return NvSuccess;
	} else if (Stat==NvRmMemStat_UsedCarveout) {
		*Result = (NvU32)total_co - (NvU32)free_co;
		return NvSuccess;
	} else if (Stat==NvRmMemStat_LargestFreeCarveoutBlock) {
		*Result = (NvU32)max_free;
		return NvSuccess;
	}

	return NvError_BadParameter;
}

NvU8 NvRmMemRd08(NvRmMemHandle hMem, NvU32 Offset)
{
	NvU8 val;
	NvRmMemRead(hMem, Offset, &val, sizeof(val));
	return val;
}

NvU16 NvRmMemRd16(NvRmMemHandle hMem, NvU32 Offset)
{
	NvU16 val;
	NvRmMemRead(hMem, Offset, &val, sizeof(val));
	return val;
}

NvU32 NvRmMemRd32(NvRmMemHandle hMem, NvU32 Offset)
{
	NvU32 val;
	NvRmMemRead(hMem, Offset, &val, sizeof(val));
	return val;
}

void NvRmMemWr08(NvRmMemHandle hMem, NvU32 Offset, NvU8 Data)
{
	NvRmMemWrite(hMem, Offset, &Data, sizeof(Data));
}

void NvRmMemWr16(NvRmMemHandle hMem, NvU32 Offset, NvU16 Data)
{
	NvRmMemWrite(hMem, Offset, &Data, sizeof(Data));
}

void NvRmMemWr32(NvRmMemHandle hMem, NvU32 Offset, NvU32 Data)
{
	NvRmMemWrite(hMem, Offset, &Data, sizeof(Data));
}

void NvRmMemRead(NvRmMemHandle hMem, NvU32 Offset, void *pDst, NvU32 Size)
{
	NvRmMemReadStrided(hMem, Offset, Size, pDst, Size, Size, 1);
}

void NvRmMemWrite(NvRmMemHandle hMem, NvU32 Offset,
	const void *pSrc, NvU32 Size)
{
	NvRmMemWriteStrided(hMem, Offset, Size, pSrc, Size, Size, 1);
}

void NvRmMemMove(NvRmMemHandle dstHMem, NvU32 dstOffset,
	NvRmMemHandle srcHMem, NvU32 srcOffset, NvU32 Size)
{
	while (Size--) {
		NvU8 tmp = NvRmMemRd08(srcHMem, srcOffset);
		NvRmMemWr08(dstHMem, dstOffset, tmp);
		dstOffset++;
		srcOffset++;
	}
}

NvU32 NvRmMemGetCacheLineSize(void)
{
	return 32;
}

void *NvRmHostAlloc(size_t size)
{
	return NvOsAlloc(size);
}

void NvRmHostFree(void *ptr)
{
	NvOsFree(ptr);
}

NvError NvRmMemMapIntoCallerPtr(NvRmMemHandle hMem, void *pCallerPtr,
	NvU32 Offset, NvU32 Size)
{
	return NvError_NotSupported;
}

NvError NvRmMemHandlePreserveHandle(NvRmMemHandle hMem, NvU32 *pKey)
{
	return NvError_NotSupported;
}

#endif

static u32 nvmap_get_physaddr(struct nvmap_handle *h)
{
	u32 addr;

	if (h->heap_pgalloc && h->pgalloc.contig) {
		addr = page_to_phys(h->pgalloc.pages[0]);
	} else if (h->heap_pgalloc) {
		BUG_ON(!h->pgalloc.area);
		addr = h->pgalloc.area->iovm_start;
	} else {
		addr = h->carveout.base;
	}

	return addr;
}

struct nvmap_handle *nvmap_alloc(
	size_t size, size_t align,
	unsigned int flags, void **map)
{
	struct nvmap_handle_ref *r = NULL;
	struct nvmap_handle *h;
	int err;

	err = _nvmap_do_create(&nvmap_context.init_data,
			NVMEM_IOC_CREATE, (unsigned long)size, true, &r);
	if (err || !r)
		return ERR_PTR(err);
	h = r->h;

	err = _nvmap_do_alloc(&nvmap_context.init_data,
			(unsigned long)h, NVMAP_KERNEL_DEFAULT_HEAPS,
			align, flags);
	if (err) {
		_nvmap_do_free(&nvmap_context.init_data, (unsigned long)h);
		return ERR_PTR(err);
	}

	if (!map)
		return h;

	if (h->heap_pgalloc) {
		*map = vm_map_ram(h->pgalloc.pages, h->size >> PAGE_SHIFT, -1,
				_nvmap_flag_to_pgprot(h->flags, pgprot_kernel));
	} else {
		size_t mapaddr = h->carveout.base;
		size_t mapsize = h->size;

		mapsize += (mapaddr & ~PAGE_MASK);
		mapaddr &= PAGE_MASK;
		mapsize = (mapsize + PAGE_SIZE - 1) & PAGE_MASK;

		/* TODO: [ahatala 2010-06-21] honor coherency flag? */
		*map = ioremap_wc(mapaddr, mapsize);
		if (*map)
			*map += (h->carveout.base - mapaddr);
	}
	if (!*map) {
		_nvmap_do_free(&nvmap_context.init_data, (unsigned long)h);
		return ERR_PTR(-ENOMEM);
	}
	/* TODO: [ahatala 2010-06-22] get rid of kern_map */
	h->kern_map = *map;
	return h;
}

void nvmap_free(struct nvmap_handle *h, void *map)
{
	if (map) {
		BUG_ON(h->kern_map != map);

		if (h->heap_pgalloc) {
			vm_unmap_ram(map, h->size >> PAGE_SHIFT);
		} else {
			unsigned long addr = (unsigned long)map;
			addr &= ~PAGE_MASK;
			iounmap((void *)addr);
		}
		h->kern_map = NULL;
	}
	_nvmap_do_free(&nvmap_context.init_data, (unsigned long)h);
}

u32 nvmap_pin_single(struct nvmap_handle *h)
{
	int ret;
	do {
		ret = _nvmap_handle_pin_fast(1, &h);
		if (ret) {
			pr_err("%s: failed to pin handle\n", __func__);
			dump_stack();
		}
	} while (ret);

	return nvmap_get_physaddr(h);
}

int nvmap_pin_array(struct file *filp,
		struct nvmap_pinarray_elem *arr, int num_elems,
		struct nvmap_handle **unique_arr, int *num_unique, bool wait)
{
	struct nvmap_pinarray_elem *elem;
	struct nvmap_file_priv *priv = filp->private_data;
	int i, unique_idx = 0;
	unsigned long pfn = 0;
	void *pteaddr = NULL;
	int ret = 0;

	mutex_lock(&nvmap_pin_lock);

	/* find unique handles, pin them and collect into unpin array */
	for (elem = arr, i = num_elems; i && !ret; i--, elem++) {
		struct nvmap_handle *to_pin = elem->pin_mem;
		if (to_pin->poison != NVDA_POISON) {
			pr_err("%s: handle is poisoned\n", __func__);
			ret = -EFAULT;
		}
		else if (!(to_pin->flags & NVMEM_HANDLE_VISITED)) {
			if (!priv->su && !to_pin->global) {
				struct nvmap_handle_ref *r;
				spin_lock(&priv->ref_lock);
				r = _nvmap_ref_lookup_locked(priv,
							(unsigned long)to_pin);
				spin_unlock(&priv->ref_lock);
				if (!r) {
					pr_err("%s: handle access failure\n", __func__);
					ret = -EPERM;
					break;
				}
			}
			if (wait) {
				ret = wait_event_interruptible(
					nvmap_pin_wait,
					!_nvmap_handle_pin_locked(to_pin));
			}
			else
				ret = _nvmap_handle_pin_locked(to_pin);
			if (!ret) {
				to_pin->flags |= NVMEM_HANDLE_VISITED;
				unique_arr[unique_idx++] = to_pin;
			}
		}
	}

	/* clear visited flags before releasing mutex */
	i = unique_idx;
	while (i--)
		unique_arr[i]->flags &= ~NVMEM_HANDLE_VISITED;

	mutex_unlock(&nvmap_pin_lock);

	if (!ret)
		ret = nvmap_map_pte(pfn, pgprot_kernel, &pteaddr);

	if (unlikely(ret)) {
		int do_wake = 0;
		i = unique_idx;
		while (i--)
			do_wake |= _nvmap_handle_unpin(unique_arr[i]);
		if (do_wake)
			wake_up(&nvmap_pin_wait);
		return ret;
	}

	for (elem = arr, i = num_elems; i; i--, elem++) {
		struct nvmap_handle *h_patch = elem->patch_mem;
		struct nvmap_handle *h_pin = elem->pin_mem;
		struct page *page = NULL;
		u32* patch_addr;

		/* commit iovmm mapping */
		if (h_pin->heap_pgalloc && h_pin->pgalloc.dirty)
			_nvmap_handle_iovmm_map(h_pin);

		/* patch */
		if (h_patch->kern_map) {
			patch_addr = (u32*)((unsigned long)h_patch->kern_map +
					elem->patch_offset);
		} else {
			unsigned long phys, new_pfn;
			if (h_patch->heap_pgalloc) {
				page = h_patch->pgalloc.pages[elem->patch_offset >> PAGE_SHIFT];
				get_page(page);
				phys = page_to_phys(page) + (elem->patch_offset & ~PAGE_MASK);
			} else {
				phys = h_patch->carveout.base + elem->patch_offset;
			}
			new_pfn = __phys_to_pfn(phys);
			if (new_pfn != pfn) {
				_nvmap_set_pte_at((unsigned long)pteaddr, new_pfn,
						_nvmap_flag_to_pgprot(h_patch->flags, pgprot_kernel));
				pfn = new_pfn;
			}
			patch_addr = (u32*)((unsigned long)pteaddr + (phys & ~PAGE_MASK));
		}

		*patch_addr = nvmap_get_physaddr(h_pin) + elem->pin_offset;

		if (page)
			put_page(page);
	}
	nvmap_unmap_pte(pteaddr);
	*num_unique = unique_idx;
	return 0;
}

void nvmap_unpin(struct nvmap_handle **h, int num_handles)
{
	int do_wake = 0;

	while (num_handles--) {
		BUG_ON(!*h);
		do_wake |= _nvmap_handle_unpin(*h);
		h++;
	}

	if (do_wake) wake_up(&nvmap_pin_wait);
}

int nvmap_validate_file(struct file *f)
{
	return (f->f_op==&knvmap_fops || f->f_op==&nvmap_fops) ? 0 : -EFAULT;
}
