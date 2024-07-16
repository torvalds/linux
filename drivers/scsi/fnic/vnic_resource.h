/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */
#ifndef _VNIC_RESOURCE_H_
#define _VNIC_RESOURCE_H_

#define VNIC_RES_MAGIC		0x766E6963L	/* 'vnic' */
#define VNIC_RES_VERSION	0x00000000L

/* vNIC resource types */
enum vnic_res_type {
	RES_TYPE_EOL,			/* End-of-list */
	RES_TYPE_WQ,			/* Work queues */
	RES_TYPE_RQ,			/* Receive queues */
	RES_TYPE_CQ,			/* Completion queues */
	RES_TYPE_RSVD1,
	RES_TYPE_NIC_CFG,		/* Enet NIC config registers */
	RES_TYPE_RSVD2,
	RES_TYPE_RSVD3,
	RES_TYPE_RSVD4,
	RES_TYPE_RSVD5,
	RES_TYPE_INTR_CTRL,		/* Interrupt ctrl table */
	RES_TYPE_INTR_TABLE,		/* MSI/MSI-X Interrupt table */
	RES_TYPE_INTR_PBA,		/* MSI/MSI-X PBA table */
	RES_TYPE_INTR_PBA_LEGACY,	/* Legacy intr status */
	RES_TYPE_RSVD6,
	RES_TYPE_RSVD7,
	RES_TYPE_DEVCMD,		/* Device command region */
	RES_TYPE_PASS_THRU_PAGE,	/* Pass-thru page */
	RES_TYPE_SUBVNIC,               /* subvnic resource type */
	RES_TYPE_MQ_WQ,                 /* MQ Work queues */
	RES_TYPE_MQ_RQ,                 /* MQ Receive queues */
	RES_TYPE_MQ_CQ,                 /* MQ Completion queues */
	RES_TYPE_DEPRECATED1,           /* Old version of devcmd 2 */
	RES_TYPE_DEPRECATED2,           /* Old version of devcmd 2 */
	RES_TYPE_DEVCMD2,               /* Device control region */

	RES_TYPE_MAX,			/* Count of resource types */
};

struct vnic_resource_header {
	u32 magic;
	u32 version;
};

struct vnic_resource {
	u8 type;
	u8 bar;
	u8 pad[2];
	u32 bar_offset;
	u32 count;
};

#endif /* _VNIC_RESOURCE_H_ */
