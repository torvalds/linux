/*
 *  linux/drivers/char/mem.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Added devfs support.
 *    Jan-11-1998, C. Scott Ananian <cananian@alumni.princeton.edu>
 *  Shared /dev/zero mmapping support, Feb 2000, Kanoj Sarcar <kanoj@sgi.com>
 */

#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/raw.h>
#include <linux/tty.h>
#include <linux/capability.h>
#include <linux/ptrace.h>
#include <linux/device.h>
#include <linux/highmem.h>
#include <linux/crash_dump.h>
#include <linux/backing-dev.h>
#include <linux/bootmem.h>
#include <linux/splice.h>
#include <linux/pfn.h>

#include <asm/uaccess.h>
#include <asm/io.h>

#ifdef CONFIG_IA64
# include <linux/efi.h>
#endif

static inline unsigned long size_inside_page(unsigned long start,
					     unsigned long size)
{
	unsigned long sz;

	sz = PAGE_SIZE - (start & (PAGE_SIZE - 1));

	return min(sz, size);
}

#ifndef ARCH_HAS_VALID_PHYS_ADDR_RANGE
static inline int valid_phys_addr_range(unsigned long addr, size_t count)
{
	if (addr + count > __pa(high_memory))
		return 0;

	return 1;
}

static inline int valid_mmap_phys_addr_range(unsigned long pfn, size_t size)
{
	return 1;
}
#endif

#ifdef CONFIG_STRICT_DEVMEM
static inline int range_is_allowed(unsigned long pfn, unsigned long size)
{
	u64 from = ((u64)pfn) << PAGE_SHIFT;
	u64 to = from + size;
	u64 cursor = from;

	while (cursor < to) {
		if (!devmem_is_allowed(pfn)) {
			printk(KERN_INFO
		"Program %s tried to access /dev/mem between %Lx->%Lx.\n",
				current->comm, from, to);
			return 0;
		}
		cursor += PAGE_SIZE;
		pfn++;
	}
	return 1;
}
#else
static inline int range_is_allowed(unsigned long pfn, unsigned long size)
{
	return 1;
}
#endif

void __weak unxlate_dev_mem_ptr(unsigned long phys, void *addr)
{
}

/*
 * This funcion reads the *physical* memory. The f_pos points directly to the
 * memory location.
 */
static ssize_t read_mem(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	ssize_t read, sz;
	char *ptr;

	if (!valid_phys_addr_range(p, count))
		return -EFAULT;
	read = 0;
#ifdef __ARCH_HAS_NO_PAGE_ZERO_MAPPED
	/* we don't have page 0 mapped on sparc and m68k.. */
	if (p < PAGE_SIZE) {
		sz = size_inside_page(p, count);
		if (sz > 0) {
			if (clear_user(buf, sz))
				return -EFAULT;
			buf += sz;
			p += sz;
			count -= sz;
			read += sz;
		}
	}
#endif

	while (count > 0) {
		unsigned long remaining;

		sz = size_inside_page(p, count);

		if (!range_is_allowed(p >> PAGE_SHIFT, count))
			return -EPERM;

		/*
		 * On ia64 if a page has been mapped somewhere as uncached, then
		 * it must also be accessed uncached by the kernel or data
		 * corruption may occur.
		 */
		ptr = xlate_dev_mem_ptr(p);
		if (!ptr)
			return -EFAULT;

		remaining = copy_to_user(buf, ptr, sz);
		unxlate_dev_mem_ptr(p, ptr);
		if (remaining)
			return -EFAULT;

		buf += sz;
		p += sz;
		count -= sz;
		read += sz;
	}

	*ppos += read;
	return read;
}

