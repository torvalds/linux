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

#ifndef __BFI_IOC_H__
#define __BFI_IOC_H__

#include "bfi.h"
#include <defs/bfa_defs_ioc.h>

#pragma pack(1)

enum bfi_ioc_h2i_msgs {
	BFI_IOC_H2I_ENABLE_REQ 		= 1,
	BFI_IOC_H2I_DISABLE_REQ 	= 2,
	BFI_IOC_H2I_GETATTR_REQ 	= 3,
	BFI_IOC_H2I_DBG_SYNC	 	= 4,
	BFI_IOC_H2I_DBG_DUMP	 	= 5,
};

enum bfi_ioc_i2h_msgs {
	BFI_IOC_I2H_ENABLE_REPLY	= BFA_I2HM(1),
	BFI_IOC_I2H_DISABLE_REPLY 	= BFA_I2HM(2),
	BFI_IOC_I2H_GETATTR_REPLY 	= BFA_I2HM(3),
	BFI_IOC_I2H_READY_EVENT 	= BFA_I2HM(4),
	BFI_IOC_I2H_HBEAT		= BFA_I2HM(5),
};

/**
 * BFI_IOC_H2I_GETATTR_REQ message
 */
struct bfi_ioc_getattr_req_s {
	struct bfi_mhdr_s	mh;
	union bfi_addr_u	attr_addr;
};

struct bfi_ioc_attr_s {
	wwn_t           mfg_pwwn;       /* Mfg port wwn */
	wwn_t           mfg_nwwn;       /* Mfg node wwn */
	mac_t		mfg_mac;	/* Mfg mac      */
	u16		rsvd_a;
	wwn_t           pwwn;
	wwn_t           nwwn;
	mac_t           mac;            /* PBC or Mfg mac */
	u16        	rsvd_b;
	char            brcd_serialnum[STRSZ(BFA_MFG_SERIALNUM_SIZE)];
	u8         pcie_gen;
	u8         pcie_lanes_orig;
	u8         pcie_lanes;
	u8         rx_bbcredit;	/*  receive buffer credits */
	u32        adapter_prop;	/*  adapter properties     */
	u16        maxfrsize;	/*  max receive frame size */
	char       asic_rev;
	u8         rsvd_c;
	char       fw_version[BFA_VERSION_LEN];
	char       optrom_version[BFA_VERSION_LEN];
	struct bfa_mfg_vpd_s	vpd;
	u32        card_type;	/* card type */
};

/**
 * BFI_IOC_I2H_GETATTR_REPLY message
 */
struct bfi_ioc_getattr_reply_s {
	struct bfi_mhdr_s  mh;		/*  Common msg header          */
	u8		status;	/*  cfg reply status           */
	u8		rsvd[3];
};

/**
 * Firmware memory page offsets
 */
#define BFI_IOC_SMEM_PG0_CB	(0x40)
#define BFI_IOC_SMEM_PG0_CT	(0x180)

/**
 * Firmware trace offset
 */
#define BFI_IOC_TRC_OFF		(0x4b00)
#define BFI_IOC_TRC_ENTS	256

#define BFI_IOC_FW_SIGNATURE	(0xbfadbfad)
#define BFI_IOC_MD5SUM_SZ	4
struct bfi_ioc_image_hdr_s {
	u32        signature;	/*  constant signature */
	u32        rsvd_a;
	u32        exec;		/*  exec vector        */
	u32        param;		/*  parameters         */
	u32        rsvd_b[4];
	u32        md5sum[BFI_IOC_MD5SUM_SZ];
};

/**
 *  BFI_IOC_I2H_READY_EVENT message
 */
struct bfi_ioc_rdy_event_s {
	struct bfi_mhdr_s  mh;			/*  common msg header */
	u8         init_status;	/*  init event status */
	u8         rsvd[3];
};

struct bfi_ioc_hbeat_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u32	   hb_count;	/*  current heart beat count	*/
};

/**
 * IOC hardware/firmware state
 */
