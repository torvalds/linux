/*
 *      memrar_handler 1.0:  An Intel restricted access region handler device
 *
 *      Copyright (C) 2010 Intel Corporation. All rights reserved.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of version 2 of the GNU General
 *      Public License as published by the Free Software Foundation.
 *
 *      This program is distributed in the hope that it will be
 *      useful, but WITHOUT ANY WARRANTY; without even the implied
 *      warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *      PURPOSE.  See the GNU General Public License for more details.
 *      You should have received a copy of the GNU General Public
 *      License along with this program; if not, write to the Free
 *      Software Foundation, Inc., 59 Temple Place - Suite 330,
 *      Boston, MA  02111-1307, USA.
 *      The full GNU General Public License is included in this
 *      distribution in the file called COPYING.
 *
 * -------------------------------------------------------------------
 *
 *      Moorestown restricted access regions (RAR) provide isolated
 *      areas of main memory that are only acceessible by authorized
 *      devices.
 *
 *      The Intel Moorestown RAR handler module exposes a kernel space
 *      RAR memory management mechanism.  It is essentially a
 *      RAR-specific allocator.
 *
 *      Besides providing RAR buffer management, the RAR handler also
 *      behaves in many ways like an OS virtual memory manager.  For
 *      example, the RAR "handles" created by the RAR handler are
 *      analogous to user space virtual addresses.
 *
 *      RAR memory itself is never accessed directly by the RAR
 *      handler.
 */

#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/kref.h>
#include <linux/mutex.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/io.h>

#include "../rar_register/rar_register.h"

#include "memrar.h"
#include "memrar_allocator.h"


#define MEMRAR_VER "1.0"

/*
 * Moorestown supports three restricted access regions.
 *
 * We only care about the first two, video and audio.  The third,
 * reserved for Chaabi and the P-unit, will be handled by their
 * respective drivers.
 */
#define MRST_NUM_RAR 2

/* ---------------- -------------------- ------------------- */

/**
 * struct memrar_buffer_info - struct that keeps track of all RAR buffers
 * @list:	Linked list of memrar_buffer_info objects.
 * @buffer:	Core RAR buffer information.
 * @refcount:	Reference count.
 * @owner:	File handle corresponding to process that reserved the
 *		block of memory in RAR.  This will be zero for buffers
 *		allocated by other drivers instead of by a user space
 *		process.
 *
 * This structure encapsulates a link list of RAR buffers, as well as
 * other characteristics specific to a given list node, such as the
 * reference count on the corresponding RAR buffer.
 */
struct memrar_buffer_info {
	struct list_head list;
	struct RAR_buffer buffer;
	struct kref refcount;
	struct file *owner;
};

/**
 * struct memrar_rar_info - characteristics of a given RAR
 * @base:	Base bus address of the RAR.
 * @length:	Length of the RAR.
 * @iobase:	Virtual address of RAR mapped into kernel.
 * @allocator:	Allocator associated with the RAR.  Note the allocator
 *		"capacity" may be smaller than the RAR length if the
 *		length is not a multiple of the configured allocator
 *		block size.
 * @buffers:	Table that keeps track of all reserved RAR buffers.
 * @lock:	Lock used to synchronize access to RAR-specific data
 *		structures.
 *
 * Each RAR has an associated memrar_rar_info structure that describes
 * where in memory the RAR is located, how large it is, and a list of
 * reserved RAR buffers inside that RAR.  Each RAR also has a mutex
 * associated with it to reduce lock contention when operations on
 * multiple RARs are performed in parallel.
 */
struct memrar_rar_info {
	dma_addr_t base;
	unsigned long length;
	void __iomem *iobase;
	struct memrar_allocator *allocator;
	struct memrar_buffer_info buffers;
	struct mutex lock;
};

/*
 * Array of RAR characteristics.
 */
static struct memrar_rar_info memrars[MRST_NUM_RAR];

/* ---------------- -------------------- ------------------- */

/* Validate RAR type. */
static inline int memrar_is_valid_rar_type(u32 type)
{
	return type == RAR_TYPE_VIDEO || type == RAR_TYPE_AUDIO;
}

/* Check if an address/handle falls with the given RAR memory range. */
static inline int memrar_handle_in_range(struct memrar_rar_info *rar,
					 u32 vaddr)
{
	unsigned long const iobase = (unsigned long) (rar->iobase);
	return (vaddr >= iobase && vaddr < iobase + rar->length);
}

