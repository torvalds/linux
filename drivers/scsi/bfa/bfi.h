/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
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

#ifndef __BFI_H__
#define __BFI_H__

#include "bfa_defs.h"
#include "bfa_defs_svc.h"

#pragma pack(1)

/*
 * BFI FW image type
 */
#define	BFI_FLASH_CHUNK_SZ			256	/*  Flash chunk size */
#define	BFI_FLASH_CHUNK_SZ_WORDS	(BFI_FLASH_CHUNK_SZ/sizeof(u32))
enum {
	BFI_IMAGE_CB_FC,
	BFI_IMAGE_CT_FC,
	BFI_IMAGE_CT_CNA,
	BFI_IMAGE_MAX,
};

/*
 * Msg header common to all msgs
 */
struct bfi_mhdr_s {
	u8		msg_class;	/*  @ref bfi_mclass_t		    */
	u8		msg_id;		/*  msg opcode with in the class   */
	union {
		struct {
			u8	rsvd;
			u8	lpu_id;	/*  msg destination		    */
		} h2i;
		u16	i2htok;	/*  token in msgs to host	    */
	} mtag;
};

#define bfi_h2i_set(_mh, _mc, _op, _lpuid) do {		\
	(_mh).msg_class		= (_mc);      \
	(_mh).msg_id		= (_op);      \
	(_mh).mtag.h2i.lpu_id	= (_lpuid);      \
} while (0)

#define bfi_i2h_set(_mh, _mc, _op, _i2htok) do {		\
	(_mh).msg_class		= (_mc);      \
	(_mh).msg_id		= (_op);      \
	(_mh).mtag.i2htok	= (_i2htok);      \
} while (0)

/*
 * Message opcodes: 0-127 to firmware, 128-255 to host
 */
#define BFI_I2H_OPCODE_BASE	128
#define BFA_I2HM(_x)		((_x) + BFI_I2H_OPCODE_BASE)

/*
 ****************************************************************************
 *
 * Scatter Gather Element and Page definition
 *
 ****************************************************************************
 */

#define BFI_SGE_INLINE	1
#define BFI_SGE_INLINE_MAX	(BFI_SGE_INLINE + 1)

/*
 * SG Flags
 */
enum {
	BFI_SGE_DATA		= 0,	/*  data address, not last	     */
	BFI_SGE_DATA_CPL	= 1,	/*  data addr, last in current page */
	BFI_SGE_DATA_LAST	= 3,	/*  data address, last		     */
	BFI_SGE_LINK		= 2,	/*  link address		     */
	BFI_SGE_PGDLEN		= 2,	/*  cumulative data length for page */
};

/*
 * DMA addresses
 */
union bfi_addr_u {
	struct {
		__be32	addr_lo;
		__be32	addr_hi;
	} a32;
};

/*
 * Scatter Gather Element
 */
struct bfi_sge_s {
#ifdef __BIG_ENDIAN
	u32	flags:2,
			rsvd:2,
			sg_len:28;
#else
	u32	sg_len:28,
			rsvd:2,
			flags:2;
#endif
	union bfi_addr_u sga;
};

/*
 * Scatter Gather Page
 */
#define BFI_SGPG_DATA_SGES		7
#define BFI_SGPG_SGES_MAX		(BFI_SGPG_DATA_SGES + 1)
#define BFI_SGPG_RSVD_WD_LEN	8
struct bfi_sgpg_s {
	struct bfi_sge_s sges[BFI_SGPG_SGES_MAX];
	u32	rsvd[BFI_SGPG_RSVD_WD_LEN];
};

/*
 * Large Message structure - 128 Bytes size Msgs
 */
#define BFI_LMSG_SZ		128
#define BFI_LMSG_PL_WSZ	\
			((BFI_LMSG_SZ - sizeof(struct bfi_mhdr_s)) / 4)

struct bfi_msg_s {
	struct bfi_mhdr_s mhdr;
	u32	pl[BFI_LMSG_PL_WSZ];
};

/*
 * Mailbox message structure
 */
#define BFI_MBMSG_SZ		7
struct bfi_mbmsg_s {
	struct bfi_mhdr_s	mh;
	u32		pl[BFI_MBMSG_SZ];
};

/*
 * Message Classes
 */
