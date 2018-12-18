/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  IBM System z PNET ID Support
 *
 *    Copyright IBM Corp. 2018
 */

#ifndef _ASM_S390_PNET_H
#define _ASM_S390_PNET_H

#include <linux/device.h>
#include <linux/types.h>

#define PNETIDS_LEN		64	/* Total utility string length in bytes
					 * to cover up to 4 PNETIDs of 16 bytes
					 * for up to 4 device ports
					 */
#define MAX_PNETID_LEN		16	/* Max.length of a single port PNETID */
#define MAX_PNETID_PORTS	(PNETIDS_LEN / MAX_PNETID_LEN)
					/* Max. # of ports with a PNETID */

int pnet_id_by_dev_port(struct device *dev, unsigned short port, u8 *pnetid);
#endif /* _ASM_S390_PNET_H */
