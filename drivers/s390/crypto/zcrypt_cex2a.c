/*
 *  zcrypt 2.1.0
 *
 *  Copyright IBM Corp. 2001, 2012
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *  Major cleanup & driver split: Martin Schwidefsky <schwidefsky@de.ibm.com>
 *				  Ralph Wuerthner <rwuerthn@de.ibm.com>
 *  MSGTYPE restruct:		  Holger Dengler <hd@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>
#include <linux/mod_devicetable.h>

#include "ap_bus.h"
#include "zcrypt_api.h"
#include "zcrypt_error.h"
#include "zcrypt_cex2a.h"
#include "zcrypt_msgtype50.h"

#define CEX2A_MIN_MOD_SIZE	  1	/*    8 bits	*/
#define CEX2A_MAX_MOD_SIZE	256	/* 2048 bits	*/
#define CEX3A_MIN_MOD_SIZE	CEX2A_MIN_MOD_SIZE
#define CEX3A_MAX_MOD_SIZE	512	/* 4096 bits	*/

#define CEX2A_MAX_MESSAGE_SIZE	0x390	/* sizeof(struct type50_crb2_msg)    */
#define CEX2A_MAX_RESPONSE_SIZE 0x110	/* max outputdatalength + type80_hdr */

#define CEX3A_MAX_RESPONSE_SIZE	0x210	/* 512 bit modulus
					 * (max outputdatalength) +
					 * type80_hdr*/
#define CEX3A_MAX_MESSAGE_SIZE	sizeof(struct type50_crb3_msg)

#define CEX2A_CLEANUP_TIME	(15*HZ)
#define CEX3A_CLEANUP_TIME	CEX2A_CLEANUP_TIME

MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("CEX2A Cryptographic Coprocessor device driver, " \
		   "Copyright IBM Corp. 2001, 2012");
MODULE_LICENSE("GPL");

static struct ap_device_id zcrypt_cex2a_card_ids[] = {
	{ .dev_type = AP_DEVICE_TYPE_CEX2A,
	  .match_flags = AP_DEVICE_ID_MATCH_CARD_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX3A,
	  .match_flags = AP_DEVICE_ID_MATCH_CARD_TYPE },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ap, zcrypt_cex2a_card_ids);

static struct ap_device_id zcrypt_cex2a_queue_ids[] = {
	{ .dev_type = AP_DEVICE_TYPE_CEX2A,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ .dev_type = AP_DEVICE_TYPE_CEX3A,
	  .match_flags = AP_DEVICE_ID_MATCH_QUEUE_TYPE },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ap, zcrypt_cex2a_queue_ids);

/**
 * Probe function for CEX2A card devices. It always accepts the AP device
 * since the bus_match already checked the card type.
 * @ap_dev: pointer to the AP device.
 */
static int zcrypt_cex2a_card_probe(struct ap_device *ap_dev)
{
	/*
	 * Normalized speed ratings per crypto adapter
	 * MEX_1k, MEX_2k, MEX_4k, CRT_1k, CRT_2k, CRT_4k, RNG, SECKEY
	 */
	static const int CEX2A_SPEED_IDX[] = {
		800, 1000, 2000,  900, 1200, 2400, 0, 0};
	static const int CEX3A_SPEED_IDX[] = {
		400,  500, 1000,  450,	550, 1200, 0, 0};

	struct ap_card *ac = to_ap_card(&ap_dev->device);
	struct zcrypt_card *zc;
	int rc = 0;

	zc = zcrypt_card_alloc();
	if (!zc)
		return -ENOMEM;
	zc->card = ac;
	ac->private = zc;

	if (ac->ap_dev.device_type == AP_DEVICE_TYPE_CEX2A) {
		zc->min_mod_size = CEX2A_MIN_MOD_SIZE;
		zc->max_mod_size = CEX2A_MAX_MOD_SIZE;
		memcpy(zc->speed_rating, CEX2A_SPEED_IDX,
		       sizeof(CEX2A_SPEED_IDX));
		zc->max_exp_bit_length = CEX2A_MAX_MOD_SIZE;
		zc->type_string = "CEX2A";
		zc->user_space_type = ZCRYPT_CEX2A;
	} else if (ac->ap_dev.device_type == AP_DEVICE_TYPE_CEX3A) {
		zc->min_mod_size = CEX2A_MIN_MOD_SIZE;
		zc->max_mod_size = CEX2A_MAX_MOD_SIZE;
		zc->max_exp_bit_length = CEX2A_MAX_MOD_SIZE;
		if (ap_test_bit(&ac->functions, AP_FUNC_MEX4K) &&
		    ap_test_bit(&ac->functions, AP_FUNC_CRT4K)) {
			zc->max_mod_size = CEX3A_MAX_MOD_SIZE;
			zc->max_exp_bit_length = CEX3A_MAX_MOD_SIZE;
		}
		memcpy(zc->speed_rating, CEX3A_SPEED_IDX,
		       sizeof(CEX3A_SPEED_IDX));
		zc->type_string = "CEX3A";
		zc->user_space_type = ZCRYPT_CEX3A;
	} else {
		zcrypt_card_free(zc);
		return -ENODEV;
	}
	zc->online = 1;

	rc = zcrypt_card_register(zc);
	if (rc) {
		ac->private = NULL;
		zcrypt_card_free(zc);
	}

	return rc;
}

