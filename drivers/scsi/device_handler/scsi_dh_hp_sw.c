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
	unsigned char sense[SCSI_SENSE_BUFFERSIZE];
	int path_state;
	int retries;
	int retry_cnt;
	struct scsi_device *sdev;
	activate_complete	callback_fn;
	void			*callback_data;
};

static int hp_sw_start_stop(struct hp_sw_dh_data *);

static inline struct hp_sw_dh_data *get_hp_sw_data(struct scsi_device *sdev)
{
	struct scsi_dh_data *scsi_dh_data = sdev->scsi_dh_data;
	BUG_ON(scsi_dh_data == NULL);
	return ((struct hp_sw_dh_data *) scsi_dh_data->buf);
}

/*
 * tur_done - Handle TEST UNIT READY return status
 * @sdev: sdev the command has been sent to
 * @errors: blk error code
 *
 * Returns SCSI_DH_DEV_OFFLINED if the sdev is on the passive path
 */
static int tur_done(struct scsi_device *sdev, unsigned char *sense)
{
	struct scsi_sense_hdr sshdr;
	int ret;

	ret = scsi_normalize_sense(sense, SCSI_SENSE_BUFFERSIZE, &sshdr);
	if (!ret) {
		sdev_printk(KERN_WARNING, sdev,
			    "%s: sending tur failed, no sense available\n",
			    HP_SW_NAME);
		ret = SCSI_DH_IO;
		goto done;
	}
	switch (sshdr.sense_key) {
	case UNIT_ATTENTION:
		ret = SCSI_DH_IMM_RETRY;
		break;
	case NOT_READY:
		if ((sshdr.asc == 0x04) && (sshdr.ascq == 2)) {
			/*
			 * LUN not ready - Initialization command required
			 *
			 * This is the passive path
			 */
			ret = SCSI_DH_DEV_OFFLINED;
			break;
		}
		/* Fallthrough */
	default:
		sdev_printk(KERN_WARNING, sdev,
			   "%s: sending tur failed, sense %x/%x/%x\n",
			   HP_SW_NAME, sshdr.sense_key, sshdr.asc,
			   sshdr.ascq);
		break;
	}

done:
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
	struct request *req;
	int ret;

retry:
	req = blk_get_request(sdev->request_queue, WRITE, GFP_NOIO);
	if (IS_ERR(req))
		return SCSI_DH_RES_TEMP_UNAVAIL;

	blk_rq_set_block_pc(req);
	req->cmd_flags |= REQ_FAILFAST_DEV | REQ_FAILFAST_TRANSPORT |
			  REQ_FAILFAST_DRIVER;
	req->cmd_len = COMMAND_SIZE(TEST_UNIT_READY);
	req->cmd[0] = TEST_UNIT_READY;
	req->timeout = HP_SW_TIMEOUT;
	req->sense = h->sense;
	memset(req->sense, 0, SCSI_SENSE_BUFFERSIZE);
	req->sense_len = 0;

	ret = blk_execute_rq(req->q, NULL, req, 1);
	if (ret == -EIO) {
		if (req->sense_len > 0) {
			ret = tur_done(sdev, h->sense);
		} else {
			sdev_printk(KERN_WARNING, sdev,
				    "%s: sending tur failed with %x\n",
				    HP_SW_NAME, req->errors);
			ret = SCSI_DH_IO;
		}
	} else {
		h->path_state = HP_SW_PATH_ACTIVE;
		ret = SCSI_DH_OK;
	}
	if (ret == SCSI_DH_IMM_RETRY) {
		blk_put_request(req);
		goto retry;
	}
	if (ret == SCSI_DH_DEV_OFFLINED) {
		h->path_state = HP_SW_PATH_PASSIVE;
		ret = SCSI_DH_OK;
	}

	blk_put_request(req);

	return ret;
}

/*
 * start_done - Handle START STOP UNIT return status
 * @sdev: sdev the command has been sent to
 * @errors: blk error code
 */
