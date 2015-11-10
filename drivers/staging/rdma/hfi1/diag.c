/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * This file contains support for diagnostic functions.  It is accessed by
 * opening the hfi1_diag device, normally minor number 129.  Diagnostic use
 * of the chip may render the chip or board unusable until the driver
 * is unloaded, or in some cases, until the system is rebooted.
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
#include <linux/module.h>
#include <rdma/ib_smi.h>
#include "hfi.h"
#include "device.h"
#include "common.h"
#include "trace.h"

#undef pr_fmt
#define pr_fmt(fmt) DRIVER_NAME ": " fmt
#define snoop_dbg(fmt, ...) \
	hfi1_cdbg(SNOOP, fmt, ##__VA_ARGS__)

/* Snoop option mask */
#define SNOOP_DROP_SEND	(1 << 0)
#define SNOOP_USE_METADATA	(1 << 1)

static u8 snoop_flags;

/*
 * Extract packet length from LRH header.
 * Why & 0x7FF? Because len is only 11 bits in case it wasn't 0'd we throw the
 * bogus bits away. This is in Dwords so multiply by 4 to get size in bytes
 */
#define HFI1_GET_PKT_LEN(x)      (((be16_to_cpu((x)->lrh[2]) & 0x7FF)) << 2)

enum hfi1_filter_status {
	HFI1_FILTER_HIT,
	HFI1_FILTER_ERR,
	HFI1_FILTER_MISS
};

/* snoop processing functions */
rhf_rcv_function_ptr snoop_rhf_rcv_functions[8] = {
	[RHF_RCV_TYPE_EXPECTED] = snoop_recv_handler,
	[RHF_RCV_TYPE_EAGER]    = snoop_recv_handler,
	[RHF_RCV_TYPE_IB]       = snoop_recv_handler,
	[RHF_RCV_TYPE_ERROR]    = snoop_recv_handler,
	[RHF_RCV_TYPE_BYPASS]   = snoop_recv_handler,
	[RHF_RCV_TYPE_INVALID5] = process_receive_invalid,
	[RHF_RCV_TYPE_INVALID6] = process_receive_invalid,
	[RHF_RCV_TYPE_INVALID7] = process_receive_invalid
};

/* Snoop packet structure */
struct snoop_packet {
	struct list_head list;
	u32 total_len;
	u8 data[];
};

/* Do not make these an enum or it will blow up the capture_md */
#define PKT_DIR_EGRESS 0x0
#define PKT_DIR_INGRESS 0x1

/* Packet capture metadata returned to the user with the packet. */
struct capture_md {
	u8 port;
	u8 dir;
	u8 reserved[6];
	union {
		u64 pbc;
		u64 rhf;
	} u;
};

static atomic_t diagpkt_count = ATOMIC_INIT(0);
static struct cdev diagpkt_cdev;
static struct device *diagpkt_device;

static ssize_t diagpkt_write(struct file *fp, const char __user *data,
				 size_t count, loff_t *off);

static const struct file_operations diagpkt_file_ops = {
	.owner = THIS_MODULE,
	.write = diagpkt_write,
	.llseek = noop_llseek,
};

/*
 * This is used for communication with user space for snoop extended IOCTLs
 */
struct hfi1_link_info {
	__be64 node_guid;
	u8 port_mode;
	u8 port_state;
	u16 link_speed_active;
	u16 link_width_active;
	u16 vl15_init;
	u8 port_number;
	/*
	 * Add padding to make this a full IB SMP payload. Note: changing the
	 * size of this structure will make the IOCTLs created with _IOWR
	 * change.
	 * Be sure to run tests on all IOCTLs when making changes to this
	 * structure.
	 */
	u8 res[47];
};

/*
 * This starts our ioctl sequence numbers *way* off from the ones
 * defined in ib_core.
 */
#define SNOOP_CAPTURE_VERSION 0x1

#define IB_IOCTL_MAGIC          0x1b /* See Documentation/ioctl-number.txt */
#define HFI1_SNOOP_IOC_MAGIC IB_IOCTL_MAGIC
#define HFI1_SNOOP_IOC_BASE_SEQ 0x80

#define HFI1_SNOOP_IOCGETLINKSTATE \
	_IO(HFI1_SNOOP_IOC_MAGIC, HFI1_SNOOP_IOC_BASE_SEQ)
#define HFI1_SNOOP_IOCSETLINKSTATE \
	_IO(HFI1_SNOOP_IOC_MAGIC, HFI1_SNOOP_IOC_BASE_SEQ+1)
#define HFI1_SNOOP_IOCCLEARQUEUE \
	_IO(HFI1_SNOOP_IOC_MAGIC, HFI1_SNOOP_IOC_BASE_SEQ+2)
#define HFI1_SNOOP_IOCCLEARFILTER \
	_IO(HFI1_SNOOP_IOC_MAGIC, HFI1_SNOOP_IOC_BASE_SEQ+3)
#define HFI1_SNOOP_IOCSETFILTER \
	_IO(HFI1_SNOOP_IOC_MAGIC, HFI1_SNOOP_IOC_BASE_SEQ+4)
#define HFI1_SNOOP_IOCGETVERSION \
	_IO(HFI1_SNOOP_IOC_MAGIC, HFI1_SNOOP_IOC_BASE_SEQ+5)
#define HFI1_SNOOP_IOCSET_OPTS \
	_IO(HFI1_SNOOP_IOC_MAGIC, HFI1_SNOOP_IOC_BASE_SEQ+6)

/*
 * These offsets +6/+7 could change, but these are already known and used
 * IOCTL numbers so don't change them without a good reason.
 */
#define HFI1_SNOOP_IOCGETLINKSTATE_EXTRA \
	_IOWR(HFI1_SNOOP_IOC_MAGIC, HFI1_SNOOP_IOC_BASE_SEQ+6, \
		struct hfi1_link_info)
#define HFI1_SNOOP_IOCSETLINKSTATE_EXTRA \
	_IOWR(HFI1_SNOOP_IOC_MAGIC, HFI1_SNOOP_IOC_BASE_SEQ+7, \
		struct hfi1_link_info)

static int hfi1_snoop_open(struct inode *in, struct file *fp);
static ssize_t hfi1_snoop_read(struct file *fp, char __user *data,
				size_t pkt_len, loff_t *off);
static ssize_t hfi1_snoop_write(struct file *fp, const char __user *data,
				 size_t count, loff_t *off);
static long hfi1_ioctl(struct file *fp, unsigned int cmd, unsigned long arg);
static unsigned int hfi1_snoop_poll(struct file *fp,
					struct poll_table_struct *wait);
static int hfi1_snoop_release(struct inode *in, struct file *fp);

struct hfi1_packet_filter_command {
	int opcode;
	int length;
	void *value_ptr;
};

/* Can't re-use PKT_DIR_*GRESS here because 0 means no packets for this */
#define HFI1_SNOOP_INGRESS 0x1
#define HFI1_SNOOP_EGRESS  0x2

enum hfi1_packet_filter_opcodes {
	FILTER_BY_LID,
	FILTER_BY_DLID,
	FILTER_BY_MAD_MGMT_CLASS,
	FILTER_BY_QP_NUMBER,
	FILTER_BY_PKT_TYPE,
	FILTER_BY_SERVICE_LEVEL,
	FILTER_BY_PKEY,
	FILTER_BY_DIRECTION,
};

static const struct file_operations snoop_file_ops = {
	.owner = THIS_MODULE,
	.open = hfi1_snoop_open,
	.read = hfi1_snoop_read,
	.unlocked_ioctl = hfi1_ioctl,
	.poll = hfi1_snoop_poll,
	.write = hfi1_snoop_write,
	.release = hfi1_snoop_release
};

struct hfi1_filter_array {
	int (*filter)(void *, void *, void *);
};

static int hfi1_filter_lid(void *ibhdr, void *packet_data, void *value);
static int hfi1_filter_dlid(void *ibhdr, void *packet_data, void *value);
static int hfi1_filter_mad_mgmt_class(void *ibhdr, void *packet_data,
				      void *value);
static int hfi1_filter_qp_number(void *ibhdr, void *packet_data, void *value);
static int hfi1_filter_ibpacket_type(void *ibhdr, void *packet_data,
				     void *value);
static int hfi1_filter_ib_service_level(void *ibhdr, void *packet_data,
					void *value);
static int hfi1_filter_ib_pkey(void *ibhdr, void *packet_data, void *value);
static int hfi1_filter_direction(void *ibhdr, void *packet_data, void *value);

static struct hfi1_filter_array hfi1_filters[] = {
	{ hfi1_filter_lid },
	{ hfi1_filter_dlid },
	{ hfi1_filter_mad_mgmt_class },
	{ hfi1_filter_qp_number },
	{ hfi1_filter_ibpacket_type },
	{ hfi1_filter_ib_service_level },
	{ hfi1_filter_ib_pkey },
	{ hfi1_filter_direction },
};

#define HFI1_MAX_FILTERS	ARRAY_SIZE(hfi1_filters)
#define HFI1_DIAG_MINOR_BASE	129

static int hfi1_snoop_add(struct hfi1_devdata *dd, const char *name);

int hfi1_diag_add(struct hfi1_devdata *dd)
{
	char name[16];
	int ret = 0;

	snprintf(name, sizeof(name), "%s_diagpkt%d", class_name(),
		 dd->unit);
	/*
	 * Do this for each device as opposed to the normal diagpkt
	 * interface which is one per host
	 */
	ret = hfi1_snoop_add(dd, name);
	if (ret)
		dd_dev_err(dd, "Unable to init snoop/capture device");

	snprintf(name, sizeof(name), "%s_diagpkt", class_name());
	if (atomic_inc_return(&diagpkt_count) == 1) {
		ret = hfi1_cdev_init(HFI1_DIAGPKT_MINOR, name,
				     &diagpkt_file_ops, &diagpkt_cdev,
				     &diagpkt_device, false);
	}

	return ret;
}

/* this must be called w/ dd->snoop_in_lock held */
static void drain_snoop_list(struct list_head *queue)
{
	struct list_head *pos, *q;
	struct snoop_packet *packet;

	list_for_each_safe(pos, q, queue) {
		packet = list_entry(pos, struct snoop_packet, list);
		list_del(pos);
		kfree(packet);
	}
}

static void hfi1_snoop_remove(struct hfi1_devdata *dd)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&dd->hfi1_snoop.snoop_lock, flags);
	drain_snoop_list(&dd->hfi1_snoop.queue);
	hfi1_cdev_cleanup(&dd->hfi1_snoop.cdev, &dd->hfi1_snoop.class_dev);
	spin_unlock_irqrestore(&dd->hfi1_snoop.snoop_lock, flags);
}