/* Retrieve RAR information associated with the given handle. */
static struct memrar_rar_info *memrar_get_rar_info(u32 vaddr)
{
	int i;
	for (i = 0; i < MRST_NUM_RAR; ++i) {
		struct memrar_rar_info * const rar = &memrars[i];
		if (memrar_handle_in_range(rar, vaddr))
			return rar;
	}

	return NULL;
}

/*
 * Retrieve bus address from given handle.
 *
 * Returns address corresponding to given handle.  Zero if handle is
 * invalid.
 */
static dma_addr_t memrar_get_bus_address(
	struct memrar_rar_info *rar,
	u32 vaddr)
{
	unsigned long const iobase = (unsigned long) (rar->iobase);

	if (!memrar_handle_in_range(rar, vaddr))
		return 0;

	/*
	 * An assumption is made that the virtual address offset is
	 * the same as the bus address offset, at least based on the
	 * way this driver is implemented.  For example, vaddr + 2 ==
	 * baddr + 2.
	 *
	 * @todo Is that a valid assumption?
	 */
	return rar->base + (vaddr - iobase);
}

/*
 * Retrieve physical address from given handle.
 *
 * Returns address corresponding to given handle.  Zero if handle is
 * invalid.
 */
static dma_addr_t memrar_get_physical_address(
	struct memrar_rar_info *rar,
	u32 vaddr)
{
	/*
	 * @todo This assumes that the bus address and physical
	 *       address are the same.  That is true for Moorestown
	 *       but not necessarily on other platforms.  This
	 *       deficiency should be addressed at some point.
	 */
	return memrar_get_bus_address(rar, vaddr);
}

/*
 * Core block release code.
 *
 * Note: This code removes the node from a list.  Make sure any list
 *       iteration is performed using list_for_each_safe().
 */
static void memrar_release_block_i(struct kref *ref)
{
	/*
	 * Last reference is being released.  Remove from the table,
	 * and reclaim resources.
	 */

	struct memrar_buffer_info * const node =
		container_of(ref, struct memrar_buffer_info, refcount);

	struct RAR_block_info * const user_info =
		&node->buffer.info;

	struct memrar_allocator * const allocator =
		memrars[user_info->type].allocator;

	list_del(&node->list);

	memrar_allocator_free(allocator, user_info->handle);

	kfree(node);
}

/*
 * Initialize RAR parameters, such as bus addresses, etc.
 */