static int start_done(struct scsi_device *sdev, unsigned char *sense)
{
	struct scsi_sense_hdr sshdr;
	int rc;

	rc = scsi_normalize_sense(sense, SCSI_SENSE_BUFFERSIZE, &sshdr);
	if (!rc) {
		sdev_printk(KERN_WARNING, sdev,
			    "%s: sending start_stop_unit failed, "
			    "no sense available\n",
			    HP_SW_NAME);
		return SCSI_DH_IO;
	}
	switch (sshdr.sense_key) {
	case NOT_READY:
		if ((sshdr.asc == 0x04) && (sshdr.ascq == 3)) {
			/*
			 * LUN not ready - manual intervention required
			 *
			 * Switch-over in progress, retry.
			 */
			rc = SCSI_DH_RETRY;
			break;
		}
		/* fall through */
	default:
		sdev_printk(KERN_WARNING, sdev,
			   "%s: sending start_stop_unit failed, sense %x/%x/%x\n",
			   HP_SW_NAME, sshdr.sense_key, sshdr.asc,
			   sshdr.ascq);
		rc = SCSI_DH_IO;
	}

	return rc;
}

static void start_stop_endio(struct request *req, int error)
{
	struct hp_sw_dh_data *h = req->end_io_data;
	unsigned err = SCSI_DH_OK;

	if (error || host_byte(req->errors) != DID_OK ||
			msg_byte(req->errors) != COMMAND_COMPLETE) {
		sdev_printk(KERN_WARNING, h->sdev,
			    "%s: sending start_stop_unit failed with %x\n",
			    HP_SW_NAME, req->errors);
		err = SCSI_DH_IO;
		goto done;
	}

	if (req->sense_len > 0) {
		err = start_done(h->sdev, h->sense);
		if (err == SCSI_DH_RETRY) {
			err = SCSI_DH_IO;
			if (--h->retry_cnt) {
				blk_put_request(req);
				err = hp_sw_start_stop(h);
				if (err == SCSI_DH_OK)
					return;
			}
		}
	}
done:
	req->end_io_data = NULL;
	__blk_put_request(req->q, req);
	if (h->callback_fn) {
		h->callback_fn(h->callback_data, err);
		h->callback_fn = h->callback_data = NULL;
	}
	return;

}

/*
 * hp_sw_start_stop - Send START STOP UNIT command
 * @sdev: sdev command should be sent to
 *
 * Sending START STOP UNIT activates the SP.
 */
static int hp_sw_start_stop(struct hp_sw_dh_data *h)
{
	struct request *req;

	req = blk_get_request(h->sdev->request_queue, WRITE, GFP_ATOMIC);
	if (IS_ERR(req))
		return SCSI_DH_RES_TEMP_UNAVAIL;

	blk_rq_set_block_pc(req);
	req->cmd_flags |= REQ_FAILFAST_DEV | REQ_FAILFAST_TRANSPORT |
			  REQ_FAILFAST_DRIVER;
	req->cmd_len = COMMAND_SIZE(START_STOP);
	req->cmd[0] = START_STOP;
	req->cmd[4] = 1;	/* Start spin cycle */
	req->timeout = HP_SW_TIMEOUT;
	req->sense = h->sense;
	memset(req->sense, 0, SCSI_SENSE_BUFFERSIZE);
	req->sense_len = 0;
	req->end_io_data = h;

	blk_execute_rq_nowait(req->q, NULL, req, 1, start_stop_endio);
	return SCSI_DH_OK;
}

