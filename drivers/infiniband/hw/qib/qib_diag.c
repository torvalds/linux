/*
 * Copyright (c) 2012 Intel Corporation. All rights reserved.
 * Copyright (c) 2006 - 2012 QLogic Corporation. All rights reserved.
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
 * opening the qib_diag device, normally minor number 129.  Diagnostic use
 * of the QLogic_IB chip may render the chip or board unusable until the
 * driver is unloaded, or in some cases, until the system is rebooted.
 *
 * Accesses to the chip through this interface are not similar to going
 * through the /sys/bus/pci resource mmap interface.
 */

#include <linux/io.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include "qib.h"
#include "qib_common.h"

#undef pr_fmt
#define pr_fmt(fmt) QIB_DRV_NAME ": " fmt

/*
 * Each client that opens the diag device must read then write
 * offset 0, to prevent lossage from random cat or od. diag_state
 * sequences this "handshake".
 */
enum diag_state { UNUSED = 0, OPENED, INIT, READY };

/* State for an individual client. PID so children cannot abuse handshake */
static struct qib_diag_client {
	struct qib_diag_client *next;
	struct qib_devdata *dd;
	pid_t pid;
	enum diag_state state;
} *client_pool;

/*
 * Get a client struct. Recycled if possible, else kmalloc.
 * Must be called with qib_mutex held
 */
static struct qib_diag_client *get_client(struct qib_devdata *dd)
{
	struct qib_diag_client *dc;

	dc = client_pool;
	if (dc)
		/* got from pool remove it and use */
		client_pool = dc->next;
	else
		/* None in pool, alloc and init */
		dc = kmalloc(sizeof *dc, GFP_KERNEL);

	if (dc) {
		dc->next = NULL;
		dc->dd = dd;
		dc->pid = current->pid;
		dc->state = OPENED;
	}
	return dc;
}

/*
 * Return to pool. Must be called with qib_mutex held
 */
static void return_client(struct qib_diag_client *dc)
{
	struct qib_devdata *dd = dc->dd;
	struct qib_diag_client *tdc, *rdc;

	rdc = NULL;
	if (dc == dd->diag_client) {
		dd->diag_client = dc->next;
		rdc = dc;
	} else {
		tdc = dc->dd->diag_client;
		while (tdc) {
			if (dc == tdc->next) {
				tdc->next = dc->next;
				rdc = dc;
				break;
			}
			tdc = tdc->next;
		}
	}
	if (rdc) {
		rdc->state = UNUSED;
		rdc->dd = NULL;
		rdc->pid = 0;
		rdc->next = client_pool;
		client_pool = rdc;
	}
}

static int qib_diag_open(struct inode *in, struct file *fp);
static int qib_diag_release(struct inode *in, struct file *fp);
static ssize_t qib_diag_read(struct file *fp, char __user *data,
			     size_t count, loff_t *off);
static ssize_t qib_diag_write(struct file *fp, const char __user *data,
			      size_t count, loff_t *off);

static const struct file_operations diag_file_ops = {
	.owner = THIS_MODULE,
	.write = qib_diag_write,
	.read = qib_diag_read,
	.open = qib_diag_open,
	.release = qib_diag_release,
	.llseek = default_llseek,
};

static atomic_t diagpkt_count = ATOMIC_INIT(0);
static struct cdev *diagpkt_cdev;
static struct device *diagpkt_device;

static ssize_t qib_diagpkt_write(struct file *fp, const char __user *data,
				 size_t count, loff_t *off);

static const struct file_operations diagpkt_file_ops = {
	.owner = THIS_MODULE,
	.write = qib_diagpkt_write,
	.llseek = noop_llseek,
};

int qib_diag_add(struct qib_devdata *dd)
{
	char name[16];
	int ret = 0;

	if (atomic_inc_return(&diagpkt_count) == 1) {
		ret = qib_cdev_init(QIB_DIAGPKT_MINOR, "ipath_diagpkt",
				    &diagpkt_file_ops, &diagpkt_cdev,
				    &diagpkt_device);
		if (ret)
			goto done;
	}

	snprintf(name, sizeof(name), "ipath_diag%d", dd->unit);
	ret = qib_cdev_init(QIB_DIAG_MINOR_BASE + dd->unit, name,
			    &diag_file_ops, &dd->diag_cdev,
			    &dd->diag_device);
done:
	return ret;
}