void hfi1_diag_remove(struct hfi1_devdata *dd)
{

	hfi1_snoop_remove(dd);
	if (atomic_dec_and_test(&diagpkt_count))
		hfi1_cdev_cleanup(&diagpkt_cdev, &diagpkt_device);
	hfi1_cdev_cleanup(&dd->diag_cdev, &dd->diag_device);
}


/*
 * Allocated structure shared between the credit return mechanism and
 * diagpkt_send().
 */
struct diagpkt_wait {
	struct completion credits_returned;
	int code;
	atomic_t count;
};

/*
 * When each side is finished with the structure, they call this.
 * The last user frees the structure.
 */
static void put_diagpkt_wait(struct diagpkt_wait *wait)
{
	if (atomic_dec_and_test(&wait->count))
		kfree(wait);
}

/*
 * Callback from the credit return code.  Set the complete, which
 * will let diapkt_send() continue.
 */
static void diagpkt_complete(void *arg, int code)
{
	struct diagpkt_wait *wait = (struct diagpkt_wait *)arg;

	wait->code = code;
	complete(&wait->credits_returned);
	put_diagpkt_wait(wait);	/* finished with the structure */
}

/**
 * diagpkt_send - send a packet
 * @dp: diag packet descriptor
 */
static ssize_t diagpkt_send(struct diag_pkt *dp)
{
	struct hfi1_devdata *dd;
	struct send_context *sc;
	struct pio_buf *pbuf;
	u32 *tmpbuf = NULL;
	ssize_t ret = 0;
	u32 pkt_len, total_len;
	pio_release_cb credit_cb = NULL;
	void *credit_arg = NULL;
	struct diagpkt_wait *wait = NULL;

	dd = hfi1_lookup(dp->unit);
	if (!dd || !(dd->flags & HFI1_PRESENT) || !dd->kregbase) {
		ret = -ENODEV;
		goto bail;
	}
	if (!(dd->flags & HFI1_INITTED)) {
		/* no hardware, freeze, etc. */
		ret = -ENODEV;
		goto bail;
	}

	if (dp->version != _DIAG_PKT_VERS) {
		dd_dev_err(dd, "Invalid version %u for diagpkt_write\n",
			    dp->version);
		ret = -EINVAL;
		goto bail;
	}

	/* send count must be an exact number of dwords */
	if (dp->len & 3) {
		ret = -EINVAL;
		goto bail;
	}

	/* there is only port 1 */
	if (dp->port != 1) {
		ret = -EINVAL;
		goto bail;
	}

	/* need a valid context */
	if (dp->sw_index >= dd->num_send_contexts) {
		ret = -EINVAL;
		goto bail;
	}
	/* can only use kernel contexts */
	if (dd->send_contexts[dp->sw_index].type != SC_KERNEL) {
		ret = -EINVAL;
		goto bail;
	}
	/* must be allocated */
	sc = dd->send_contexts[dp->sw_index].sc;
	if (!sc) {
		ret = -EINVAL;
		goto bail;
	}
	/* must be enabled */
	if (!(sc->flags & SCF_ENABLED)) {
		ret = -EINVAL;
		goto bail;
	}

	/* allocate a buffer and copy the data in */
	tmpbuf = vmalloc(dp->len);
	if (!tmpbuf) {
		ret = -ENOMEM;
		goto bail;
	}

	if (copy_from_user(tmpbuf,
			   (const void __user *) (unsigned long) dp->data,
			   dp->len)) {
		ret = -EFAULT;
		goto bail;
	}

	/*
	 * pkt_len is how much data we have to write, includes header and data.
	 * total_len is length of the packet in Dwords plus the PBC should not
	 * include the CRC.
	 */
	pkt_len = dp->len >> 2;
	total_len = pkt_len + 2; /* PBC + packet */

	/* if 0, fill in a default */
	if (dp->pbc == 0) {
		struct hfi1_pportdata *ppd = dd->pport;

		hfi1_cdbg(PKT, "Generating PBC");
		dp->pbc = create_pbc(ppd, 0, 0, 0, total_len);
	} else {
		hfi1_cdbg(PKT, "Using passed in PBC");
	}

	hfi1_cdbg(PKT, "Egress PBC content is 0x%llx", dp->pbc);

	/*
	 * The caller wants to wait until the packet is sent and to
	 * check for errors.  The best we can do is wait until
	 * the buffer credits are returned and check if any packet
	 * error has occurred.  If there are any late errors, this
	 * could miss it.  If there are other senders who generate
	 * an error, this may find it.  However, in general, it
	 * should catch most.
	 */
	if (dp->flags & F_DIAGPKT_WAIT) {
		/* always force a credit return */
		dp->pbc |= PBC_CREDIT_RETURN;
		/* turn on credit return interrupts */
		sc_add_credit_return_intr(sc);
		wait = kmalloc(sizeof(*wait), GFP_KERNEL);
		if (!wait) {
			ret = -ENOMEM;
			goto bail;
		}
		init_completion(&wait->credits_returned);
		atomic_set(&wait->count, 2);
		wait->code = PRC_OK;

		credit_cb = diagpkt_complete;
		credit_arg = wait;
	}

	pbuf = sc_buffer_alloc(sc, total_len, credit_cb, credit_arg);
	if (!pbuf) {
		/*
		 * No send buffer means no credit callback.  Undo
		 * the wait set-up that was done above.  We free wait
		 * because the callback will never be called.
		 */
		if (dp->flags & F_DIAGPKT_WAIT) {
			sc_del_credit_return_intr(sc);
			kfree(wait);
			wait = NULL;
		}
		ret = -ENOSPC;
		goto bail;
	}

	pio_copy(dd, pbuf, dp->pbc, tmpbuf, pkt_len);
	/* no flush needed as the HW knows the packet size */

	ret = sizeof(*dp);

	if (dp->flags & F_DIAGPKT_WAIT) {
		/* wait for credit return */
		ret = wait_for_completion_interruptible(
						&wait->credits_returned);
		/*
		 * If the wait returns an error, the wait was interrupted,
		 * e.g. with a ^C in the user program.  The callback is
		 * still pending.  This is OK as the wait structure is
		 * kmalloc'ed and the structure will free itself when
		 * all users are done with it.
		 *
		 * A context disable occurs on a send context restart, so
		 * include that in the list of errors below to check for.
		 * NOTE: PRC_FILL_ERR is at best informational and cannot
		 * be depended on.
		 */
		if (!ret && (((wait->code & PRC_STATUS_ERR)
				|| (wait->code & PRC_FILL_ERR)
				|| (wait->code & PRC_SC_DISABLE))))
			ret = -EIO;

		put_diagpkt_wait(wait);	/* finished with the structure */
		sc_del_credit_return_intr(sc);
	}

bail:
	vfree(tmpbuf);
	return ret;
}

