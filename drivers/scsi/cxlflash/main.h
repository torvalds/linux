/*
 * CXL Flash Device Driver
 *
 * Written by: Manoj N. Kumar <manoj@linux.vnet.ibm.com>, IBM Corporation
 *             Matthew R. Ochs <mrochs@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) 2015 IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _CXLFLASH_MAIN_H
#define _CXLFLASH_MAIN_H

#include <linux/list.h>
#include <linux/types.h>
#include <scsi/scsi.h>
#include <scsi/scsi_device.h>

#define CXLFLASH_NAME		"cxlflash"
#define CXLFLASH_ADAPTER_NAME	"IBM POWER CXL Flash Adapter"

#define PCI_DEVICE_ID_IBM_CORSA		0x04F0
#define PCI_DEVICE_ID_IBM_FLASH_GT	0x0600
#define PCI_DEVICE_ID_IBM_BRIARD	0x0624

/* Since there is only one target, make it 0 */
#define CXLFLASH_TARGET		0
#define CXLFLASH_MAX_CDB_LEN	16

/* Really only one target per bus since the Texan is directly attached */
#define CXLFLASH_MAX_NUM_TARGETS_PER_BUS	1
#define CXLFLASH_MAX_NUM_LUNS_PER_TARGET	65536

#define CXLFLASH_PCI_ERROR_RECOVERY_TIMEOUT	(120 * HZ)

#define NUM_FC_PORTS	CXLFLASH_NUM_FC_PORTS	/* ports per AFU */

/* FC defines */
#define FC_MTIP_CMDCONFIG 0x010
#define FC_MTIP_STATUS 0x018

#define FC_PNAME 0x300
#define FC_CONFIG 0x320
#define FC_CONFIG2 0x328
#define FC_STATUS 0x330
#define FC_ERROR 0x380
#define FC_ERRCAP 0x388
#define FC_ERRMSK 0x390
#define FC_CNT_CRCERR 0x538
#define FC_CRC_THRESH 0x580

#define FC_MTIP_CMDCONFIG_ONLINE	0x20ULL
#define FC_MTIP_CMDCONFIG_OFFLINE	0x40ULL

#define FC_MTIP_STATUS_MASK		0x30ULL
#define FC_MTIP_STATUS_ONLINE		0x20ULL
#define FC_MTIP_STATUS_OFFLINE		0x10ULL

/* TIMEOUT and RETRY definitions */

/* AFU command timeout values */
#define MC_AFU_SYNC_TIMEOUT	5	/* 5 secs */

/* AFU command room retry limit */
#define MC_ROOM_RETRY_CNT	10

/* FC CRC clear periodic timer */
#define MC_CRC_THRESH 100	/* threshold in 5 mins */

#define FC_PORT_STATUS_RETRY_CNT 100	/* 100 100ms retries = 10 seconds */
#define FC_PORT_STATUS_RETRY_INTERVAL_US 100000	/* microseconds */

/* VPD defines */
#define CXLFLASH_VPD_LEN	256
#define WWPN_LEN	16
#define WWPN_BUF_LEN	(WWPN_LEN + 1)

enum undo_level {
	UNDO_NOOP = 0,
	FREE_IRQ,
	UNMAP_ONE,
	UNMAP_TWO,
	UNMAP_THREE
};

struct dev_dependent_vals {
	u64 max_sectors;
	u64 flags;
#define CXLFLASH_NOTIFY_SHUTDOWN   0x0000000000000001ULL
};

struct asyc_intr_info {
	u64 status;
	char *desc;
	u8 port;
	u8 action;
#define CLR_FC_ERROR	0x01
#define LINK_RESET	0x02
#define SCAN_HOST	0x04
};

#endif /* _CXLFLASH_MAIN_H */
