/*
 *  linux/drivers/s390/crypto/zcrypt_api.h
 *
 *  zcrypt 2.1.0
 *
 *  Copyright (C)  2001, 2006 IBM Corporation
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *	       Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *  Major cleanup & driver split: Martin Schwidefsky <schwidefsky@de.ibm.com>
 *				  Ralph Wuerthner <rwuerthn@de.ibm.com>
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

#ifndef _ZCRYPT_API_H_
#define _ZCRYPT_API_H_

/**
 * Macro definitions
 *
 * PDEBUG debugs in the form "zcrypt: function_name -> message"
 *
 * PRINTK is like PDEBUG, except that it is always enabled
 * PRINTKN is like PRINTK, except that it does not include the function name
 * PRINTKW is like PRINTK, except that it uses KERN_WARNING
 * PRINTKC is like PRINTK, except that it uses KERN_CRIT
 */
#define DEV_NAME	"zcrypt"

#define PRINTK(fmt, args...) \
	printk(KERN_DEBUG DEV_NAME ": %s -> " fmt, __FUNCTION__ , ## args)
#define PRINTKN(fmt, args...) \
	printk(KERN_DEBUG DEV_NAME ": " fmt, ## args)
#define PRINTKW(fmt, args...) \
	printk(KERN_WARNING DEV_NAME ": %s -> " fmt, __FUNCTION__ , ## args)
#define PRINTKC(fmt, args...) \
	printk(KERN_CRIT DEV_NAME ": %s -> " fmt, __FUNCTION__ , ## args)

#ifdef ZCRYPT_DEBUG
#define PDEBUG(fmt, args...) \
	printk(KERN_DEBUG DEV_NAME ": %s -> " fmt, __FUNCTION__ , ## args)
#else
#define PDEBUG(fmt, args...) do {} while (0)
#endif

#include "ap_bus.h"
#include <asm/zcrypt.h>

/* deprecated status calls */
#define ICAZ90STATUS		_IOR(ZCRYPT_IOCTL_MAGIC, 0x10, struct ica_z90_status)
#define Z90STAT_PCIXCCCOUNT	_IOR(ZCRYPT_IOCTL_MAGIC, 0x43, int)

/**
 * This structure is deprecated and the corresponding ioctl() has been
 * replaced with individual ioctl()s for each piece of data!
 */
struct ica_z90_status {
	int totalcount;
	int leedslitecount; // PCICA
	int leeds2count;    // PCICC
	// int PCIXCCCount; is not in struct for backward compatibility
	int requestqWaitCount;
	int pendingqWaitCount;
	int totalOpenCount;
	int cryptoDomain;
	// status: 0=not there, 1=PCICA, 2=PCICC, 3=PCIXCC_MCL2, 4=PCIXCC_MCL3,
	//	   5=CEX2C
	unsigned char status[64];
	// qdepth: # work elements waiting for each device
	unsigned char qdepth[64];
};

/**
 * device type for an actual device is either PCICA, PCICC, PCIXCC_MCL2,
 * PCIXCC_MCL3, CEX2C, or CEX2A
 *
 * NOTE: PCIXCC_MCL3 refers to a PCIXCC with May 2004 version of Licensed
 *	 Internal Code (LIC) (EC J12220 level 29).
 *	 PCIXCC_MCL2 refers to any LIC before this level.
 */
#define ZCRYPT_PCICA		1
#define ZCRYPT_PCICC		2
#define ZCRYPT_PCIXCC_MCL2	3
#define ZCRYPT_PCIXCC_MCL3	4
#define ZCRYPT_CEX2C		5
#define ZCRYPT_CEX2A		6

struct zcrypt_device;

struct zcrypt_ops {
	long (*rsa_modexpo)(struct zcrypt_device *, struct ica_rsa_modexpo *);
	long (*rsa_modexpo_crt)(struct zcrypt_device *,
				struct ica_rsa_modexpo_crt *);
	long (*send_cprb)(struct zcrypt_device *, struct ica_xcRB *);
};

struct zcrypt_device {
	struct list_head list;		/* Device list. */
	spinlock_t lock;		/* Per device lock. */
	struct kref refcount;		/* device refcounting */
	struct ap_device *ap_dev;	/* The "real" ap device. */
	struct zcrypt_ops *ops;		/* Crypto operations. */
	int online;			/* User online/offline */

	int user_space_type;		/* User space device id. */
	char *type_string;		/* User space device name. */
	int min_mod_size;		/* Min number of bits. */
	int max_mod_size;		/* Max number of bits. */
	int short_crt;			/* Card has crt length restriction. */
	int speed_rating;		/* Speed of the crypto device. */

	int request_count;		/* # current requests. */

	struct ap_message reply;	/* Per-device reply structure. */
};

struct zcrypt_device *zcrypt_device_alloc(size_t);
void zcrypt_device_free(struct zcrypt_device *);
void zcrypt_device_get(struct zcrypt_device *);
int zcrypt_device_put(struct zcrypt_device *);
int zcrypt_device_register(struct zcrypt_device *);
void zcrypt_device_unregister(struct zcrypt_device *);
int zcrypt_api_init(void);
void zcrypt_api_exit(void);

#endif /* _ZCRYPT_API_H_ */