static ssize_t write_mem(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	ssize_t written, sz;
	unsigned long copied;
	void *ptr;

	if (!valid_phys_addr_range(p, count))
		return -EFAULT;

	written = 0;

#ifdef __ARCH_HAS_NO_PAGE_ZERO_MAPPED
	/* we don't have page 0 mapped on sparc and m68k.. */
	if (p < PAGE_SIZE) {
		sz = size_inside_page(p, count);
		/* Hmm. Do something? */
		buf += sz;
		p += sz;
		count -= sz;
		written += sz;
	}
#endif

	while (count > 0) {
		sz = size_inside_page(p, count);

		if (!range_is_allowed(p >> PAGE_SHIFT, sz))
			return -EPERM;

		/*
		 * On ia64 if a page has been mapped somewhere as uncached, then
		 * it must also be accessed uncached by the kernel or data
		 * corruption may occur.
		 */
		ptr = xlate_dev_mem_ptr(p);
		if (!ptr) {
			if (written)
				break;
			return -EFAULT;
		}

		copied = copy_from_user(ptr, buf, sz);
		unxlate_dev_mem_ptr(p, ptr);
		if (copied) {
			written += sz - copied;
			if (written)
				break;
			return -EFAULT;
		}

		buf += sz;
		p += sz;
		count -= sz;
		written += sz;
	}

	*ppos += written;
	return written;
}

int __weak phys_mem_access_prot_allowed(struct file *file,
	unsigned long pfn, unsigned long size, pgprot_t *vma_prot)
{
	return 1;
}

#ifndef __HAVE_PHYS_MEM_ACCESS_PROT

/*
 * Architectures vary in how they handle caching for addresses
 * outside of main memory.
 *
 */
static int uncached_access(struct file *file, unsigned long addr)
{
#if defined(CONFIG_IA64)
	/*
	 * On ia64, we ignore O_DSYNC because we cannot tolerate memory
	 * attribute aliases.
	 */
	return !(efi_mem_attributes(addr) & EFI_MEMORY_WB);
#elif defined(CONFIG_MIPS)
	{
		extern int __uncached_access(struct file *file,
					     unsigned long addr);

		return __uncached_access(file, addr);
	}
#else
	/*
	 * Accessing memory above the top the kernel knows about or through a
	 * file pointer
	 * that was marked O_DSYNC will be done non-cached.
	 */
	if (file->f_flags & O_DSYNC)
		return 1;
	return addr >= __pa(high_memory);
#endif
}

static pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				     unsigned long size, pgprot_t vma_prot)
{
#ifdef pgprot_noncached
	unsigned long offset = pfn << PAGE_SHIFT;

	if (uncached_access(file, offset))
		return pgprot_noncached(vma_prot);
#endif
	return vma_prot;
}
#endif

#ifndef CONFIG_MMU
static unsigned long get_unmapped_area_mem(struct file *file,
					   unsigned long addr,
					   unsigned long len,
					   unsigned long pgoff,
					   unsigned long flags)
{
	if (!valid_mmap_phys_addr_range(pgoff, len))
		return (unsigned long) -EINVAL;
	return pgoff << PAGE_SHIFT;
}

/* can't do an in-place private mapping if there's no MMU */
static inline int private_mapping_ok(struct vm_area_struct *vma)
{
	return vma->vm_flags & VM_MAYSHARE;
}
#else
#define get_unmapped_area_mem	NULL

static inline int private_mapping_ok(struct vm_area_struct *vma)
{
	return 1;
}
#endif

static const struct vm_operations_struct mmap_mem_ops = {
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = generic_access_phys
#endif
};

static int mmap_mem(struct file *file, struct vm_area_struct *vma)
{
	size_t size = vma->vm_end - vma->vm_start;

	if (!valid_mmap_phys_addr_range(vma->vm_pgoff, size))
		return -EINVAL;

	if (!private_mapping_ok(vma))
		return -ENOSYS;

	if (!range_is_allowed(vma->vm_pgoff, size))
		return -EPERM;

	if (!phys_mem_access_prot_allowed(file, vma->vm_pgoff, size,
						&vma->vm_page_prot))
		return -EINVAL;

	vma->vm_page_prot = phys_mem_access_prot(file, vma->vm_pgoff,
						 size,
						 vma->vm_page_prot);

	vma->vm_ops = &mmap_mem_ops;

	/* Remap-pfn-range will mark the range VM_IO and VM_RESERVED */
	if (remap_pfn_range(vma,
			    vma->vm_start,
			    vma->vm_pgoff,
			    size,
			    vma->vm_page_prot)) {
		return -EAGAIN;
	}
	return 0;
}

