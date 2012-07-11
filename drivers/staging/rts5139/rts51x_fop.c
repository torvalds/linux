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
 * Maintainer:
 *   Edwin Rong (edwin_rong@realsil.com.cn)
 *   No. 450, Shenhu Road, Suzhou Industry Park, Suzhou, China
 */

#include "rts51x.h"

#ifdef SUPPORT_FILE_OP

#include <linux/types.h>
#include <linux/stat.h>
#include <linux/kref.h>
#include <linux/slab.h>

#include "rts51x_chip.h"
#include "rts51x_card.h"
#include "rts51x_fop.h"
#include "sd_cprm.h"
#include "rts51x.h"

#define RTS5139_IOC_MAGIC		0x39

#define RTS5139_IOC_SD_DIRECT		_IOWR(RTS5139_IOC_MAGIC, 0xA0, int)
#define RTS5139_IOC_SD_GET_RSP		_IOWR(RTS5139_IOC_MAGIC, 0xA1, int)

static int rts51x_sd_direct_cmnd(struct rts51x_chip *chip,
				 struct sd_direct_cmnd *cmnd)
{
	int retval;
	u8 dir, cmd12, standby, acmd, cmd_idx, rsp_code;
	u8 *buf;
	u32 arg, len;

	dir = (cmnd->cmnd[0] >> 3) & 0x03;
	cmd12 = (cmnd->cmnd[0] >> 2) & 0x01;
	standby = (cmnd->cmnd[0] >> 1) & 0x01;
	acmd = cmnd->cmnd[0] & 0x01;
	cmd_idx = cmnd->cmnd[1];
	arg = ((u32) (cmnd->cmnd[2]) << 24) | ((u32) (cmnd->cmnd[3]) << 16) |
	    ((u32) (cmnd->cmnd[4]) << 8) | cmnd->cmnd[5];
	len =
	    ((u32) (cmnd->cmnd[6]) << 16) | ((u32) (cmnd->cmnd[7]) << 8) |
	    cmnd->cmnd[8];
	rsp_code = cmnd->cmnd[9];

	if (dir) {
		if (!cmnd->buf || (cmnd->buf_len < len))
			TRACE_RET(chip, STATUS_FAIL);
	}

	switch (dir) {
	case 0:
		/* No data */
		retval = ext_sd_execute_no_data(chip, chip->card2lun[SD_CARD],
						cmd_idx, standby, acmd,
						rsp_code, arg);
		if (retval != TRANSPORT_GOOD)
			TRACE_RET(chip, STATUS_FAIL);
		break;

	case 1:
		/* Read from card */
		buf = kmalloc(cmnd->buf_len, GFP_KERNEL);
		if (!buf)
			TRACE_RET(chip, STATUS_NOMEM);

		retval = ext_sd_execute_read_data(chip, chip->card2lun[SD_CARD],
						  cmd_idx, cmd12, standby, acmd,
						  rsp_code, arg, len, buf,
						  cmnd->buf_len, 0);
		if (retval != TRANSPORT_GOOD) {
			kfree(buf);
			TRACE_RET(chip, STATUS_FAIL);
		}

		retval =
		    copy_to_user((void *)cmnd->buf, (void *)buf, cmnd->buf_len);
		if (retval) {
			kfree(buf);
			TRACE_RET(chip, STATUS_NOMEM);
		}

		kfree(buf);
		break;

	case 2:
		/* Write to card */
		buf = kmalloc(cmnd->buf_len, GFP_KERNEL);
		if (!buf)
			TRACE_RET(chip, STATUS_NOMEM);

		retval =
		    copy_from_user((void *)buf, (void *)cmnd->buf,
				   cmnd->buf_len);
		if (retval) {
			kfree(buf);
			TRACE_RET(chip, STATUS_NOMEM);
		}

		retval =
		    ext_sd_execute_write_data(chip, chip->card2lun[SD_CARD],
					      cmd_idx, cmd12, standby, acmd,
					      rsp_code, arg, len, buf,
					      cmnd->buf_len, 0);
		if (retval != TRANSPORT_GOOD) {
			kfree(buf);
			TRACE_RET(chip, STATUS_FAIL);
		}

		kfree(buf);

		break;

	default:
		TRACE_RET(chip, STATUS_FAIL);
	}

	return STATUS_SUCCESS;
}