static ssize_t diagpkt_write(struct file *fp, const char __user *data,
				 size_t count, loff_t *off)
{
	struct hfi1_devdata *dd;
	struct send_context *sc;
	u8 vl;

	struct diag_pkt dp;

	if (count != sizeof(dp))
		return -EINVAL;

	if (copy_from_user(&dp, data, sizeof(dp)))
		return -EFAULT;

	/*
	* The Send Context is derived from the PbcVL value
	* if PBC is populated
	*/
	if (dp.pbc) {
		dd = hfi1_lookup(dp.unit);
		if (dd == NULL)
			return -ENODEV;
		vl = (dp.pbc >> PBC_VL_SHIFT) & PBC_VL_MASK;
		sc = dd->vld[vl].sc;
		if (sc) {
			dp.sw_index = sc->sw_index;
			hfi1_cdbg(
			       PKT,
			       "Packet sent over VL %d via Send Context %u(%u)",
			       vl, sc->sw_index, sc->hw_context);
		}
	}

	return diagpkt_send(&dp);
}

static int hfi1_snoop_add(struct hfi1_devdata *dd, const char *name)
{
	int ret = 0;

	dd->hfi1_snoop.mode_flag = 0;
	spin_lock_init(&dd->hfi1_snoop.snoop_lock);
	INIT_LIST_HEAD(&dd->hfi1_snoop.queue);
	init_waitqueue_head(&dd->hfi1_snoop.waitq);

	ret = hfi1_cdev_init(HFI1_SNOOP_CAPTURE_BASE + dd->unit, name,
			     &snoop_file_ops,
			     &dd->hfi1_snoop.cdev, &dd->hfi1_snoop.class_dev,
			     false);

	if (ret) {
		dd_dev_err(dd, "Couldn't create %s device: %d", name, ret);
		hfi1_cdev_cleanup(&dd->hfi1_snoop.cdev,
				 &dd->hfi1_snoop.class_dev);
	}

	return ret;
}

static struct hfi1_devdata *hfi1_dd_from_sc_inode(struct inode *in)
{
	int unit = iminor(in) - HFI1_SNOOP_CAPTURE_BASE;
	struct hfi1_devdata *dd = NULL;

	dd = hfi1_lookup(unit);
	return dd;

}

/* clear or restore send context integrity checks */
static void adjust_integrity_checks(struct hfi1_devdata *dd)
{
	struct send_context *sc;
	unsigned long sc_flags;
	int i;

	spin_lock_irqsave(&dd->sc_lock, sc_flags);
	for (i = 0; i < dd->num_send_contexts; i++) {
		int enable;

		sc = dd->send_contexts[i].sc;

		if (!sc)
			continue;	/* not allocated */

		enable = likely(!HFI1_CAP_IS_KSET(NO_INTEGRITY)) &&
			 dd->hfi1_snoop.mode_flag != HFI1_PORT_SNOOP_MODE;

		set_pio_integrity(sc);

		if (enable) /* take HFI_CAP_* flags into account */
			hfi1_init_ctxt(sc);
	}
	spin_unlock_irqrestore(&dd->sc_lock, sc_flags);
}

static int hfi1_snoop_open(struct inode *in, struct file *fp)
{
	int ret;
	int mode_flag = 0;
	unsigned long flags = 0;
	struct hfi1_devdata *dd;
	struct list_head *queue;

	mutex_lock(&hfi1_mutex);

	dd = hfi1_dd_from_sc_inode(in);
	if (dd == NULL) {
		ret = -ENODEV;
		goto bail;
	}

	/*
	 * File mode determines snoop or capture. Some existing user
	 * applications expect the capture device to be able to be opened RDWR
	 * because they expect a dedicated capture device. For this reason we
	 * support a module param to force capture mode even if the file open
	 * mode matches snoop.
	 */
	if ((fp->f_flags & O_ACCMODE) == O_RDONLY) {
		snoop_dbg("Capture Enabled");
		mode_flag = HFI1_PORT_CAPTURE_MODE;
	} else if ((fp->f_flags & O_ACCMODE) == O_RDWR) {
		snoop_dbg("Snoop Enabled");
		mode_flag = HFI1_PORT_SNOOP_MODE;
	} else {
		snoop_dbg("Invalid");
		ret =  -EINVAL;
		goto bail;
	}
	queue = &dd->hfi1_snoop.queue;

	/*
	 * We are not supporting snoop and capture at the same time.
	 */
	spin_lock_irqsave(&dd->hfi1_snoop.snoop_lock, flags);
	if (dd->hfi1_snoop.mode_flag) {
		ret = -EBUSY;
		spin_unlock_irqrestore(&dd->hfi1_snoop.snoop_lock, flags);
		goto bail;
	}

	dd->hfi1_snoop.mode_flag = mode_flag;
	drain_snoop_list(queue);

	dd->hfi1_snoop.filter_callback = NULL;
	dd->hfi1_snoop.filter_value = NULL;

	/*
	 * Send side packet integrity checks are not helpful when snooping so
	 * disable and re-enable when we stop snooping.
	 */
	if (mode_flag == HFI1_PORT_SNOOP_MODE) {
		/* clear after snoop mode is on */
		adjust_integrity_checks(dd); /* clear */

		/*
		 * We also do not want to be doing the DLID LMC check for
		 * ingressed packets.
		 */
		dd->hfi1_snoop.dcc_cfg = read_csr(dd, DCC_CFG_PORT_CONFIG1);
		write_csr(dd, DCC_CFG_PORT_CONFIG1,
			  (dd->hfi1_snoop.dcc_cfg >> 32) << 32);
	}

	/*
	 * As soon as we set these function pointers the recv and send handlers
	 * are active. This is a race condition so we must make sure to drain
	 * the queue and init filter values above. Technically we should add
	 * locking here but all that will happen is on recv a packet will get
	 * allocated and get stuck on the snoop_lock before getting added to the
	 * queue. Same goes for send.
	 */
	dd->rhf_rcv_function_map = snoop_rhf_rcv_functions;
	dd->process_pio_send = snoop_send_pio_handler;
	dd->process_dma_send = snoop_send_pio_handler;
	dd->pio_inline_send = snoop_inline_pio_send;

	spin_unlock_irqrestore(&dd->hfi1_snoop.snoop_lock, flags);
	ret = 0;

bail:
	mutex_unlock(&hfi1_mutex);

	return ret;
}

