/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/time.h>
#include <linux/delay.h>
#if WMT_CREATE_NODE_DYNAMIC
#include <linux/device.h>
#endif
#include <linux/printk.h>

#include "osal_typedef.h"
#include "stp_exp.h"
#include "wmt_exp.h"

MODULE_LICENSE("Dual BSD/GPL");

#ifdef MTK_BT_HCI
#define MTK_BT_DEBUG 0
#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#endif


#define BT_DRIVER_NAME "mtk_stp_BT_chrdev"
#define BT_DEV_MAJOR 192	/* Never used number */

#define PFX                         "[MTK-BT] "
#define BT_LOG_DBG                  3
#define BT_LOG_INFO                 2
#define BT_LOG_WARN                 1
#define BT_LOG_ERR                  0

#define COMBO_IOC_MAGIC             0xb0
#define COMBO_IOCTL_FW_ASSERT       _IOWR(COMBO_IOC_MAGIC, 0, int)
#define COMBO_IOCTL_BT_IC_HW_VER    _IOWR(COMBO_IOC_MAGIC, 1, void*)
#define COMBO_IOCTL_BT_IC_FW_VER    _IOWR(COMBO_IOC_MAGIC, 2, void*)
#define COMBO_IOC_BT_HWVER          _IOWR(COMBO_IOC_MAGIC, 3, void*)

static UINT32 gDbgLevel = BT_LOG_INFO;