#ifdef CONFIG_DEVKMEM
static int mmap_kmem(struct file *file, struct vm_area_struct *vma)
{
	unsigned long pfn;

	/* Turn a kernel-virtual address into a physical page frame */
	pfn = __pa((u64)vma->vm_pgoff << PAGE_SHIFT) >> PAGE_SHIFT;

	/*
	 * RED-PEN: on some architectures there is more mapped memory than
	 * available in mem_map which pfn_valid checks for. Perhaps should add a
	 * new macro here.
	 *
	 * RED-PEN: vmalloc is not supported right now.
	 */
	if (!pfn_valid(pfn))
		return -EIO;

	vma->vm_pgoff = pfn;
	return mmap_mem(file, vma);
}
#endif

#ifdef CONFIG_CRASH_DUMP
/*
 * Read memory corresponding to the old kernel.
 */
static ssize_t read_oldmem(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	unsigned long pfn, offset;
	size_t read = 0, csize;
	int rc = 0;

	while (count) {
		pfn = *ppos / PAGE_SIZE;
		if (pfn > saved_max_pfn)
			return read;

		offset = (unsigned long)(*ppos % PAGE_SIZE);
		if (count > PAGE_SIZE - offset)
			csize = PAGE_SIZE - offset;
		else
			csize = count;

		rc = copy_oldmem_page(pfn, buf, csize, offset, 1);
		if (rc < 0)
			return rc;
		buf += csize;
		*ppos += csize;
		read += csize;
		count -= csize;
	}
	return read;
}
#endif

#ifdef CONFIG_DEVKMEM
/*
 * This function reads the *virtual* memory as seen by the kernel.
 */
static ssize_t read_kmem(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	ssize_t low_count, read, sz;
	char * kbuf; /* k-addr because vread() takes vmlist_lock rwlock */
	int err = 0;

	read = 0;
	if (p < (unsigned long) high_memory) {
		low_count = count;
		if (count > (unsigned long)high_memory - p)
			low_count = (unsigned long)high_memory - p;

#ifdef __ARCH_HAS_NO_PAGE_ZERO_MAPPED
		/* we don't have page 0 mapped on sparc and m68k.. */
		if (p < PAGE_SIZE && low_count > 0) {
			sz = size_inside_page(p, low_count);
			if (clear_user(buf, sz))
				return -EFAULT;
			buf += sz;
			p += sz;
			read += sz;
			low_count -= sz;
			count -= sz;
		}
#endif
		while (low_count > 0) {
			sz = size_inside_page(p, low_count);

			/*
			 * On ia64 if a page has been mapped somewhere as
			 * uncached, then it must also be accessed uncached
			 * by the kernel or data corruption may occur
			 */
			kbuf = xlate_dev_kmem_ptr((char *)p);

			if (copy_to_user(buf, kbuf, sz))
				return -EFAULT;
			buf += sz;
			p += sz;
			read += sz;
			low_count -= sz;
			count -= sz;
		}
	}

	if (count > 0) {
		kbuf = (char *)__get_free_page(GFP_KERNEL);
		if (!kbuf)
			return -ENOMEM;
		while (count > 0) {
			sz = size_inside_page(p, count);
			if (!is_vmalloc_or_module_addr((void *)p)) {
				err = -ENXIO;
				break;
			}
			sz = vread(kbuf, (char *)p, sz);
			if (!sz)
				break;
			if (copy_to_user(buf, kbuf, sz)) {
				err = -EFAULT;
				break;
			}
			count -= sz;
			buf += sz;
			read += sz;
			p += sz;
		}
		free_page((unsigned long)kbuf);
	}
	*ppos = p;
	return read ? read : err;
}


