/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __BFI_IOCFC_H__
#define __BFI_IOCFC_H__

#include "bfi.h"
#include <defs/bfa_defs_ioc.h>
#include <defs/bfa_defs_iocfc.h>
#include <defs/bfa_defs_boot.h>

#pragma pack(1)

enum bfi_iocfc_h2i_msgs {
	BFI_IOCFC_H2I_CFG_REQ 		= 1,
	BFI_IOCFC_H2I_GET_STATS_REQ 	= 2,
	BFI_IOCFC_H2I_CLEAR_STATS_REQ	= 3,
	BFI_IOCFC_H2I_SET_INTR_REQ 	= 4,
	BFI_IOCFC_H2I_UPDATEQ_REQ = 5,
};

enum bfi_iocfc_i2h_msgs {
	BFI_IOCFC_I2H_CFG_REPLY		= BFA_I2HM(1),
	BFI_IOCFC_I2H_GET_STATS_RSP 	= BFA_I2HM(2),
	BFI_IOCFC_I2H_CLEAR_STATS_RSP	= BFA_I2HM(3),
	BFI_IOCFC_I2H_UPDATEQ_RSP = BFA_I2HM(5),
};

struct bfi_iocfc_cfg_s {
	u8         num_cqs; 	/*  Number of CQs to be used     */
	u8         sense_buf_len;	/*  SCSI sense length            */
	u8         trunk_enabled;	/*  port trunking enabled        */
	u8         trunk_ports;	/*  trunk ports bit map          */
	u32        endian_sig;	/*  endian signature of host     */

	/**
	 * Request and response circular queue base addresses, size and
	 * shadow index pointers.
	 */
	union bfi_addr_u  req_cq_ba[BFI_IOC_MAX_CQS];
	union bfi_addr_u  req_shadow_ci[BFI_IOC_MAX_CQS];
	u16    req_cq_elems[BFI_IOC_MAX_CQS];
	union bfi_addr_u  rsp_cq_ba[BFI_IOC_MAX_CQS];
	union bfi_addr_u  rsp_shadow_pi[BFI_IOC_MAX_CQS];
	u16    rsp_cq_elems[BFI_IOC_MAX_CQS];

	union bfi_addr_u  stats_addr;	/*  DMA-able address for stats	  */
	union bfi_addr_u  cfgrsp_addr;	/*  config response dma address  */
	union bfi_addr_u  ioim_snsbase;  /*  IO sense buffer base address */
	struct bfa_iocfc_intr_attr_s intr_attr; /*  IOC interrupt attributes */
};

/**
 * Boot target wwn information for this port. This contains either the stored
 * or discovered boot target port wwns for the port.
 */
struct bfi_iocfc_bootwwns {
	wwn_t		wwn[BFA_BOOT_BOOTLUN_MAX];
	u8		nwwns;
	u8		rsvd[7];
};

struct bfi_iocfc_cfgrsp_s {
	struct bfa_iocfc_fwcfg_s	fwcfg;
	struct bfa_iocfc_intr_attr_s	intr_attr;
	struct bfi_iocfc_bootwwns	bootwwns;
};

/**
 * BFI_IOCFC_H2I_CFG_REQ message
 */
struct bfi_iocfc_cfg_req_s {
	struct bfi_mhdr_s      mh;
	union bfi_addr_u      ioc_cfg_dma_addr;
};

/**
 * BFI_IOCFC_I2H_CFG_REPLY message
 */
struct bfi_iocfc_cfg_reply_s {
	struct bfi_mhdr_s  mh;		/*  Common msg header          */
	u8         cfg_success;	/*  cfg reply status           */
	u8         lpu_bm;		/*  LPUs assigned for this IOC */
	u8         rsvd[2];
};

/**
 *  BFI_IOCFC_H2I_GET_STATS_REQ & BFI_IOCFC_H2I_CLEAR_STATS_REQ messages
 */
struct bfi_iocfc_stats_req_s {
	struct bfi_mhdr_s mh;		/*  msg header            */
	u32        msgtag;		/*  msgtag for reply      */
};

/**
 * BFI_IOCFC_I2H_GET_STATS_RSP & BFI_IOCFC_I2H_CLEAR_STATS_RSP messages
 */
struct bfi_iocfc_stats_rsp_s {
	struct bfi_mhdr_s mh;		/*  common msg header     */
	u8         status;		/*  reply status          */
	u8         rsvd[3];
	u32        msgtag;		/*  msgtag for reply      */
};

/**
 * BFI_IOCFC_H2I_SET_INTR_REQ message
 */
struct bfi_iocfc_set_intr_req_s {
	struct bfi_mhdr_s mh;		/*  common msg header     */
	u8		coalesce;	/*  enable intr coalescing*/
	u8         rsvd[3];
	u16	delay;		/*  delay timer 0..1125us  */
	u16	latency;	/*  latency timer 0..225us */
};

/**
 * BFI_IOCFC_H2I_UPDATEQ_REQ message
 */
struct bfi_iocfc_updateq_req_s {
	struct bfi_mhdr_s mh;		/*  common msg header     */
	u32 reqq_ba;			/*  reqq base addr        */
	u32 rspq_ba;			/*  rspq base addr        */
	u32 reqq_sci;			/*  reqq shadow ci        */
	u32 rspq_spi;			/*  rspq shadow pi        */
};

/**
 * BFI_IOCFC_I2H_UPDATEQ_RSP message
 */
struct bfi_iocfc_updateq_rsp_s {
	struct bfi_mhdr_s mh;		/*  common msg header     */
	u8         status;		/*  updateq  status       */
	u8         rsvd[3];
};

/**
 * H2I Messages
 */
union bfi_iocfc_h2i_msg_u {
	struct bfi_mhdr_s 		mh;
	struct bfi_iocfc_cfg_req_s 	cfg_req;
	struct bfi_iocfc_stats_req_s stats_get;
	struct bfi_iocfc_stats_req_s stats_clr;
	struct bfi_iocfc_updateq_req_s updateq_req;
	u32       			mboxmsg[BFI_IOC_MSGSZ];
};

/**
 * I2H Messages
 */
union bfi_iocfc_i2h_msg_u {
	struct bfi_mhdr_s      		mh;
	struct bfi_iocfc_cfg_reply_s 		cfg_reply;
	struct bfi_iocfc_stats_rsp_s stats_get_rsp;
	struct bfi_iocfc_stats_rsp_s stats_clr_rsp;
	struct bfi_iocfc_updateq_rsp_s updateq_rsp;
	u32       			mboxmsg[BFI_IOC_MSGSZ];
};

#pragma pack()

#endif /* __BFI_IOCFC_H__ */