enum bfi_ioc_state {
	BFI_IOC_UNINIT 	 = 0,		/*  not initialized                 */
	BFI_IOC_INITING 	 = 1,	/*  h/w is being initialized        */
	BFI_IOC_HWINIT 	 = 2,		/*  h/w is initialized              */
	BFI_IOC_CFG 	 = 3,		/*  IOC configuration in progress   */
	BFI_IOC_OP 		 = 4,	/*  IOC is operational              */
	BFI_IOC_DISABLING 	 = 5,	/*  IOC is being disabled           */
	BFI_IOC_DISABLED 	 = 6,	/*  IOC is disabled                 */
	BFI_IOC_CFG_DISABLED = 7,	/*  IOC is being disabled;transient */
	BFI_IOC_FAIL       = 8,		/*  IOC heart-beat failure          */
	BFI_IOC_MEMTEST      = 9,	/*  IOC is doing memtest            */
};

#define BFI_IOC_ENDIAN_SIG  0x12345678

enum {
	BFI_ADAPTER_TYPE_FC   = 0x01,		/*  FC adapters           */
	BFI_ADAPTER_TYPE_MK   = 0x0f0000,	/*  adapter type mask     */
	BFI_ADAPTER_TYPE_SH   = 16,	        /*  adapter type shift    */
	BFI_ADAPTER_NPORTS_MK = 0xff00,		/*  number of ports mask  */
	BFI_ADAPTER_NPORTS_SH = 8,	        /*  number of ports shift */
	BFI_ADAPTER_SPEED_MK  = 0xff,		/*  adapter speed mask    */
	BFI_ADAPTER_SPEED_SH  = 0,	        /*  adapter speed shift   */
	BFI_ADAPTER_PROTO     = 0x100000,	/*  prototype adapaters   */
	BFI_ADAPTER_TTV       = 0x200000,	/*  TTV debug capable     */
	BFI_ADAPTER_UNSUPP    = 0x400000,	/*  unknown adapter type  */
};

#define BFI_ADAPTER_GETP(__prop, __adap_prop)          		\
    (((__adap_prop) & BFI_ADAPTER_ ## __prop ## _MK) >>         \
     BFI_ADAPTER_ ## __prop ## _SH)
#define BFI_ADAPTER_SETP(__prop, __val)         		\
    ((__val) << BFI_ADAPTER_ ## __prop ## _SH)
#define BFI_ADAPTER_IS_PROTO(__adap_type)   			\
    ((__adap_type) & BFI_ADAPTER_PROTO)
#define BFI_ADAPTER_IS_TTV(__adap_type)     			\
    ((__adap_type) & BFI_ADAPTER_TTV)
#define BFI_ADAPTER_IS_UNSUPP(__adap_type)  			\
    ((__adap_type) & BFI_ADAPTER_UNSUPP)
#define BFI_ADAPTER_IS_SPECIAL(__adap_type)                     \
    ((__adap_type) & (BFI_ADAPTER_TTV | BFI_ADAPTER_PROTO |     \
			BFI_ADAPTER_UNSUPP))

/**
 * BFI_IOC_H2I_ENABLE_REQ & BFI_IOC_H2I_DISABLE_REQ messages
 */
struct bfi_ioc_ctrl_req_s {
	struct bfi_mhdr_s	mh;
	u8			ioc_class;
	u8         	rsvd[3];
};

/**
 * BFI_IOC_I2H_ENABLE_REPLY & BFI_IOC_I2H_DISABLE_REPLY messages
 */
struct bfi_ioc_ctrl_reply_s {
	struct bfi_mhdr_s  mh;		/*  Common msg header     */
	u8         status;		/*  enable/disable status */
	u8         rsvd[3];
};

#define BFI_IOC_MSGSZ   8
/**
 * H2I Messages
 */
union bfi_ioc_h2i_msg_u {
	struct bfi_mhdr_s 	mh;
	struct bfi_ioc_ctrl_req_s enable_req;
	struct bfi_ioc_ctrl_req_s disable_req;
	struct bfi_ioc_getattr_req_s getattr_req;
	u32       		mboxmsg[BFI_IOC_MSGSZ];
};

/**
 * I2H Messages
 */
union bfi_ioc_i2h_msg_u {
	struct bfi_mhdr_s      	mh;
	struct bfi_ioc_rdy_event_s 	rdy_event;
	u32       		mboxmsg[BFI_IOC_MSGSZ];
};

#pragma pack()

#endif /* __BFI_IOC_H__ */

