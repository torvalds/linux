/*
 * Copyright (c) 2006, 2007 QLogic Corporation. All rights reserved.
 * Copyright (c) 2003, 2004, 2005, 2006 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * This file contains support for diagnostic functions.  It is accessed by
 * opening the ipath_diag device, normally minor number 129.  Diagnostic use
 * of the InfiniPath chip may render the chip or board unusable until the
 * driver is unloaded, or in some cases, until the system is rebooted.
 *
 * Accesses to the chip through this interface are not similar to going
 * through the /sys/bus/pci resource mmap interface.
 */

#include <linux/io.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>

#include "ipath_kernel.h"
#include "ipath_common.h"

int ipath_diag_inuse;
static int diag_set_link;

static int ipath_diag_open(struct inode *in, struct file *fp);
static int ipath_diag_release(struct inode *in, struct file *fp);
static ssize_t ipath_diag_read(struct file *fp, char __user *data,
			       size_t count, loff_t *off);
static ssize_t ipath_diag_write(struct file *fp, const char __user *data,
				size_t count, loff_t *off);

static const struct file_operations diag_file_ops = {
	.owner = THIS_MODULE,
	.write = ipath_diag_write,
	.read = ipath_diag_read,
	.open = ipath_diag_open,
	.release = ipath_diag_release
};

static ssize_t ipath_diagpkt_write(struct file *fp,
				   const char __user *data,
				   size_t count, loff_t *off);

static const struct file_operations diagpkt_file_ops = {
	.owner = THIS_MODULE,
	.write = ipath_diagpkt_write,
};

static atomic_t diagpkt_count = ATOMIC_INIT(0);
static struct cdev *diagpkt_cdev;
static struct class_device *diagpkt_class_dev;

int ipath_diag_add(struct ipath_devdata *dd)
{
	char name[16];
	int ret = 0;

	if (atomic_inc_return(&diagpkt_count) == 1) {
		ret = ipath_cdev_init(IPATH_DIAGPKT_MINOR,
				      "ipath_diagpkt", &diagpkt_file_ops,
				      &diagpkt_cdev, &diagpkt_class_dev);

		if (ret) {
			ipath_dev_err(dd, "Couldn't create ipath_diagpkt "
				      "device: %d", ret);
			goto done;
		}
	}

	snprintf(name, sizeof(name), "ipath_diag%d", dd->ipath_unit);

	ret = ipath_cdev_init(IPATH_DIAG_MINOR_BASE + dd->ipath_unit, name,
			      &diag_file_ops, &dd->diag_cdev,
			      &dd->diag_class_dev);
	if (ret)
		ipath_dev_err(dd, "Couldn't create %s device: %d",
			      name, ret);

done:
	return ret;
}

void ipath_diag_remove(struct ipath_devdata *dd)
{
	if (atomic_dec_and_test(&diagpkt_count))
		ipath_cdev_cleanup(&diagpkt_cdev, &diagpkt_class_dev);

	ipath_cdev_cleanup(&dd->diag_cdev, &dd->diag_class_dev);
}

/**
 * ipath_read_umem64 - read a 64-bit quantity from the chip into user space
 * @dd: the infinipath device
 * @uaddr: the location to store the data in user memory
 * @caddr: the source chip address (full pointer, not offset)
 * @count: number of bytes to copy (multiple of 32 bits)
 *
 * This function also localizes all chip memory accesses.
 * The copy should be written such that we read full cacheline packets
 * from the chip.  This is usually used for a single qword
 *
 * NOTE:  This assumes the chip address is 64-bit aligned.
 */
static int ipath_read_umem64(struct ipath_devdata *dd, void __user *uaddr,
			     const void __iomem *caddr, size_t count)
{
	const u64 __iomem *reg_addr = caddr;
	const u64 __iomem *reg_end = reg_addr + (count / sizeof(u64));
	int ret;

	/* not very efficient, but it works for now */
	if (reg_addr < dd->ipath_kregbase || reg_end > dd->ipath_kregend) {
		ret = -EINVAL;
		goto bail;
	}
	while (reg_addr < reg_end) {
		u64 data = readq(reg_addr);
		if (copy_to_user(uaddr, &data, sizeof(u64))) {
			ret = -EFAULT;
			goto bail;
		}
		reg_addr++;
		uaddr += sizeof(u64);
	}
	ret = 0;
bail:
	return ret;
}

