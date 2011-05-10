/* Driver for Realtek RTS51xx USB card reader
 *
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * Author:
 *   wwang (wei_wang@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/version.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <linux/cdrom.h>

#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/usb_usual.h>

#include "usb.h"
#include "transport.h"
#include "protocol.h"
#include "debug.h"

MODULE_DESCRIPTION("Driver for Realtek USB Card Reader");
MODULE_AUTHOR("wwang <wei_wang@realsil.com.cn>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.03");

static int auto_delink_en = 1;
module_param(auto_delink_en, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(auto_delink_en, "enable auto delink");

struct rts51x_status {
	u16 vid;
	u16 pid;
	u8 cur_lun;
	u8 card_type;
	u8 total_lun;
	u16 fw_ver;
	u8 phy_exist;
	u8 multi_flag;
	u8 multi_card;
	u8 log_exist;
	union {
		u8 detailed_type1;
		u8 detailed_type2;
	} detailed_type;
	u8 function[2];
};

struct rts51x_chip {
	u16			vendor_id;
	u16			product_id;
	char			max_lun;

	struct rts51x_status	*status;
	int			status_len;

	u32			flag;
};

/* flag definition */
#define FLIDX_AUTO_DELINK		0x01

#define SCSI_LUN(srb)			((srb)->device->lun)

/* Bit Operation */
#define SET_BIT(data, idx)		((data) |= 1 << (idx))
#define CLR_BIT(data, idx)		((data) &= ~(1 << (idx)))
#define CHK_BIT(data, idx)		((data) & (1 << (idx)))

#define SET_AUTO_DELINK(chip)		((chip)->flag |= FLIDX_AUTO_DELINK)
#define CLR_AUTO_DELINK(chip)		((chip)->flag &= ~FLIDX_AUTO_DELINK)
#define CHK_AUTO_DELINK(chip)		((chip)->flag & FLIDX_AUTO_DELINK)

#define RTS51X_GET_VID(chip)		((chip)->vendor_id)
#define RTS51X_GET_PID(chip)		((chip)->product_id)

#define FW_VERSION(chip)		((chip)->status[0].fw_ver)
#define STATUS_LEN(chip)		((chip)->status_len)

/* Check card reader function */
#define SUPPORT_DETAILED_TYPE1(chip)	\
		CHK_BIT((chip)->status[0].function[0], 1)
#define SUPPORT_OT(chip)		\
		CHK_BIT((chip)->status[0].function[0], 2)
#define SUPPORT_OC(chip)		\
		CHK_BIT((chip)->status[0].function[0], 3)
#define SUPPORT_AUTO_DELINK(chip)	\
		CHK_BIT((chip)->status[0].function[0], 4)
#define SUPPORT_SDIO(chip)		\
		CHK_BIT((chip)->status[0].function[1], 0)
#define SUPPORT_DETAILED_TYPE2(chip)	\
		CHK_BIT((chip)->status[0].function[1], 1)

#define CHECK_PID(chip, pid)		(RTS51X_GET_PID(chip) == (pid))
#define CHECK_FW_VER(chip, fw_ver)	(FW_VERSION(chip) == (fw_ver))
#define CHECK_ID(chip, pid, fw_ver)	\
		(CHECK_PID((chip), (pid)) && CHECK_FW_VER((chip), (fw_ver)))

#define wait_timeout_x(task_state, msecs)	\
do {						\
	set_current_state((task_state));	\
	schedule_timeout((msecs) * HZ / 1000);	\
} while (0)

#define wait_timeout(msecs)		\
		wait_timeout_x(TASK_INTERRUPTIBLE, (msecs))

static int init_realtek_cr(struct us_data *us);

/*
 * The table of devices
 */
#define UNUSUAL_DEV(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax, \
		    vendorName, productName, useProtocol, useTransport, \
		    initFunction, flags) \
{\
	USB_DEVICE_VER(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax), \
	.driver_info = (flags)|(USB_US_TYPE_STOR<<24)\
}

