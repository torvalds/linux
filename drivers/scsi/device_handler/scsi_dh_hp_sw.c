/*
 * Basic HP/COMPAQ MSA 1000 support. This is only needed if your HW cannot be
 * upgraded.
 *
 * Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
 * Copyright (C) 2006 Mike Christie
 * Copyright (C) 2008 Hannes Reinecke <hare@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <scsi/scsi.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_dh.h>

#define HP_SW_NAME			"hp_sw"

#define HP_SW_TIMEOUT			(60 * HZ)
#define HP_SW_RETRIES			3

#define HP_SW_PATH_UNINITIALIZED	-1
#define HP_SW_PATH_ACTIVE		0
#define HP_SW_PATH_PASSIVE		1

struct hp_sw_dh_data {
	int path_state;
	int retries;
	int retry_cnt;
	struct scsi_device *sdev;
};

static int hp_sw_start_stop(struct hp_sw_dh_data *);

/*
 * tur_done - Handle TEST UNIT READY return status
 * @sdev: sdev the command has been sent to
 * @errors: blk error code
 *
 * Returns SCSI_DH_DEV_OFFLINED if the sdev is on the passive path
 */
static int tur_done(struct scsi_device *sdev, struct hp_sw_dh_data *h,
		    struct scsi_sense_hdr *sshdr)
{
	int ret = SCSI_DH_IO;

	switch (sshdr->sense_key) {
	case UNIT_ATTENTION:
		ret = SCSI_DH_IMM_RETRY;
		break;
	case NOT_READY:
		if (sshdr->asc == 0x04 && sshdr->ascq == 2) {
			/*
			 * LUN not ready - Initialization command required
			 *
			 * This is the passive path
			 */
			h->path_state = HP_SW_PATH_PASSIVE;
			ret = SCSI_DH_OK;
			break;
		}
		/* Fallthrough */
	default:
		sdev_printk(KERN_WARNING, sdev,
			   "%s: sending tur failed, sense %x/%x/%x\n",
			   HP_SW_NAME, sshdr->sense_key, sshdr->asc,
			   sshdr->ascq);
		break;
	}
	return ret;
}

/*
 * hp_sw_tur - Send TEST UNIT READY
 * @sdev: sdev command should be sent to
 *
 * Use the TEST UNIT READY command to determine
 * the path state.
 */
static int hp_sw_tur(struct scsi_device *sdev, struct hp_sw_dh_data *h)
{
	unsigned char cmd[6] = { TEST_UNIT_READY };
	struct scsi_sense_hdr sshdr;
	int ret = SCSI_DH_OK, res;
	u64 req_flags = REQ_FAILFAST_DEV | REQ_FAILFAST_TRANSPORT |
		REQ_FAILFAST_DRIVER;

retry:
	res = scsi_execute_req_flags(sdev, cmd, DMA_NONE, NULL, 0, &sshdr,
				     HP_SW_TIMEOUT, HP_SW_RETRIES,
				     NULL, req_flags, 0);
	if (res) {
		if (scsi_sense_valid(&sshdr))
			ret = tur_done(sdev, h, &sshdr);
		else {
			sdev_printk(KERN_WARNING, sdev,
				    "%s: sending tur failed with %x\n",
				    HP_SW_NAME, res);
			ret = SCSI_DH_IO;
		}
	} else {
		h->path_state = HP_SW_PATH_ACTIVE;
		ret = SCSI_DH_OK;
	}
	if (ret == SCSI_DH_IMM_RETRY)
		goto retry;

	return ret;
}

/*
 * hp_sw_start_stop - Send START STOP UNIT command
 * @sdev: sdev command should be sent to
 *
 * Sending START STOP UNIT activates the SP.
 */
