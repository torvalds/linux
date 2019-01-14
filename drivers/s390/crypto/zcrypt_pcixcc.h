/* SPDX-License-Identifier: GPL-2.0+ */
/*
 *  zcrypt 2.1.0
 *
 *  Copyright IBM Corp. 2001, 2012
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *  Major cleanup & driver split: Martin Schwidefsky <schwidefsky@de.ibm.com>
 *  MSGTYPE restruct:		  Holger Dengler <hd@linux.vnet.ibm.com>
 */

#ifndef _ZCRYPT_PCIXCC_H_
#define _ZCRYPT_PCIXCC_H_

int zcrypt_pcixcc_init(void);
void zcrypt_pcixcc_exit(void);

#endif /* _ZCRYPT_PCIXCC_H_ */