/**
 * This is called to remove the CEX2A card driver information
 * if an AP card device is removed.
 */
static void zcrypt_cex2a_card_remove(struct ap_device *ap_dev)
{
	struct zcrypt_card *zc = to_ap_card(&ap_dev->device)->private;

	if (zc)
		zcrypt_card_unregister(zc);
}

static struct ap_driver zcrypt_cex2a_card_driver = {
	.probe = zcrypt_cex2a_card_probe,
	.remove = zcrypt_cex2a_card_remove,
	.ids = zcrypt_cex2a_card_ids,
};

/**
 * Probe function for CEX2A queue devices. It always accepts the AP device
 * since the bus_match already checked the queue type.
 * @ap_dev: pointer to the AP device.
 */
static int zcrypt_cex2a_queue_probe(struct ap_device *ap_dev)
{
	struct ap_queue *aq = to_ap_queue(&ap_dev->device);
	struct zcrypt_queue *zq = NULL;
	int rc;

	switch (ap_dev->device_type) {
	case AP_DEVICE_TYPE_CEX2A:
		zq = zcrypt_queue_alloc(CEX2A_MAX_RESPONSE_SIZE);
		if (!zq)
			return -ENOMEM;
		break;
	case AP_DEVICE_TYPE_CEX3A:
		zq = zcrypt_queue_alloc(CEX3A_MAX_RESPONSE_SIZE);
		if (!zq)
			return -ENOMEM;
		break;
	}
	if (!zq)
		return -ENODEV;
	zq->ops = zcrypt_msgtype(MSGTYPE50_NAME, MSGTYPE50_VARIANT_DEFAULT);
	zq->queue = aq;
	zq->online = 1;
	atomic_set(&zq->load, 0);
	ap_queue_init_reply(aq, &zq->reply);
	aq->request_timeout = CEX2A_CLEANUP_TIME,
	aq->private = zq;
	rc = zcrypt_queue_register(zq);
	if (rc) {
		aq->private = NULL;
		zcrypt_queue_free(zq);
	}

	return rc;
}

/**
 * This is called to remove the CEX2A queue driver information
 * if an AP queue device is removed.
 */
static void zcrypt_cex2a_queue_remove(struct ap_device *ap_dev)
{
	struct ap_queue *aq = to_ap_queue(&ap_dev->device);
	struct zcrypt_queue *zq = aq->private;

	ap_queue_remove(aq);
	if (zq)
		zcrypt_queue_unregister(zq);
}

static struct ap_driver zcrypt_cex2a_queue_driver = {
	.probe = zcrypt_cex2a_queue_probe,
	.remove = zcrypt_cex2a_queue_remove,
	.suspend = ap_queue_suspend,
	.resume = ap_queue_resume,
	.ids = zcrypt_cex2a_queue_ids,
};

int __init zcrypt_cex2a_init(void)
{
	int rc;

	rc = ap_driver_register(&zcrypt_cex2a_card_driver,
				THIS_MODULE, "cex2acard");
	if (rc)
		return rc;

	rc = ap_driver_register(&zcrypt_cex2a_queue_driver,
				THIS_MODULE, "cex2aqueue");
	if (rc)
		ap_driver_unregister(&zcrypt_cex2a_card_driver);

	return rc;
}

void __exit zcrypt_cex2a_exit(void)
{
	ap_driver_unregister(&zcrypt_cex2a_queue_driver);
	ap_driver_unregister(&zcrypt_cex2a_card_driver);
}

module_init(zcrypt_cex2a_init);
module_exit(zcrypt_cex2a_exit);