static const struct usb_device_id realtek_cr_ids[] = {
#	include "unusual_realtek.h"
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, realtek_cr_ids);

#undef UNUSUAL_DEV

/*
 * The flags table
 */
#define UNUSUAL_DEV(idVendor, idProduct, bcdDeviceMin, bcdDeviceMax, \
		    vendor_name, product_name, use_protocol, use_transport, \
		    init_function, Flags) \
{ \
	.vendorName = vendor_name,	\
	.productName = product_name,	\
	.useProtocol = use_protocol,	\
	.useTransport = use_transport,	\
	.initFunction = init_function,	\
}

static struct us_unusual_dev realtek_cr_unusual_dev_list[] = {
#	include "unusual_realtek.h"
	{ }		/* Terminating entry */
};

#undef UNUSUAL_DEV

static int rts51x_bulk_transport(struct us_data *us, u8 lun,
				 u8 *cmd, int cmd_len, u8 *buf, int buf_len,
				 enum dma_data_direction dir, int *act_len)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct bulk_cs_wrap *bcs = (struct bulk_cs_wrap *) us->iobuf;
	int result;
	unsigned int residue;
	unsigned int cswlen;
	unsigned int cbwlen = US_BULK_CB_WRAP_LEN;

	/* set up the command wrapper */
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = cpu_to_le32(buf_len);
	bcb->Flags = (dir == DMA_FROM_DEVICE) ? 1 << 7 : 0;
	bcb->Tag = ++us->tag;
	bcb->Lun = lun;
	bcb->Length = cmd_len;

	/* copy the command payload */
	memset(bcb->CDB, 0, sizeof(bcb->CDB));
	memcpy(bcb->CDB, cmd, bcb->Length);

	/* send it to out endpoint */
	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
				bcb, cbwlen, NULL);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* DATA STAGE */
	/* send/receive data payload, if there is any */

	if (buf && buf_len) {
		unsigned int pipe = (dir == DMA_FROM_DEVICE) ?
				us->recv_bulk_pipe : us->send_bulk_pipe;
		result = usb_stor_bulk_transfer_buf(us, pipe,
				buf, buf_len, NULL);
		if (result == USB_STOR_XFER_ERROR)
			return USB_STOR_TRANSPORT_ERROR;
	}

	/* get CSW for device status */
	result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
				bcs, US_BULK_CS_WRAP_LEN, &cswlen);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* check bulk status */
	if (bcs->Signature != cpu_to_le32(US_BULK_CS_SIGN)) {
		US_DEBUGP("Signature mismatch: got %08X, expecting %08X\n",
			  le32_to_cpu(bcs->Signature),
			  US_BULK_CS_SIGN);
		return USB_STOR_TRANSPORT_ERROR;
	}

	residue = bcs->Residue;
	if (bcs->Tag != us->tag)
		return USB_STOR_TRANSPORT_ERROR;

	/* try to compute the actual residue, based on how much data
	 * was really transferred and what the device tells us */
	if (residue)
		residue = residue < buf_len ? residue : buf_len;

	if (act_len)
		*act_len = buf_len - residue;

	/* based on the status code, we report good or bad */
	switch (bcs->Status) {
	case US_BULK_STAT_OK:
		/* command good -- note that data could be short */
		return USB_STOR_TRANSPORT_GOOD;

	case US_BULK_STAT_FAIL:
		/* command failed */
		return USB_STOR_TRANSPORT_FAILED;

	case US_BULK_STAT_PHASE:
		/* phase error -- note that a transport reset will be
			* invoked by the invoke_transport() function
			*/
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* we should never get here, but if we do, we're in trouble */
	return USB_STOR_TRANSPORT_ERROR;
}

/* Determine what the maximum LUN supported is */
static int rts51x_get_max_lun(struct us_data *us)
{
	int result;

	/* issue the command */
	us->iobuf[0] = 0;
	result = usb_stor_control_msg(us, us->recv_ctrl_pipe,
				 US_BULK_GET_MAX_LUN,
				 USB_DIR_IN | USB_TYPE_CLASS |
				 USB_RECIP_INTERFACE,
				 0, us->ifnum, us->iobuf, 1, 10*HZ);

	US_DEBUGP("GetMaxLUN command result is %d, data is %d\n",
		  result, us->iobuf[0]);

	/* if we have a successful request, return the result */
	if (result > 0)
		return us->iobuf[0];

	return 0;
}