static int hp_sw_start_stop(struct hp_sw_dh_data *h)
{
	unsigned char cmd[6] = { START_STOP, 0, 0, 0, 1, 0 };
	struct scsi_sense_hdr sshdr;
	struct scsi_device *sdev = h->sdev;
	int res, rc = SCSI_DH_OK;
	int retry_cnt = HP_SW_RETRIES;
	u64 req_flags = REQ_FAILFAST_DEV | REQ_FAILFAST_TRANSPORT |
		REQ_FAILFAST_DRIVER;

retry:
	res = scsi_execute_req_flags(sdev, cmd, DMA_NONE, NULL, 0, &sshdr,
				     HP_SW_TIMEOUT, HP_SW_RETRIES,
				     NULL, req_flags, 0);
	if (res) {
		if (!scsi_sense_valid(&sshdr)) {
			sdev_printk(KERN_WARNING, sdev,
				    "%s: sending start_stop_unit failed, "
				    "no sense available\n", HP_SW_NAME);
			return SCSI_DH_IO;
		}
		switch (sshdr.sense_key) {
		case NOT_READY:
			if (sshdr.asc == 0x04 && sshdr.ascq == 3) {
				/*
				 * LUN not ready - manual intervention required
				 *
				 * Switch-over in progress, retry.
				 */
				if (--retry_cnt)
					goto retry;
				rc = SCSI_DH_RETRY;
				break;
			}
			/* fall through */
		default:
			sdev_printk(KERN_WARNING, sdev,
				    "%s: sending start_stop_unit failed, "
				    "sense %x/%x/%x\n", HP_SW_NAME,
				    sshdr.sense_key, sshdr.asc, sshdr.ascq);
			rc = SCSI_DH_IO;
		}
	}
	return rc;
}

static int hp_sw_prep_fn(struct scsi_device *sdev, struct request *req)
{
	struct hp_sw_dh_data *h = sdev->handler_data;
	int ret = BLKPREP_OK;

	if (h->path_state != HP_SW_PATH_ACTIVE) {
		ret = BLKPREP_KILL;
		req->rq_flags |= RQF_QUIET;
	}
	return ret;

}

/*
 * hp_sw_activate - Activate a path
 * @sdev: sdev on the path to be activated
 *
 * The HP Active/Passive firmware is pretty simple;
 * the passive path reports NOT READY with sense codes
 * 0x04/0x02; a START STOP UNIT command will then
 * activate the passive path (and deactivate the
 * previously active one).
 */
static int hp_sw_activate(struct scsi_device *sdev,
				activate_complete fn, void *data)
{
	int ret = SCSI_DH_OK;
	struct hp_sw_dh_data *h = sdev->handler_data;

	ret = hp_sw_tur(sdev, h);

	if (ret == SCSI_DH_OK && h->path_state == HP_SW_PATH_PASSIVE)
		ret = hp_sw_start_stop(h);

	if (fn)
		fn(data, ret);
	return 0;
}

static int hp_sw_bus_attach(struct scsi_device *sdev)
{
	struct hp_sw_dh_data *h;
	int ret;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return -ENOMEM;
	h->path_state = HP_SW_PATH_UNINITIALIZED;
	h->retries = HP_SW_RETRIES;
	h->sdev = sdev;

	ret = hp_sw_tur(sdev, h);
	if (ret != SCSI_DH_OK || h->path_state == HP_SW_PATH_UNINITIALIZED)
		goto failed;

	sdev_printk(KERN_INFO, sdev, "%s: attached to %s path\n",
		    HP_SW_NAME, h->path_state == HP_SW_PATH_ACTIVE?
		    "active":"passive");

	sdev->handler_data = h;
	return 0;
failed:
	kfree(h);
	return -EINVAL;
}

static void hp_sw_bus_detach( struct scsi_device *sdev )
{
	kfree(sdev->handler_data);
	sdev->handler_data = NULL;
}

static struct scsi_device_handler hp_sw_dh = {
	.name		= HP_SW_NAME,
	.module		= THIS_MODULE,
	.attach		= hp_sw_bus_attach,
	.detach		= hp_sw_bus_detach,
	.activate	= hp_sw_activate,
	.prep_fn	= hp_sw_prep_fn,
};

static int __init hp_sw_init(void)
{
	return scsi_register_device_handler(&hp_sw_dh);
}

static void __exit hp_sw_exit(void)
{
	scsi_unregister_device_handler(&hp_sw_dh);
}

module_init(hp_sw_init);
module_exit(hp_sw_exit);

MODULE_DESCRIPTION("HP Active/Passive driver");
MODULE_AUTHOR("Mike Christie <michaelc@cs.wisc.edu");
MODULE_LICENSE("GPL");