static int hfi1_snoop_release(struct inode *in, struct file *fp)
{
	unsigned long flags = 0;
	struct hfi1_devdata *dd;
	int mode_flag;

	dd = hfi1_dd_from_sc_inode(in);
	if (dd == NULL)
		return -ENODEV;

	spin_lock_irqsave(&dd->hfi1_snoop.snoop_lock, flags);

	/* clear the snoop mode before re-adjusting send context CSRs */
	mode_flag = dd->hfi1_snoop.mode_flag;
	dd->hfi1_snoop.mode_flag = 0;

	/*
	 * Drain the queue and clear the filters we are done with it. Don't
	 * forget to restore the packet integrity checks
	 */
	drain_snoop_list(&dd->hfi1_snoop.queue);
	if (mode_flag == HFI1_PORT_SNOOP_MODE) {
		/* restore after snoop mode is clear */
		adjust_integrity_checks(dd); /* restore */

		/*
		 * Also should probably reset the DCC_CONFIG1 register for DLID
		 * checking on incoming packets again. Use the value saved when
		 * opening the snoop device.
		 */
		write_csr(dd, DCC_CFG_PORT_CONFIG1, dd->hfi1_snoop.dcc_cfg);
	}

	dd->hfi1_snoop.filter_callback = NULL;
	kfree(dd->hfi1_snoop.filter_value);
	dd->hfi1_snoop.filter_value = NULL;

	/*
	 * User is done snooping and capturing, return control to the normal
	 * handler. Re-enable SDMA handling.
	 */
	dd->rhf_rcv_function_map = dd->normal_rhf_rcv_functions;
	dd->process_pio_send = hfi1_verbs_send_pio;
	dd->process_dma_send = hfi1_verbs_send_dma;
	dd->pio_inline_send = pio_copy;

	spin_unlock_irqrestore(&dd->hfi1_snoop.snoop_lock, flags);

	snoop_dbg("snoop/capture device released");

	return 0;
}

static unsigned int hfi1_snoop_poll(struct file *fp,
				    struct poll_table_struct *wait)
{
	int ret = 0;
	unsigned long flags = 0;

	struct hfi1_devdata *dd;

	dd = hfi1_dd_from_sc_inode(fp->f_inode);
	if (dd == NULL)
		return -ENODEV;

	spin_lock_irqsave(&dd->hfi1_snoop.snoop_lock, flags);

	poll_wait(fp, &dd->hfi1_snoop.waitq, wait);
	if (!list_empty(&dd->hfi1_snoop.queue))
		ret |= POLLIN | POLLRDNORM;

	spin_unlock_irqrestore(&dd->hfi1_snoop.snoop_lock, flags);
	return ret;

}

static ssize_t hfi1_snoop_write(struct file *fp, const char __user *data,
				size_t count, loff_t *off)
{
	struct diag_pkt dpkt;
	struct hfi1_devdata *dd;
	size_t ret;
	u8 byte_two, sl, sc5, sc4, vl, byte_one;
	struct send_context *sc;
	u32 len;
	u64 pbc;
	struct hfi1_ibport *ibp;
	struct hfi1_pportdata *ppd;

	dd = hfi1_dd_from_sc_inode(fp->f_inode);
	if (dd == NULL)
		return -ENODEV;

	ppd = dd->pport;
	snoop_dbg("received %lu bytes from user", count);

	memset(&dpkt, 0, sizeof(struct diag_pkt));
	dpkt.version = _DIAG_PKT_VERS;
	dpkt.unit = dd->unit;
	dpkt.port = 1;

	if (likely(!(snoop_flags & SNOOP_USE_METADATA))) {
		/*
		* We need to generate the PBC and not let diagpkt_send do it,
		* to do this we need the VL and the length in dwords.
		* The VL can be determined by using the SL and looking up the
		* SC. Then the SC can be converted into VL. The exception to
		* this is those packets which are from an SMI queue pair.
		* Since we can't detect anything about the QP here we have to
		* rely on the SC. If its 0xF then we assume its SMI and
		* do not look at the SL.
		*/
		if (copy_from_user(&byte_one, data, 1))
			return -EINVAL;

		if (copy_from_user(&byte_two, data+1, 1))
			return -EINVAL;

		sc4 = (byte_one >> 4) & 0xf;
		if (sc4 == 0xF) {
			snoop_dbg("Detected VL15 packet ignoring SL in packet");
			vl = sc4;
		} else {
			sl = (byte_two >> 4) & 0xf;
			ibp = to_iport(&dd->verbs_dev.ibdev, 1);
			sc5 = ibp->sl_to_sc[sl];
			vl = sc_to_vlt(dd, sc5);
			if (vl != sc4) {
				snoop_dbg("VL %d does not match SC %d of packet",
					  vl, sc4);
				return -EINVAL;
			}
		}

		sc = dd->vld[vl].sc; /* Look up the context based on VL */
		if (sc) {
			dpkt.sw_index = sc->sw_index;
			snoop_dbg("Sending on context %u(%u)", sc->sw_index,
				  sc->hw_context);
		} else {
			snoop_dbg("Could not find context for vl %d", vl);
			return -EINVAL;
		}

		len = (count >> 2) + 2; /* Add in PBC */
		pbc = create_pbc(ppd, 0, 0, vl, len);
	} else {
		if (copy_from_user(&pbc, data, sizeof(pbc)))
			return -EINVAL;
		vl = (pbc >> PBC_VL_SHIFT) & PBC_VL_MASK;
		sc = dd->vld[vl].sc; /* Look up the context based on VL */
		if (sc) {
			dpkt.sw_index = sc->sw_index;
		} else {
			snoop_dbg("Could not find context for vl %d", vl);
			return -EINVAL;
		}
		data += sizeof(pbc);
		count -= sizeof(pbc);
	}
	dpkt.len = count;
	dpkt.data = (unsigned long)data;

	snoop_dbg("PBC: vl=0x%llx Length=0x%llx",
		  (pbc >> 12) & 0xf,
		  (pbc & 0xfff));

	dpkt.pbc = pbc;
	ret = diagpkt_send(&dpkt);
	/*
	 * diagpkt_send only returns number of bytes in the diagpkt so patch
	 * that up here before returning.
	 */
	if (ret == sizeof(dpkt))
		return count;

	return ret;
}