static int rts51x_read_mem(struct us_data *us, u16 addr, u8 *data, u16 len)
{
	int retval;
	u8 cmnd[12] = {0};

	US_DEBUGP("%s, addr = 0x%x, len = %d\n", __func__, addr, len);

	cmnd[0] = 0xF0;
	cmnd[1] = 0x0D;
	cmnd[2] = (u8)(addr >> 8);
	cmnd[3] = (u8)addr;
	cmnd[4] = (u8)(len >> 8);
	cmnd[5] = (u8)len;

	retval = rts51x_bulk_transport(us, 0, cmnd, 12,
				       data, len, DMA_FROM_DEVICE, NULL);
	if (retval != USB_STOR_TRANSPORT_GOOD)
		return -EIO;

	return 0;
}

static int rts51x_write_mem(struct us_data *us, u16 addr, u8 *data, u16 len)
{
	int retval;
	u8 cmnd[12] = {0};

	US_DEBUGP("%s, addr = 0x%x, len = %d\n", __func__, addr, len);

	cmnd[0] = 0xF0;
	cmnd[1] = 0x0E;
	cmnd[2] = (u8)(addr >> 8);
	cmnd[3] = (u8)addr;
	cmnd[4] = (u8)(len >> 8);
	cmnd[5] = (u8)len;

	retval = rts51x_bulk_transport(us, 0, cmnd, 12,
				       data, len, DMA_TO_DEVICE, NULL);
	if (retval != USB_STOR_TRANSPORT_GOOD)
		return -EIO;

	return 0;
}

static int rts51x_read_status(struct us_data *us,
			      u8 lun, u8 *status, int len, int *actlen)
{
	int retval;
	u8 cmnd[12] = {0};

	US_DEBUGP("%s, lun = %d\n", __func__, lun);

	cmnd[0] = 0xF0;
	cmnd[1] = 0x09;

	retval = rts51x_bulk_transport(us, lun, cmnd, 12,
				       status, len, DMA_FROM_DEVICE, actlen);
	if (retval != USB_STOR_TRANSPORT_GOOD)
		return -EIO;

	return 0;
}

static int rts51x_check_status(struct us_data *us, u8 lun)
{
	struct rts51x_chip *chip = (struct rts51x_chip *)(us->extra);
	int retval;
	u8 buf[16];

	retval = rts51x_read_status(us, lun, buf, 16, &(chip->status_len));
	if (retval < 0)
		return -EIO;

	US_DEBUGP("chip->status_len = %d\n", chip->status_len);

	chip->status[lun].vid = ((u16)buf[0] << 8) | buf[1];
	chip->status[lun].pid = ((u16)buf[2] << 8) | buf[3];
	chip->status[lun].cur_lun = buf[4];
	chip->status[lun].card_type = buf[5];
	chip->status[lun].total_lun = buf[6];
	chip->status[lun].fw_ver = ((u16)buf[7] << 8) | buf[8];
	chip->status[lun].phy_exist = buf[9];
	chip->status[lun].multi_flag = buf[10];
	chip->status[lun].multi_card = buf[11];
	chip->status[lun].log_exist = buf[12];
	if (chip->status_len == 16) {
		chip->status[lun].detailed_type.detailed_type1 = buf[13];
		chip->status[lun].function[0] = buf[14];
		chip->status[lun].function[1] = buf[15];
	}

	return 0;
}

static int enable_oscillator(struct us_data *us)
{
	int retval;
	u8 value;

	retval = rts51x_read_mem(us, 0xFE77, &value, 1);
	if (retval < 0)
		return -EIO;

	value |= 0x04;
	retval = rts51x_write_mem(us, 0xFE77, &value, 1);
	if (retval < 0)
		return -EIO;

	retval = rts51x_read_mem(us, 0xFE77, &value, 1);
	if (retval < 0)
		return -EIO;

	if (!(value & 0x04))
		return -EIO;

	return 0;
}