#define BT_DBG_FUNC(fmt, arg...)	\
	do { if (gDbgLevel >= BT_LOG_DBG)	\
		pr_debug(PFX "%s: "  fmt, __func__ , ##arg);	\
	} while (0)
#define BT_INFO_FUNC(fmt, arg...)	\
	do { if (gDbgLevel >= BT_LOG_INFO)	\
		pr_warn(PFX "%s: "  fmt, __func__ , ##arg);	\
	} while (0)
#define BT_WARN_FUNC(fmt, arg...)	\
	do { if (gDbgLevel >= BT_LOG_WARN)	\
		pr_err(PFX "%s: "  fmt, __func__ , ##arg);	\
	} while (0)
#define BT_ERR_FUNC(fmt, arg...)	\
	do { if (gDbgLevel >= BT_LOG_ERR)	\
		pr_err(PFX "%s: "   fmt, __func__ , ##arg);	\
	} while (0)

#define VERSION "1.0"

#ifdef MTK_BT_HCI

#define   NUM_REASSEMBLY   32
struct mtk_hci {
	struct hci_dev *hdev;
	struct work_struct work;
	struct sk_buff_head txq;
        struct sk_buff *reassembly[NUM_REASSEMBLY];
};

static struct mtk_hci   mtk_hci;

#endif

#if WMT_CREATE_NODE_DYNAMIC
struct class *stpbt_class = NULL;
struct device *stpbt_dev = NULL;
#endif

static INT32 BT_devs = 1;	/* Device count */
static INT32 BT_major = BT_DEV_MAJOR;	/* Dynamic allocation */
module_param(BT_major, uint, 0);
static struct cdev BT_cdev;

#define BT_BUFFER_SIZE 2048
static UINT8 i_buf[BT_BUFFER_SIZE];	/* Input buffer of read() */
static UINT8 o_buf[BT_BUFFER_SIZE];	/* Output buffer of write() */

static struct semaphore wr_mtx, rd_mtx;
/* Wait queue for poll and read */
static wait_queue_head_t inq;
static DECLARE_WAIT_QUEUE_HEAD(BT_wq);
static INT32 flag;
/* Reset flag for whole chip reset senario */
static volatile INT32 rstflag;

#ifdef MTK_BT_HCI
static int hci_reassembly(struct hci_dev *hdev, int type, void *data,
	int count, __u8 index)
{
	int len = 0;
	int hlen = 0;
	int offset = 0;
	int remain = count;
	struct sk_buff *skb;
	struct bt_skb_cb *scb;
	u16 opcode = 0;
	unsigned char *pdata = data;

        struct mtk_hci *info = NULL;
	struct hci_event_hdr *ehdr = NULL;
	struct hci_ev_cmd_complete *ev = NULL;
	struct hci_rp_read_local_ext_features *ext  = NULL;

        info = hci_get_drvdata(hdev);
        if ( NULL == info ) {
            printk(KERN_ERR "mtk_bt_hci: invalid info point\n");
            return 0;
        }

	if ((type < HCI_ACLDATA_PKT || type > HCI_EVENT_PKT) ||
		index >= NUM_REASSEMBLY)
		return -EILSEQ;

	skb = info->reassembly[index];

	if (!skb) {
		switch (type) {
		case HCI_ACLDATA_PKT:
			len = HCI_MAX_FRAME_SIZE;
			hlen = HCI_ACL_HDR_SIZE;
			break;
		case HCI_EVENT_PKT:
			len = HCI_MAX_EVENT_SIZE;
			hlen = HCI_EVENT_HDR_SIZE;
			break;
		case HCI_SCODATA_PKT:
			len = HCI_MAX_SCO_SIZE;
			hlen = HCI_SCO_HDR_SIZE;
			break;
		}

		skb = bt_skb_alloc(len, GFP_ATOMIC);
		if (!skb)
			return -ENOMEM;

		scb = (void *) skb->cb;
		scb->expect = hlen;
		scb->pkt_type = type;

		info->reassembly[index] = skb;
	}

	while (count) {
		scb = (void *) skb->cb;
		len = min_t(uint, scb->expect, count);

		/*
		 * Workaround for MT7623+MT6625 BT: the max page in response of cmd READ_LOCAL_EXT_FEATURES
                 * should be 1, instead of 2, so changing it to 1 here
		 */

		if (HCI_EVENT_PKT == type) 
 		{
			ehdr = (void *)pdata;
			offset = sizeof(struct hci_event_hdr);
			if ( HCI_EV_CMD_COMPLETE == ehdr->evt) 
			{
				ev = (struct hci_ev_cmd_complete *)&pdata[offset];

				offset += sizeof(struct hci_ev_cmd_complete);

				opcode = __le16_to_cpu(ev->opcode);
				if(HCI_OP_READ_LOCAL_EXT_FEATURES == opcode) {
					ext = (struct hci_rp_read_local_ext_features *) &pdata[offset];
					if( !ext->status && ext->max_page >= 2) {
						pr_info("%s: this workaround is applied for mediatek BT\n", __func__);
						ext->max_page = 1;
					}						
				}
			
			}
		}

		memcpy(skb_put(skb, len), data, len);

		count -= len;
		data += len;
		scb->expect -= len;
		remain = count;

		switch (type) {
		case HCI_EVENT_PKT:
			if (skb->len == HCI_EVENT_HDR_SIZE) {
				struct hci_event_hdr *h = hci_event_hdr(skb);

				scb->expect = h->plen;

				if (skb_tailroom(skb) < scb->expect) {
					kfree_skb(skb);
					info->reassembly[index] = NULL;
					return -ENOMEM;
				}
			}

			break;

		case HCI_ACLDATA_PKT:
			if (skb->len  == HCI_ACL_HDR_SIZE) {
				struct hci_acl_hdr *h = hci_acl_hdr(skb);

				scb->expect = __le16_to_cpu(h->dlen);

				if (skb_tailroom(skb) < scb->expect) {
					kfree_skb(skb);
					info->reassembly[index] = NULL;
					return -ENOMEM;
				}
			}
			break;

		case HCI_SCODATA_PKT:
			if (skb->len == HCI_SCO_HDR_SIZE) {
				struct hci_sco_hdr *h = hci_sco_hdr(skb);

				scb->expect = h->dlen;

				if (skb_tailroom(skb) < scb->expect) {
					kfree_skb(skb);
					info->reassembly[index] = NULL;
					return -ENOMEM;
				}
			}
			break;
		}

		if (scb->expect == 0) {
			/* Complete frame */

			bt_cb(skb)->pkt_type = type;
			hci_recv_frame(hdev, skb);

			info->reassembly[index] = NULL;
			return remain;
		}
	}

	return remain;
}

int hci_recv_fragment(struct hci_dev *hdev, int type, void *data, int count)
{
	int rem = 0;

	if (type < HCI_ACLDATA_PKT || type > HCI_EVENT_PKT)
		return -EILSEQ;

	while (count) {
		rem = hci_reassembly(hdev, type, data, count, type - 1);
		if (rem < 0)
			return rem;

		data += (count - rem);
		count = rem;
	}

	return rem;
}
#endif

#ifdef MTK_BT_HCI
void
hex_dump(char *prefix, char *p, int len)
{
	int i;

	pr_err("%s ", prefix);
	for (i = 0; i < len; i++)
		pr_err("%02x ", (*p++ & 0xff));
	pr_err("\n");
}

static int
mtk_bt_hci_open(struct hci_dev *hdev)
{
	int err = 0;

#if MTK_BT_DEBUG == 1
	pr_err("# %s\n", __func__);
#endif

	err = mtk_wcn_wmt_func_on(WMTDRV_TYPE_BT);
	if (err != MTK_WCN_BOOL_TRUE) {
		pr_err("%s func on failed with %d\n", __func__, err);
		return -ENODEV;
	}

	set_bit(HCI_RUNNING, &hdev->flags);

	mtk_wcn_stp_set_bluez(1);

	return 0;
}

static int
mtk_bt_hci_close(struct hci_dev *hdev)
{
	int err = 0;

#if MTK_BT_DEBUG == 1
	pr_err("# %s\n", __func__);
#endif

	mtk_wcn_stp_set_bluez(0);

	clear_bit(HCI_RUNNING, &hdev->flags);

	err = mtk_wcn_wmt_func_off(WMTDRV_TYPE_BT);
	if (err != MTK_WCN_BOOL_TRUE) {
		pr_err("%s func off failed with %d\n", __func__, err);
		return -EIO;
	}

	return 0;
}

static void
mtk_bt_hci_work(struct work_struct *work)
{
	int err;
	struct sk_buff *skb;

#if MTK_BT_DEBUG == 1
	pr_err("# %s\n", __func__);
#endif

	while ((skb = skb_dequeue(&mtk_hci.txq))) {
		skb_push(skb, 1);
		skb->data[0] = bt_cb(skb)->pkt_type;

#if MTK_BT_DEBUG == 1
		hex_dump(">>", skb->data, skb->len);
#endif

		err = mtk_wcn_stp_send_data(skb->data, skb->len, BT_TASK_INDX);
		if (err < 0) {
			pr_err("%s err=%d\n", __func__, err);
			mtk_hci.hdev->stat.err_tx++;
			skb_queue_head(&mtk_hci.txq, skb);
			break;
		}

		mtk_hci.hdev->stat.byte_tx += skb->len;
		kfree_skb(skb);
	}
}

static int
mtk_bt_hci_send(struct hci_dev *hdev, struct sk_buff *skb)
{
#if MTK_BT_DEBUG == 1
	pr_err("# %s\n", __func__);
#endif

	if (mtk_hci.hdev && !test_bit(HCI_RUNNING, &mtk_hci.hdev->flags))
		return -EBUSY;

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		mtk_hci.hdev->stat.cmd_tx++;
		break;

	case HCI_ACLDATA_PKT:
		mtk_hci.hdev->stat.acl_tx++;
		break;

	case HCI_SCODATA_PKT:
		mtk_hci.hdev->stat.sco_tx++;
		break;

	default:
		return -EILSEQ;
	}

	skb_queue_tail(&mtk_hci.txq, skb);
	schedule_work(&mtk_hci.work);

	return 0;
}

static int
mtk_bt_hci_flush(struct hci_dev *hdev)
{
	pr_err("%s: todo\n", __func__);

	return 0;
}

static void
mtk_bt_hci_receive(const PUINT8 data, INT32 size)
{
	int err;

#if MTK_BT_DEBUG == 1
	pr_err("# %s\n", __func__);
	hex_dump("<<", data, size);
#endif

	err = hci_recv_fragment(mtk_hci.hdev, data[0], (void *)&data[1], size - 1);
	if (err < 0)
		pr_err("%s: hci_recv_fragment failed with %d\n", __func__, err);

	if (mtk_hci.hdev)
		mtk_hci.hdev->stat.byte_rx += size - 1;
}

static void
mtk_bt_hci_notify(struct hci_dev *hdev, unsigned int evt)
{
	static const char * const notify_str[] = {
		"null",
		"HCI_NOTIFY_CONN_ADD",
		"HCI_NOTIFY_CONN_DEL",
		"HCI_NOTIFY_VOICE_SETTING"
	};

	if (evt > HCI_NOTIFY_VOICE_SETTING)
		pr_info("%s event=0x%x\n", __func__, evt);
	else
		pr_info("%s event(%d)=%s\n", __func__, evt, notify_str[evt]);
}
#endif

#ifdef MTK_BT_HCI

int mtk_bt_hci_init(void)
{
    	INT32 hci_err = 0;

	mtk_hci.hdev = hci_alloc_dev();
	if (!(mtk_hci.hdev)) {
		mtk_hci.hdev = NULL;
		BT_ERR_FUNC("%s hci_alloc_dev failed\n", __func__);
		return -ENOMEM;
	}
	
	mtk_hci.hdev->bus = HCI_SDIO;
	mtk_hci.hdev->open = mtk_bt_hci_open;
	mtk_hci.hdev->close = mtk_bt_hci_close;
	mtk_hci.hdev->send = mtk_bt_hci_send;
	mtk_hci.hdev->flush = mtk_bt_hci_flush;
	mtk_hci.hdev->notify = mtk_bt_hci_notify;
	SET_HCIDEV_DEV(mtk_hci.hdev, stpbt_dev);

        hci_set_drvdata(mtk_hci.hdev, &mtk_hci);       

	mtk_wcn_stp_register_if_rx(mtk_bt_hci_receive);

	hci_err = hci_register_dev(mtk_hci.hdev);
	if (hci_err) {
		BT_ERR_FUNC("%s hci_register_dev failed with %d\n", __func__, hci_err);
		hci_free_dev(mtk_hci.hdev);
		mtk_hci.hdev = NULL;
		return hci_err;
	}

	skb_queue_head_init(&mtk_hci.txq);
	INIT_WORK(&mtk_hci.work, mtk_bt_hci_work);

	return 0;
}
#endif


static VOID bt_cdev_rst_cb(ENUM_WMTDRV_TYPE_T src,
			   ENUM_WMTDRV_TYPE_T dst, ENUM_WMTMSG_TYPE_T type, PVOID buf, UINT32 sz)
{
	/*
	   Handle whole chip reset messages
	 */
	ENUM_WMTRSTMSG_TYPE_T rst_msg;

	if (sz <= sizeof(ENUM_WMTRSTMSG_TYPE_T)) {
		memcpy((PINT8)&rst_msg, (PINT8)buf, sz);
		BT_DBG_FUNC("src = %d, dst = %d, type = %d, buf = 0x%x sz = %d, max = %d\n", src,
			     dst, type, rst_msg, sz, WMTRSTMSG_RESET_MAX);
		if ((src == WMTDRV_TYPE_WMT) && (dst == WMTDRV_TYPE_BT)
		    && (type == WMTMSG_TYPE_RESET)) {
			if (rst_msg == WMTRSTMSG_RESET_START) {
				BT_INFO_FUNC("BT reset start!\n");
				rstflag = 1;
				wake_up_interruptible(&inq);

			} else if (rst_msg == WMTRSTMSG_RESET_END) {
				BT_INFO_FUNC("BT reset end!\n");
				rstflag = 2;
				wake_up_interruptible(&inq);
			}
		}
	} else {
		/* Invalid message format */
		BT_WARN_FUNC("Invalid message format!\n");
	}
}

VOID BT_event_cb(VOID)
{
	BT_DBG_FUNC("BT_event_cb()\n");

	flag = 1;

	/*
	* Finally, wake up any reader blocked in poll or read
	*/
	wake_up_interruptible(&inq);
	wake_up(&BT_wq);
}

unsigned int BT_poll(struct file *filp, poll_table *wait)
{
	UINT32 mask = 0;

/* down(&wr_mtx); */
	/*
	 * The buffer is circular; it is considered full
	 * if "wp" is right behind "rp". "left" is 0 if the
	 * buffer is empty, and it is "1" if it is completely full.
	 */
	if (mtk_wcn_stp_is_rxqueue_empty(BT_TASK_INDX)) {
		poll_wait(filp, &inq, wait);

		if (!mtk_wcn_stp_is_rxqueue_empty(BT_TASK_INDX) || rstflag)
			/* BT Rx queue has valid data, or whole chip reset occurs */
			mask |= POLLIN | POLLRDNORM;	/* Readable */
	} else {
		mask |= POLLIN | POLLRDNORM;	/* Readable */
	}

	/* Do we need condition here? */
	mask |= POLLOUT | POLLWRNORM;	/* Writable */
/* up(&wr_mtx); */
	return mask;
}

ssize_t BT_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	INT32 retval = 0;
	INT32 write_size;
	INT32 written = 0;

	down(&wr_mtx);

	BT_DBG_FUNC("%s: count %zd pos %lld\n", __func__, count, *f_pos);
	if (rstflag) {
		if (rstflag == 1) {	/* Reset start */
			retval = -88;
			BT_INFO_FUNC("%s: detect whole chip reset start\n", __func__);
		} else if (rstflag == 2) {	/* Reset end */
			retval = -99;
			BT_INFO_FUNC("%s: detect whole chip reset end\n", __func__);
		}
		goto OUT;
	}

	if (count > 0) {
		if (count < BT_BUFFER_SIZE) {
			write_size = count;
		} else {
			write_size = BT_BUFFER_SIZE;
			BT_ERR_FUNC("%s: count > BT_BUFFER_SIZE\n", __func__);
		}

		if (copy_from_user(&o_buf[0], &buf[0], write_size)) {
			retval = -EFAULT;
			goto OUT;
		}

		written = mtk_wcn_stp_send_data(&o_buf[0], write_size, BT_TASK_INDX);
		if (0 == written) {
			retval = -ENOSPC;
			/* No space is available, native program should not call BT_write with no delay */
			BT_ERR_FUNC
			    ("Packet length %zd, sent length %d, retval = %d\n",
			     count, written, retval);
		} else {
			retval = written;
		}

	} else {
		retval = -EFAULT;
		BT_ERR_FUNC("Packet length %zd is not allowed, retval = %d\n", count, retval);
	}

OUT:
	up(&wr_mtx);
	return retval;
}

ssize_t BT_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	static int chip_reset_count;
	INT32 retval = 0;

	down(&rd_mtx);

	BT_DBG_FUNC("%s: count %zd pos %lld\n", __func__, count, *f_pos);
	if (rstflag) {
		if (rstflag == 1) {	/* Reset start */
			retval = -88;
			if ((chip_reset_count%500) == 0)
				BT_INFO_FUNC("%s: detect whole chip reset start, %d\n", __func__, chip_reset_count);
			chip_reset_count++;
		} else if (rstflag == 2) {	/* Reset end */
			retval = -99;
			BT_INFO_FUNC("%s: detect whole chip reset end\n", __func__);
			chip_reset_count = 0;
		}
		goto OUT;
	}

	if (count > BT_BUFFER_SIZE) {
		count = BT_BUFFER_SIZE;
		BT_ERR_FUNC("%s: count > BT_BUFFER_SIZE\n", __func__);
	}

	retval = mtk_wcn_stp_receive_data(i_buf, count, BT_TASK_INDX);

	while (retval == 0) {	/* Got nothing, wait for STP's signal */
		/*
		* If nonblocking mode, return directly.
		* O_NONBLOCK is specified during open()
		*/
		if (filp->f_flags & O_NONBLOCK) {
			BT_DBG_FUNC("Non-blocking BT_read\n");
			retval = -EAGAIN;
			goto OUT;
		}

		BT_DBG_FUNC("%s: wait_event 1\n", __func__);
		wait_event(BT_wq, flag != 0);
		BT_DBG_FUNC("%s: wait_event 2\n", __func__);
		flag = 0;
		retval = mtk_wcn_stp_receive_data(i_buf, count, BT_TASK_INDX);
		BT_DBG_FUNC("%s: mtk_wcn_stp_receive_data returns %d\n", __func__, retval);
	}

	/* Got something from STP driver */
	if (copy_to_user(buf, i_buf, retval)) {
		retval = -EFAULT;
		goto OUT;
	}

OUT:
	up(&rd_mtx);
	BT_DBG_FUNC("%s: retval = %d\n", __func__, retval);
	return retval;
}

/* int BT_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg) */
long BT_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	INT32 retval = 0;
	MTK_WCN_BOOL bRet = MTK_WCN_BOOL_TRUE;
	ENUM_WMTHWVER_TYPE_T hw_ver_sym = WMTHWVER_INVALID;

	BT_DBG_FUNC("%s:  cmd: 0x%x\n", __func__, cmd);

	switch (cmd) {
	case COMBO_IOC_BT_HWVER:
		/* Get combo HW version */
		hw_ver_sym = mtk_wcn_wmt_hwver_get();
		BT_INFO_FUNC("%s: HW version = %d, sizeof(hw_ver_sym) = %zd\n",
			     __func__, hw_ver_sym, sizeof(hw_ver_sym));
		if (copy_to_user((int __user *)arg, &hw_ver_sym, sizeof(hw_ver_sym)))
			retval = -EFAULT;
		break;

	case COMBO_IOCTL_FW_ASSERT:
		/* Trigger FW assert for debug */
		BT_INFO_FUNC("%s: Host trigger FW assert......, reason:%lu\n", __func__, arg);
		bRet = mtk_wcn_wmt_assert(WMTDRV_TYPE_BT, arg);
		if (bRet == MTK_WCN_BOOL_TRUE) {
			BT_INFO_FUNC("Host trigger FW assert succeed\n");
			retval = 0;
		} else {
			BT_ERR_FUNC("Host trigger FW assert Failed\n");
			retval = (-EBUSY);
		}
		break;
	case COMBO_IOCTL_BT_IC_HW_VER:
		retval = mtk_wcn_wmt_ic_info_get(WMTCHIN_HWVER);
		break;
	case COMBO_IOCTL_BT_IC_FW_VER:
		retval = mtk_wcn_wmt_ic_info_get(WMTCHIN_FWVER);
		break;
	default:
		retval = -EFAULT;
		BT_ERR_FUNC("Unknown cmd (%d)\n", cmd);
		break;
	}

	return retval;
}