static void qib_unregister_observers(struct qib_devdata *dd);

void qib_diag_remove(struct qib_devdata *dd)
{
	struct qib_diag_client *dc;

	if (atomic_dec_and_test(&diagpkt_count))
		qib_cdev_cleanup(&diagpkt_cdev, &diagpkt_device);

	qib_cdev_cleanup(&dd->diag_cdev, &dd->diag_device);

	/*
	 * Return all diag_clients of this device. There should be none,
	 * as we are "guaranteed" that no clients are still open
	 */
	while (dd->diag_client)
		return_client(dd->diag_client);

	/* Now clean up all unused client structs */
	while (client_pool) {
		dc = client_pool;
		client_pool = dc->next;
		kfree(dc);
	}
	/* Clean up observer list */
	qib_unregister_observers(dd);
}

/* qib_remap_ioaddr32 - remap an offset into chip address space to __iomem *
 *
 * @dd: the qlogic_ib device
 * @offs: the offset in chip-space
 * @cntp: Pointer to max (byte) count for transfer starting at offset
 * This returns a u32 __iomem * so it can be used for both 64 and 32-bit
 * mapping. It is needed because with the use of PAT for control of
 * write-combining, the logically contiguous address-space of the chip
 * may be split into virtually non-contiguous spaces, with different
 * attributes, which are them mapped to contiguous physical space
 * based from the first BAR.
 *
 * The code below makes the same assumptions as were made in
 * init_chip_wc_pat() (qib_init.c), copied here:
 * Assumes chip address space looks like:
 *		- kregs + sregs + cregs + uregs (in any order)
 *		- piobufs (2K and 4K bufs in either order)
 *	or:
 *		- kregs + sregs + cregs (in any order)
 *		- piobufs (2K and 4K bufs in either order)
 *		- uregs
 *
 * If cntp is non-NULL, returns how many bytes from offset can be accessed
 * Returns 0 if the offset is not mapped.
 */
static u32 __iomem *qib_remap_ioaddr32(struct qib_devdata *dd, u32 offset,
				       u32 *cntp)
{
	u32 kreglen;
	u32 snd_bottom, snd_lim = 0;
	u32 __iomem *krb32 = (u32 __iomem *)dd->kregbase;
	u32 __iomem *map = NULL;
	u32 cnt = 0;
	u32 tot4k, offs4k;

	/* First, simplest case, offset is within the first map. */
	kreglen = (dd->kregend - dd->kregbase) * sizeof(u64);
	if (offset < kreglen) {
		map = krb32 + (offset / sizeof(u32));
		cnt = kreglen - offset;
		goto mapped;
	}

	/*
	 * Next check for user regs, the next most common case,
	 * and a cheap check because if they are not in the first map
	 * they are last in chip.
	 */
	if (dd->userbase) {
		/* If user regs mapped, they are after send, so set limit. */
		u32 ulim = (dd->cfgctxts * dd->ureg_align) + dd->uregbase;
		if (!dd->piovl15base)
			snd_lim = dd->uregbase;
		krb32 = (u32 __iomem *)dd->userbase;
		if (offset >= dd->uregbase && offset < ulim) {
			map = krb32 + (offset - dd->uregbase) / sizeof(u32);
			cnt = ulim - offset;
			goto mapped;
		}
	}

	/*
	 * Lastly, check for offset within Send Buffers.
	 * This is gnarly because struct devdata is deliberately vague
	 * about things like 7322 VL15 buffers, and we are not in
	 * chip-specific code here, so should not make many assumptions.
	 * The one we _do_ make is that the only chip that has more sndbufs
	 * than we admit is the 7322, and it has userregs above that, so
	 * we know the snd_lim.
	 */
	/* Assume 2K buffers are first. */
	snd_bottom = dd->pio2k_bufbase;
	if (snd_lim == 0) {
		u32 tot2k = dd->piobcnt2k * ALIGN(dd->piosize2k, dd->palign);
		snd_lim = snd_bottom + tot2k;
	}
	/* If 4k buffers exist, account for them by bumping
	 * appropriate limit.
	 */
	tot4k = dd->piobcnt4k * dd->align4k;
	offs4k = dd->piobufbase >> 32;
	if (dd->piobcnt4k) {
		if (snd_bottom > offs4k)
			snd_bottom = offs4k;
		else {
			/* 4k above 2k. Bump snd_lim, if needed*/
			if (!dd->userbase || dd->piovl15base)
				snd_lim = offs4k + tot4k;
		}
	}
	/*
	 * Judgement call: can we ignore the space between SendBuffs and
	 * UserRegs, where we would like to see vl15 buffs, but not more?
	 */
	if (offset >= snd_bottom && offset < snd_lim) {
		offset -= snd_bottom;
		map = (u32 __iomem *)dd->piobase + (offset / sizeof(u32));
		cnt = snd_lim - offset;
	}

	if (!map && offs4k && dd->piovl15base) {
		snd_lim = offs4k + tot4k + 2 * dd->align4k;
		if (offset >= (offs4k + tot4k) && offset < snd_lim) {
			map = (u32 __iomem *)dd->piovl15base +
				((offset - (offs4k + tot4k)) / sizeof(u32));
			cnt = snd_lim - offset;
		}
	}

mapped:
	if (cntp)
		*cntp = cnt;
	return map;
}