static int do_config_autodelink(struct us_data *us, int enable, int force)
{
	int retval;
	u8 value;

	retval = rts51x_read_mem(us, 0xFE47, &value, 1);
	if (retval < 0)
		return -EIO;

	if (enable) {
		if (force)
			value |= 0x03;
		else
			value |= 0x01;
	} else {
		value &= ~0x03;
	}

	US_DEBUGP("In %s,set 0xfe47 to 0x%x\n", __func__, value);

	retval = rts51x_write_mem(us, 0xFE47, &value, 1);
	if (retval < 0)
		return -EIO;

	return 0;
}

static int config_autodelink_after_power_on(struct us_data *us)
{
	struct rts51x_chip *chip = (struct rts51x_chip *)(us->extra);
	int retval;
	u8 value;

	if (!CHK_AUTO_DELINK(chip))
		return 0;

	retval = rts51x_read_mem(us, 0xFE47, &value, 1);
	if (retval < 0)
		return -EIO;

	if (auto_delink_en) {
		CLR_BIT(value, 0);
		CLR_BIT(value, 1);
		SET_BIT(value, 2);

		if (CHECK_ID(chip, 0x0138, 0x3882))
			CLR_BIT(value, 2);

		SET_BIT(value, 7);

		retval = rts51x_write_mem(us, 0xFE47, &value, 1);
		if (retval < 0)
			return -EIO;

		retval = enable_oscillator(us);
		if (retval == 0)
			(void)do_config_autodelink(us, 1, 0);
	} else {
		/* Autodelink controlled by firmware */

		SET_BIT(value, 2);

		if (CHECK_ID(chip, 0x0138, 0x3882))
			CLR_BIT(value, 2);

		if (CHECK_ID(chip, 0x0159, 0x5889) ||
				CHECK_ID(chip, 0x0138, 0x3880)) {
			CLR_BIT(value, 0);
			CLR_BIT(value, 7);
		}

		retval = rts51x_write_mem(us, 0xFE47, &value, 1);
		if (retval < 0)
			return -EIO;

		if (CHECK_ID(chip, 0x0159, 0x5888)) {
			value = 0xFF;
			retval = rts51x_write_mem(us, 0xFE79, &value, 1);
			if (retval < 0)
				return -EIO;

			value = 0x01;
			retval = rts51x_write_mem(us, 0x48, &value, 1);
			if (retval < 0)
				return -EIO;
		}
	}

	return 0;
}

static int config_autodelink_before_power_down(struct us_data *us)
{
	struct rts51x_chip *chip = (struct rts51x_chip *)(us->extra);
	int retval;
	u8 value;

	if (!CHK_AUTO_DELINK(chip))
		return 0;

	if (auto_delink_en) {
		retval = rts51x_read_mem(us, 0xFE77, &value, 1);
		if (retval < 0)
			return -EIO;

		SET_BIT(value, 2);
		retval = rts51x_write_mem(us, 0xFE77, &value, 1);
		if (retval < 0)
			return -EIO;

		if (CHECK_ID(chip, 0x0159, 0x5888)) {
			value = 0x01;
			retval = rts51x_write_mem(us, 0x48, &value, 1);
			if (retval < 0)
				return -EIO;
		}

		retval = rts51x_read_mem(us, 0xFE47, &value, 1);
		if (retval < 0)
			return -EIO;

		SET_BIT(value, 0);
		if (CHECK_ID(chip, 0x0138, 0x3882))
			SET_BIT(value, 2);
		retval = rts51x_write_mem(us, 0xFE77, &value, 1);
		if (retval < 0)
			return -EIO;
	} else {
		if (CHECK_ID(chip, 0x0159, 0x5889) ||
				CHECK_ID(chip, 0x0138, 0x3880) ||
				CHECK_ID(chip, 0x0138, 0x3882)) {
			retval = rts51x_read_mem(us, 0xFE47, &value, 1);
			if (retval < 0)
				return -EIO;

			if (CHECK_ID(chip, 0x0159, 0x5889) ||
					CHECK_ID(chip, 0x0138, 0x3880)) {
				SET_BIT(value, 0);
				SET_BIT(value, 7);
			}

			if (CHECK_ID(chip, 0x0138, 0x3882))
				SET_BIT(value, 2);

			retval = rts51x_write_mem(us, 0xFE47, &value, 1);
			if (retval < 0)
				return -EIO;
		}

		if (CHECK_ID(chip, 0x0159, 0x5888)) {
			value = 0x01;
			retval = rts51x_write_mem(us, 0x48, &value, 1);
			if (retval < 0)
				return -EIO;
		}
	}

	return 0;
}