long BT_compat_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return BT_unlocked_ioctl(filp, cmd, arg);
}

static int BT_open(struct inode *inode, struct file *file)
{
	BT_INFO_FUNC("%s: major %d minor %d pid %d\n", __func__, imajor(inode), iminor(inode), current->pid);

	/* Turn on BT */
	if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_BT)) {
		BT_WARN_FUNC("WMT turn on BT fail!\n");
		return -ENODEV;
	}

	BT_INFO_FUNC("WMT turn on BT OK!\n");
	rstflag = 0;

	if (mtk_wcn_stp_is_ready()) {

		mtk_wcn_stp_set_bluez(0);

		BT_INFO_FUNC("Now it's in MTK Bluetooth Mode\n");
		BT_INFO_FUNC("STP is ready!\n");

		BT_DBG_FUNC("Register BT event callback!\n");
		mtk_wcn_stp_register_event_cb(BT_TASK_INDX, BT_event_cb);
	} else {
		BT_ERR_FUNC("STP is not ready\n");
		mtk_wcn_wmt_func_off(WMTDRV_TYPE_BT);
		return -ENODEV;
	}

	BT_DBG_FUNC("Register BT reset callback!\n");
	mtk_wcn_wmt_msgcb_reg(WMTDRV_TYPE_BT, bt_cdev_rst_cb);

	/* init_MUTEX(&wr_mtx); */
	sema_init(&wr_mtx, 1);
	/* init_MUTEX(&rd_mtx); */
	sema_init(&rd_mtx, 1);
	BT_INFO_FUNC("%s: finish\n", __func__);

	return 0;
}