/*
 * qib_read_umem64 - read a 64-bit quantity from the chip into user space
 * @dd: the qlogic_ib device
 * @uaddr: the location to store the data in user memory
 * @regoffs: the offset from BAR0 (_NOT_ full pointer, anymore)
 * @count: number of bytes to copy (multiple of 32 bits)
 *
 * This function also localizes all chip memory accesses.
 * The copy should be written such that we read full cacheline packets
 * from the chip.  This is usually used for a single qword
 *
 * NOTE:  This assumes the chip address is 64-bit aligned.
 */
static int qib_read_umem64(struct qib_devdata *dd, void __user *uaddr,
			   u32 regoffs, size_t count)
{
	const u64 __iomem *reg_addr;
	const u64 __iomem *reg_end;
	u32 limit;
	int ret;

	reg_addr = (const u64 __iomem *)qib_remap_ioaddr32(dd, regoffs, &limit);
	if (reg_addr == NULL || limit == 0 || !(dd->flags & QIB_PRESENT)) {
		ret = -EINVAL;
		goto bail;
	}
	if (count >= limit)
		count = limit;
	reg_end = reg_addr + (count / sizeof(u64));

	/* not very efficient, but it works for now */
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

/*
 * qib_write_umem64 - write a 64-bit quantity to the chip from user space
 * @dd: the qlogic_ib device
 * @regoffs: the offset from BAR0 (_NOT_ full pointer, anymore)
 * @uaddr: the source of the data in user memory
 * @count: the number of bytes to copy (multiple of 32 bits)
 *
 * This is usually used for a single qword
 * NOTE:  This assumes the chip address is 64-bit aligned.
 */

static int qib_write_umem64(struct qib_devdata *dd, u32 regoffs,
			    const void __user *uaddr, size_t count)
{
	u64 __iomem *reg_addr;
	const u64 __iomem *reg_end;
	u32 limit;
	int ret;

	reg_addr = (u64 __iomem *)qib_remap_ioaddr32(dd, regoffs, &limit);
	if (reg_addr == NULL || limit == 0 || !(dd->flags & QIB_PRESENT)) {
		ret = -EINVAL;
		goto bail;
	}
	if (count >= limit)
		count = limit;
	reg_end = reg_addr + (count / sizeof(u64));

	/* not very efficient, but it works for now */
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

/*
 * qib_read_umem32 - read a 32-bit quantity from the chip into user space
 * @dd: the qlogic_ib device
 * @uaddr: the location to store the data in user memory
 * @regoffs: the offset from BAR0 (_NOT_ full pointer, anymore)
 * @count: number of bytes to copy
 *
 * read 32 bit values, not 64 bit; for memories that only
 * support 32 bit reads; usually a single dword.
 */
static int qib_read_umem32(struct qib_devdata *dd, void __user *uaddr,
			   u32 regoffs, size_t count)
{
	const u32 __iomem *reg_addr;
	const u32 __iomem *reg_end;
	u32 limit;
	int ret;

	reg_addr = qib_remap_ioaddr32(dd, regoffs, &limit);
	if (reg_addr == NULL || limit == 0 || !(dd->flags & QIB_PRESENT)) {
		ret = -EINVAL;
		goto bail;
	}
	if (count >= limit)
		count = limit;
	reg_end = reg_addr + (count / sizeof(u32));

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

/*
 * qib_write_umem32 - write a 32-bit quantity to the chip from user space
 * @dd: the qlogic_ib device
 * @regoffs: the offset from BAR0 (_NOT_ full pointer, anymore)
 * @uaddr: the source of the data in user memory
 * @count: number of bytes to copy
 *
 * write 32 bit values, not 64 bit; for memories that only
 * support 32 bit write; usually a single dword.
 */

static int qib_write_umem32(struct qib_devdata *dd, u32 regoffs,
			    const void __user *uaddr, size_t count)
{
	u32 __iomem *reg_addr;
	const u32 __iomem *reg_end;
	u32 limit;
	int ret;

	reg_addr = qib_remap_ioaddr32(dd, regoffs, &limit);
	if (reg_addr == NULL || limit == 0 || !(dd->flags & QIB_PRESENT)) {
		ret = -EINVAL;
		goto bail;
	}
	if (count >= limit)
		count = limit;
	reg_end = reg_addr + (count / sizeof(u32));

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

static int qib_diag_open(struct inode *in, struct file *fp)
{
	int unit = iminor(in) - QIB_DIAG_MINOR_BASE;
	struct qib_devdata *dd;
	struct qib_diag_client *dc;
	int ret;

	mutex_lock(&qib_mutex);

	dd = qib_lookup(unit);

	if (dd == NULL || !(dd->flags & QIB_PRESENT) ||
	    !dd->kregbase) {
		ret = -ENODEV;
		goto bail;
	}

	dc = get_client(dd);
	if (!dc) {
		ret = -ENOMEM;
		goto bail;
	}
	dc->next = dd->diag_client;
	dd->diag_client = dc;
	fp->private_data = dc;
	ret = 0;
bail:
	mutex_unlock(&qib_mutex);

	return ret;
}

/**
 * qib_diagpkt_write - write an IB packet
 * @fp: the diag data device file pointer
 * @data: qib_diag_pkt structure saying where to get the packet
 * @count: size of data to write
 * @off: unused by this code
 */
static ssize_t qib_diagpkt_write(struct file *fp,
				 const char __user *data,
				 size_t count, loff_t *off)
{
	u32 __iomem *piobuf;
	u32 plen, pbufn, maxlen_reserve;
	struct qib_diag_xpkt dp;
	u32 *tmpbuf = NULL;
	struct qib_devdata *dd;
	struct qib_pportdata *ppd;
	ssize_t ret = 0;

	if (count != sizeof(dp)) {
		ret = -EINVAL;
		goto bail;
	}
	if (copy_from_user(&dp, data, sizeof(dp))) {
		ret = -EFAULT;
		goto bail;
	}

	dd = qib_lookup(dp.unit);
	if (!dd || !(dd->flags & QIB_PRESENT) || !dd->kregbase) {
		ret = -ENODEV;
		goto bail;
	}
	if (!(dd->flags & QIB_INITTED)) {
		/* no hardware, freeze, etc. */
		ret = -ENODEV;
		goto bail;
	}

	if (dp.version != _DIAG_XPKT_VERS) {
		qib_dev_err(dd, "Invalid version %u for diagpkt_write\n",
			    dp.version);
		ret = -EINVAL;
		goto bail;
	}
	/* send count must be an exact number of dwords */
	if (dp.len & 3) {
		ret = -EINVAL;
		goto bail;
	}
	if (!dp.port || dp.port > dd->num_pports) {
		ret = -EINVAL;
		goto bail;
	}
	ppd = &dd->pport[dp.port - 1];

	/*
	 * need total length before first word written, plus 2 Dwords. One Dword
	 * is for padding so we get the full user data when not aligned on
	 * a word boundary. The other Dword is to make sure we have room for the
	 * ICRC which gets tacked on later.
	 */
	maxlen_reserve = 2 * sizeof(u32);
	if (dp.len > ppd->ibmaxlen - maxlen_reserve) {
		ret = -EINVAL;
		goto bail;
	}

	plen = sizeof(u32) + dp.len;

	tmpbuf = vmalloc(plen);
	if (!tmpbuf) {
		qib_devinfo(dd->pcidev,
			"Unable to allocate tmp buffer, failing\n");
		ret = -ENOMEM;
		goto bail;
	}

	if (copy_from_user(tmpbuf,
			   (const void __user *) (unsigned long) dp.data,
			   dp.len)) {
		ret = -EFAULT;
		goto bail;
	}

	plen >>= 2;             /* in dwords */

	if (dp.pbc_wd == 0)
		dp.pbc_wd = plen;

	piobuf = dd->f_getsendbuf(ppd, dp.pbc_wd, &pbufn);
	if (!piobuf) {
		ret = -EBUSY;
		goto bail;
	}
	/* disarm it just to be extra sure */
	dd->f_sendctrl(dd->pport, QIB_SENDCTRL_DISARM_BUF(pbufn));

	/* disable header check on pbufn for this packet */
	dd->f_txchk_change(dd, pbufn, 1, TXCHK_CHG_TYPE_DIS1, NULL);

	writeq(dp.pbc_wd, piobuf);
	/*
	 * Copy all but the trigger word, then flush, so it's written
	 * to chip before trigger word, then write trigger word, then
	 * flush again, so packet is sent.
	 */
	if (dd->flags & QIB_PIO_FLUSH_WC) {
		qib_flush_wc();
		qib_pio_copy(piobuf + 2, tmpbuf, plen - 1);
		qib_flush_wc();
		__raw_writel(tmpbuf[plen - 1], piobuf + plen + 1);
	} else
		qib_pio_copy(piobuf + 2, tmpbuf, plen);

	if (dd->flags & QIB_USE_SPCL_TRIG) {
		u32 spcl_off = (pbufn >= dd->piobcnt2k) ? 2047 : 1023;

		qib_flush_wc();
		__raw_writel(0xaebecede, piobuf + spcl_off);
	}

	/*
	 * Ensure buffer is written to the chip, then re-enable
	 * header checks (if supported by chip).  The txchk
	 * code will ensure seen by chip before returning.
	 */
	qib_flush_wc();
	qib_sendbuf_done(dd, pbufn);
	dd->f_txchk_change(dd, pbufn, 1, TXCHK_CHG_TYPE_ENAB1, NULL);

	ret = sizeof(dp);

bail:
	vfree(tmpbuf);
	return ret;
}

static int qib_diag_release(struct inode *in, struct file *fp)
{
	mutex_lock(&qib_mutex);
	return_client(fp->private_data);
	fp->private_data = NULL;
	mutex_unlock(&qib_mutex);
	return 0;
}

/*
 * Chip-specific code calls to register its interest in
 * a specific range.
 */
struct diag_observer_list_elt {
	struct diag_observer_list_elt *next;
	const struct diag_observer *op;
};

int qib_register_observer(struct qib_devdata *dd,
			  const struct diag_observer *op)
{
	struct diag_observer_list_elt *olp;
	unsigned long flags;

	if (!dd || !op)
		return -EINVAL;
	olp = vmalloc(sizeof *olp);
	if (!olp) {
		pr_err("vmalloc for observer failed\n");
		return -ENOMEM;
	}

	spin_lock_irqsave(&dd->qib_diag_trans_lock, flags);
	olp->op = op;
	olp->next = dd->diag_observer_list;
	dd->diag_observer_list = olp;
	spin_unlock_irqrestore(&dd->qib_diag_trans_lock, flags);

	return 0;
}

/* Remove all registered observers when device is closed */
static void qib_unregister_observers(struct qib_devdata *dd)
{
	struct diag_observer_list_elt *olp;
	unsigned long flags;

	spin_lock_irqsave(&dd->qib_diag_trans_lock, flags);
	olp = dd->diag_observer_list;
	while (olp) {
		/* Pop one observer, let go of lock */
		dd->diag_observer_list = olp->next;
		spin_unlock_irqrestore(&dd->qib_diag_trans_lock, flags);
		vfree(olp);
		/* try again. */
		spin_lock_irqsave(&dd->qib_diag_trans_lock, flags);
		olp = dd->diag_observer_list;
	}
	spin_unlock_irqrestore(&dd->qib_diag_trans_lock, flags);
}

/*
 * Find the observer, if any, for the specified address. Initial implementation
 * is simple stack of observers. This must be called with diag transaction
 * lock held.
 */
static const struct diag_observer *diag_get_observer(struct qib_devdata *dd,
						     u32 addr)
{
	struct diag_observer_list_elt *olp;
	const struct diag_observer *op = NULL;

	olp = dd->diag_observer_list;
	while (olp) {
		op = olp->op;
		if (addr >= op->bottom && addr <= op->top)
			break;
		olp = olp->next;
	}
	if (!olp)
		op = NULL;

	return op;
}

static ssize_t qib_diag_read(struct file *fp, char __user *data,
			     size_t count, loff_t *off)
{
	struct qib_diag_client *dc = fp->private_data;
	struct qib_devdata *dd = dc->dd;
	void __iomem *kreg_base;
	ssize_t ret;

	if (dc->pid != current->pid) {
		ret = -EPERM;
		goto bail;
	}

	kreg_base = dd->kregbase;

	if (count == 0)
		ret = 0;
	else if ((count % 4) || (*off % 4))
		/* address or length is not 32-bit aligned, hence invalid */
		ret = -EINVAL;
	else if (dc->state < READY && (*off || count != 8))
		ret = -EINVAL;  /* prevent cat /dev/qib_diag* */
	else {
		unsigned long flags;
		u64 data64 = 0;
		int use_32;
		const struct diag_observer *op;

		use_32 = (count % 8) || (*off % 8);
		ret = -1;
		spin_lock_irqsave(&dd->qib_diag_trans_lock, flags);
		/*
		 * Check for observer on this address range.
		 * we only support a single 32 or 64-bit read
		 * via observer, currently.
		 */
		op = diag_get_observer(dd, *off);
		if (op) {
			u32 offset = *off;
			ret = op->hook(dd, op, offset, &data64, 0, use_32);
		}
		/*
		 * We need to release lock before any copy_to_user(),
		 * whether implicit in qib_read_umem* or explicit below.
		 */
		spin_unlock_irqrestore(&dd->qib_diag_trans_lock, flags);
		if (!op) {
			if (use_32)
				/*
				 * Address or length is not 64-bit aligned;
				 * do 32-bit rd
				 */
				ret = qib_read_umem32(dd, data, (u32) *off,
						      count);
			else
				ret = qib_read_umem64(dd, data, (u32) *off,
						      count);
		} else if (ret == count) {
			/* Below finishes case where observer existed */
			ret = copy_to_user(data, &data64, use_32 ?
					   sizeof(u32) : sizeof(u64));
			if (ret)
				ret = -EFAULT;
		}
	}

	if (ret >= 0) {
		*off += count;
		ret = count;
		if (dc->state == OPENED)
			dc->state = INIT;
	}
bail:
	return ret;
}

static ssize_t qib_diag_write(struct file *fp, const char __user *data,
			      size_t count, loff_t *off)
{
	struct qib_diag_client *dc = fp->private_data;
	struct qib_devdata *dd = dc->dd;
	void __iomem *kreg_base;
	ssize_t ret;

	if (dc->pid != current->pid) {
		ret = -EPERM;
		goto bail;
	}

	kreg_base = dd->kregbase;

	if (count == 0)
		ret = 0;
	else if ((count % 4) || (*off % 4))
		/* address or length is not 32-bit aligned, hence invalid */
		ret = -EINVAL;
	else if (dc->state < READY &&
		((*off || count != 8) || dc->state != INIT))
		/* No writes except second-step of init seq */
		ret = -EINVAL;  /* before any other write allowed */
	else {
		unsigned long flags;
		const struct diag_observer *op = NULL;
		int use_32 =  (count % 8) || (*off % 8);

		/*
		 * Check for observer on this address range.
		 * We only support a single 32 or 64-bit write
		 * via observer, currently. This helps, because
		 * we would otherwise have to jump through hoops
		 * to make "diag transaction" meaningful when we
		 * cannot do a copy_from_user while holding the lock.
		 */
		if (count == 4 || count == 8) {
			u64 data64;
			u32 offset = *off;
			ret = copy_from_user(&data64, data, count);
			if (ret) {
				ret = -EFAULT;
				goto bail;
			}
			spin_lock_irqsave(&dd->qib_diag_trans_lock, flags);
			op = diag_get_observer(dd, *off);
			if (op)
				ret = op->hook(dd, op, offset, &data64, ~0Ull,
					       use_32);
			spin_unlock_irqrestore(&dd->qib_diag_trans_lock, flags);
		}

		if (!op) {
			if (use_32)
				/*
				 * Address or length is not 64-bit aligned;
				 * do 32-bit write
				 */
				ret = qib_write_umem32(dd, (u32) *off, data,
						       count);
			else
				ret = qib_write_umem64(dd, (u32) *off, data,
						       count);
		}
	}

	if (ret >= 0) {
		*off += count;
		ret = count;
		if (dc->state == INIT)
			dc->state = READY; /* all read/write OK now */
	}
bail:
	return ret;
}