static int memrar_init_rar_resources(char const *devname)
{
	/* ---- Sanity Checks ----
	 * 1. RAR bus addresses in both Lincroft and Langwell RAR
	 *    registers should be the same.
	 *    a. There's no way we can do this through IA.
	 *
	 * 2. Secure device ID in Langwell RAR registers should be set
	 *    appropriately, e.g. only LPE DMA for the audio RAR, and
	 *    security for the other Langwell based RAR registers.
	 *    a. There's no way we can do this through IA.
	 *
	 * 3. Audio and video RAR registers and RAR access should be
	 *    locked down.  If not, enable RAR access control.  Except
	 *    for debugging purposes, there is no reason for them to
	 *    be unlocked.
	 *    a.  We can only do this for the Lincroft (IA) side.
	 *
	 * @todo Should the RAR handler driver even be aware of audio
	 *       and video RAR settings?
	 */

	/*
	 * RAR buffer block size.
	 *
	 * We choose it to be the size of a page to simplify the
	 * /dev/memrar mmap() implementation and usage.  Otherwise
	 * paging is not involved once an RAR is locked down.
	 */
	static size_t const RAR_BLOCK_SIZE = PAGE_SIZE;

	int z;
	int found_rar = 0;

	BUG_ON(MRST_NUM_RAR != ARRAY_SIZE(memrars));

	for (z = 0; z != MRST_NUM_RAR; ++z) {
		dma_addr_t low, high;
		struct memrar_rar_info * const rar = &memrars[z];

		BUG_ON(!memrar_is_valid_rar_type(z));

		mutex_init(&rar->lock);

		/*
		 * Initialize the process table before we reach any
		 * code that exit on failure since the finalization
		 * code requires an initialized list.
		 */
		INIT_LIST_HEAD(&rar->buffers.list);

		if (rar_get_address(z, &low, &high) != 0) {
			/* No RAR is available. */
			break;
		} else if (low == 0 || high == 0) {
			/*
			 * We don't immediately break out of the loop
			 * since the next type of RAR may be enabled.
			 */
			rar->base      = 0;
			rar->length    = 0;
			rar->iobase    = NULL;
			rar->allocator = NULL;
			continue;
		}

		/*
		 * @todo Verify that LNC and LNW RAR register contents
		 *       addresses, security, etc are compatible and
		 *       consistent).
		 */

		rar->length = high - low + 1;

		/* Claim RAR memory as our own. */
		if (request_mem_region(low, rar->length, devname) == NULL) {
			rar->length = 0;

			pr_err("%s: Unable to claim RAR[%d] memory.\n",
			       devname,
			       z);
			pr_err("%s: RAR[%d] disabled.\n", devname, z);

			/*
			 * Rather than break out of the loop by
			 * returning -EBUSY, for example, we may be
			 * able to claim memory of the next RAR region
			 * as our own.
			 */
			continue;
		}

		rar->base = low;

		/*
		 * Now map it into the kernel address space.
		 *
		 * Note that the RAR memory may only be accessed by IA
		 * when debugging.  Otherwise attempts to access the
		 * RAR memory when it is locked down will result in
		 * behavior similar to writing to /dev/null and
		 * reading from /dev/zero.  This behavior is enforced
		 * by the hardware.  Even if we don't access the
		 * memory, mapping it into the kernel provides us with
		 * a convenient RAR handle to bus address mapping.
		 */
		rar->iobase = ioremap_nocache(rar->base, rar->length);
		if (rar->iobase == NULL) {
			pr_err("%s: Unable to map RAR memory.\n",
			       devname);
			return -ENOMEM;
		}

		/* Initialize corresponding memory allocator. */
		rar->allocator = memrar_create_allocator(
			(unsigned long) rar->iobase,
			rar->length,
			RAR_BLOCK_SIZE);
		if (rar->allocator == NULL)
			return -1;

		/*
		 * -------------------------------------------------
		 * Make sure all RARs handled by us are locked down.
		 * -------------------------------------------------
		 */

		/* Enable RAR protection on the Lincroft side. */
		if (0) {
			/*
			 * This is mostly a sanity check since the
			 * vendor should have locked down RAR in the
			 * SMIP header RAR configuration.
			 */
			rar_lock(z);
		} else {
			pr_warning("%s: LNC RAR[%d] no lock sanity check.\n",
				   devname,
				   z);
		}

		/* ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ */
		/* |||||||||||||||||||||||||||||||||||||||||||||||||| */

		/*
		 * It would be nice if we could verify that RAR
		 * protection on the Langwell side is enabled, but
		 * there is no way to do that from here.  The
		 * necessary Langwell RAR registers are not accessible
		 * from the Lincroft (IA) side.
		 *
		 * Hopefully the ODM did the right thing and enabled
		 * Langwell side RAR protection in the integrated
		 * firmware SMIP header.
		 */

		pr_info("%s: BRAR[%d] bus address range = "
			"[0x%lx, 0x%lx]\n",
			devname,
			z,
			(unsigned long) low,
			(unsigned long) high);

		pr_info("%s: BRAR[%d] size = %u KiB\n",
			devname,
			z,
			rar->allocator->capacity / 1024);

		found_rar = 1;
	}

	if (!found_rar)	{
		/*
		 * No RAR support.  Don't bother continuing.
		 *
		 * Note that this is not a failure.
		 */
		pr_info("%s: No Moorestown RAR support available.\n",
			devname);
		return -ENODEV;
	}

	return 0;
}

/*
 * Finalize RAR resources.
 */
static void memrar_fini_rar_resources(void)
{
	int z;
	struct memrar_buffer_info *pos;
	struct memrar_buffer_info *tmp;

	/*
	 * @todo Do we need to hold a lock at this point in time?
	 *       (module initialization failure or exit?)
	 */

	for (z = MRST_NUM_RAR; z-- != 0; ) {
		struct memrar_rar_info * const rar = &memrars[z];

		/* Clean up remaining resources. */

		list_for_each_entry_safe(pos,
					 tmp,
					 &rar->buffers.list,
					 list) {
			kref_put(&pos->refcount, memrar_release_block_i);
		}

		memrar_destroy_allocator(rar->allocator);
		rar->allocator = NULL;

		iounmap(rar->iobase);
		rar->iobase = NULL;

		release_mem_region(rar->base, rar->length);
		rar->base = 0;

		rar->length = 0;
	}
}