enum bfi_mclass {
	BFI_MC_IOC		= 1,	/*  IO Controller (IOC)	    */
	BFI_MC_FCPORT		= 5,	/*  FC port			    */
	BFI_MC_IOCFC		= 6,	/*  FC - IO Controller (IOC)	    */
	BFI_MC_LL               = 7,    /*  Link Layer                      */
	BFI_MC_UF		= 8,	/*  Unsolicited frame receive	    */
	BFI_MC_FCXP		= 9,	/*  FC Transport		    */
	BFI_MC_LPS		= 10,	/*  lport fc login services	    */
	BFI_MC_RPORT		= 11,	/*  Remote port		    */
	BFI_MC_ITNIM		= 12,	/*  I-T nexus (Initiator mode)	    */
	BFI_MC_IOIM_READ	= 13,	/*  read IO (Initiator mode)	    */
	BFI_MC_IOIM_WRITE	= 14,	/*  write IO (Initiator mode)	    */
	BFI_MC_IOIM_IO		= 15,	/*  IO (Initiator mode)	    */
	BFI_MC_IOIM		= 16,	/*  IO (Initiator mode)	    */
	BFI_MC_IOIM_IOCOM	= 17,	/*  good IO completion		    */
	BFI_MC_TSKIM		= 18,	/*  Initiator Task management	    */
	BFI_MC_PORT		= 21,	/*  Physical port		    */
	BFI_MC_MAX		= 32
};

#define BFI_IOC_MAX_CQS		4
#define BFI_IOC_MAX_CQS_ASIC	8
#define BFI_IOC_MSGLEN_MAX	32	/* 32 bytes */

#define BFI_BOOT_TYPE_OFF		8
#define BFI_BOOT_LOADER_OFF		12

#define BFI_BOOT_TYPE_NORMAL		0
#define	BFI_BOOT_TYPE_FLASH		1
#define	BFI_BOOT_TYPE_MEMTEST		2

#define BFI_BOOT_LOADER_OS		0
#define BFI_BOOT_LOADER_BIOS		1
#define BFI_BOOT_LOADER_UEFI		2

/*
 *----------------------------------------------------------------------
 *				IOC
 *----------------------------------------------------------------------
 */

enum bfi_ioc_h2i_msgs {
	BFI_IOC_H2I_ENABLE_REQ		= 1,
	BFI_IOC_H2I_DISABLE_REQ		= 2,
	BFI_IOC_H2I_GETATTR_REQ		= 3,
	BFI_IOC_H2I_DBG_SYNC		= 4,
	BFI_IOC_H2I_DBG_DUMP		= 5,
};

enum bfi_ioc_i2h_msgs {
	BFI_IOC_I2H_ENABLE_REPLY	= BFA_I2HM(1),
	BFI_IOC_I2H_DISABLE_REPLY	= BFA_I2HM(2),
	BFI_IOC_I2H_GETATTR_REPLY	= BFA_I2HM(3),
	BFI_IOC_I2H_READY_EVENT		= BFA_I2HM(4),
	BFI_IOC_I2H_HBEAT		= BFA_I2HM(5),
};

/*
 * BFI_IOC_H2I_GETATTR_REQ message
 */
struct bfi_ioc_getattr_req_s {
	struct bfi_mhdr_s	mh;
	union bfi_addr_u	attr_addr;
};

struct bfi_ioc_attr_s {
	wwn_t		mfg_pwwn;	/*  Mfg port wwn	   */
	wwn_t		mfg_nwwn;	/*  Mfg node wwn	   */
	mac_t		mfg_mac;	/*  Mfg mac		   */
	u16	rsvd_a;
	wwn_t		pwwn;
	wwn_t		nwwn;
	mac_t		mac;		/*  PBC or Mfg mac	   */
	u16	rsvd_b;
	mac_t		fcoe_mac;
	u16	rsvd_c;
	char		brcd_serialnum[STRSZ(BFA_MFG_SERIALNUM_SIZE)];
	u8		pcie_gen;
	u8		pcie_lanes_orig;
	u8		pcie_lanes;
	u8		rx_bbcredit;	/*  receive buffer credits */
	u32	adapter_prop;	/*  adapter properties     */
	u16	maxfrsize;	/*  max receive frame size */
	char		asic_rev;
	u8		rsvd_d;
	char		fw_version[BFA_VERSION_LEN];
	char		optrom_version[BFA_VERSION_LEN];
	struct		bfa_mfg_vpd_s	vpd;
	u32	card_type;	/*  card type			*/
};

/*
 * BFI_IOC_I2H_GETATTR_REPLY message
 */
