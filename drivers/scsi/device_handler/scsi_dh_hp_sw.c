/*
 * Basic HP/COMPAQ MSA 1000 support. This is only needed if your HW cannot be
 * upgraded.
 *
 * Copyright (C) 2006 Red Hat, Inc.  All rights reserved.
 * Copyright (C) 2006 Mike Christie
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

#include <scsi/scsi.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_dh.h>

#define HP_SW_NAME	"hp_sw"

#define HP_SW_TIMEOUT (60 * HZ)
#define HP_SW_RETRIES 3

struct hp_sw_dh_data {
	unsigned char sense[SCSI_SENSE_BUFFERSIZE];
	int retries;
};

static inline struct hp_sw_dh_data *get_hp_sw_data(struct scsi_device *sdev)
{
	struct scsi_dh_data *scsi_dh_data = sdev->scsi_dh_data;
	BUG_ON(scsi_dh_data == NULL);
	return ((struct hp_sw_dh_data *) scsi_dh_data->buf);
}

static int hp_sw_done(struct scsi_device *sdev)
{
	struct hp_sw_dh_data *h = get_hp_sw_data(sdev);
	struct scsi_sense_hdr sshdr;
	int rc;

	sdev_printk(KERN_INFO, sdev, "hp_sw_done\n");

	rc = scsi_normalize_sense(h->sense, SCSI_SENSE_BUFFERSIZE, &sshdr);
	if (!rc)
		goto done;
	switch (sshdr.sense_key) {
	case NOT_READY:
		if ((sshdr.asc == 0x04) && (sshdr.ascq == 3)) {
			rc = SCSI_DH_RETRY;
			h->retries++;
			break;
		}
		/* fall through */
	default:
		h->retries++;
		rc = SCSI_DH_IMM_RETRY;
	}

done:
	if (rc == SCSI_DH_OK || rc == SCSI_DH_IO)
		h->retries = 0;
	else if (h->retries > HP_SW_RETRIES) {
		h->retries = 0;
		rc = SCSI_DH_IO;
	}
	return rc;
}

static int hp_sw_activate(struct scsi_device *sdev)
{
	struct hp_sw_dh_data *h = get_hp_sw_data(sdev);
	struct request *req;
	int ret = SCSI_DH_RES_TEMP_UNAVAIL;

	req = blk_get_request(sdev->request_queue, WRITE, GFP_ATOMIC);
	if (!req)
		goto done;

	sdev_printk(KERN_INFO, sdev, "sending START_STOP.");

	req->cmd_type = REQ_TYPE_BLOCK_PC;
	req->cmd_flags |= REQ_FAILFAST;
	req->cmd_len = COMMAND_SIZE(START_STOP);
	memset(req->cmd, 0, MAX_COMMAND_SIZE);
	req->cmd[0] = START_STOP;
	req->cmd[4] = 1;	/* Start spin cycle */
	req->timeout = HP_SW_TIMEOUT;
	req->sense = h->sense;
	memset(req->sense, 0, SCSI_SENSE_BUFFERSIZE);
	req->sense_len = 0;

	ret = blk_execute_rq(req->q, NULL, req, 1);
	if (!ret) /* SUCCESS */
		ret = hp_sw_done(sdev);
	else
		ret = SCSI_DH_IO;
done:
	return ret;
}

static const struct {
	char *vendor;
	char *model;
} hp_sw_dh_data_list[] = {
	{"COMPAQ", "MSA"},
	{"HP", "HSV"},
	{"DEC", "HSG80"},
	{NULL, NULL},
};

static int hp_sw_bus_notify(struct notifier_block *, unsigned long, void *);

static struct scsi_device_handler hp_sw_dh = {
	.name		= HP_SW_NAME,
	.module		= THIS_MODULE,
	.nb.notifier_call = hp_sw_bus_notify,
	.activate	= hp_sw_activate,
};

static int hp_sw_bus_notify(struct notifier_block *nb,
			    unsigned long action, void *data)
{
	struct device *dev = data;
	struct scsi_device *sdev = to_scsi_device(dev);
	struct scsi_dh_data *scsi_dh_data;
	int i, found = 0;
	unsigned long flags;

	if (action == BUS_NOTIFY_ADD_DEVICE) {
		for (i = 0; hp_sw_dh_data_list[i].vendor; i++) {
			if (!strncmp(sdev->vendor, hp_sw_dh_data_list[i].vendor,
				     strlen(hp_sw_dh_data_list[i].vendor)) &&
			    !strncmp(sdev->model, hp_sw_dh_data_list[i].model,
				     strlen(hp_sw_dh_data_list[i].model))) {
				found = 1;
				break;
			}
		}
		if (!found)
			goto out;

		scsi_dh_data = kzalloc(sizeof(struct scsi_device_handler *)
				+ sizeof(struct hp_sw_dh_data) , GFP_KERNEL);
		if (!scsi_dh_data) {
			sdev_printk(KERN_ERR, sdev, "Attach Failed %s.\n",
				    HP_SW_NAME);
			goto out;
		}

		scsi_dh_data->scsi_dh = &hp_sw_dh;
		spin_lock_irqsave(sdev->request_queue->queue_lock, flags);
		sdev->scsi_dh_data = scsi_dh_data;
		spin_unlock_irqrestore(sdev->request_queue->queue_lock, flags);
		try_module_get(THIS_MODULE);

		sdev_printk(KERN_NOTICE, sdev, "Attached %s.\n", HP_SW_NAME);
	} else if (action == BUS_NOTIFY_DEL_DEVICE) {
		if (sdev->scsi_dh_data == NULL ||
				sdev->scsi_dh_data->scsi_dh != &hp_sw_dh)
			goto out;

		spin_lock_irqsave(sdev->request_queue->queue_lock, flags);
		scsi_dh_data = sdev->scsi_dh_data;
		sdev->scsi_dh_data = NULL;
		spin_unlock_irqrestore(sdev->request_queue->queue_lock, flags);
		module_put(THIS_MODULE);

		sdev_printk(KERN_NOTICE, sdev, "Dettached %s.\n", HP_SW_NAME);

		kfree(scsi_dh_data);
	}

out:
	return 0;
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

MODULE_DESCRIPTION("HP MSA 1000");
MODULE_AUTHOR("Mike Christie <michaelc@cs.wisc.edu");
MODULE_LICENSE("GPL");
