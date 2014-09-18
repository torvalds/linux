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
#include <asm/uaccess.h>

#include "ap_bus.h"
#include "zcrypt_api.h"
#include "zcrypt_error.h"
#include "zcrypt_cex2a.h"
#include "zcrypt_msgtype50.h"

#define CEX2A_MIN_MOD_SIZE	  1	/*    8 bits	*/
#define CEX2A_MAX_MOD_SIZE	256	/* 2048 bits	*/
#define CEX3A_MIN_MOD_SIZE	CEX2A_MIN_MOD_SIZE
#define CEX3A_MAX_MOD_SIZE	512	/* 4096 bits	*/

#define CEX2A_SPEED_RATING	970
#define CEX3A_SPEED_RATING	900 /* Fixme: Needs finetuning */

#define CEX2A_MAX_MESSAGE_SIZE	0x390	/* sizeof(struct type50_crb2_msg)    */
#define CEX2A_MAX_RESPONSE_SIZE 0x110	/* max outputdatalength + type80_hdr */

#define CEX3A_MAX_RESPONSE_SIZE	0x210	/* 512 bit modulus
					 * (max outputdatalength) +
					 * type80_hdr*/
#define CEX3A_MAX_MESSAGE_SIZE	sizeof(struct type50_crb3_msg)

#define CEX2A_CLEANUP_TIME	(15*HZ)
#define CEX3A_CLEANUP_TIME	CEX2A_CLEANUP_TIME

static struct ap_device_id zcrypt_cex2a_ids[] = {
	{ AP_DEVICE(AP_DEVICE_TYPE_CEX2A) },
	{ AP_DEVICE(AP_DEVICE_TYPE_CEX3A) },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ap, zcrypt_cex2a_ids);
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("CEX2A Cryptographic Coprocessor device driver, " \
		   "Copyright IBM Corp. 2001, 2012");
MODULE_LICENSE("GPL");

static int zcrypt_cex2a_probe(struct ap_device *ap_dev);
static void zcrypt_cex2a_remove(struct ap_device *ap_dev);

static struct ap_driver zcrypt_cex2a_driver = {
	.probe = zcrypt_cex2a_probe,
	.remove = zcrypt_cex2a_remove,
	.ids = zcrypt_cex2a_ids,
	.request_timeout = CEX2A_CLEANUP_TIME,
};

/**
 * Probe function for CEX2A cards. It always accepts the AP device
 * since the bus_match already checked the hardware type.
 * @ap_dev: pointer to the AP device.
 */
static int zcrypt_cex2a_probe(struct ap_device *ap_dev)
{
	struct zcrypt_device *zdev = NULL;
	int rc = 0;

	switch (ap_dev->device_type) {
	case AP_DEVICE_TYPE_CEX2A:
		zdev = zcrypt_device_alloc(CEX2A_MAX_RESPONSE_SIZE);
		if (!zdev)
			return -ENOMEM;
		zdev->user_space_type = ZCRYPT_CEX2A;
		zdev->type_string = "CEX2A";
		zdev->min_mod_size = CEX2A_MIN_MOD_SIZE;
		zdev->max_mod_size = CEX2A_MAX_MOD_SIZE;
		zdev->short_crt = 1;
		zdev->speed_rating = CEX2A_SPEED_RATING;
		zdev->max_exp_bit_length = CEX2A_MAX_MOD_SIZE;
		break;
	case AP_DEVICE_TYPE_CEX3A:
		zdev = zcrypt_device_alloc(CEX3A_MAX_RESPONSE_SIZE);
		if (!zdev)
			return -ENOMEM;
		zdev->user_space_type = ZCRYPT_CEX3A;
		zdev->type_string = "CEX3A";
		zdev->min_mod_size = CEX2A_MIN_MOD_SIZE;
		zdev->max_mod_size = CEX2A_MAX_MOD_SIZE;
		zdev->max_exp_bit_length = CEX2A_MAX_MOD_SIZE;
		if (ap_test_bit(&ap_dev->functions, AP_FUNC_MEX4K) &&
		    ap_test_bit(&ap_dev->functions, AP_FUNC_CRT4K)) {
			zdev->max_mod_size = CEX3A_MAX_MOD_SIZE;
			zdev->max_exp_bit_length = CEX3A_MAX_MOD_SIZE;
		}
		zdev->short_crt = 1;
		zdev->speed_rating = CEX3A_SPEED_RATING;
		break;
	}
	if (!zdev)
		return -ENODEV;
	zdev->ops = zcrypt_msgtype_request(MSGTYPE50_NAME,
					   MSGTYPE50_VARIANT_DEFAULT);
	zdev->ap_dev = ap_dev;
	zdev->online = 1;
	ap_dev->reply = &zdev->reply;
	ap_dev->private = zdev;
	rc = zcrypt_device_register(zdev);
	if (rc) {
		ap_dev->private = NULL;
		zcrypt_msgtype_release(zdev->ops);
		zcrypt_device_free(zdev);
	}
	return rc;
}

/**
 * This is called to remove the extended CEX2A driver information
 * if an AP device is removed.
 */
static void zcrypt_cex2a_remove(struct ap_device *ap_dev)
{
	struct zcrypt_device *zdev = ap_dev->private;
	struct zcrypt_ops *zops = zdev->ops;

	zcrypt_device_unregister(zdev);
	zcrypt_msgtype_release(zops);
}

int __init zcrypt_cex2a_init(void)
{
	return ap_driver_register(&zcrypt_cex2a_driver, THIS_MODULE, "cex2a");
}

void __exit zcrypt_cex2a_exit(void)
{
	ap_driver_unregister(&zcrypt_cex2a_driver);
}

module_init(zcrypt_cex2a_init);
module_exit(zcrypt_cex2a_exit);