static long memrar_reserve_block(struct RAR_buffer *request,
				 struct file *filp)
{
	struct RAR_block_info * const rinfo = &request->info;
	struct RAR_buffer *buffer;
	struct memrar_buffer_info *buffer_info;
	u32 handle;
	struct memrar_rar_info *rar = NULL;

	/* Prevent array overflow. */
	if (!memrar_is_valid_rar_type(rinfo->type))
		return -EINVAL;

	rar = &memrars[rinfo->type];

	/* Reserve memory in RAR. */
	handle = memrar_allocator_alloc(rar->allocator, rinfo->size);
	if (handle == 0)
		return -ENOMEM;

	buffer_info = kmalloc(sizeof(*buffer_info), GFP_KERNEL);

	if (buffer_info == NULL) {
		memrar_allocator_free(rar->allocator, handle);
		return -ENOMEM;
	}

	buffer = &buffer_info->buffer;
	buffer->info.type = rinfo->type;
	buffer->info.size = rinfo->size;

	/* Memory handle corresponding to the bus address. */
	buffer->info.handle = handle;
	buffer->bus_address = memrar_get_bus_address(rar, handle);

	/*
	 * Keep track of owner so that we can later cleanup if
	 * necessary.
	 */
	buffer_info->owner = filp;

	kref_init(&buffer_info->refcount);

	mutex_lock(&rar->lock);
	list_add(&buffer_info->list, &rar->buffers.list);
	mutex_unlock(&rar->lock);

	rinfo->handle = buffer->info.handle;
	request->bus_address = buffer->bus_address;

	return 0;
}

static long memrar_release_block(u32 addr)
{
	struct memrar_buffer_info *pos;
	struct memrar_buffer_info *tmp;
	struct memrar_rar_info * const rar = memrar_get_rar_info(addr);
	long result = -EINVAL;

	if (rar == NULL)
		return -EFAULT;

	mutex_lock(&rar->lock);

	/*
	 * Iterate through the buffer list to find the corresponding
	 * buffer to be released.
	 */
	list_for_each_entry_safe(pos,
				 tmp,
				 &rar->buffers.list,
				 list) {
		struct RAR_block_info * const info =
			&pos->buffer.info;

		/*
		 * Take into account handle offsets that may have been
		 * added to the base handle, such as in the following
		 * scenario:
		 *
		 *     u32 handle = base + offset;
		 *     rar_handle_to_bus(handle);
		 *     rar_release(handle);
		 */
		if (addr >= info->handle
		    && addr < (info->handle + info->size)
		    && memrar_is_valid_rar_type(info->type)) {
			kref_put(&pos->refcount, memrar_release_block_i);
			result = 0;
			break;
		}
	}

	mutex_unlock(&rar->lock);

	return result;
}

static long memrar_get_stat(struct RAR_stat *r)
{
	long result = -EINVAL;

	if (likely(r != NULL) && memrar_is_valid_rar_type(r->type)) {
		struct memrar_allocator * const allocator =
			memrars[r->type].allocator;

		BUG_ON(allocator == NULL);

		/*
		 * Allocator capacity doesn't change over time.  No
		 * need to synchronize.
		 */
		r->capacity = allocator->capacity;

		mutex_lock(&allocator->lock);

		r->largest_block_size = allocator->largest_free_area;

		mutex_unlock(&allocator->lock);

		result = 0;
	}

	return result;
}