static ssize_t hfi1_snoop_read(struct file *fp, char __user *data,
			       size_t pkt_len, loff_t *off)
{
	ssize_t ret = 0;
	unsigned long flags = 0;
	struct snoop_packet *packet = NULL;
	struct hfi1_devdata *dd;

	dd = hfi1_dd_from_sc_inode(fp->f_inode);
	if (dd == NULL)
		return -ENODEV;

	spin_lock_irqsave(&dd->hfi1_snoop.snoop_lock, flags);

	while (list_empty(&dd->hfi1_snoop.queue)) {
		spin_unlock_irqrestore(&dd->hfi1_snoop.snoop_lock, flags);

		if (fp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		if (wait_event_interruptible(
				dd->hfi1_snoop.waitq,
				!list_empty(&dd->hfi1_snoop.queue)))
			return -EINTR;

		spin_lock_irqsave(&dd->hfi1_snoop.snoop_lock, flags);
	}

	if (!list_empty(&dd->hfi1_snoop.queue)) {
		packet = list_entry(dd->hfi1_snoop.queue.next,
				    struct snoop_packet, list);
		list_del(&packet->list);
		spin_unlock_irqrestore(&dd->hfi1_snoop.snoop_lock, flags);
		if (pkt_len >= packet->total_len) {
			if (copy_to_user(data, packet->data,
				packet->total_len))
				ret = -EFAULT;
			else
				ret = packet->total_len;
		} else
			ret = -EINVAL;

		kfree(packet);
	} else
		spin_unlock_irqrestore(&dd->hfi1_snoop.snoop_lock, flags);

	return ret;
}

static long hfi1_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	struct hfi1_devdata *dd;
	void *filter_value = NULL;
	long ret = 0;
	int value = 0;
	u8 physState = 0;
	u8 linkState = 0;
	u16 devState = 0;
	unsigned long flags = 0;
	unsigned long *argp = NULL;
	struct hfi1_packet_filter_command filter_cmd = {0};
	int mode_flag = 0;
	struct hfi1_pportdata *ppd = NULL;
	unsigned int index;
	struct hfi1_link_info link_info;

	dd = hfi1_dd_from_sc_inode(fp->f_inode);
	if (dd == NULL)
		return -ENODEV;

	spin_lock_irqsave(&dd->hfi1_snoop.snoop_lock, flags);

	mode_flag = dd->hfi1_snoop.mode_flag;

	if (((_IOC_DIR(cmd) & _IOC_READ)
	    && !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd)))
	    || ((_IOC_DIR(cmd) & _IOC_WRITE)
	    && !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd)))) {
		ret = -EFAULT;
	} else if (!capable(CAP_SYS_ADMIN)) {
		ret = -EPERM;
	} else if ((mode_flag & HFI1_PORT_CAPTURE_MODE) &&
		   (cmd != HFI1_SNOOP_IOCCLEARQUEUE) &&
		   (cmd != HFI1_SNOOP_IOCCLEARFILTER) &&
		   (cmd != HFI1_SNOOP_IOCSETFILTER)) {
		/* Capture devices are allowed only 3 operations
		 * 1.Clear capture queue
		 * 2.Clear capture filter
		 * 3.Set capture filter
		 * Other are invalid.
		 */
		ret = -EINVAL;
	} else {
		switch (cmd) {
		case HFI1_SNOOP_IOCSETLINKSTATE:
			snoop_dbg("HFI1_SNOOP_IOCSETLINKSTATE is not valid");
			ret = -EINVAL;
			break;

		case HFI1_SNOOP_IOCSETLINKSTATE_EXTRA:
			memset(&link_info, 0, sizeof(link_info));

			if (copy_from_user(&link_info,
				(struct hfi1_link_info __user *)arg,
				sizeof(link_info)))
				ret = -EFAULT;

			value = link_info.port_state;
			index = link_info.port_number;
			if (index > dd->num_pports - 1) {
				ret = -EINVAL;
				break;
			}

			ppd = &dd->pport[index];
			if (!ppd) {
				ret = -EINVAL;
				break;
			}

			/* What we want to transition to */
			physState = (value >> 4) & 0xF;
			linkState = value & 0xF;
			snoop_dbg("Setting link state 0x%x", value);

			switch (linkState) {
			case IB_PORT_NOP:
				if (physState == 0)
					break;
					/* fall through */
			case IB_PORT_DOWN:
				switch (physState) {
				case 0:
					devState = HLS_DN_DOWNDEF;
					break;
				case 2:
					devState = HLS_DN_POLL;
					break;
				case 3:
					devState = HLS_DN_DISABLE;
					break;
				default:
					ret = -EINVAL;
					goto done;
				}
				ret = set_link_state(ppd, devState);
				break;
			case IB_PORT_ARMED:
				ret = set_link_state(ppd, HLS_UP_ARMED);
				if (!ret)
					send_idle_sma(dd, SMA_IDLE_ARM);
				break;
			case IB_PORT_ACTIVE:
				ret = set_link_state(ppd, HLS_UP_ACTIVE);
				if (!ret)
					send_idle_sma(dd, SMA_IDLE_ACTIVE);
				break;
			default:
				ret = -EINVAL;
				break;
			}

			if (ret)
				break;
			/* fall through */
		case HFI1_SNOOP_IOCGETLINKSTATE:
		case HFI1_SNOOP_IOCGETLINKSTATE_EXTRA:
			if (cmd == HFI1_SNOOP_IOCGETLINKSTATE_EXTRA) {
				memset(&link_info, 0, sizeof(link_info));
				if (copy_from_user(&link_info,
					(struct hfi1_link_info __user *)arg,
					sizeof(link_info)))
					ret = -EFAULT;
				index = link_info.port_number;
			} else {
				ret = __get_user(index, (int __user *) arg);
				if (ret !=  0)
					break;
			}

			if (index > dd->num_pports - 1) {
				ret = -EINVAL;
				break;
			}

			ppd = &dd->pport[index];
			if (!ppd) {
				ret = -EINVAL;
				break;
			}
			value = hfi1_ibphys_portstate(ppd);
			value <<= 4;
			value |= driver_lstate(ppd);

			snoop_dbg("Link port | Link State: %d", value);

			if ((cmd == HFI1_SNOOP_IOCGETLINKSTATE_EXTRA) ||
			    (cmd == HFI1_SNOOP_IOCSETLINKSTATE_EXTRA)) {
				link_info.port_state = value;
				link_info.node_guid = cpu_to_be64(ppd->guid);
				link_info.link_speed_active =
							ppd->link_speed_active;
				link_info.link_width_active =
							ppd->link_width_active;
				if (copy_to_user(
					(struct hfi1_link_info __user *)arg,
					&link_info, sizeof(link_info)))
					ret = -EFAULT;
			} else {
				ret = __put_user(value, (int __user *)arg);
			}
			break;

		case HFI1_SNOOP_IOCCLEARQUEUE:
			snoop_dbg("Clearing snoop queue");
			drain_snoop_list(&dd->hfi1_snoop.queue);
			break;

		case HFI1_SNOOP_IOCCLEARFILTER:
			snoop_dbg("Clearing filter");
			if (dd->hfi1_snoop.filter_callback) {
				/* Drain packets first */
				drain_snoop_list(&dd->hfi1_snoop.queue);
				dd->hfi1_snoop.filter_callback = NULL;
			}
			kfree(dd->hfi1_snoop.filter_value);
			dd->hfi1_snoop.filter_value = NULL;
			break;

		case HFI1_SNOOP_IOCSETFILTER:
			snoop_dbg("Setting filter");
			/* just copy command structure */
			argp = (unsigned long *)arg;
			if (copy_from_user(&filter_cmd, (void __user *)argp,
					     sizeof(filter_cmd))) {
				ret = -EFAULT;
				break;
			}
			if (filter_cmd.opcode >= HFI1_MAX_FILTERS) {
				pr_alert("Invalid opcode in request\n");
				ret = -EINVAL;
				break;
			}

			snoop_dbg("Opcode %d Len %d Ptr %p",
				   filter_cmd.opcode, filter_cmd.length,
				   filter_cmd.value_ptr);

			filter_value = kzalloc(
						filter_cmd.length * sizeof(u8),
						GFP_KERNEL);
			if (!filter_value) {
				pr_alert("Not enough memory\n");
				ret = -ENOMEM;
				break;
			}
			/* copy remaining data from userspace */
			if (copy_from_user((u8 *)filter_value,
					(void __user *)filter_cmd.value_ptr,
					filter_cmd.length)) {
				kfree(filter_value);
				ret = -EFAULT;
				break;
			}
			/* Drain packets first */
			drain_snoop_list(&dd->hfi1_snoop.queue);
			dd->hfi1_snoop.filter_callback =
				hfi1_filters[filter_cmd.opcode].filter;
			/* just in case we see back to back sets */
			kfree(dd->hfi1_snoop.filter_value);
			dd->hfi1_snoop.filter_value = filter_value;

			break;
		case HFI1_SNOOP_IOCGETVERSION:
			value = SNOOP_CAPTURE_VERSION;
			snoop_dbg("Getting version: %d", value);
			ret = __put_user(value, (int __user *)arg);
			break;
		case HFI1_SNOOP_IOCSET_OPTS:
			snoop_flags = 0;
			ret = __get_user(value, (int __user *) arg);
			if (ret != 0)
				break;

			snoop_dbg("Setting snoop option %d", value);
			if (value & SNOOP_DROP_SEND)
				snoop_flags |= SNOOP_DROP_SEND;
			if (value & SNOOP_USE_METADATA)
				snoop_flags |= SNOOP_USE_METADATA;
			break;
		default:
			ret = -ENOTTY;
			break;
		}
	}