struct bfi_ioc_getattr_reply_s {
	struct	bfi_mhdr_s	mh;	/*  Common msg header		*/
	u8			status;	/*  cfg reply status		*/
	u8			rsvd[3];
};

/*
 * Firmware memory page offsets
 */
#define BFI_IOC_SMEM_PG0_CB	(0x40)
#define BFI_IOC_SMEM_PG0_CT	(0x180)

/*
 * Firmware statistic offset
 */
#define BFI_IOC_FWSTATS_OFF	(0x6B40)
#define BFI_IOC_FWSTATS_SZ	(4096)

/*
 * Firmware trace offset
 */
#define BFI_IOC_TRC_OFF		(0x4b00)
#define BFI_IOC_TRC_ENTS	256

#define BFI_IOC_FW_SIGNATURE	(0xbfadbfad)
#define BFI_IOC_MD5SUM_SZ	4
struct bfi_ioc_image_hdr_s {
	u32	signature;	/*  constant signature */
	u32	rsvd_a;
	u32	exec;		/*  exec vector	*/
	u32	param;		/*  parameters		*/
	u32	rsvd_b[4];
	u32	md5sum[BFI_IOC_MD5SUM_SZ];
};

/*
 *  BFI_IOC_I2H_READY_EVENT message
 */
struct bfi_ioc_rdy_event_s {
	struct bfi_mhdr_s	mh;		/*  common msg header */
	u8			init_status;	/*  init event status */
	u8			rsvd[3];
};

struct bfi_ioc_hbeat_s {
	struct bfi_mhdr_s  mh;		/*  common msg header		*/
	u32	   hb_count;	/*  current heart beat count	*/
};

/*
 * IOC hardware/firmware state
 */
enum bfi_ioc_state {
	BFI_IOC_UNINIT		= 0,	/*  not initialized		     */
	BFI_IOC_INITING		= 1,	/*  h/w is being initialized	     */
	BFI_IOC_HWINIT		= 2,	/*  h/w is initialized		     */
	BFI_IOC_CFG		= 3,	/*  IOC configuration in progress   */
	BFI_IOC_OP		= 4,	/*  IOC is operational		     */
	BFI_IOC_DISABLING	= 5,	/*  IOC is being disabled	     */
	BFI_IOC_DISABLED	= 6,	/*  IOC is disabled		     */
	BFI_IOC_CFG_DISABLED	= 7,	/*  IOC is being disabled;transient */
	BFI_IOC_FAIL		= 8,	/*  IOC heart-beat failure	     */
	BFI_IOC_MEMTEST		= 9,	/*  IOC is doing memtest	     */
};

#define BFI_IOC_ENDIAN_SIG  0x12345678

enum {
	BFI_ADAPTER_TYPE_FC	= 0x01,		/*  FC adapters	   */
	BFI_ADAPTER_TYPE_MK	= 0x0f0000,	/*  adapter type mask     */
	BFI_ADAPTER_TYPE_SH	= 16,	        /*  adapter type shift    */
	BFI_ADAPTER_NPORTS_MK	= 0xff00,	/*  number of ports mask  */
	BFI_ADAPTER_NPORTS_SH	= 8,	        /*  number of ports shift */
	BFI_ADAPTER_SPEED_MK	= 0xff,		/*  adapter speed mask    */
	BFI_ADAPTER_SPEED_SH	= 0,	        /*  adapter speed shift   */
	BFI_ADAPTER_PROTO	= 0x100000,	/*  prototype adapaters   */
	BFI_ADAPTER_TTV		= 0x200000,	/*  TTV debug capable     */
	BFI_ADAPTER_UNSUPP	= 0x400000,	/*  unknown adapter type  */
};