static long memrar_ioctl(struct file *filp,
			 unsigned int cmd,
			 unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	long result = 0;

	struct RAR_buffer buffer;
	struct RAR_block_info * const request = &buffer.info;
	struct RAR_stat rar_info;
	u32 rar_handle;

	switch (cmd) {
	case RAR_HANDLER_RESERVE:
		if (copy_from_user(request,
				   argp,
				   sizeof(*request)))
			return -EFAULT;

		result = memrar_reserve_block(&buffer, filp);
		if (result != 0)
			return result;

		return copy_to_user(argp, request, sizeof(*request));

	case RAR_HANDLER_RELEASE:
		if (copy_from_user(&rar_handle,
				   argp,
				   sizeof(rar_handle)))
			return -EFAULT;

		return memrar_release_block(rar_handle);

	case RAR_HANDLER_STAT:
		if (copy_from_user(&rar_info,
				   argp,
				   sizeof(rar_info)))
			return -EFAULT;

		/*
		 * Populate the RAR_stat structure based on the RAR
		 * type given by the user
		 */
		if (memrar_get_stat(&rar_info) != 0)
			return -EINVAL;

		/*
		 * @todo Do we need to verify destination pointer
		 *       "argp" is non-zero?  Is that already done by
		 *       copy_to_user()?
		 */
		return copy_to_user(argp,
				    &rar_info,
				    sizeof(rar_info)) ? -EFAULT : 0;

	default:
		return -ENOTTY;
	}

	return 0;
}

static int memrar_mmap(struct file *filp, struct vm_area_struct *vma)
{
	/*
	 * This mmap() implementation is predominantly useful for
	 * debugging since the CPU will be prevented from accessing
	 * RAR memory by the hardware when RAR is properly locked
	 * down.
	 *
	 * In order for this implementation to be useful RAR memory
	 * must be not be locked down.  However, we only want to do
	 * that when debugging.  DO NOT leave RAR memory unlocked in a
	 * deployed device that utilizes RAR.
	 */

	size_t const size = vma->vm_end - vma->vm_start;

	/* Users pass the RAR handle as the mmap() offset parameter. */
	unsigned long const handle = vma->vm_pgoff << PAGE_SHIFT;

	struct memrar_rar_info * const rar = memrar_get_rar_info(handle);

	unsigned long pfn;

	/* Invalid RAR handle or size passed to mmap(). */
	if (rar == NULL
	    || handle == 0
	    || size > (handle - (unsigned long) rar->iobase))
		return -EINVAL;

	/*
	 * Retrieve physical address corresponding to the RAR handle,
	 * and convert it to a page frame.
	 */
	pfn = memrar_get_physical_address(rar, handle) >> PAGE_SHIFT;


	pr_debug("memrar: mapping RAR range [0x%lx, 0x%lx) into user space.\n",
		 handle,
		 handle + size);

	/*
	 * Map RAR memory into user space.  This is really only useful
	 * for debugging purposes since the memory won't be
	 * accessible, i.e. reads return zero and writes are ignored,
	 * when RAR access control is enabled.
	 */
	if (remap_pfn_range(vma,
			    vma->vm_start,
			    pfn,
			    size,
			    vma->vm_page_prot))
		return -EAGAIN;

	/* vma->vm_ops = &memrar_mem_ops; */

	return 0;
}

static int memrar_open(struct inode *inode, struct file *filp)
{
	/* Nothing to do yet. */

	return 0;
}

static int memrar_release(struct inode *inode, struct file *filp)
{
	/* Free all regions associated with the given file handle. */

	struct memrar_buffer_info *pos;
	struct memrar_buffer_info *tmp;
	int z;

	for (z = 0; z != MRST_NUM_RAR; ++z) {
		struct memrar_rar_info * const rar = &memrars[z];

		mutex_lock(&rar->lock);

		list_for_each_entry_safe(pos,
					 tmp,
					 &rar->buffers.list,
					 list) {
			if (filp == pos->owner)
				kref_put(&pos->refcount,
					 memrar_release_block_i);
		}

		mutex_unlock(&rar->lock);
	}

	return 0;
}

/*
 * This function is part of the kernel space memrar driver API.
 */
size_t rar_reserve(struct RAR_buffer *buffers, size_t count)
{
	struct RAR_buffer * const end =
		(buffers == NULL ? buffers : buffers + count);
	struct RAR_buffer *i;

	size_t reserve_count = 0;

	for (i = buffers; i != end; ++i) {
		if (memrar_reserve_block(i, NULL) == 0)
			++reserve_count;
		else
			i->bus_address = 0;
	}

	return reserve_count;
}
EXPORT_SYMBOL(rar_reserve);

/*
 * This function is part of the kernel space memrar driver API.
 */
