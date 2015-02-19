/**
 * imr.c
 *
 * Copyright(c) 2013 Intel Corporation.
 * Copyright(c) 2015 Bryan O'Donoghue <pure.logic@nexus-software.ie>
 *
 * IMR registers define an isolated region of memory that can
 * be masked to prohibit certain system agents from accessing memory.
 * When a device behind a masked port performs an access - snooped or
 * not, an IMR may optionally prevent that transaction from changing
 * the state of memory or from getting correct data in response to the
 * operation.
 *
 * Write data will be dropped and reads will return 0xFFFFFFFF, the
 * system will reset and system BIOS will print out an error message to
 * inform the user that an IMR has been violated.
 *
 * This code is based on the Linux MTRR code and reference code from
 * Intel's Quark BSP EFI, Linux and grub code.
 *
 * See quark-x1000-datasheet.pdf for register definitions.
 * http://www.intel.com/content/dam/www/public/us/en/documents/datasheets/quark-x1000-datasheet.pdf
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <asm-generic/sections.h>
#include <asm/cpu_device_id.h>
#include <asm/imr.h>
#include <asm/iosf_mbi.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/types.h>

struct imr_device {
	struct dentry	*file;
	bool		init;
	struct mutex	lock;
	int		max_imr;
	int		reg_base;
};

static struct imr_device imr_dev;

/*
 * IMR read/write mask control registers.
 * See quark-x1000-datasheet.pdf sections 12.7.4.5 and 12.7.4.6 for
 * bit definitions.
 *
 * addr_hi
 * 31		Lock bit
 * 30:24	Reserved
 * 23:2		1 KiB aligned lo address
 * 1:0		Reserved
 *
 * addr_hi
 * 31:24	Reserved
 * 23:2		1 KiB aligned hi address
 * 1:0		Reserved
 */
#define IMR_LOCK	BIT(31)

struct imr_regs {
	u32 addr_lo;
	u32 addr_hi;
	u32 rmask;
	u32 wmask;
};

#define IMR_NUM_REGS	(sizeof(struct imr_regs)/sizeof(u32))
#define IMR_SHIFT	8
#define imr_to_phys(x)	((x) << IMR_SHIFT)
#define phys_to_imr(x)	((x) >> IMR_SHIFT)

/**
 * imr_is_enabled - true if an IMR is enabled false otherwise.
 *
 * Determines if an IMR is enabled based on address range and read/write
 * mask. An IMR set with an address range set to zero and a read/write
 * access mask set to all is considered to be disabled. An IMR in any
 * other state - for example set to zero but without read/write access
 * all is considered to be enabled. This definition of disabled is how
 * firmware switches off an IMR and is maintained in kernel for
 * consistency.
 *
 * @imr:	pointer to IMR descriptor.
 * @return:	true if IMR enabled false if disabled.
 */
static inline int imr_is_enabled(struct imr_regs *imr)
{
	return !(imr->rmask == IMR_READ_ACCESS_ALL &&
		 imr->wmask == IMR_WRITE_ACCESS_ALL &&
		 imr_to_phys(imr->addr_lo) == 0 &&
		 imr_to_phys(imr->addr_hi) == 0);
}

/**
 * imr_read - read an IMR at a given index.
 *
 * Requires caller to hold imr mutex.
 *
 * @idev:	pointer to imr_device structure.
 * @imr_id:	IMR entry to read.
 * @imr:	IMR structure representing address and access masks.
 * @return:	0 on success or error code passed from mbi_iosf on failure.
 */
static int imr_read(struct imr_device *idev, u32 imr_id, struct imr_regs *imr)
{
	u32 reg = imr_id * IMR_NUM_REGS + idev->reg_base;
	int ret;

	ret = iosf_mbi_read(QRK_MBI_UNIT_MM, QRK_MBI_MM_READ,
				reg++, &imr->addr_lo);
	if (ret)
		return ret;

	ret = iosf_mbi_read(QRK_MBI_UNIT_MM, QRK_MBI_MM_READ,
				reg++, &imr->addr_hi);
	if (ret)
		return ret;

	ret = iosf_mbi_read(QRK_MBI_UNIT_MM, QRK_MBI_MM_READ,
				reg++, &imr->rmask);
	if (ret)
		return ret;

	ret = iosf_mbi_read(QRK_MBI_UNIT_MM, QRK_MBI_MM_READ,
				reg++, &imr->wmask);
	if (ret)
		return ret;

	return 0;
}