/**
 * ipath_write_umem64 - write a 64-bit quantity to the chip from user space
 * @dd: the infinipath device
 * @caddr: the destination chip address (full pointer, not offset)
 * @uaddr: the source of the data in user memory
 * @count: the number of bytes to copy (multiple of 32 bits)
 *
 * This is usually used for a single qword
 * NOTE:  This assumes the chip address is 64-bit aligned.
 */

static int ipath_write_umem64(struct ipath_devdata *dd, void __iomem *caddr,
			      const void __user *uaddr, size_t count)
{
	u64 __iomem *reg_addr = caddr;
	const u64 __iomem *reg_end = reg_addr + (count / sizeof(u64));
	int ret;

	/* not very efficient, but it works for now */
	if (reg_addr < dd->ipath_kregbase || reg_end > dd->ipath_kregend) {
		ret = -EINVAL;
		goto bail;
	}
	while (reg_addr < reg_end) {
		u64 data;
		if (copy_from_user(&data, uaddr, sizeof(data))) {
			ret = -EFAULT;
			goto bail;
		}
		writeq(data, reg_addr);

		reg_addr++;
		uaddr += sizeof(u64);
	}
	ret = 0;
bail:
	return ret;
}

/**
 * ipath_read_umem32 - read a 32-bit quantity from the chip into user space
 * @dd: the infinipath device
 * @uaddr: the location to store the data in user memory
 * @caddr: the source chip address (full pointer, not offset)
 * @count: number of bytes to copy
 *
 * read 32 bit values, not 64 bit; for memories that only
 * support 32 bit reads; usually a single dword.
 */
static int ipath_read_umem32(struct ipath_devdata *dd, void __user *uaddr,
			     const void __iomem *caddr, size_t count)
{
	const u32 __iomem *reg_addr = caddr;
	const u32 __iomem *reg_end = reg_addr + (count / sizeof(u32));
	int ret;

	if (reg_addr < (u32 __iomem *) dd->ipath_kregbase ||
	    reg_end > (u32 __iomem *) dd->ipath_kregend) {
		ret = -EINVAL;
		goto bail;
	}
	/* not very efficient, but it works for now */
	while (reg_addr < reg_end) {
		u32 data = readl(reg_addr);
		if (copy_to_user(uaddr, &data, sizeof(data))) {
			ret = -EFAULT;
			goto bail;
		}

		reg_addr++;
		uaddr += sizeof(u32);

	}
	ret = 0;
bail:
	return ret;
}

/**
 * ipath_write_umem32 - write a 32-bit quantity to the chip from user space
 * @dd: the infinipath device
 * @caddr: the destination chip address (full pointer, not offset)
 * @uaddr: the source of the data in user memory
 * @count: number of bytes to copy
 *
 * write 32 bit values, not 64 bit; for memories that only
 * support 32 bit write; usually a single dword.
 */

static int ipath_write_umem32(struct ipath_devdata *dd, void __iomem *caddr,
			      const void __user *uaddr, size_t count)
{
	u32 __iomem *reg_addr = caddr;
	const u32 __iomem *reg_end = reg_addr + (count / sizeof(u32));
	int ret;

	if (reg_addr < (u32 __iomem *) dd->ipath_kregbase ||
	    reg_end > (u32 __iomem *) dd->ipath_kregend) {
		ret = -EINVAL;
		goto bail;
	}
	while (reg_addr < reg_end) {
		u32 data;
		if (copy_from_user(&data, uaddr, sizeof(data))) {
			ret = -EFAULT;
			goto bail;
		}
		writel(data, reg_addr);

		reg_addr++;
		uaddr += sizeof(u32);
	}
	ret = 0;
bail:
	return ret;
}

static int ipath_diag_open(struct inode *in, struct file *fp)
{
	int unit = iminor(in) - IPATH_DIAG_MINOR_BASE;
	struct ipath_devdata *dd;
	int ret;

	mutex_lock(&ipath_mutex);

	if (ipath_diag_inuse) {
		ret = -EBUSY;
		goto bail;
	}

	dd = ipath_lookup(unit);

	if (dd == NULL || !(dd->ipath_flags & IPATH_PRESENT) ||
	    !dd->ipath_kregbase) {
		ret = -ENODEV;
		goto bail;
	}

	fp->private_data = dd;
	ipath_diag_inuse = -2;
	diag_set_link = 0;
	ret = 0;

	/* Only expose a way to reset the device if we
	   make it into diag mode. */
	ipath_expose_reset(&dd->pcidev->dev);

bail:
	mutex_unlock(&ipath_mutex);

	return ret;
}