size_t rar_release(struct RAR_buffer *buffers, size_t count)
{
	struct RAR_buffer * const end =
		(buffers == NULL ? buffers : buffers + count);
	struct RAR_buffer *i;

	size_t release_count = 0;

	for (i = buffers; i != end; ++i) {
		u32 * const handle = &i->info.handle;
		if (memrar_release_block(*handle) == 0) {
			/*
			 * @todo We assume we should do this each time
			 *       the ref count is decremented.  Should
			 *       we instead only do this when the ref
			 *       count has dropped to zero, and the
			 *       buffer has been completely
			 *       released/unmapped?
			 */
			*handle = 0;
			++release_count;
		}
	}

	return release_count;
}
EXPORT_SYMBOL(rar_release);

/*
 * This function is part of the kernel space driver API.
 */
size_t rar_handle_to_bus(struct RAR_buffer *buffers, size_t count)
{
	struct RAR_buffer * const end =
		(buffers == NULL ? buffers : buffers + count);
	struct RAR_buffer *i;
	struct memrar_buffer_info *pos;

	size_t conversion_count = 0;

	/*
	 * Find all bus addresses corresponding to the given handles.
	 *
	 * @todo Not liking this nested loop.  Optimize.
	 */
	for (i = buffers; i != end; ++i) {
		struct memrar_rar_info * const rar =
			memrar_get_rar_info(i->info.handle);

		/*
		 * Check if we have a bogus handle, and then continue
		 * with remaining buffers.
		 */
		if (rar == NULL) {
			i->bus_address = 0;
			continue;
		}

		mutex_lock(&rar->lock);

		list_for_each_entry(pos, &rar->buffers.list, list) {
			struct RAR_block_info * const user_info =
				&pos->buffer.info;

			/*
			 * Take into account handle offsets that may
			 * have been added to the base handle, such as
			 * in the following scenario:
			 *
			 *     u32 handle = base + offset;
			 *     rar_handle_to_bus(handle);
			 */

			if (i->info.handle >= user_info->handle
			    && i->info.handle < (user_info->handle
						 + user_info->size)) {
				u32 const offset =
					i->info.handle - user_info->handle;

				i->info.type = user_info->type;
				i->info.size = user_info->size - offset;
				i->bus_address =
					pos->buffer.bus_address
					+ offset;

				/* Increment the reference count. */
				kref_get(&pos->refcount);

				++conversion_count;
				break;
			} else {
				i->bus_address = 0;
			}
		}

		mutex_unlock(&rar->lock);
	}

	return conversion_count;
}
EXPORT_SYMBOL(rar_handle_to_bus);

static const struct file_operations memrar_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = memrar_ioctl,
	.mmap           = memrar_mmap,
	.open           = memrar_open,
	.release        = memrar_release,
};

static struct miscdevice memrar_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,    /* dynamic allocation */
	.name = "memrar",               /* /dev/memrar */
	.fops = &memrar_fops
};

static char const banner[] __initdata =
	KERN_INFO
	"Intel RAR Handler: " MEMRAR_VER " initialized.\n";

static int memrar_registration_callback(void *ctx)
{
	/*
	 * We initialize the RAR parameters early on so that we can
	 * discontinue memrar device initialization and registration
	 * if suitably configured RARs are not available.
	 */
	int result = memrar_init_rar_resources(memrar_miscdev.name);

	if (result != 0)
		return result;

	result = misc_register(&memrar_miscdev);

	if (result != 0) {
		pr_err("%s: misc_register() failed.\n",
			memrar_miscdev.name);

		/* Clean up resources previously reserved. */
		memrar_fini_rar_resources();
	}

	return result;
}

static int __init memrar_init(void)
{
	printk(banner);

	return register_rar(&memrar_registration_callback, 0);
}

static void __exit memrar_exit(void)
{
	memrar_fini_rar_resources();

	misc_deregister(&memrar_miscdev);
}


module_init(memrar_init);
module_exit(memrar_exit);


MODULE_AUTHOR("Ossama Othman <ossama.othman@intel.com>");
MODULE_DESCRIPTION("Intel Restricted Access Region Handler");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(MISC_DYNAMIC_MINOR);
MODULE_VERSION(MEMRAR_VER);



/*
  Local Variables:
    c-file-style: "linux"
  End:
*/