static int rts51x_sd_get_rsp(struct rts51x_chip *chip, struct sd_rsp *rsp)
{
	struct sd_info *sd_card = &(chip->sd_card);
	int count = 0, retval;

	if (sd_card->pre_cmd_err) {
		sd_card->pre_cmd_err = 0;
		TRACE_RET(chip, STATUS_FAIL);
	}

	if (sd_card->last_rsp_type == SD_RSP_TYPE_R0)
		TRACE_RET(chip, STATUS_FAIL);
	else if (sd_card->last_rsp_type == SD_RSP_TYPE_R2)
		count = (rsp->rsp_len < 17) ? rsp->rsp_len : 17;
	else
		count = (rsp->rsp_len < 6) ? rsp->rsp_len : 6;

	retval = copy_to_user((void *)rsp->rsp, (void *)sd_card->rsp, count);
	if (retval)
		TRACE_RET(chip, STATUS_NOMEM);

	RTS51X_DEBUGP("Response length: %d\n", count);
	RTS51X_DEBUGP("Response: 0x%x 0x%x 0x%x 0x%x\n",
		       sd_card->rsp[0], sd_card->rsp[1], sd_card->rsp[2],
		       sd_card->rsp[3]);

	return STATUS_SUCCESS;
}

int rts51x_open(struct inode *inode, struct file *filp)
{
	struct rts51x_chip *chip;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	subminor = iminor(inode);

	interface = usb_find_interface(&rts51x_driver, subminor);
	if (!interface) {
		RTS51X_DEBUGP("%s - error, can't find device for minor %d\n",
			       __func__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	chip = (struct rts51x_chip *)usb_get_intfdata(interface);
	if (!chip) {
		RTS51X_DEBUGP("Can't find chip\n");
		retval = -ENODEV;
		goto exit;
	}

	/* Increase our reference to the host */
	scsi_host_get(rts51x_to_host(chip));

	/* lock the device pointers */
	mutex_lock(&(chip->usb->dev_mutex));

	/* save our object in the file's private structure */
	filp->private_data = chip;

	/* unlock the device pointers */
	mutex_unlock(&chip->usb->dev_mutex);

exit:
	return retval;
}

int rts51x_release(struct inode *inode, struct file *filp)
{
	struct rts51x_chip *chip;

	chip = (struct rts51x_chip *)filp->private_data;
	if (chip == NULL)
		return -ENODEV;

	/* Drop our reference to the host; the SCSI core will free it
	 * (and "chip" along with it) when the refcount becomes 0. */
	scsi_host_put(rts51x_to_host(chip));

	return 0;
}

ssize_t rts51x_read(struct file *filp, char __user *buf, size_t count,
		    loff_t *f_pos)
{
	return 0;
}

ssize_t rts51x_write(struct file *filp, const char __user *buf, size_t count,
		     loff_t *f_pos)
{
	return 0;
}

long rts51x_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct rts51x_chip *chip;
	struct sd_direct_cmnd cmnd;
	struct sd_rsp rsp;
	int retval = 0;

	chip = (struct rts51x_chip *)filp->private_data;
	if (chip == NULL)
		return -ENODEV;

	/* lock the device pointers */
	mutex_lock(&(chip->usb->dev_mutex));

	switch (cmd) {
	case RTS5139_IOC_SD_DIRECT:
		retval =
		    copy_from_user((void *)&cmnd, (void *)arg,
				   sizeof(struct sd_direct_cmnd));
		if (retval) {
			retval = -ENOMEM;
			TRACE_GOTO(chip, exit);
		}
		retval = rts51x_sd_direct_cmnd(chip, &cmnd);
		if (retval != STATUS_SUCCESS) {
			retval = -EIO;
			TRACE_GOTO(chip, exit);
		}
		break;

	case RTS5139_IOC_SD_GET_RSP:
		retval =
		    copy_from_user((void *)&rsp, (void *)arg,
				   sizeof(struct sd_rsp));
		if (retval) {
			retval = -ENOMEM;
			TRACE_GOTO(chip, exit);
		}
		retval = rts51x_sd_get_rsp(chip, &rsp);
		if (retval != STATUS_SUCCESS) {
			retval = -EIO;
			TRACE_GOTO(chip, exit);
		}
		break;

	default:
		break;
	}

exit:
	/* unlock the device pointers */
	mutex_unlock(&chip->usb->dev_mutex);

	return retval;
}

#endif