/**
 * ipath_diagpkt_write - write an IB packet
 * @fp: the diag data device file pointer
 * @data: ipath_diag_pkt structure saying where to get the packet
 * @count: size of data to write
 * @off: unused by this code
 */
static ssize_t ipath_diagpkt_write(struct file *fp,
				   const char __user *data,
				   size_t count, loff_t *off)
{
	u32 __iomem *piobuf;
	u32 plen, clen, pbufn;
	struct ipath_diag_pkt odp;
	struct ipath_diag_xpkt dp;
	u32 *tmpbuf = NULL;
	struct ipath_devdata *dd;
	ssize_t ret = 0;
	u64 val;

	if (count != sizeof(dp)) {
		ret = -EINVAL;
		goto bail;
	}

	if (copy_from_user(&dp, data, sizeof(dp))) {
		ret = -EFAULT;
		goto bail;
	}

	/*
	 * Due to padding/alignment issues (lessened with new struct)
	 * the old and new structs are the same length. We need to
	 * disambiguate them, which we can do because odp.len has never
	 * been less than the total of LRH+BTH+DETH so far, while
	 * dp.unit (same offset) unit is unlikely to get that high.
	 * Similarly, dp.data, the pointer to user at the same offset
	 * as odp.unit, is almost certainly at least one (512byte)page
	 * "above" NULL. The if-block below can be omitted if compatibility
	 * between a new driver and older diagnostic code is unimportant.
	 * compatibility the other direction (new diags, old driver) is
	 * handled in the diagnostic code, with a warning.
	 */
	if (dp.unit >= 20 && dp.data < 512) {
		/* very probable version mismatch. Fix it up */
		memcpy(&odp, &dp, sizeof(odp));
		/* We got a legacy dp, copy elements to dp */
		dp.unit = odp.unit;
		dp.data = odp.data;
		dp.len = odp.len;
		dp.pbc_wd = 0; /* Indicate we need to compute PBC wd */
	}

	/* send count must be an exact number of dwords */
	if (dp.len & 3) {
		ret = -EINVAL;
		goto bail;
	}

	clen = dp.len >> 2;

	dd = ipath_lookup(dp.unit);
	if (!dd || !(dd->ipath_flags & IPATH_PRESENT) ||
	    !dd->ipath_kregbase) {
		ipath_cdbg(VERBOSE, "illegal unit %u for diag data send\n",
			   dp.unit);
		ret = -ENODEV;
		goto bail;
	}

	if (ipath_diag_inuse && !diag_set_link &&
	    !(dd->ipath_flags & IPATH_LINKACTIVE)) {
		diag_set_link = 1;
		ipath_cdbg(VERBOSE, "Trying to set to set link active for "
			   "diag pkt\n");
		ipath_set_linkstate(dd, IPATH_IB_LINKARM);
		ipath_set_linkstate(dd, IPATH_IB_LINKACTIVE);
	}

	if (!(dd->ipath_flags & IPATH_INITTED)) {
		/* no hardware, freeze, etc. */
		ipath_cdbg(VERBOSE, "unit %u not usable\n", dd->ipath_unit);
		ret = -ENODEV;
		goto bail;
	}
	/* Check link state, but not if we have custom PBC */
	val = dd->ipath_lastibcstat & IPATH_IBSTATE_MASK;
	if (!dp.pbc_wd && val != IPATH_IBSTATE_INIT &&
		val != IPATH_IBSTATE_ARM && val != IPATH_IBSTATE_ACTIVE) {
		ipath_cdbg(VERBOSE, "unit %u not ready (state %llx)\n",
			   dd->ipath_unit, (unsigned long long) val);
		ret = -EINVAL;
		goto bail;
	}

	/* need total length before first word written */
	/* +1 word is for the qword padding */
	plen = sizeof(u32) + dp.len;

	if ((plen + 4) > dd->ipath_ibmaxlen) {
		ipath_dbg("Pkt len 0x%x > ibmaxlen %x\n",
			  plen - 4, dd->ipath_ibmaxlen);
		ret = -EINVAL;
		goto bail;	/* before writing pbc */
	}
	tmpbuf = vmalloc(plen);
	if (!tmpbuf) {
		dev_info(&dd->pcidev->dev, "Unable to allocate tmp buffer, "
			 "failing\n");
		ret = -ENOMEM;
		goto bail;
	}

	if (copy_from_user(tmpbuf,
			   (const void __user *) (unsigned long) dp.data,
			   dp.len)) {
		ret = -EFAULT;
		goto bail;
	}

	piobuf = ipath_getpiobuf(dd, &pbufn);
	if (!piobuf) {
		ipath_cdbg(VERBOSE, "No PIO buffers avail unit for %u\n",
			   dd->ipath_unit);
		ret = -EBUSY;
		goto bail;
	}

	plen >>= 2;		/* in dwords */

	if (ipath_debug & __IPATH_PKTDBG)
		ipath_cdbg(VERBOSE, "unit %u 0x%x+1w pio%d\n",
			   dd->ipath_unit, plen - 1, pbufn);

	if (dp.pbc_wd == 0)
		/* Legacy operation, use computed pbc_wd */
		dp.pbc_wd = plen;

	/* we have to flush after the PBC for correctness on some cpus
	 * or WC buffer can be written out of order */
	writeq(dp.pbc_wd, piobuf);
	ipath_flush_wc();
	/* copy all by the trigger word, then flush, so it's written
	 * to chip before trigger word, then write trigger word, then
	 * flush again, so packet is sent. */
	__iowrite32_copy(piobuf + 2, tmpbuf, clen - 1);
	ipath_flush_wc();
	__raw_writel(tmpbuf[clen - 1], piobuf + clen + 1);
	ipath_flush_wc();

	ret = sizeof(dp);

bail:
	vfree(tmpbuf);
	return ret;
}