done:
	spin_unlock_irqrestore(&dd->hfi1_snoop.snoop_lock, flags);
	return ret;
}

static void snoop_list_add_tail(struct snoop_packet *packet,
				struct hfi1_devdata *dd)
{
	unsigned long flags = 0;

	spin_lock_irqsave(&dd->hfi1_snoop.snoop_lock, flags);
	if (likely((dd->hfi1_snoop.mode_flag & HFI1_PORT_SNOOP_MODE) ||
		   (dd->hfi1_snoop.mode_flag & HFI1_PORT_CAPTURE_MODE))) {
		list_add_tail(&packet->list, &dd->hfi1_snoop.queue);
		snoop_dbg("Added packet to list");
	}

	/*
	 * Technically we can could have closed the snoop device while waiting
	 * on the above lock and it is gone now. The snoop mode_flag will
	 * prevent us from adding the packet to the queue though.
	 */

	spin_unlock_irqrestore(&dd->hfi1_snoop.snoop_lock, flags);
	wake_up_interruptible(&dd->hfi1_snoop.waitq);
}

static inline int hfi1_filter_check(void *val, const char *msg)
{
	if (!val) {
		snoop_dbg("Error invalid %s value for filter", msg);
		return HFI1_FILTER_ERR;
	}
	return 0;
}

static int hfi1_filter_lid(void *ibhdr, void *packet_data, void *value)
{
	struct hfi1_ib_header *hdr;
	int ret;

	ret = hfi1_filter_check(ibhdr, "header");
	if (ret)
		return ret;
	ret = hfi1_filter_check(value, "user");
	if (ret)
		return ret;
	hdr = (struct hfi1_ib_header *)ibhdr;

	if (*((u16 *)value) == be16_to_cpu(hdr->lrh[3])) /* matches slid */
		return HFI1_FILTER_HIT; /* matched */

	return HFI1_FILTER_MISS; /* Not matched */
}

static int hfi1_filter_dlid(void *ibhdr, void *packet_data, void *value)
{
	struct hfi1_ib_header *hdr;
	int ret;

	ret = hfi1_filter_check(ibhdr, "header");
	if (ret)
		return ret;
	ret = hfi1_filter_check(value, "user");
	if (ret)
		return ret;

	hdr = (struct hfi1_ib_header *)ibhdr;

	if (*((u16 *)value) == be16_to_cpu(hdr->lrh[1]))
		return HFI1_FILTER_HIT;

	return HFI1_FILTER_MISS;
}

/* Not valid for outgoing packets, send handler passes null for data*/
static int hfi1_filter_mad_mgmt_class(void *ibhdr, void *packet_data,
				      void *value)
{
	struct hfi1_ib_header *hdr;
	struct hfi1_other_headers *ohdr = NULL;
	struct ib_smp *smp = NULL;
	u32 qpn = 0;
	int ret;

	ret = hfi1_filter_check(ibhdr, "header");
	if (ret)
		return ret;
	ret = hfi1_filter_check(packet_data, "packet_data");
	if (ret)
		return ret;
	ret = hfi1_filter_check(value, "user");
	if (ret)
		return ret;

	hdr = (struct hfi1_ib_header *)ibhdr;

	/* Check for GRH */
	if ((be16_to_cpu(hdr->lrh[0]) & 3) == HFI1_LRH_BTH)
		ohdr = &hdr->u.oth; /* LRH + BTH + DETH */
	else
		ohdr = &hdr->u.l.oth; /* LRH + GRH + BTH + DETH */

	qpn = be32_to_cpu(ohdr->bth[1]) & 0x00FFFFFF;
	if (qpn <= 1) {
		smp = (struct ib_smp *)packet_data;
		if (*((u8 *)value) == smp->mgmt_class)
			return HFI1_FILTER_HIT;
		else
			return HFI1_FILTER_MISS;
	}
	return HFI1_FILTER_ERR;
}

static int hfi1_filter_qp_number(void *ibhdr, void *packet_data, void *value)
{

	struct hfi1_ib_header *hdr;
	struct hfi1_other_headers *ohdr = NULL;
	int ret;

	ret = hfi1_filter_check(ibhdr, "header");
	if (ret)
		return ret;
	ret = hfi1_filter_check(value, "user");
	if (ret)
		return ret;

	hdr = (struct hfi1_ib_header *)ibhdr;

	/* Check for GRH */
	if ((be16_to_cpu(hdr->lrh[0]) & 3) == HFI1_LRH_BTH)
		ohdr = &hdr->u.oth; /* LRH + BTH + DETH */
	else
		ohdr = &hdr->u.l.oth; /* LRH + GRH + BTH + DETH */
	if (*((u32 *)value) == (be32_to_cpu(ohdr->bth[1]) & 0x00FFFFFF))
		return HFI1_FILTER_HIT;

	return HFI1_FILTER_MISS;
}

static int hfi1_filter_ibpacket_type(void *ibhdr, void *packet_data,
				     void *value)
{
	u32 lnh = 0;
	u8 opcode = 0;
	struct hfi1_ib_header *hdr;
	struct hfi1_other_headers *ohdr = NULL;
	int ret;

	ret = hfi1_filter_check(ibhdr, "header");
	if (ret)
		return ret;
	ret = hfi1_filter_check(value, "user");
	if (ret)
		return ret;

	hdr = (struct hfi1_ib_header *)ibhdr;

	lnh = (be16_to_cpu(hdr->lrh[0]) & 3);

	if (lnh == HFI1_LRH_BTH)
		ohdr = &hdr->u.oth;
	else if (lnh == HFI1_LRH_GRH)
		ohdr = &hdr->u.l.oth;
	else
		return HFI1_FILTER_ERR;

	opcode = be32_to_cpu(ohdr->bth[0]) >> 24;

	if (*((u8 *)value) == ((opcode >> 5) & 0x7))
		return HFI1_FILTER_HIT;

	return HFI1_FILTER_MISS;
}

static int hfi1_filter_ib_service_level(void *ibhdr, void *packet_data,
					void *value)
{
	struct hfi1_ib_header *hdr;
	int ret;

	ret = hfi1_filter_check(ibhdr, "header");
	if (ret)
		return ret;
	ret = hfi1_filter_check(value, "user");
	if (ret)
		return ret;

	hdr = (struct hfi1_ib_header *)ibhdr;

	if ((*((u8 *)value)) == ((be16_to_cpu(hdr->lrh[0]) >> 4) & 0xF))
		return HFI1_FILTER_HIT;

	return HFI1_FILTER_MISS;
}

