/*
 *  Copyright IBM Corp. 2012
 *  Author(s): Holger Dengler <hd@linux.vnet.ibm.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/atomic.h>
#include <linux/uaccess.h>

#include "ap_bus.h"
#include "zcrypt_api.h"
#include "zcrypt_msgtype6.h"
#include "zcrypt_msgtype50.h"
#include "zcrypt_error.h"
#include "zcrypt_cex4.h"

#define CEX4A_MIN_MOD_SIZE	  1	/*    8 bits	*/
#define CEX4A_MAX_MOD_SIZE_2K	256	/* 2048 bits	*/
#define CEX4A_MAX_MOD_SIZE_4K	512	/* 4096 bits	*/

#define CEX4C_MIN_MOD_SIZE	 16	/*  256 bits	*/
#define CEX4C_MAX_MOD_SIZE	512	/* 4096 bits	*/

#define CEX4A_SPEED_RATING	900	 /* TODO new card, new speed rating */
#define CEX4C_SPEED_RATING	6500	 /* TODO new card, new speed rating */
#define CEX4P_SPEED_RATING	7000	 /* TODO new card, new speed rating */
#define CEX5A_SPEED_RATING	450	 /* TODO new card, new speed rating */
#define CEX5C_SPEED_RATING	3250	 /* TODO new card, new speed rating */
#define CEX5P_SPEED_RATING	3500	 /* TODO new card, new speed rating */

#define CEX4A_MAX_MESSAGE_SIZE	MSGTYPE50_CRB3_MAX_MSG_SIZE
#define CEX4C_MAX_MESSAGE_SIZE	MSGTYPE06_MAX_MSG_SIZE

/* Waiting time for requests to be processed.
 * Currently there are some types of request which are not deterministic.
 * But the maximum time limit managed by the stomper code is set to 60sec.
 * Hence we have to wait at least that time period.
 */
#define CEX4_CLEANUP_TIME	(900*HZ)

static struct ap_device_id zcrypt_cex4_ids[] = {
	{ AP_DEVICE(AP_DEVICE_TYPE_CEX4)  },
	{ AP_DEVICE(AP_DEVICE_TYPE_CEX5)  },
	{ /* end of list */ },
};

MODULE_DEVICE_TABLE(ap, zcrypt_cex4_ids);
MODULE_AUTHOR("IBM Corporation");
MODULE_DESCRIPTION("CEX4 Cryptographic Card device driver, " \
		   "Copyright IBM Corp. 2012");
MODULE_LICENSE("GPL");

static int zcrypt_cex4_probe(struct ap_device *ap_dev);
static void zcrypt_cex4_remove(struct ap_device *ap_dev);

static struct ap_driver zcrypt_cex4_driver = {
	.probe = zcrypt_cex4_probe,
	.remove = zcrypt_cex4_remove,
	.ids = zcrypt_cex4_ids,
	.request_timeout = CEX4_CLEANUP_TIME,
};

/**
 * Probe function for CEX4 cards. It always accepts the AP device
 * since the bus_match already checked the hardware type.
 * @ap_dev: pointer to the AP device.
 */
