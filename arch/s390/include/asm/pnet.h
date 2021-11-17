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

int pnet_id_by_dev_port(struct device *dev, unsigned short port, u8 *pnetid);
#endif /* _ASM_S390_PNET_H */