static ssize_t do_write_kmem(unsigned long p, const char __user *buf,
				size_t count, loff_t *ppos)
{
	ssize_t written, sz;
	unsigned long copied;

	written = 0;
#ifdef __ARCH_HAS_NO_PAGE_ZERO_MAPPED
	/* we don't have page 0 mapped on sparc and m68k.. */
	if (p < PAGE_SIZE) {
		sz = size_inside_page(p, count);
		/* Hmm. Do something? */
		buf += sz;
		p += sz;
		count -= sz;
		written += sz;
	}
#endif

	while (count > 0) {
		char *ptr;

		sz = size_inside_page(p, count);

		/*
		 * On ia64 if a page has been mapped somewhere as uncached, then
		 * it must also be accessed uncached by the kernel or data
		 * corruption may occur.
		 */
		ptr = xlate_dev_kmem_ptr((char *)p);

		copied = copy_from_user(ptr, buf, sz);
		if (copied) {
			written += sz - copied;
			if (written)
				break;
			return -EFAULT;
		}
		buf += sz;
		p += sz;
		count -= sz;
		written += sz;
	}

	*ppos += written;
	return written;
}

/*
 * This function writes to the *virtual* memory as seen by the kernel.
 */
static ssize_t write_kmem(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	ssize_t wrote = 0;
	ssize_t virtr = 0;
	char * kbuf; /* k-addr because vwrite() takes vmlist_lock rwlock */
	int err = 0;

	if (p < (unsigned long) high_memory) {
		unsigned long to_write = min_t(unsigned long, count,
					       (unsigned long)high_memory - p);
		wrote = do_write_kmem(p, buf, to_write, ppos);
		if (wrote != to_write)
			return wrote;
		p += wrote;
		buf += wrote;
		count -= wrote;
	}

	if (count > 0) {
		kbuf = (char *)__get_free_page(GFP_KERNEL);
		if (!kbuf)
			return wrote ? wrote : -ENOMEM;
		while (count > 0) {
			unsigned long sz = size_inside_page(p, count);
			unsigned long n;

			if (!is_vmalloc_or_module_addr((void *)p)) {
				err = -ENXIO;
				break;
			}
			n = copy_from_user(kbuf, buf, sz);
			if (n) {
				err = -EFAULT;
				break;
			}
			vwrite(kbuf, (char *)p, sz);
			count -= sz;
			buf += sz;
			virtr += sz;
			p += sz;
		}
		free_page((unsigned long)kbuf);
	}

	*ppos = p;
	return virtr + wrote ? : err;
}
#endif

#ifdef CONFIG_DEVPORT
static ssize_t read_port(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	unsigned long i = *ppos;
	char __user *tmp = buf;

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;
	while (count-- > 0 && i < 65536) {
		if (__put_user(inb(i), tmp) < 0)
			return -EFAULT;
		i++;
		tmp++;
	}
	*ppos = i;
	return tmp-buf;
}

static ssize_t write_port(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	unsigned long i = *ppos;
	const char __user * tmp = buf;

	if (!access_ok(VERIFY_READ, buf, count))
		return -EFAULT;
	while (count-- > 0 && i < 65536) {
		char c;
		if (__get_user(c, tmp)) {
			if (tmp > buf)
				break;
			return -EFAULT;
		}
		outb(c, i);
		i++;
		tmp++;
	}
	*ppos = i;
	return tmp-buf;
}
#endif