static int ipath_diag_release(struct inode *in, struct file *fp)
{
	mutex_lock(&ipath_mutex);
	ipath_diag_inuse = 0;
	fp->private_data = NULL;
	mutex_unlock(&ipath_mutex);
	return 0;
}

static ssize_t ipath_diag_read(struct file *fp, char __user *data,
			       size_t count, loff_t *off)
{
	struct ipath_devdata *dd = fp->private_data;
	void __iomem *kreg_base;
	ssize_t ret;

	kreg_base = dd->ipath_kregbase;

	if (count == 0)
		ret = 0;
	else if ((count % 4) || (*off % 4))
		/* address or length is not 32-bit aligned, hence invalid */
		ret = -EINVAL;
	else if (ipath_diag_inuse < 1 && (*off || count != 8))
		ret = -EINVAL;  /* prevent cat /dev/ipath_diag* */
	else if ((count % 8) || (*off % 8))
		/* address or length not 64-bit aligned; do 32-bit reads */
		ret = ipath_read_umem32(dd, data, kreg_base + *off, count);
	else
		ret = ipath_read_umem64(dd, data, kreg_base + *off, count);

	if (ret >= 0) {
		*off += count;
		ret = count;
		if (ipath_diag_inuse == -2)
			ipath_diag_inuse++;
	}

	return ret;
}

static ssize_t ipath_diag_write(struct file *fp, const char __user *data,
				size_t count, loff_t *off)
{
	struct ipath_devdata *dd = fp->private_data;
	void __iomem *kreg_base;
	ssize_t ret;

	kreg_base = dd->ipath_kregbase;

	if (count == 0)
		ret = 0;
	else if ((count % 4) || (*off % 4))
		/* address or length is not 32-bit aligned, hence invalid */
		ret = -EINVAL;
	else if ((ipath_diag_inuse == -1 && (*off || count != 8)) ||
		 ipath_diag_inuse == -2)  /* read qw off 0, write qw off 0 */
		ret = -EINVAL;  /* before any other write allowed */
	else if ((count % 8) || (*off % 8))
		/* address or length not 64-bit aligned; do 32-bit writes */
		ret = ipath_write_umem32(dd, kreg_base + *off, data, count);
	else
		ret = ipath_write_umem64(dd, kreg_base + *off, data, count);

	if (ret >= 0) {
		*off += count;
		ret = count;
		if (ipath_diag_inuse == -1)
			ipath_diag_inuse = 1; /* all read/write OK now */
	}

	return ret;
}