/**
 * imr_write - write an IMR at a given index.
 *
 * Requires caller to hold imr mutex.
 * Note lock bits need to be written independently of address bits.
 *
 * @idev:	pointer to imr_device structure.
 * @imr_id:	IMR entry to write.
 * @imr:	IMR structure representing address and access masks.
 * @lock:	indicates if the IMR lock bit should be applied.
 * @return:	0 on success or error code passed from mbi_iosf on failure.
 */
static int imr_write(struct imr_device *idev, u32 imr_id,
		     struct imr_regs *imr, bool lock)
{
	unsigned long flags;
	u32 reg = imr_id * IMR_NUM_REGS + idev->reg_base;
	int ret;

	local_irq_save(flags);

	ret = iosf_mbi_write(QRK_MBI_UNIT_MM, QRK_MBI_MM_WRITE, reg++,
				imr->addr_lo);
	if (ret)
		goto failed;

	ret = iosf_mbi_write(QRK_MBI_UNIT_MM, QRK_MBI_MM_WRITE,
				reg++, imr->addr_hi);
	if (ret)
		goto failed;

	ret = iosf_mbi_write(QRK_MBI_UNIT_MM, QRK_MBI_MM_WRITE,
				reg++, imr->rmask);
	if (ret)
		goto failed;

	ret = iosf_mbi_write(QRK_MBI_UNIT_MM, QRK_MBI_MM_WRITE,
				reg++, imr->wmask);
	if (ret)
		goto failed;

	/* Lock bit must be set separately to addr_lo address bits. */
	if (lock) {
		imr->addr_lo |= IMR_LOCK;
		ret = iosf_mbi_write(QRK_MBI_UNIT_MM, QRK_MBI_MM_WRITE,
					reg - IMR_NUM_REGS, imr->addr_lo);
		if (ret)
			goto failed;
	}

	local_irq_restore(flags);
	return 0;
failed:
	/*
	 * If writing to the IOSF failed then we're in an unknown state,
	 * likely a very bad state. An IMR in an invalid state will almost
	 * certainly lead to a memory access violation.
	 */
	local_irq_restore(flags);
	WARN(ret, "IOSF-MBI write fail range 0x%08x-0x%08x unreliable\n",
	     imr_to_phys(imr->addr_lo), imr_to_phys(imr->addr_hi) + IMR_MASK);

	return ret;
}

/**
 * imr_dbgfs_state_show - print state of IMR registers.
 *
 * @s:		pointer to seq_file for output.
 * @unused:	unused parameter.
 * @return:	0 on success or error code passed from mbi_iosf on failure.
 */
static int imr_dbgfs_state_show(struct seq_file *s, void *unused)
{
	phys_addr_t base;
	phys_addr_t end;
	int i;
	struct imr_device *idev = s->private;
	struct imr_regs imr;
	size_t size;
	int ret = -ENODEV;

	mutex_lock(&idev->lock);

	for (i = 0; i < idev->max_imr; i++) {

		ret = imr_read(idev, i, &imr);
		if (ret)
			break;

		/*
		 * Remember to add IMR_ALIGN bytes to size to indicate the
		 * inherent IMR_ALIGN size bytes contained in the masked away
		 * lower ten bits.
		 */
		if (imr_is_enabled(&imr)) {
			base = imr_to_phys(imr.addr_lo);
			end = imr_to_phys(imr.addr_hi) + IMR_MASK;
		} else {
			base = 0;
			end = 0;
		}
		size = end - base;
		seq_printf(s, "imr%02i: base=%pa, end=%pa, size=0x%08zx "
			   "rmask=0x%08x, wmask=0x%08x, %s, %s\n", i,
			   &base, &end, size, imr.rmask, imr.wmask,
			   imr_is_enabled(&imr) ? "enabled " : "disabled",
			   imr.addr_lo & IMR_LOCK ? "locked" : "unlocked");
	}

	mutex_unlock(&idev->lock);
	return ret;
}

/**
 * imr_state_open - debugfs open callback.
 *
 * @inode:	pointer to struct inode.
 * @file:	pointer to struct file.
 * @return:	result of single open.
 */