static int BT_close(struct inode *inode, struct file *file)
{
	BT_INFO_FUNC("%s: major %d minor %d pid %d\n", __func__, imajor(inode), iminor(inode), current->pid);
	rstflag = 0;
	mtk_wcn_wmt_msgcb_unreg(WMTDRV_TYPE_BT);
	mtk_wcn_stp_register_event_cb(BT_TASK_INDX, NULL);

	if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_off(WMTDRV_TYPE_BT)) {
		BT_ERR_FUNC("WMT turn off BT fail!\n");
		return -EIO;	/* Mostly, native program will not check this return value. */
	}

	BT_INFO_FUNC("WMT turn off BT OK!\n");

	return 0;
}

const struct file_operations BT_fops = {
	.open = BT_open,
	.release = BT_close,
	.read = BT_read,
	.write = BT_write,
	/* .ioctl = BT_ioctl, */
	.unlocked_ioctl = BT_unlocked_ioctl,
	.compat_ioctl = BT_compat_ioctl,
	.poll = BT_poll
};



static int BT_init(void)
{
	dev_t dev = MKDEV(BT_major, 0);
	INT32 alloc_ret = 0;
	INT32 cdev_err = 0;

	/* Static allocate char device */
	alloc_ret = register_chrdev_region(dev, 1, BT_DRIVER_NAME);
	if (alloc_ret) {
		BT_ERR_FUNC("%s: Failed to register char device\n", __func__);
		return alloc_ret;
	}

	cdev_init(&BT_cdev, &BT_fops);
	BT_cdev.owner = THIS_MODULE;

	cdev_err = cdev_add(&BT_cdev, dev, BT_devs);
	if (cdev_err)
		goto error;

#if WMT_CREATE_NODE_DYNAMIC
	stpbt_class = class_create(THIS_MODULE, "stpbt");
	if (IS_ERR(stpbt_class))
		goto error;
	stpbt_dev = device_create(stpbt_class, NULL, dev, NULL, "stpbt");
	if (IS_ERR(stpbt_dev))
		goto error;
#endif

	BT_INFO_FUNC("%s driver(major %d) installed\n", BT_DRIVER_NAME, BT_major);

	/* Init wait queue */
	init_waitqueue_head(&(inq));

#ifdef MTK_BT_HCI
 	mtk_bt_hci_init();
#endif

	return 0;

error:
#if WMT_CREATE_NODE_DYNAMIC
	if (!IS_ERR(stpbt_dev))
		device_destroy(stpbt_class, dev);
	if (!IS_ERR(stpbt_class)) {
		class_destroy(stpbt_class);
		stpbt_class = NULL;
	}
#endif
	if (cdev_err == 0)
		cdev_del(&BT_cdev);

	if (alloc_ret == 0)
		unregister_chrdev_region(dev, BT_devs);

	return -1;
}

static void BT_exit(void)
{
	dev_t dev = MKDEV(BT_major, 0);

#if WMT_CREATE_NODE_DYNAMIC
	if (stpbt_dev) {
		device_destroy(stpbt_class, dev);
		stpbt_dev = NULL;
	}
	if (stpbt_class) {
		class_destroy(stpbt_class);
		stpbt_class = NULL;
	}
#endif

	cdev_del(&BT_cdev);
	unregister_chrdev_region(dev, BT_devs);

	BT_INFO_FUNC("%s driver removed\n", BT_DRIVER_NAME);
}

#ifdef MTK_WCN_REMOVE_KERNEL_MODULE

int mtk_wcn_stpbt_drv_init(void)
{
	return BT_init();
}
EXPORT_SYMBOL(mtk_wcn_stpbt_drv_init);

void mtk_wcn_stpbt_drv_exit(void)
{
	return BT_exit();
}
EXPORT_SYMBOL(mtk_wcn_stpbt_drv_exit);

#else

module_init(BT_init);
module_exit(BT_exit);

#endif