static ssize_t read_null(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t write_null(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	return count;
}

static int pipe_to_null(struct pipe_inode_info *info, struct pipe_buffer *buf,
			struct splice_desc *sd)
{
	return sd->len;
}

static ssize_t splice_write_null(struct pipe_inode_info *pipe, struct file *out,
				 loff_t *ppos, size_t len, unsigned int flags)
{
	return splice_from_pipe(pipe, out, ppos, len, flags, pipe_to_null);
}

static ssize_t read_zero(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	size_t written;

	if (!count)
		return 0;

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	written = 0;
	while (count) {
		unsigned long unwritten;
		size_t chunk = count;

		if (chunk > PAGE_SIZE)
			chunk = PAGE_SIZE;	/* Just for latency reasons */
		unwritten = __clear_user(buf, chunk);
		written += chunk - unwritten;
		if (unwritten)
			break;
		if (signal_pending(current))
			return written ? written : -ERESTARTSYS;
		buf += chunk;
		count -= chunk;
		cond_resched();
	}
	return written ? written : -EFAULT;
}

static int mmap_zero(struct file *file, struct vm_area_struct *vma)
{
#ifndef CONFIG_MMU
	return -ENOSYS;
#endif
	if (vma->vm_flags & VM_SHARED)
		return shmem_zero_setup(vma);
	return 0;
}

static ssize_t write_full(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	return -ENOSPC;
}

/*
 * Special lseek() function for /dev/null and /dev/zero.  Most notably, you
 * can fopen() both devices with "a" now.  This was previously impossible.
 * -- SRB.
 */
static loff_t null_lseek(struct file *file, loff_t offset, int orig)
{
	return file->f_pos = 0;
}

/*
 * The memory devices use the full 32/64 bits of the offset, and so we cannot
 * check against negative addresses: they are ok. The return value is weird,
 * though, in that case (0).
 *
 * also note that seeking relative to the "end of file" isn't supported:
 * it has no meaning, so it returns -EINVAL.
 */
static loff_t memory_lseek(struct file *file, loff_t offset, int orig)
{
	loff_t ret;

	mutex_lock(&file->f_path.dentry->d_inode->i_mutex);
	switch (orig) {
	case SEEK_CUR:
		offset += file->f_pos;
		if ((unsigned long long)offset <
		    (unsigned long long)file->f_pos) {
			ret = -EOVERFLOW;
			break;
		}
	case SEEK_SET:
		/* to avoid userland mistaking f_pos=-9 as -EBADF=-9 */
		if ((unsigned long long)offset >= ~0xFFFULL) {
			ret = -EOVERFLOW;
			break;
		}
		file->f_pos = offset;
		ret = file->f_pos;
		force_successful_syscall_return();
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&file->f_path.dentry->d_inode->i_mutex);
	return ret;
}

static int open_port(struct inode * inode, struct file * filp)
{
	return capable(CAP_SYS_RAWIO) ? 0 : -EPERM;
}

#define zero_lseek	null_lseek
#define full_lseek      null_lseek
#define write_zero	write_null
#define read_full       read_zero
#define open_mem	open_port
#define open_kmem	open_mem
#define open_oldmem	open_mem

static const struct file_operations mem_fops = {
	.llseek		= memory_lseek,
	.read		= read_mem,
	.write		= write_mem,
	.mmap		= mmap_mem,
	.open		= open_mem,
	.get_unmapped_area = get_unmapped_area_mem,
};

#ifdef CONFIG_DEVKMEM
static const struct file_operations kmem_fops = {
	.llseek		= memory_lseek,
	.read		= read_kmem,
	.write		= write_kmem,
	.mmap		= mmap_kmem,
	.open		= open_kmem,
	.get_unmapped_area = get_unmapped_area_mem,
};
#endif

static const struct file_operations null_fops = {
	.llseek		= null_lseek,
	.read		= read_null,
	.write		= write_null,
	.splice_write	= splice_write_null,
};

#ifdef CONFIG_DEVPORT
static const struct file_operations port_fops = {
	.llseek		= memory_lseek,
	.read		= read_port,
	.write		= write_port,
	.open		= open_port,
};
#endif

static const struct file_operations zero_fops = {
	.llseek		= zero_lseek,
	.read		= read_zero,
	.write		= write_zero,
	.mmap		= mmap_zero,
};

/*
 * capabilities for /dev/zero
 * - permits private mappings, "copies" are taken of the source of zeros
 */
static struct backing_dev_info zero_bdi = {
	.name		= "char/mem",
	.capabilities	= BDI_CAP_MAP_COPY,
};

static const struct file_operations full_fops = {
	.llseek		= full_lseek,
	.read		= read_full,
	.write		= write_full,
};

#ifdef CONFIG_CRASH_DUMP
static const struct file_operations oldmem_fops = {
	.read	= read_oldmem,
	.open	= open_oldmem,
};
#endif

static ssize_t kmsg_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	char *tmp;
	ssize_t ret;

	tmp = kmalloc(count + 1, GFP_KERNEL);
	if (tmp == NULL)
		return -ENOMEM;
	ret = -EFAULT;
	if (!copy_from_user(tmp, buf, count)) {
		tmp[count] = 0;
		ret = printk("%s", tmp);
		if (ret > count)
			/* printk can add a prefix */
			ret = count;
	}
	kfree(tmp);
	return ret;
}

