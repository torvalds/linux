/*****************************************************************************/
/* srp.h -- SCSI RDMA Protocol definitions                                   */
/*                                                                           */
/* Written By: Colin Devilbis, IBM Corporation                               */
/*                                                                           */
/* Copyright (C) 2003 IBM Corporation                                        */
/*                                                                           */
/* This program is free software; you can redistribute it and/or modify      */
/* it under the terms of the GNU General Public License as published by      */
/* the Free Software Foundation; either version 2 of the License, or         */
/* (at your option) any later version.                                       */
/*                                                                           */
/* This program is distributed in the hope that it will be useful,           */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             */
/* GNU General Public License for more details.                              */
/*                                                                           */
/* You should have received a copy of the GNU General Public License         */
/* along with this program; if not, write to the Free Software               */
/* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */
/*                                                                           */
/*                                                                           */
/* This file contains structures and definitions for IBM RPA (RS/6000        */
/* platform architecture) implementation of the SRP (SCSI RDMA Protocol)     */
/* standard.  SRP is used on IBM iSeries and pSeries platforms to send SCSI  */
/* commands between logical partitions.                                      */
/*                                                                           */
/* SRP Information Units (IUs) are sent on a "Command/Response Queue" (CRQ)  */
/* between partitions.  The definitions in this file are architected,        */
/* and cannot be changed without breaking compatibility with other versions  */
/* of Linux and other operating systems (AIX, OS/400) that talk this protocol*/
/* between logical partitions                                                */
/*****************************************************************************/
#ifndef VIOSRP_H
#define VIOSRP_H
#include "srp.h"

enum viosrp_crq_formats {
	VIOSRP_SRP_FORMAT = 0x01,
	VIOSRP_MAD_FORMAT = 0x02,
	VIOSRP_OS400_FORMAT = 0x03,
	VIOSRP_AIX_FORMAT = 0x04,
	VIOSRP_LINUX_FORMAT = 0x06,
	VIOSRP_INLINE_FORMAT = 0x07
};

struct viosrp_crq {
	u8 valid;		/* used by RPA */
	u8 format;		/* SCSI vs out-of-band */
	u8 reserved;
	u8 status;		/* non-scsi failure? (e.g. DMA failure) */
	u16 timeout;		/* in seconds */
	u16 IU_length;		/* in bytes */
	u64 IU_data_ptr;	/* the TCE for transferring data */
};

/* MADs are Management requests above and beyond the IUs defined in the SRP
 * standard.  
 */
enum viosrp_mad_types {
	VIOSRP_EMPTY_IU_TYPE = 0x01,
	VIOSRP_ERROR_LOG_TYPE = 0x02,
	VIOSRP_ADAPTER_INFO_TYPE = 0x03,
	VIOSRP_HOST_CONFIG_TYPE = 0x04
};

/* 
 * Common MAD header
 */
struct mad_common {
	u32 type;
	u16 status;
	u16 length;
	u64 tag;
};

/*
 * All SRP (and MAD) requests normally flow from the
 * client to the server.  There is no way for the server to send
 * an asynchronous message back to the client.  The Empty IU is used
 * to hang out a meaningless request to the server so that it can respond
 * asynchrouously with something like a SCSI AER 
 */
struct viosrp_empty_iu {
	struct mad_common common;
	u64 buffer;
	u32 port;
};

struct viosrp_error_log {
	struct mad_common common;
	u64 buffer;
};

struct viosrp_adapter_info {
	struct mad_common common;
	u64 buffer;
};

struct viosrp_host_config {
	struct mad_common common;
	u64 buffer;
};

union mad_iu {
	struct viosrp_empty_iu empty_iu;
	struct viosrp_error_log error_log;
	struct viosrp_adapter_info adapter_info;
	struct viosrp_host_config host_config;
};

union viosrp_iu {
	union srp_iu srp;
	union mad_iu mad;
};

struct mad_adapter_info_data {
	char srp_version[8];
	char partition_name[96];
	u32 partition_number;
	u32 mad_version;
	u32 os_type;
	u32 port_max_txu[8];	/* per-port maximum transfer */
};

#endif