static int imr_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, imr_dbgfs_state_show, inode->i_private);
}

static const struct file_operations imr_state_ops = {
	.open		= imr_state_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/**
 * imr_debugfs_register - register debugfs hooks.
 *
 * @idev:	pointer to imr_device structure.
 * @return:	0 on success - errno on failure.
 */
static int imr_debugfs_register(struct imr_device *idev)
{
	idev->file = debugfs_create_file("imr_state", S_IFREG | S_IRUGO, NULL,
					 idev, &imr_state_ops);
	return PTR_ERR_OR_ZERO(idev->file);
}

/**
 * imr_debugfs_unregister - unregister debugfs hooks.
 *
 * @idev:	pointer to imr_device structure.
 * @return:
 */
static void imr_debugfs_unregister(struct imr_device *idev)
{
	debugfs_remove(idev->file);
}

/**
 * imr_check_params - check passed address range IMR alignment and non-zero size
 *
 * @base:	base address of intended IMR.
 * @size:	size of intended IMR.
 * @return:	zero on valid range -EINVAL on unaligned base/size.
 */
static int imr_check_params(phys_addr_t base, size_t size)
{
	if ((base & IMR_MASK) || (size & IMR_MASK)) {
		pr_err("base %pa size 0x%08zx must align to 1KiB\n",
			&base, size);
		return -EINVAL;
	}
	if (size == 0)
		return -EINVAL;

	return 0;
}

/**
 * imr_raw_size - account for the IMR_ALIGN bytes that addr_hi appends.
 *
 * IMR addr_hi has a built in offset of plus IMR_ALIGN (0x400) bytes from the
 * value in the register. We need to subtract IMR_ALIGN bytes from input sizes
 * as a result.
 *
 * @size:	input size bytes.
 * @return:	reduced size.
 */
static inline size_t imr_raw_size(size_t size)
{
	return size - IMR_ALIGN;
}

/**
 * imr_address_overlap - detects an address overlap.
 *
 * @addr:	address to check against an existing IMR.
 * @imr:	imr being checked.
 * @return:	true for overlap false for no overlap.
 */
static inline int imr_address_overlap(phys_addr_t addr, struct imr_regs *imr)
{
	return addr >= imr_to_phys(imr->addr_lo) && addr <= imr_to_phys(imr->addr_hi);
}

/**
 * imr_add_range - add an Isolated Memory Region.
 *
 * @base:	physical base address of region aligned to 1KiB.
 * @size:	physical size of region in bytes must be aligned to 1KiB.
 * @read_mask:	read access mask.
 * @write_mask:	write access mask.
 * @lock:	indicates whether or not to permanently lock this region.
 * @return:	zero on success or negative value indicating error.
 */
int imr_add_range(phys_addr_t base, size_t size,
		  unsigned int rmask, unsigned int wmask, bool lock)
{
	phys_addr_t end;
	unsigned int i;
	struct imr_device *idev = &imr_dev;
	struct imr_regs imr;
	size_t raw_size;
	int reg;
	int ret;

	if (WARN_ONCE(idev->init == false, "driver not initialized"))
		return -ENODEV;

	ret = imr_check_params(base, size);
	if (ret)
		return ret;

	/* Tweak the size value. */
	raw_size = imr_raw_size(size);
	end = base + raw_size;

	/*
	 * Check for reserved IMR value common to firmware, kernel and grub
	 * indicating a disabled IMR.
	 */
	imr.addr_lo = phys_to_imr(base);
	imr.addr_hi = phys_to_imr(end);
	imr.rmask = rmask;
	imr.wmask = wmask;
	if (!imr_is_enabled(&imr))
		return -ENOTSUPP;

	mutex_lock(&idev->lock);

	/*
	 * Find a free IMR while checking for an existing overlapping range.
	 * Note there's no restriction in silicon to prevent IMR overlaps.
	 * For the sake of simplicity and ease in defining/debugging an IMR
	 * memory map we exclude IMR overlaps.
	 */
	reg = -1;
	for (i = 0; i < idev->max_imr; i++) {
		ret = imr_read(idev, i, &imr);
		if (ret)
			goto failed;

		/* Find overlap @ base or end of requested range. */
		ret = -EINVAL;
		if (imr_is_enabled(&imr)) {
			if (imr_address_overlap(base, &imr))
				goto failed;
			if (imr_address_overlap(end, &imr))
				goto failed;
		} else {
			reg = i;
		}
	}

	/* Error out if we have no free IMR entries. */
	if (reg == -1) {
		ret = -ENOMEM;
		goto failed;
	}

	pr_debug("add %d phys %pa-%pa size %zx mask 0x%08x wmask 0x%08x\n",
		 reg, &base, &end, raw_size, rmask, wmask);

	/* Enable IMR at specified range and access mask. */
	imr.addr_lo = phys_to_imr(base);
	imr.addr_hi = phys_to_imr(end);
	imr.rmask = rmask;
	imr.wmask = wmask;

	ret = imr_write(idev, reg, &imr, lock);
	if (ret < 0) {
		/*
		 * In the highly unlikely event iosf_mbi_write failed
		 * attempt to rollback the IMR setup skipping the trapping
		 * of further IOSF write failures.
		 */
		imr.addr_lo = 0;
		imr.addr_hi = 0;
		imr.rmask = IMR_READ_ACCESS_ALL;
		imr.wmask = IMR_WRITE_ACCESS_ALL;
		imr_write(idev, reg, &imr, false);
	}
failed:
	mutex_unlock(&idev->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(imr_add_range);

/**
 * __imr_remove_range - delete an Isolated Memory Region.
 *
 * This function allows you to delete an IMR by its index specified by reg or
 * by address range specified by base and size respectively. If you specify an
 * index on its own the base and size parameters are ignored.
 * imr_remove_range(0, base, size); delete IMR at index 0 base/size ignored.
 * imr_remove_range(-1, base, size); delete IMR from base to base+size.
 *
 * @reg:	imr index to remove.
 * @base:	physical base address of region aligned to 1 KiB.
 * @size:	physical size of region in bytes aligned to 1 KiB.
 * @return:	-EINVAL on invalid range or out or range id
 *		-ENODEV if reg is valid but no IMR exists or is locked
 *		0 on success.
 */
static int __imr_remove_range(int reg, phys_addr_t base, size_t size)
{
	phys_addr_t end;
	bool found = false;
	unsigned int i;
	struct imr_device *idev = &imr_dev;
	struct imr_regs imr;
	size_t raw_size;
	int ret = 0;

	if (WARN_ONCE(idev->init == false, "driver not initialized"))
		return -ENODEV;

	/*
	 * Validate address range if deleting by address, else we are
	 * deleting by index where base and size will be ignored.
	 */
	if (reg == -1) {
		ret = imr_check_params(base, size);
		if (ret)
			return ret;
	}

	/* Tweak the size value. */
	raw_size = imr_raw_size(size);
	end = base + raw_size;

	mutex_lock(&idev->lock);

	if (reg >= 0) {
		/* If a specific IMR is given try to use it. */
		ret = imr_read(idev, reg, &imr);
		if (ret)
			goto failed;

		if (!imr_is_enabled(&imr) || imr.addr_lo & IMR_LOCK) {
			ret = -ENODEV;
			goto failed;
		}
		found = true;
	} else {
		/* Search for match based on address range. */
		for (i = 0; i < idev->max_imr; i++) {
			ret = imr_read(idev, i, &imr);
			if (ret)
				goto failed;

			if (!imr_is_enabled(&imr) || imr.addr_lo & IMR_LOCK)
				continue;

			if ((imr_to_phys(imr.addr_lo) == base) &&
			    (imr_to_phys(imr.addr_hi) == end)) {
				found = true;
				reg = i;
				break;
			}
		}
	}

	if (!found) {
		ret = -ENODEV;
		goto failed;
	}

	pr_debug("remove %d phys %pa-%pa size %zx\n", reg, &base, &end, raw_size);

	/* Tear down the IMR. */
	imr.addr_lo = 0;
	imr.addr_hi = 0;
	imr.rmask = IMR_READ_ACCESS_ALL;
	imr.wmask = IMR_WRITE_ACCESS_ALL;

	ret = imr_write(idev, reg, &imr, false);

failed:
	mutex_unlock(&idev->lock);
	return ret;
}

/**
 * imr_remove_range - delete an Isolated Memory Region by address
 *
 * This function allows you to delete an IMR by an address range specified
 * by base and size respectively.
 * imr_remove_range(base, size); delete IMR from base to base+size.
 *
 * @base:	physical base address of region aligned to 1 KiB.
 * @size:	physical size of region in bytes aligned to 1 KiB.
 * @return:	-EINVAL on invalid range or out or range id
 *		-ENODEV if reg is valid but no IMR exists or is locked
 *		0 on success.
 */
int imr_remove_range(phys_addr_t base, size_t size)
{
	return __imr_remove_range(-1, base, size);
}
EXPORT_SYMBOL_GPL(imr_remove_range);

/**
 * imr_clear - delete an Isolated Memory Region by index
 *
 * This function allows you to delete an IMR by an address range specified
 * by the index of the IMR. Useful for initial sanitization of the IMR
 * address map.
 * imr_ge(base, size); delete IMR from base to base+size.
 *
 * @reg:	imr index to remove.
 * @return:	-EINVAL on invalid range or out or range id
 *		-ENODEV if reg is valid but no IMR exists or is locked
 *		0 on success.
 */
static inline int imr_clear(int reg)
{
	return __imr_remove_range(reg, 0, 0);
}

/**
 * imr_fixup_memmap - Tear down IMRs used during bootup.
 *
 * BIOS and Grub both setup IMRs around compressed kernel, initrd memory
 * that need to be removed before the kernel hands out one of the IMR
 * encased addresses to a downstream DMA agent such as the SD or Ethernet.
 * IMRs on Galileo are setup to immediately reset the system on violation.
 * As a result if you're running a root filesystem from SD - you'll need
 * the boot-time IMRs torn down or you'll find seemingly random resets when
 * using your filesystem.
 *
 * @idev:	pointer to imr_device structure.
 * @return:
 */
static void __init imr_fixup_memmap(struct imr_device *idev)
{
	phys_addr_t base = virt_to_phys(&_text);
	size_t size = virt_to_phys(&__end_rodata) - base;
	int i;
	int ret;

	/* Tear down all existing unlocked IMRs. */
	for (i = 0; i < idev->max_imr; i++)
		imr_clear(i);

	/*
	 * Setup a locked IMR around the physical extent of the kernel
	 * from the beginning of the .text secton to the end of the
	 * .rodata section as one physically contiguous block.
	 */
	ret = imr_add_range(base, size, IMR_CPU, IMR_CPU, true);
	if (ret < 0) {
		pr_err("unable to setup IMR for kernel: (%p - %p)\n",
			&_text, &__end_rodata);
	} else {
		pr_info("protecting kernel .text - .rodata: %zu KiB (%p - %p)\n",
			size / 1024, &_text, &__end_rodata);
	}

}

static const struct x86_cpu_id imr_ids[] __initconst = {
	{ X86_VENDOR_INTEL, 5, 9 },	/* Intel Quark SoC X1000. */
	{}
};
MODULE_DEVICE_TABLE(x86cpu, imr_ids);

/**
 * imr_init - entry point for IMR driver.
 *
 * return: -ENODEV for no IMR support 0 if good to go.
 */
static int __init imr_init(void)
{
	struct imr_device *idev = &imr_dev;
	int ret;

	if (!x86_match_cpu(imr_ids) || !iosf_mbi_available())
		return -ENODEV;

	idev->max_imr = QUARK_X1000_IMR_MAX;
	idev->reg_base = QUARK_X1000_IMR_REGBASE;
	idev->init = true;

	mutex_init(&idev->lock);
	ret = imr_debugfs_register(idev);
	if (ret != 0)
		pr_warn("debugfs register failed!\n");
	imr_fixup_memmap(idev);
	return 0;
}

/**
 * imr_exit - exit point for IMR code.
 *
 * Deregisters debugfs, leave IMR state as-is.
 *
 * return:
 */
static void __exit imr_exit(void)
{
	imr_debugfs_unregister(&imr_dev);
}

module_init(imr_init);
module_exit(imr_exit);

MODULE_AUTHOR("Bryan O'Donoghue <pure.logic@nexus-software.ie>");
MODULE_DESCRIPTION("Intel Isolated Memory Region driver");
MODULE_LICENSE("Dual BSD/GPL");
