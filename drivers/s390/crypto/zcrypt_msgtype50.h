/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  Copyright IBM Corp. 2001, 2012
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *  Major cleanup & driver split: Martin Schwidefsky <schwidefsky@de.ibm.com>
 *  MSGTYPE restruct:		  Holger Dengler <hd@linux.vnet.ibm.com>
 */

#ifndef _ZCRYPT_MSGTYPE50_H_
#define _ZCRYPT_MSGTYPE50_H_

#define MSGTYPE50_NAME			"zcrypt_msgtype50"
#define MSGTYPE50_VARIANT_DEFAULT	0

#define MSGTYPE50_CRB2_MAX_MSG_SIZE 0x390 /* sizeof(struct type50_crb2_msg) */
#define MSGTYPE50_CRB3_MAX_MSG_SIZE 0x710 /* sizeof(struct type50_crb3_msg) */

#define MSGTYPE_ADJUSTMENT 0x08  /* type04 extension (not needed in type50) */

unsigned int get_rsa_modex_fc(struct ica_rsa_modexpo *, int *);
unsigned int get_rsa_crt_fc(struct ica_rsa_modexpo_crt *, int *);

void zcrypt_msgtype50_init(void);
void zcrypt_msgtype50_exit(void);

#endif /* _ZCRYPT_MSGTYPE50_H_ */