static int hp_sw_prep_fn(struct scsi_device *sdev, struct request *req)
{
	struct hp_sw_dh_data *h = get_hp_sw_data(sdev);
	int ret = BLKPREP_OK;

	if (h->path_state != HP_SW_PATH_ACTIVE) {
		ret = BLKPREP_KILL;
		req->cmd_flags |= REQ_QUIET;
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
	struct hp_sw_dh_data *h = get_hp_sw_data(sdev);

	ret = hp_sw_tur(sdev, h);

	if (ret == SCSI_DH_OK && h->path_state == HP_SW_PATH_PASSIVE) {
		h->retry_cnt = h->retries;
		h->callback_fn = fn;
		h->callback_data = data;
		ret = hp_sw_start_stop(h);
		if (ret == SCSI_DH_OK)
			return 0;
		h->callback_fn = h->callback_data = NULL;
	}

	if (fn)
		fn(data, ret);
	return 0;
}

static const struct scsi_dh_devlist hp_sw_dh_data_list[] = {
	{"COMPAQ", "MSA1000 VOLUME"},
	{"COMPAQ", "HSV110"},
	{"HP", "HSV100"},
	{"DEC", "HSG80"},
	{NULL, NULL},
};

static bool hp_sw_match(struct scsi_device *sdev)
{
	int i;

	if (scsi_device_tpgs(sdev))
		return false;

	for (i = 0; hp_sw_dh_data_list[i].vendor; i++) {
		if (!strncmp(sdev->vendor, hp_sw_dh_data_list[i].vendor,
			strlen(hp_sw_dh_data_list[i].vendor)) &&
		    !strncmp(sdev->model, hp_sw_dh_data_list[i].model,
			strlen(hp_sw_dh_data_list[i].model))) {
			return true;
		}
	}
	return false;
}

static int hp_sw_bus_attach(struct scsi_device *sdev);
static void hp_sw_bus_detach(struct scsi_device *sdev);

static struct scsi_device_handler hp_sw_dh = {
	.name		= HP_SW_NAME,
	.module		= THIS_MODULE,
	.devlist	= hp_sw_dh_data_list,
	.attach		= hp_sw_bus_attach,
	.detach		= hp_sw_bus_detach,
	.activate	= hp_sw_activate,
	.prep_fn	= hp_sw_prep_fn,
	.match		= hp_sw_match,
};

static int hp_sw_bus_attach(struct scsi_device *sdev)
{
	struct scsi_dh_data *scsi_dh_data;
	struct hp_sw_dh_data *h;
	unsigned long flags;
	int ret;

	scsi_dh_data = kzalloc(sizeof(*scsi_dh_data)
			       + sizeof(*h) , GFP_KERNEL);
	if (!scsi_dh_data) {
		sdev_printk(KERN_ERR, sdev, "%s: Attach Failed\n",
			    HP_SW_NAME);
		return 0;
	}

	scsi_dh_data->scsi_dh = &hp_sw_dh;
	h = (struct hp_sw_dh_data *) scsi_dh_data->buf;
	h->path_state = HP_SW_PATH_UNINITIALIZED;
	h->retries = HP_SW_RETRIES;
	h->sdev = sdev;

	ret = hp_sw_tur(sdev, h);
	if (ret != SCSI_DH_OK || h->path_state == HP_SW_PATH_UNINITIALIZED)
		goto failed;

	if (!try_module_get(THIS_MODULE))
		goto failed;

	spin_lock_irqsave(sdev->request_queue->queue_lock, flags);
	sdev->scsi_dh_data = scsi_dh_data;
	spin_unlock_irqrestore(sdev->request_queue->queue_lock, flags);

	sdev_printk(KERN_INFO, sdev, "%s: attached to %s path\n",
		    HP_SW_NAME, h->path_state == HP_SW_PATH_ACTIVE?
		    "active":"passive");

	return 0;

failed:
	kfree(scsi_dh_data);
	sdev_printk(KERN_ERR, sdev, "%s: not attached\n",
		    HP_SW_NAME);
	return -EINVAL;
}

static void hp_sw_bus_detach( struct scsi_device *sdev )
{
	struct scsi_dh_data *scsi_dh_data;
	unsigned long flags;

	spin_lock_irqsave(sdev->request_queue->queue_lock, flags);
	scsi_dh_data = sdev->scsi_dh_data;
	sdev->scsi_dh_data = NULL;
	spin_unlock_irqrestore(sdev->request_queue->queue_lock, flags);
	module_put(THIS_MODULE);

	sdev_printk(KERN_NOTICE, sdev, "%s: Detached\n", HP_SW_NAME);

	kfree(scsi_dh_data);
}

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