#define BFI_ADAPTER_GETP(__prop, __adap_prop)			\
	(((__adap_prop) & BFI_ADAPTER_ ## __prop ## _MK) >>	\
		BFI_ADAPTER_ ## __prop ## _SH)
#define BFI_ADAPTER_SETP(__prop, __val)				\
	((__val) << BFI_ADAPTER_ ## __prop ## _SH)
#define BFI_ADAPTER_IS_PROTO(__adap_type)			\
	((__adap_type) & BFI_ADAPTER_PROTO)
#define BFI_ADAPTER_IS_TTV(__adap_type)				\
	((__adap_type) & BFI_ADAPTER_TTV)
#define BFI_ADAPTER_IS_UNSUPP(__adap_type)			\
	((__adap_type) & BFI_ADAPTER_UNSUPP)
#define BFI_ADAPTER_IS_SPECIAL(__adap_type)			\
	((__adap_type) & (BFI_ADAPTER_TTV | BFI_ADAPTER_PROTO |	\
			BFI_ADAPTER_UNSUPP))

/*
 * BFI_IOC_H2I_ENABLE_REQ & BFI_IOC_H2I_DISABLE_REQ messages
 */
struct bfi_ioc_ctrl_req_s {
	struct bfi_mhdr_s	mh;
	u8			ioc_class;
	u8			rsvd[3];
	u32		tv_sec;
};
#define bfi_ioc_enable_req_t struct bfi_ioc_ctrl_req_s;
#define bfi_ioc_disable_req_t struct bfi_ioc_ctrl_req_s;

/*
 * BFI_IOC_I2H_ENABLE_REPLY & BFI_IOC_I2H_DISABLE_REPLY messages
 */
struct bfi_ioc_ctrl_reply_s {
	struct bfi_mhdr_s	mh;		/*  Common msg header     */
	u8			status;		/*  enable/disable status */
	u8			rsvd[3];
};
#define bfi_ioc_enable_reply_t struct bfi_ioc_ctrl_reply_s;
#define bfi_ioc_disable_reply_t struct bfi_ioc_ctrl_reply_s;

#define BFI_IOC_MSGSZ   8
/*
 * H2I Messages
 */
union bfi_ioc_h2i_msg_u {
	struct bfi_mhdr_s		mh;
	struct bfi_ioc_ctrl_req_s	enable_req;
	struct bfi_ioc_ctrl_req_s	disable_req;
	struct bfi_ioc_getattr_req_s	getattr_req;
	u32			mboxmsg[BFI_IOC_MSGSZ];
};

/*
 * I2H Messages
 */
union bfi_ioc_i2h_msg_u {
	struct bfi_mhdr_s		mh;
	struct bfi_ioc_rdy_event_s	rdy_event;
	u32			mboxmsg[BFI_IOC_MSGSZ];
};


/*
 *----------------------------------------------------------------------
 *				PBC
 *----------------------------------------------------------------------
 */

#define BFI_PBC_MAX_BLUNS	8
#define BFI_PBC_MAX_VPORTS	16

/*
 * PBC boot lun configuration
 */
struct bfi_pbc_blun_s {
	wwn_t		tgt_pwwn;
	struct scsi_lun	tgt_lun;
};

/*
 * PBC virtual port configuration
 */
struct bfi_pbc_vport_s {
	wwn_t		vp_pwwn;
	wwn_t		vp_nwwn;
};

/*
 * BFI pre-boot configuration information
 */
struct bfi_pbc_s {
	u8		port_enabled;
	u8		boot_enabled;
	u8		nbluns;
	u8		nvports;
	u8		port_speed;
	u8		rsvd_a;
	u16	hss;
	wwn_t		pbc_pwwn;
	wwn_t		pbc_nwwn;
	struct bfi_pbc_blun_s blun[BFI_PBC_MAX_BLUNS];
	struct bfi_pbc_vport_s vport[BFI_PBC_MAX_VPORTS];
};

/*
 *----------------------------------------------------------------------
 *				MSGQ
 *----------------------------------------------------------------------
 */
#define BFI_MSGQ_FULL(_q)	(((_q->pi + 1) % _q->q_depth) == _q->ci)
#define BFI_MSGQ_EMPTY(_q)	(_q->pi == _q->ci)
#define BFI_MSGQ_UPDATE_CI(_q)	(_q->ci = (_q->ci + 1) % _q->q_depth)
#define BFI_MSGQ_UPDATE_PI(_q)	(_q->pi = (_q->pi + 1) % _q->q_depth)

/* q_depth must be power of 2 */
#define BFI_MSGQ_FREE_CNT(_q)	((_q->ci - _q->pi - 1) & (_q->q_depth - 1))

enum bfi_msgq_h2i_msgs_e {
	BFI_MSGQ_H2I_INIT_REQ	= 1,
	BFI_MSGQ_H2I_DOORBELL	= 2,
	BFI_MSGQ_H2I_SHUTDOWN	= 3,
};

enum bfi_msgq_i2h_msgs_e {
	BFI_MSGQ_I2H_INIT_RSP	= 1,
	BFI_MSGQ_I2H_DOORBELL	= 2,
};


/* Messages(commands/responsed/AENS will have the following header */
struct bfi_msgq_mhdr_s {
	u8		msg_class;
	u8		msg_id;
	u16	msg_token;
	u16	num_entries;
	u8		enet_id;
	u8		rsvd[1];
};

#define bfi_msgq_mhdr_set(_mh, _mc, _mid, _tok, _enet_id) do {        \
	(_mh).msg_class		= (_mc);      \
	(_mh).msg_id		= (_mid);      \
	(_mh).msg_token		= (_tok);      \
	(_mh).enet_id		= (_enet_id);      \
} while (0)

/*
 * Mailbox  for messaging interface
 *
*/
#define BFI_MSGQ_CMD_ENTRY_SIZE		(64)    /* TBD */
#define BFI_MSGQ_RSP_ENTRY_SIZE		(64)    /* TBD */
#define BFI_MSGQ_MSG_SIZE_MAX		(2048)  /* TBD */

struct bfi_msgq_s {
	union bfi_addr_u addr;
	u16 q_depth;     /* Total num of entries in the queue */
	u8 rsvd[2];
};

/* BFI_ENET_MSGQ_CFG_REQ TBD init or cfg? */
struct bfi_msgq_cfg_req_s {
	struct bfi_mhdr_s mh;
	struct bfi_msgq_s cmdq;
	struct bfi_msgq_s rspq;
};

/* BFI_ENET_MSGQ_CFG_RSP */
struct bfi_msgq_cfg_rsp_s {
	struct bfi_mhdr_s mh;
	u8 cmd_status;
	u8 rsvd[3];
};


/* BFI_MSGQ_H2I_DOORBELL */
struct bfi_msgq_h2i_db_s {
	struct bfi_mhdr_s mh;
	u16 cmdq_pi;
	u16 rspq_ci;
};

/* BFI_MSGQ_I2H_DOORBELL */
struct bfi_msgq_i2h_db_s {
	struct bfi_mhdr_s mh;
	u16 rspq_pi;
	u16 cmdq_ci;
};

#pragma pack()

/* BFI port specific */
#pragma pack(1)

enum bfi_port_h2i {
	BFI_PORT_H2I_ENABLE_REQ         = (1),
	BFI_PORT_H2I_DISABLE_REQ        = (2),
	BFI_PORT_H2I_GET_STATS_REQ      = (3),
	BFI_PORT_H2I_CLEAR_STATS_REQ    = (4),
};

enum bfi_port_i2h {
	BFI_PORT_I2H_ENABLE_RSP         = BFA_I2HM(1),
	BFI_PORT_I2H_DISABLE_RSP        = BFA_I2HM(2),
	BFI_PORT_I2H_GET_STATS_RSP      = BFA_I2HM(3),
	BFI_PORT_I2H_CLEAR_STATS_RSP    = BFA_I2HM(4),
};

/*
 * Generic REQ type
 */
struct bfi_port_generic_req_s {
	struct bfi_mhdr_s  mh;          /*  msg header		*/
	u32     msgtag;         /*  msgtag for reply                */
	u32     rsvd;
};

/*
 * Generic RSP type
 */
struct bfi_port_generic_rsp_s {
	struct bfi_mhdr_s  mh;          /*  common msg header               */
	u8              status;         /*  port enable status              */
	u8              rsvd[3];
	u32     msgtag;         /*  msgtag for reply                */
};

/*
 * BFI_PORT_H2I_GET_STATS_REQ
 */
struct bfi_port_get_stats_req_s {
	struct bfi_mhdr_s  mh;          /*  common msg header               */
	union bfi_addr_u   dma_addr;
};

union bfi_port_h2i_msg_u {
	struct bfi_mhdr_s               mh;
	struct bfi_port_generic_req_s   enable_req;
	struct bfi_port_generic_req_s   disable_req;
	struct bfi_port_get_stats_req_s getstats_req;
	struct bfi_port_generic_req_s   clearstats_req;
};

union bfi_port_i2h_msg_u {
	struct bfi_mhdr_s               mh;
	struct bfi_port_generic_rsp_s   enable_rsp;
	struct bfi_port_generic_rsp_s   disable_rsp;
	struct bfi_port_generic_rsp_s   getstats_rsp;
	struct bfi_port_generic_rsp_s   clearstats_rsp;
};

#pragma pack()

#endif /* __BFI_H__ */