static int hfi1_filter_ib_pkey(void *ibhdr, void *packet_data, void *value)
{

	u32 lnh = 0;
	struct hfi1_ib_header *hdr;
	struct hfi1_other_headers *ohdr = NULL;
	int ret;

	ret = hfi1_filter_check(ibhdr, "header");
	if (ret)
		return ret;
	ret = hfi1_filter_check(value, "user");
	if (ret)
		return ret;

	hdr = (struct hfi1_ib_header *)ibhdr;

	lnh = (be16_to_cpu(hdr->lrh[0]) & 3);
	if (lnh == HFI1_LRH_BTH)
		ohdr = &hdr->u.oth;
	else if (lnh == HFI1_LRH_GRH)
		ohdr = &hdr->u.l.oth;
	else
		return HFI1_FILTER_ERR;

	/* P_key is 16-bit entity, however top most bit indicates
	 * type of membership. 0 for limited and 1 for Full.
	 * Limited members cannot accept information from other
	 * Limited members, but communication is allowed between
	 * every other combination of membership.
	 * Hence we'll omit comparing top-most bit while filtering
	 */

	if ((*(u16 *)value & 0x7FFF) ==
		((be32_to_cpu(ohdr->bth[0])) & 0x7FFF))
		return HFI1_FILTER_HIT;

	return HFI1_FILTER_MISS;
}

/*
 * If packet_data is NULL then this is coming from one of the send functions.
 * Thus we know if its an ingressed or egressed packet.
 */
static int hfi1_filter_direction(void *ibhdr, void *packet_data, void *value)
{
	u8 user_dir = *(u8 *)value;
	int ret;

	ret = hfi1_filter_check(value, "user");
	if (ret)
		return ret;

	if (packet_data) {
		/* Incoming packet */
		if (user_dir & HFI1_SNOOP_INGRESS)
			return HFI1_FILTER_HIT;
	} else {
		/* Outgoing packet */
		if (user_dir & HFI1_SNOOP_EGRESS)
			return HFI1_FILTER_HIT;
	}

	return HFI1_FILTER_MISS;
}

/*
 * Allocate a snoop packet. The structure that is stored in the ring buffer, not
 * to be confused with an hfi packet type.
 */
static struct snoop_packet *allocate_snoop_packet(u32 hdr_len,
						  u32 data_len,
						  u32 md_len)
{

	struct snoop_packet *packet = NULL;

	packet = kzalloc(sizeof(struct snoop_packet) + hdr_len + data_len
			 + md_len,
			 GFP_ATOMIC | __GFP_NOWARN);
	if (likely(packet))
		INIT_LIST_HEAD(&packet->list);


	return packet;
}

/*
 * Instead of having snoop and capture code intermixed with the recv functions,
 * both the interrupt handler and hfi1_ib_rcv() we are going to hijack the call
 * and land in here for snoop/capture but if not enabled the call will go
 * through as before. This gives us a single point to constrain all of the snoop
 * snoop recv logic. There is nothing special that needs to happen for bypass
 * packets. This routine should not try to look into the packet. It just copied
 * it. There is no guarantee for filters when it comes to bypass packets as
 * there is no specific support. Bottom line is this routine does now even know
 * what a bypass packet is.
 */
int snoop_recv_handler(struct hfi1_packet *packet)
{
	struct hfi1_pportdata *ppd = packet->rcd->ppd;
	struct hfi1_ib_header *hdr = packet->hdr;
	int header_size = packet->hlen;
	void *data = packet->ebuf;
	u32 tlen = packet->tlen;
	struct snoop_packet *s_packet = NULL;
	int ret;
	int snoop_mode = 0;
	u32 md_len = 0;
	struct capture_md md;

	snoop_dbg("PACKET IN: hdr size %d tlen %d data %p", header_size, tlen,
		  data);

	trace_snoop_capture(ppd->dd, header_size, hdr, tlen - header_size,
			    data);

	if (!ppd->dd->hfi1_snoop.filter_callback) {
		snoop_dbg("filter not set");
		ret = HFI1_FILTER_HIT;
	} else {
		ret = ppd->dd->hfi1_snoop.filter_callback(hdr, data,
					ppd->dd->hfi1_snoop.filter_value);
	}

	switch (ret) {
	case HFI1_FILTER_ERR:
		snoop_dbg("Error in filter call");
		break;
	case HFI1_FILTER_MISS:
		snoop_dbg("Filter Miss");
		break;
	case HFI1_FILTER_HIT:

		if (ppd->dd->hfi1_snoop.mode_flag & HFI1_PORT_SNOOP_MODE)
			snoop_mode = 1;
		if ((snoop_mode == 0) ||
		    unlikely(snoop_flags & SNOOP_USE_METADATA))
			md_len = sizeof(struct capture_md);


		s_packet = allocate_snoop_packet(header_size,
						 tlen - header_size,
						 md_len);

		if (unlikely(s_packet == NULL)) {
			dd_dev_warn_ratelimited(ppd->dd, "Unable to allocate snoop/capture packet\n");
			break;
		}

		if (md_len > 0) {
			memset(&md, 0, sizeof(struct capture_md));
			md.port = 1;
			md.dir = PKT_DIR_INGRESS;
			md.u.rhf = packet->rhf;
			memcpy(s_packet->data, &md, md_len);
		}

		/* We should always have a header */
		if (hdr) {
			memcpy(s_packet->data + md_len, hdr, header_size);
		} else {
			dd_dev_err(ppd->dd, "Unable to copy header to snoop/capture packet\n");
			kfree(s_packet);
			break;
		}

		/*
		 * Packets with no data are possible. If there is no data needed
		 * to take care of the last 4 bytes which are normally included
		 * with data buffers and are included in tlen.  Since we kzalloc
		 * the buffer we do not need to set any values but if we decide
		 * not to use kzalloc we should zero them.
		 */
		if (data)
			memcpy(s_packet->data + header_size + md_len, data,
			       tlen - header_size);

		s_packet->total_len = tlen + md_len;
		snoop_list_add_tail(s_packet, ppd->dd);

		/*
		 * If we are snooping the packet not capturing then throw away
		 * after adding to the list.
		 */
		snoop_dbg("Capturing packet");
		if (ppd->dd->hfi1_snoop.mode_flag & HFI1_PORT_SNOOP_MODE) {
			snoop_dbg("Throwing packet away");
			/*
			 * If we are dropping the packet we still may need to
			 * handle the case where error flags are set, this is
			 * normally done by the type specific handler but that
			 * won't be called in this case.
			 */
			if (unlikely(rhf_err_flags(packet->rhf)))
				handle_eflags(packet);

			/* throw the packet on the floor */
			return RHF_RCV_CONTINUE;
		}
		break;
	default:
		break;
	}

	/*
	 * We do not care what type of packet came in here - just pass it off
	 * to the normal handler.
	 */
	return ppd->dd->normal_rhf_rcv_functions[rhf_rcv_type(packet->rhf)]
			(packet);
}

/*
 * Handle snooping and capturing packets when sdma is being used.
 */
int snoop_send_dma_handler(struct hfi1_qp *qp, struct ahg_ib_header *ibhdr,
			   u32 hdrwords, struct hfi1_sge_state *ss, u32 len,
			   u32 plen, u32 dwords, u64 pbc)
{
	pr_alert("Snooping/Capture of  Send DMA Packets Is Not Supported!\n");
	snoop_dbg("Unsupported Operation");
	return hfi1_verbs_send_dma(qp, ibhdr, hdrwords, ss, len, plen, dwords,
				  0);
}

/*
 * Handle snooping and capturing packets when pio is being used. Does not handle
 * bypass packets. The only way to send a bypass packet currently is to use the
 * diagpkt interface. When that interface is enable snoop/capture is not.
 */