static void realtek_cr_destructor(void *extra)
{
	struct rts51x_chip *chip = (struct rts51x_chip *)extra;

	if (!chip)
		return;

	kfree(chip->status);
}

#ifdef CONFIG_PM
static void realtek_pm_hook(struct us_data *us, int pm_state)
{
	if (pm_state == US_SUSPEND)
		(void)config_autodelink_before_power_down(us);
}
#endif

static int init_realtek_cr(struct us_data *us)
{
	struct rts51x_chip *chip;
	int size, i, retval;

	chip = kzalloc(sizeof(struct rts51x_chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	us->extra = chip;
	us->extra_destructor = realtek_cr_destructor;
#ifdef CONFIG_PM
	us->suspend_resume_hook = realtek_pm_hook;
#endif

	us->max_lun = chip->max_lun = rts51x_get_max_lun(us);

	US_DEBUGP("chip->max_lun = %d\n", chip->max_lun);

	size = (chip->max_lun + 1) * sizeof(struct rts51x_status);
	chip->status = kzalloc(size, GFP_KERNEL);
	if (!chip->status)
		goto INIT_FAIL;

	for (i = 0; i <= (int)(chip->max_lun); i++) {
		retval = rts51x_check_status(us, (u8)i);
		if (retval < 0)
			goto INIT_FAIL;
	}

	if (CHECK_FW_VER(chip, 0x5888) || CHECK_FW_VER(chip, 0x5889) ||
			CHECK_FW_VER(chip, 0x5901))
		SET_AUTO_DELINK(chip);
	if (STATUS_LEN(chip) == 16) {
		if (SUPPORT_AUTO_DELINK(chip))
			SET_AUTO_DELINK(chip);
	}

	US_DEBUGP("chip->flag = 0x%x\n", chip->flag);

	(void)config_autodelink_after_power_on(us);

	return 0;

INIT_FAIL:
	if (us->extra) {
		kfree(chip->status);
		kfree(us->extra);
		us->extra = NULL;
	}

	return -EIO;
}

static int realtek_cr_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct us_data *us;
	int result;

	US_DEBUGP("Probe Realtek Card Reader!\n");

	result = usb_stor_probe1(&us, intf, id,
			(id - realtek_cr_ids) + realtek_cr_unusual_dev_list);
	if (result)
		return result;

	result = usb_stor_probe2(us);
	return result;
}

static struct usb_driver realtek_cr_driver = {
	.name =		"ums-realtek",
	.probe =	realtek_cr_probe,
	.disconnect =	usb_stor_disconnect,
	.suspend =	usb_stor_suspend,
	.resume =	usb_stor_resume,
	.reset_resume =	usb_stor_reset_resume,
	.pre_reset =	usb_stor_pre_reset,
	.post_reset =	usb_stor_post_reset,
	.id_table =	realtek_cr_ids,
	.soft_unbind =	1,
};

static int __init realtek_cr_init(void)
{
	return usb_register(&realtek_cr_driver);
}

static void __exit realtek_cr_exit(void)
{
	usb_deregister(&realtek_cr_driver);
}

module_init(realtek_cr_init);
module_exit(realtek_cr_exit);
