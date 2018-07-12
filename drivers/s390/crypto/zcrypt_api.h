// SPDX-License-Identifier: GPL-2.0+
/*
 *  zcrypt 2.1.0
 *
 *  Copyright IBM Corp. 2001, 2012
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *	       Cornelia Huck <cornelia.huck@de.ibm.com>
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *  Major cleanup & driver split: Martin Schwidefsky <schwidefsky@de.ibm.com>
 *				  Ralph Wuerthner <rwuerthn@de.ibm.com>
 *  MSGTYPE restruct:		  Holger Dengler <hd@linux.vnet.ibm.com>
 */

#ifndef _ZCRYPT_API_H_
#define _ZCRYPT_API_H_

#include <linux/atomic.h>
#include <asm/debug.h>
#include <asm/zcrypt.h>
#include "ap_bus.h"

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
#define ZCRYPT_CEX3C		7
#define ZCRYPT_CEX3A		8
#define ZCRYPT_CEX4	       10
#define ZCRYPT_CEX5	       11
#define ZCRYPT_CEX6	       12

/**
 * Large random numbers are pulled in 4096 byte chunks from the crypto cards
 * and stored in a page. Be careful when increasing this buffer due to size
 * limitations for AP requests.
 */
#define ZCRYPT_RNG_BUFFER_SIZE	4096

/*
 * Identifier for Crypto Request Performance Index
 */
enum crypto_ops {
	MEX_1K,
	MEX_2K,
	MEX_4K,
	CRT_1K,
	CRT_2K,
	CRT_4K,
	HWRNG,
	SECKEY,
	NUM_OPS
};

struct zcrypt_queue;

struct zcrypt_ops {
	long (*rsa_modexpo)(struct zcrypt_queue *, struct ica_rsa_modexpo *);
	long (*rsa_modexpo_crt)(struct zcrypt_queue *,
				struct ica_rsa_modexpo_crt *);
	long (*send_cprb)(struct zcrypt_queue *, struct ica_xcRB *,
			  struct ap_message *);
	long (*send_ep11_cprb)(struct zcrypt_queue *, struct ep11_urb *,
			       struct ap_message *);
	long (*rng)(struct zcrypt_queue *, char *, struct ap_message *);
	struct list_head list;		/* zcrypt ops list. */
	struct module *owner;
	int variant;
	char name[128];
};

struct zcrypt_card {
	struct list_head list;		/* Device list. */
	struct list_head zqueues;	/* List of zcrypt queues */
	struct kref refcount;		/* device refcounting */
	struct ap_card *card;		/* The "real" ap card device. */
	int online;			/* User online/offline */

	int user_space_type;		/* User space device id. */
	char *type_string;		/* User space device name. */
	int min_mod_size;		/* Min number of bits. */
	int max_mod_size;		/* Max number of bits. */
	int max_exp_bit_length;
	int speed_rating[NUM_OPS];	/* Speed idx of crypto ops. */
	atomic_t load;			/* Utilization of the crypto device */

	int request_count;		/* # current requests. */
};

struct zcrypt_queue {
	struct list_head list;		/* Device list. */
	struct kref refcount;		/* device refcounting */
	struct zcrypt_card *zcard;
	struct zcrypt_ops *ops;		/* Crypto operations. */
	struct ap_queue *queue;		/* The "real" ap queue device. */
	int online;			/* User online/offline */

	atomic_t load;			/* Utilization of the crypto device */

	int request_count;		/* # current requests. */

	struct ap_message reply;	/* Per-device reply structure. */
};

/* transport layer rescanning */
extern atomic_t zcrypt_rescan_req;

extern spinlock_t zcrypt_list_lock;
extern int zcrypt_device_count;
extern struct list_head zcrypt_card_list;

#define for_each_zcrypt_card(_zc) \
	list_for_each_entry(_zc, &zcrypt_card_list, list)

#define for_each_zcrypt_queue(_zq, _zc) \
	list_for_each_entry(_zq, &(_zc)->zqueues, list)

struct zcrypt_card *zcrypt_card_alloc(void);
void zcrypt_card_free(struct zcrypt_card *);
void zcrypt_card_get(struct zcrypt_card *);
int zcrypt_card_put(struct zcrypt_card *);
int zcrypt_card_register(struct zcrypt_card *);
void zcrypt_card_unregister(struct zcrypt_card *);
struct zcrypt_card *zcrypt_card_get_best(unsigned int *,
					 unsigned int, unsigned int);
void zcrypt_card_put_best(struct zcrypt_card *, unsigned int);

struct zcrypt_queue *zcrypt_queue_alloc(size_t);
void zcrypt_queue_free(struct zcrypt_queue *);
void zcrypt_queue_get(struct zcrypt_queue *);
int zcrypt_queue_put(struct zcrypt_queue *);
int zcrypt_queue_register(struct zcrypt_queue *);
void zcrypt_queue_unregister(struct zcrypt_queue *);
void zcrypt_queue_force_online(struct zcrypt_queue *, int);
struct zcrypt_queue *zcrypt_queue_get_best(unsigned int, unsigned int);
void  zcrypt_queue_put_best(struct zcrypt_queue *, unsigned int);

int zcrypt_rng_device_add(void);
void zcrypt_rng_device_remove(void);

void zcrypt_msgtype_register(struct zcrypt_ops *);
void zcrypt_msgtype_unregister(struct zcrypt_ops *);
struct zcrypt_ops *zcrypt_msgtype(unsigned char *, int);
int zcrypt_api_init(void);
void zcrypt_api_exit(void);
long zcrypt_send_cprb(struct ica_xcRB *xcRB);
void zcrypt_device_status_mask_ext(struct zcrypt_device_status_ext *devstatus);

#endif /* _ZCRYPT_API_H_ */