static int zcrypt_cex4_probe(struct ap_device *ap_dev)
{
	struct zcrypt_device *zdev = NULL;
	int rc = 0;

	switch (ap_dev->device_type) {
	case AP_DEVICE_TYPE_CEX4:
	case AP_DEVICE_TYPE_CEX5:
		if (ap_test_bit(&ap_dev->functions, AP_FUNC_ACCEL)) {
			zdev = zcrypt_device_alloc(CEX4A_MAX_MESSAGE_SIZE);
			if (!zdev)
				return -ENOMEM;
			if (ap_dev->device_type == AP_DEVICE_TYPE_CEX4) {
				zdev->type_string = "CEX4A";
				zdev->speed_rating = CEX4A_SPEED_RATING;
			} else {
				zdev->type_string = "CEX5A";
				zdev->speed_rating = CEX5A_SPEED_RATING;
			}
			zdev->user_space_type = ZCRYPT_CEX3A;
			zdev->min_mod_size = CEX4A_MIN_MOD_SIZE;
			if (ap_test_bit(&ap_dev->functions, AP_FUNC_MEX4K) &&
			    ap_test_bit(&ap_dev->functions, AP_FUNC_CRT4K)) {
				zdev->max_mod_size =
					CEX4A_MAX_MOD_SIZE_4K;
				zdev->max_exp_bit_length =
					CEX4A_MAX_MOD_SIZE_4K;
			} else {
				zdev->max_mod_size =
					CEX4A_MAX_MOD_SIZE_2K;
				zdev->max_exp_bit_length =
					CEX4A_MAX_MOD_SIZE_2K;
			}
			zdev->short_crt = 1;
			zdev->ops = zcrypt_msgtype_request(MSGTYPE50_NAME,
							   MSGTYPE50_VARIANT_DEFAULT);
		} else if (ap_test_bit(&ap_dev->functions, AP_FUNC_COPRO)) {
			zdev = zcrypt_device_alloc(CEX4C_MAX_MESSAGE_SIZE);
			if (!zdev)
				return -ENOMEM;
			if (ap_dev->device_type == AP_DEVICE_TYPE_CEX4) {
				zdev->type_string = "CEX4C";
				zdev->speed_rating = CEX4C_SPEED_RATING;
			} else {
				zdev->type_string = "CEX5C";
				zdev->speed_rating = CEX5C_SPEED_RATING;
			}
			zdev->user_space_type = ZCRYPT_CEX3C;
			zdev->min_mod_size = CEX4C_MIN_MOD_SIZE;
			zdev->max_mod_size = CEX4C_MAX_MOD_SIZE;
			zdev->max_exp_bit_length = CEX4C_MAX_MOD_SIZE;
			zdev->short_crt = 0;
			zdev->ops = zcrypt_msgtype_request(MSGTYPE06_NAME,
							   MSGTYPE06_VARIANT_DEFAULT);
		} else if (ap_test_bit(&ap_dev->functions, AP_FUNC_EP11)) {
			zdev = zcrypt_device_alloc(CEX4C_MAX_MESSAGE_SIZE);
			if (!zdev)
				return -ENOMEM;
			if (ap_dev->device_type == AP_DEVICE_TYPE_CEX4) {
				zdev->type_string = "CEX4P";
				zdev->speed_rating = CEX4P_SPEED_RATING;
			} else {
				zdev->type_string = "CEX5P";
				zdev->speed_rating = CEX5P_SPEED_RATING;
			}
			zdev->user_space_type = ZCRYPT_CEX4;
			zdev->min_mod_size = CEX4C_MIN_MOD_SIZE;
			zdev->max_mod_size = CEX4C_MAX_MOD_SIZE;
			zdev->max_exp_bit_length = CEX4C_MAX_MOD_SIZE;
			zdev->short_crt = 0;
			zdev->ops = zcrypt_msgtype_request(MSGTYPE06_NAME,
							MSGTYPE06_VARIANT_EP11);
		}
		break;
	}
	if (!zdev)
		return -ENODEV;
	zdev->ap_dev = ap_dev;
	zdev->online = 1;
	ap_dev->reply = &zdev->reply;
	ap_dev->private = zdev;
	rc = zcrypt_device_register(zdev);
	if (rc) {
		zcrypt_msgtype_release(zdev->ops);
		ap_dev->private = NULL;
		zcrypt_device_free(zdev);
	}
	return rc;
}

/**
 * This is called to remove the extended CEX4 driver information
 * if an AP device is removed.
 */
static void zcrypt_cex4_remove(struct ap_device *ap_dev)
{
	struct zcrypt_device *zdev = ap_dev->private;
	struct zcrypt_ops *zops;

	if (zdev) {
		zops = zdev->ops;
		zcrypt_device_unregister(zdev);
		zcrypt_msgtype_release(zops);
	}
}

int __init zcrypt_cex4_init(void)
{
	return ap_driver_register(&zcrypt_cex4_driver, THIS_MODULE, "cex4");
}

void __exit zcrypt_cex4_exit(void)
{
	ap_driver_unregister(&zcrypt_cex4_driver);
}

module_init(zcrypt_cex4_init);
module_exit(zcrypt_cex4_exit);