static const struct file_operations kmsg_fops = {
	.write = kmsg_write,
};

static const struct memdev {
	const char *name;
	mode_t mode;
	const struct file_operations *fops;
	struct backing_dev_info *dev_info;
} devlist[] = {
	 [1] = { "mem", 0, &mem_fops, &directly_mappable_cdev_bdi },
#ifdef CONFIG_DEVKMEM
	 [2] = { "kmem", 0, &kmem_fops, &directly_mappable_cdev_bdi },
#endif
	 [3] = { "null", 0666, &null_fops, NULL },
#ifdef CONFIG_DEVPORT
	 [4] = { "port", 0, &port_fops, NULL },
#endif
	 [5] = { "zero", 0666, &zero_fops, &zero_bdi },
	 [7] = { "full", 0666, &full_fops, NULL },
	 [8] = { "random", 0666, &random_fops, NULL },
	 [9] = { "urandom", 0666, &urandom_fops, NULL },
	[11] = { "kmsg", 0, &kmsg_fops, NULL },
#ifdef CONFIG_CRASH_DUMP
	[12] = { "oldmem", 0, &oldmem_fops, NULL },
#endif
};

static int memory_open(struct inode *inode, struct file *filp)
{
	int minor;
	const struct memdev *dev;

	minor = iminor(inode);
	if (minor >= ARRAY_SIZE(devlist))
		return -ENXIO;

	dev = &devlist[minor];
	if (!dev->fops)
		return -ENXIO;

	filp->f_op = dev->fops;
	if (dev->dev_info)
		filp->f_mapping->backing_dev_info = dev->dev_info;

	if (dev->fops->open)
		return dev->fops->open(inode, filp);

	return 0;
}

static const struct file_operations memory_fops = {
	.open = memory_open,
};

static char *mem_devnode(struct device *dev, mode_t *mode)
{
	if (mode && devlist[MINOR(dev->devt)].mode)
		*mode = devlist[MINOR(dev->devt)].mode;
	return NULL;
}

static struct class *mem_class;

static int __init chr_dev_init(void)
{
	int minor;
	int err;

	err = bdi_init(&zero_bdi);
	if (err)
		return err;

	if (register_chrdev(MEM_MAJOR, "mem", &memory_fops))
		printk("unable to get major %d for memory devs\n", MEM_MAJOR);

	mem_class = class_create(THIS_MODULE, "mem");
	mem_class->devnode = mem_devnode;
	for (minor = 1; minor < ARRAY_SIZE(devlist); minor++) {
		if (!devlist[minor].name)
			continue;
		device_create(mem_class, NULL, MKDEV(MEM_MAJOR, minor),
			      NULL, devlist[minor].name);
	}

	return 0;
}

fs_initcall(chr_dev_init);