int snoop_send_pio_handler(struct hfi1_qp *qp, struct ahg_ib_header *ahdr,
			   u32 hdrwords, struct hfi1_sge_state *ss, u32 len,
			   u32 plen, u32 dwords, u64 pbc)
{
	struct hfi1_ibport *ibp = to_iport(qp->ibqp.device, qp->port_num);
	struct hfi1_pportdata *ppd = ppd_from_ibp(ibp);
	struct snoop_packet *s_packet = NULL;
	u32 *hdr = (u32 *)&ahdr->ibh;
	u32 length = 0;
	struct hfi1_sge_state temp_ss;
	void *data = NULL;
	void *data_start = NULL;
	int ret;
	int snoop_mode = 0;
	int md_len = 0;
	struct capture_md md;
	u32 vl;
	u32 hdr_len = hdrwords << 2;
	u32 tlen = HFI1_GET_PKT_LEN(&ahdr->ibh);

	md.u.pbc = 0;

	snoop_dbg("PACKET OUT: hdrword %u len %u plen %u dwords %u tlen %u",
		  hdrwords, len, plen, dwords, tlen);
	if (ppd->dd->hfi1_snoop.mode_flag & HFI1_PORT_SNOOP_MODE)
		snoop_mode = 1;
	if ((snoop_mode == 0) ||
	    unlikely(snoop_flags & SNOOP_USE_METADATA))
		md_len = sizeof(struct capture_md);

	/* not using ss->total_len as arg 2 b/c that does not count CRC */
	s_packet = allocate_snoop_packet(hdr_len, tlen - hdr_len, md_len);

	if (unlikely(s_packet == NULL)) {
		dd_dev_warn_ratelimited(ppd->dd, "Unable to allocate snoop/capture packet\n");
		goto out;
	}

	s_packet->total_len = tlen + md_len;

	if (md_len > 0) {
		memset(&md, 0, sizeof(struct capture_md));
		md.port = 1;
		md.dir = PKT_DIR_EGRESS;
		if (likely(pbc == 0)) {
			vl = be16_to_cpu(ahdr->ibh.lrh[0]) >> 12;
			md.u.pbc = create_pbc(ppd, 0, qp->s_srate, vl, plen);
		} else {
			md.u.pbc = 0;
		}
		memcpy(s_packet->data, &md, md_len);
	} else {
		md.u.pbc = pbc;
	}

	/* Copy header */
	if (likely(hdr)) {
		memcpy(s_packet->data + md_len, hdr, hdr_len);
	} else {
		dd_dev_err(ppd->dd,
			   "Unable to copy header to snoop/capture packet\n");
		kfree(s_packet);
		goto out;
	}

	if (ss) {
		data = s_packet->data + hdr_len + md_len;
		data_start = data;

		/*
		 * Copy SGE State
		 * The update_sge() function below will not modify the
		 * individual SGEs in the array. It will make a copy each time
		 * and operate on that. So we only need to copy this instance
		 * and it won't impact PIO.
		 */
		temp_ss = *ss;
		length = len;

		snoop_dbg("Need to copy %d bytes", length);
		while (length) {
			void *addr = temp_ss.sge.vaddr;
			u32 slen = temp_ss.sge.length;

			if (slen > length) {
				slen = length;
				snoop_dbg("slen %d > len %d", slen, length);
			}
			snoop_dbg("copy %d to %p", slen, addr);
			memcpy(data, addr, slen);
			update_sge(&temp_ss, slen);
			length -= slen;
			data += slen;
			snoop_dbg("data is now %p bytes left %d", data, length);
		}
		snoop_dbg("Completed SGE copy");
	}

	/*
	 * Why do the filter check down here? Because the event tracing has its
	 * own filtering and we need to have the walked the SGE list.
	 */
	if (!ppd->dd->hfi1_snoop.filter_callback) {
		snoop_dbg("filter not set\n");
		ret = HFI1_FILTER_HIT;
	} else {
		ret = ppd->dd->hfi1_snoop.filter_callback(
					&ahdr->ibh,
					NULL,
					ppd->dd->hfi1_snoop.filter_value);
	}

	switch (ret) {
	case HFI1_FILTER_ERR:
		snoop_dbg("Error in filter call");
		/* fall through */
	case HFI1_FILTER_MISS:
		snoop_dbg("Filter Miss");
		kfree(s_packet);
		break;
	case HFI1_FILTER_HIT:
		snoop_dbg("Capturing packet");
		snoop_list_add_tail(s_packet, ppd->dd);

		if (unlikely((snoop_flags & SNOOP_DROP_SEND) &&
			     (ppd->dd->hfi1_snoop.mode_flag &
			      HFI1_PORT_SNOOP_MODE))) {
			unsigned long flags;

			snoop_dbg("Dropping packet");
			if (qp->s_wqe) {
				spin_lock_irqsave(&qp->s_lock, flags);
				hfi1_send_complete(
					qp,
					qp->s_wqe,
					IB_WC_SUCCESS);
				spin_unlock_irqrestore(&qp->s_lock, flags);
			} else if (qp->ibqp.qp_type == IB_QPT_RC) {
				spin_lock_irqsave(&qp->s_lock, flags);
				hfi1_rc_send_complete(qp, &ahdr->ibh);
				spin_unlock_irqrestore(&qp->s_lock, flags);
			}
			return 0;
		}
		break;
	default:
		kfree(s_packet);
		break;
	}
out:
	return hfi1_verbs_send_pio(qp, ahdr, hdrwords, ss, len, plen, dwords,
				  md.u.pbc);
}

/*
 * Callers of this must pass a hfi1_ib_header type for the from ptr. Currently
 * this can be used anywhere, but the intention is for inline ACKs for RC and
 * CCA packets. We don't restrict this usage though.
 */
void snoop_inline_pio_send(struct hfi1_devdata *dd, struct pio_buf *pbuf,
			   u64 pbc, const void *from, size_t count)
{
	int snoop_mode = 0;
	int md_len = 0;
	struct capture_md md;
	struct snoop_packet *s_packet = NULL;

	/*
	 * count is in dwords so we need to convert to bytes.
	 * We also need to account for CRC which would be tacked on by hardware.
	 */
	int packet_len = (count << 2) + 4;
	int ret;

	snoop_dbg("ACK OUT: len %d", packet_len);

	if (!dd->hfi1_snoop.filter_callback) {
		snoop_dbg("filter not set");
		ret = HFI1_FILTER_HIT;
	} else {
		ret = dd->hfi1_snoop.filter_callback(
				(struct hfi1_ib_header *)from,
				NULL,
				dd->hfi1_snoop.filter_value);
	}

	switch (ret) {
	case HFI1_FILTER_ERR:
		snoop_dbg("Error in filter call");
		/* fall through */
	case HFI1_FILTER_MISS:
		snoop_dbg("Filter Miss");
		break;
	case HFI1_FILTER_HIT:
		snoop_dbg("Capturing packet");
		if (dd->hfi1_snoop.mode_flag & HFI1_PORT_SNOOP_MODE)
			snoop_mode = 1;
		if ((snoop_mode == 0) ||
		    unlikely(snoop_flags & SNOOP_USE_METADATA))
			md_len = sizeof(struct capture_md);

		s_packet = allocate_snoop_packet(packet_len, 0, md_len);

		if (unlikely(s_packet == NULL)) {
			dd_dev_warn_ratelimited(dd, "Unable to allocate snoop/capture packet\n");
			goto inline_pio_out;
		}

		s_packet->total_len = packet_len + md_len;

		/* Fill in the metadata for the packet */
		if (md_len > 0) {
			memset(&md, 0, sizeof(struct capture_md));
			md.port = 1;
			md.dir = PKT_DIR_EGRESS;
			md.u.pbc = pbc;
			memcpy(s_packet->data, &md, md_len);
		}

		/* Add the packet data which is a single buffer */
		memcpy(s_packet->data + md_len, from, packet_len);

		snoop_list_add_tail(s_packet, dd);

		if (unlikely((snoop_flags & SNOOP_DROP_SEND) && snoop_mode)) {
			snoop_dbg("Dropping packet");
			return;
		}
		break;
	default:
		break;
	}

inline_pio_out:
	pio_copy(dd, pbuf, pbc, from, count);

}
