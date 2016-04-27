/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014- QLogic Corporation.
 * All rights reserved
 * www.qlogic.com
 *
 * Linux driver for QLogic BR-series Fibre Channel Host Bus Adapter.
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

/* Per dma segment max size */
#define BFI_MEM_DMA_SEG_SZ	(131072)

/* Get number of dma segments required */
#define BFI_MEM_DMA_NSEGS(_num_reqs, _req_sz)				\
	((u16)(((((_num_reqs) * (_req_sz)) + BFI_MEM_DMA_SEG_SZ - 1) &	\
	 ~(BFI_MEM_DMA_SEG_SZ - 1)) / BFI_MEM_DMA_SEG_SZ))

/* Get num dma reqs - that fit in a segment */
#define BFI_MEM_NREQS_SEG(_rqsz) (BFI_MEM_DMA_SEG_SZ / (_rqsz))

/* Get segment num from tag */
#define BFI_MEM_SEG_FROM_TAG(_tag, _rqsz) ((_tag) / BFI_MEM_NREQS_SEG(_rqsz))

/* Get dma req offset in a segment */
#define BFI_MEM_SEG_REQ_OFFSET(_tag, _sz)	\
	((_tag) - (BFI_MEM_SEG_FROM_TAG(_tag, _sz) * BFI_MEM_NREQS_SEG(_sz)))

/*
 * BFI FW image type
 */
#define	BFI_FLASH_CHUNK_SZ			256	/*  Flash chunk size */
#define	BFI_FLASH_CHUNK_SZ_WORDS	(BFI_FLASH_CHUNK_SZ/sizeof(u32))
#define BFI_FLASH_IMAGE_SZ		0x100000

/*
 * Msg header common to all msgs
 */
struct bfi_mhdr_s {
	u8		msg_class;	/*  @ref bfi_mclass_t		    */
	u8		msg_id;		/*  msg opcode with in the class   */
	union {
		struct {
			u8	qid;
			u8	fn_lpu;	/*  msg destination		    */
		} h2i;
		u16	i2htok;	/*  token in msgs to host	    */
	} mtag;
};

#define bfi_fn_lpu(__fn, __lpu)	((__fn) << 1 | (__lpu))
#define bfi_mhdr_2_fn(_mh)	((_mh)->mtag.h2i.fn_lpu >> 1)

#define bfi_h2i_set(_mh, _mc, _op, _fn_lpu) do {		\
	(_mh).msg_class		= (_mc);      \
	(_mh).msg_id		= (_op);      \
	(_mh).mtag.h2i.fn_lpu	= (_fn_lpu);      \
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
 * Scatter Gather Element used for fast-path IO requests
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

/**
 * Generic DMA addr-len pair.
 */
struct bfi_alen_s {
	union bfi_addr_u	al_addr;	/* DMA addr of buffer	*/
	u32			al_len;		/* length of buffer	*/
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

/* FCP module definitions */
#define BFI_IO_MAX	(2000)
#define BFI_IOIM_SNSLEN	(256)
#define BFI_IOIM_SNSBUF_SEGS	\
	BFI_MEM_DMA_NSEGS(BFI_IO_MAX, BFI_IOIM_SNSLEN)

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
 * Supported PCI function class codes (personality)
 */
enum bfi_pcifn_class {
	BFI_PCIFN_CLASS_FC  = 0x0c04,
	BFI_PCIFN_CLASS_ETH = 0x0200,
};

/*
 * Message Classes
 */
enum bfi_mclass {
	BFI_MC_IOC		= 1,	/*  IO Controller (IOC)	    */
	BFI_MC_DIAG		= 2,    /*  Diagnostic Msgs            */
	BFI_MC_FLASH		= 3,	/*  Flash message class	*/
	BFI_MC_CEE		= 4,	/*  CEE	*/
	BFI_MC_FCPORT		= 5,	/*  FC port			    */
	BFI_MC_IOCFC		= 6,	/*  FC - IO Controller (IOC)	    */
	BFI_MC_ABLK		= 7,	/*  ASIC block configuration	    */
	BFI_MC_UF		= 8,	/*  Unsolicited frame receive	    */
	BFI_MC_FCXP		= 9,	/*  FC Transport		    */
	BFI_MC_LPS		= 10,	/*  lport fc login services	    */
	BFI_MC_RPORT		= 11,	/*  Remote port		    */
	BFI_MC_ITN		= 12,	/*  I-T nexus (Initiator mode)	    */
	BFI_MC_IOIM_READ	= 13,	/*  read IO (Initiator mode)	    */
	BFI_MC_IOIM_WRITE	= 14,	/*  write IO (Initiator mode)	    */
	BFI_MC_IOIM_IO		= 15,	/*  IO (Initiator mode)	    */
	BFI_MC_IOIM		= 16,	/*  IO (Initiator mode)	    */
	BFI_MC_IOIM_IOCOM	= 17,	/*  good IO completion		    */
	BFI_MC_TSKIM		= 18,	/*  Initiator Task management	    */
	BFI_MC_PORT		= 21,	/*  Physical port		    */
	BFI_MC_SFP		= 22,	/*  SFP module	*/
	BFI_MC_PHY		= 25,   /*  External PHY message class	*/
	BFI_MC_FRU		= 34,
	BFI_MC_MAX		= 35
};

#define BFI_IOC_MAX_CQS		4
#define BFI_IOC_MAX_CQS_ASIC	8
#define BFI_IOC_MSGLEN_MAX	32	/* 32 bytes */

/*
 *----------------------------------------------------------------------
 *				IOC
 *----------------------------------------------------------------------
 */

/*
 * Different asic generations
 */
enum bfi_asic_gen {
	BFI_ASIC_GEN_CB		= 1,	/* crossbow 8G FC		*/
	BFI_ASIC_GEN_CT		= 2,	/* catapult 8G FC or 10G CNA	*/
	BFI_ASIC_GEN_CT2	= 3,	/* catapult-2 16G FC or 10G CNA	*/
};

enum bfi_asic_mode {
	BFI_ASIC_MODE_FC	= 1,	/* FC upto 8G speed		*/
	BFI_ASIC_MODE_FC16	= 2,	/* FC upto 16G speed		*/
	BFI_ASIC_MODE_ETH	= 3,	/* Ethernet ports		*/
	BFI_ASIC_MODE_COMBO	= 4,	/* FC 16G and Ethernet 10G port	*/
};

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
	BFI_IOC_I2H_HBEAT		= BFA_I2HM(4),
	BFI_IOC_I2H_ACQ_ADDR_REPLY	= BFA_I2HM(5),
};

/*
 * BFI_IOC_H2I_GETATTR_REQ message
 */
struct bfi_ioc_getattr_req_s {
	struct bfi_mhdr_s	mh;
	union bfi_addr_u	attr_addr;
};

#define BFI_IOC_ATTR_UUID_SZ	16
struct bfi_ioc_attr_s {
	wwn_t		mfg_pwwn;	/*  Mfg port wwn	   */
	wwn_t		mfg_nwwn;	/*  Mfg node wwn	   */
	mac_t		mfg_mac;	/*  Mfg mac		   */
	u8		port_mode;	/* bfi_port_mode	   */
	u8		rsvd_a;
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
	u8	mfg_day;	/* manufacturing day */
	u8	mfg_month;	/* manufacturing month */
	u16	mfg_year;	/* manufacturing year */
	u8	uuid[BFI_IOC_ATTR_UUID_SZ];	/*!< chinook uuid */
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
#define BFA_IOC_FW_INV_SIGN	(0xdeaddead)
#define BFI_IOC_MD5SUM_SZ	4

struct bfi_ioc_fwver_s {
#ifdef __BIG_ENDIAN
	uint8_t patch;
	uint8_t maint;
	uint8_t minor;
	uint8_t major;
	uint8_t rsvd[2];
	uint8_t build;
	uint8_t phase;
#else
	uint8_t major;
	uint8_t minor;
	uint8_t maint;
	uint8_t patch;
	uint8_t phase;
	uint8_t build;
	uint8_t rsvd[2];
#endif
};

struct bfi_ioc_image_hdr_s {
	u32	signature;	/* constant signature		*/
	u8	asic_gen;	/* asic generation		*/
	u8	asic_mode;
	u8	port0_mode;	/* device mode for port 0	*/
	u8	port1_mode;	/* device mode for port 1	*/
	u32	exec;		/* exec vector			*/
	u32	bootenv;	/* fimware boot env		*/
	u32	rsvd_b[2];
	struct bfi_ioc_fwver_s	fwver;
	u32	md5sum[BFI_IOC_MD5SUM_SZ];
};

enum bfi_ioc_img_ver_cmp_e {
	BFI_IOC_IMG_VER_INCOMP,
	BFI_IOC_IMG_VER_OLD,
	BFI_IOC_IMG_VER_SAME,
	BFI_IOC_IMG_VER_BETTER
};

#define BFI_FWBOOT_DEVMODE_OFF		4
#define BFI_FWBOOT_TYPE_OFF		8
#define BFI_FWBOOT_ENV_OFF		12
#define BFI_FWBOOT_DEVMODE(__asic_gen, __asic_mode, __p0_mode, __p1_mode) \
	(((u32)(__asic_gen)) << 24 |		\
	 ((u32)(__asic_mode)) << 16 |		\
	 ((u32)(__p0_mode)) << 8 |		\
	 ((u32)(__p1_mode)))

enum bfi_fwboot_type {
	BFI_FWBOOT_TYPE_NORMAL  = 0,
	BFI_FWBOOT_TYPE_FLASH   = 1,
	BFI_FWBOOT_TYPE_MEMTEST = 2,
};

#define BFI_FWBOOT_TYPE_NORMAL	0
#define BFI_FWBOOT_TYPE_MEMTEST	2
#define BFI_FWBOOT_ENV_OS       0

enum bfi_port_mode {
	BFI_PORT_MODE_FC	= 1,
	BFI_PORT_MODE_ETH	= 2,
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

#define BFA_IOC_CB_JOIN_SH	16
#define BFA_IOC_CB_FWSTATE_MASK	0x0000ffff
#define BFA_IOC_CB_JOIN_MASK	0xffff0000

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
	u16			clscode;
	u16			rsvd;
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
	u8			port_mode;	/*  bfa_mode_s	*/
	u8			cap_bm;		/*  capability bit mask */
	u8			rsvd;
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
	struct bfi_ioc_ctrl_reply_s	fw_event;
	u32			mboxmsg[BFI_IOC_MSGSZ];
};


/*
 *----------------------------------------------------------------------
 *				PBC
 *----------------------------------------------------------------------
 */

#define BFI_PBC_MAX_BLUNS	8
#define BFI_PBC_MAX_VPORTS	16
#define BFI_PBC_PORT_DISABLED	2

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

/*
 *----------------------------------------------------------------------
 *				ABLK
 *----------------------------------------------------------------------
 */
enum bfi_ablk_h2i_msgs_e {
	BFI_ABLK_H2I_QUERY		= 1,
	BFI_ABLK_H2I_ADPT_CONFIG	= 2,
	BFI_ABLK_H2I_PORT_CONFIG	= 3,
	BFI_ABLK_H2I_PF_CREATE		= 4,
	BFI_ABLK_H2I_PF_DELETE		= 5,
	BFI_ABLK_H2I_PF_UPDATE		= 6,
	BFI_ABLK_H2I_OPTROM_ENABLE	= 7,
	BFI_ABLK_H2I_OPTROM_DISABLE	= 8,
};

enum bfi_ablk_i2h_msgs_e {
	BFI_ABLK_I2H_QUERY		= BFA_I2HM(BFI_ABLK_H2I_QUERY),
	BFI_ABLK_I2H_ADPT_CONFIG	= BFA_I2HM(BFI_ABLK_H2I_ADPT_CONFIG),
	BFI_ABLK_I2H_PORT_CONFIG	= BFA_I2HM(BFI_ABLK_H2I_PORT_CONFIG),
	BFI_ABLK_I2H_PF_CREATE		= BFA_I2HM(BFI_ABLK_H2I_PF_CREATE),
	BFI_ABLK_I2H_PF_DELETE		= BFA_I2HM(BFI_ABLK_H2I_PF_DELETE),
	BFI_ABLK_I2H_PF_UPDATE		= BFA_I2HM(BFI_ABLK_H2I_PF_UPDATE),
	BFI_ABLK_I2H_OPTROM_ENABLE	= BFA_I2HM(BFI_ABLK_H2I_OPTROM_ENABLE),
	BFI_ABLK_I2H_OPTROM_DISABLE	= BFA_I2HM(BFI_ABLK_H2I_OPTROM_DISABLE),
};

/* BFI_ABLK_H2I_QUERY */
struct bfi_ablk_h2i_query_s {
	struct bfi_mhdr_s	mh;
	union bfi_addr_u	addr;
};

/* BFI_ABL_H2I_ADPT_CONFIG, BFI_ABLK_H2I_PORT_CONFIG */
struct bfi_ablk_h2i_cfg_req_s {
	struct bfi_mhdr_s	mh;
	u8			mode;
	u8			port;
	u8			max_pf;
	u8			max_vf;
};

/*
 * BFI_ABLK_H2I_PF_CREATE, BFI_ABLK_H2I_PF_DELETE,
 */
struct bfi_ablk_h2i_pf_req_s {
	struct bfi_mhdr_s	mh;
	u8			pcifn;
	u8			port;
	u16			pers;
	u16			bw_min; /* percent BW @ max speed */
	u16			bw_max; /* percent BW @ max speed */
};

/* BFI_ABLK_H2I_OPTROM_ENABLE, BFI_ABLK_H2I_OPTROM_DISABLE */
struct bfi_ablk_h2i_optrom_s {
	struct bfi_mhdr_s	mh;
};

/*
 * BFI_ABLK_I2H_QUERY
 * BFI_ABLK_I2H_PORT_CONFIG
 * BFI_ABLK_I2H_PF_CREATE
 * BFI_ABLK_I2H_PF_DELETE
 * BFI_ABLK_I2H_PF_UPDATE
 * BFI_ABLK_I2H_OPTROM_ENABLE
 * BFI_ABLK_I2H_OPTROM_DISABLE
 */
struct bfi_ablk_i2h_rsp_s {
	struct bfi_mhdr_s	mh;
	u8			status;
	u8			pcifn;
	u8			port_mode;
};


/*
 *	CEE module specific messages
 */

/* Mailbox commands from host to firmware */
enum bfi_cee_h2i_msgs_e {
	BFI_CEE_H2I_GET_CFG_REQ = 1,
	BFI_CEE_H2I_RESET_STATS = 2,
	BFI_CEE_H2I_GET_STATS_REQ = 3,
};

enum bfi_cee_i2h_msgs_e {
	BFI_CEE_I2H_GET_CFG_RSP = BFA_I2HM(1),
	BFI_CEE_I2H_RESET_STATS_RSP = BFA_I2HM(2),
	BFI_CEE_I2H_GET_STATS_RSP = BFA_I2HM(3),
};

/*
 * H2I command structure for resetting the stats
 */
struct bfi_cee_reset_stats_s {
	struct bfi_mhdr_s  mh;
};

/*
 * Get configuration  command from host
 */
struct bfi_cee_get_req_s {
	struct bfi_mhdr_s	mh;
	union bfi_addr_u	dma_addr;
};

/*
 * Reply message from firmware
 */
struct bfi_cee_get_rsp_s {
	struct bfi_mhdr_s	mh;
	u8			cmd_status;
	u8			rsvd[3];
};

/*
 * Reply message from firmware
 */
struct bfi_cee_stats_rsp_s {
	struct bfi_mhdr_s	mh;
	u8			cmd_status;
	u8			rsvd[3];
};

/* Mailbox message structures from firmware to host	*/
union bfi_cee_i2h_msg_u {
	struct bfi_mhdr_s		mh;
	struct bfi_cee_get_rsp_s	get_rsp;
	struct bfi_cee_stats_rsp_s	stats_rsp;
};

/*
 * SFP related
 */

enum bfi_sfp_h2i_e {
	BFI_SFP_H2I_SHOW	= 1,
	BFI_SFP_H2I_SCN		= 2,
};

enum bfi_sfp_i2h_e {
	BFI_SFP_I2H_SHOW = BFA_I2HM(BFI_SFP_H2I_SHOW),
	BFI_SFP_I2H_SCN	 = BFA_I2HM(BFI_SFP_H2I_SCN),
};

/*
 *	SFP state change notification
 */
struct bfi_sfp_scn_s {
	struct bfi_mhdr_s mhr;	/* host msg header        */
	u8	event;
	u8	sfpid;
	u8	pomlvl;	/* pom level: normal/warning/alarm */
	u8	is_elb;	/* e-loopback */
};

/*
 *	SFP state
 */
enum bfa_sfp_stat_e {
	BFA_SFP_STATE_INIT	= 0,	/* SFP state is uninit	*/
	BFA_SFP_STATE_REMOVED	= 1,	/* SFP is removed	*/
	BFA_SFP_STATE_INSERTED	= 2,	/* SFP is inserted	*/
	BFA_SFP_STATE_VALID	= 3,	/* SFP is valid		*/
	BFA_SFP_STATE_UNSUPPORT	= 4,	/* SFP is unsupport	*/
	BFA_SFP_STATE_FAILED	= 5,	/* SFP i2c read fail	*/
};

/*
 *  SFP memory access type
 */
enum bfi_sfp_mem_e {
	BFI_SFP_MEM_ALL		= 0x1,  /* access all data field */
	BFI_SFP_MEM_DIAGEXT	= 0x2,  /* access diag ext data field only */
};

struct bfi_sfp_req_s {
	struct bfi_mhdr_s	mh;
	u8			memtype;
	u8			rsvd[3];
	struct bfi_alen_s	alen;
};

struct bfi_sfp_rsp_s {
	struct bfi_mhdr_s	mh;
	u8			status;
	u8			state;
	u8			rsvd[2];
};

/*
 *	FLASH module specific
 */
enum bfi_flash_h2i_msgs {
	BFI_FLASH_H2I_QUERY_REQ = 1,
	BFI_FLASH_H2I_ERASE_REQ = 2,
	BFI_FLASH_H2I_WRITE_REQ = 3,
	BFI_FLASH_H2I_READ_REQ = 4,
	BFI_FLASH_H2I_BOOT_VER_REQ = 5,
};

enum bfi_flash_i2h_msgs {
	BFI_FLASH_I2H_QUERY_RSP = BFA_I2HM(1),
	BFI_FLASH_I2H_ERASE_RSP = BFA_I2HM(2),
	BFI_FLASH_I2H_WRITE_RSP = BFA_I2HM(3),
	BFI_FLASH_I2H_READ_RSP = BFA_I2HM(4),
	BFI_FLASH_I2H_BOOT_VER_RSP = BFA_I2HM(5),
	BFI_FLASH_I2H_EVENT = BFA_I2HM(127),
};

/*
 * Flash query request
 */
struct bfi_flash_query_req_s {
	struct bfi_mhdr_s mh;	/* Common msg header */
	struct bfi_alen_s alen;
};

/*
 * Flash erase request
 */
struct bfi_flash_erase_req_s {
	struct bfi_mhdr_s	mh;	/* Common msg header */
	u32	type;	/* partition type */
	u8	instance; /* partition instance */
	u8	rsv[3];
};

/*
 * Flash write request
 */
struct bfi_flash_write_req_s {
	struct bfi_mhdr_s mh;	/* Common msg header */
	struct bfi_alen_s alen;
	u32	type;	/* partition type */
	u8	instance; /* partition instance */
	u8	last;
	u8	rsv[2];
	u32	offset;
	u32	length;
};

/*
 * Flash read request
 */
struct bfi_flash_read_req_s {
	struct bfi_mhdr_s mh;	/* Common msg header */
	u32	type;		/* partition type */
	u8	instance;	/* partition instance */
	u8	rsv[3];
	u32	offset;
	u32	length;
	struct bfi_alen_s alen;
};

/*
 * Flash query response
 */
struct bfi_flash_query_rsp_s {
	struct bfi_mhdr_s mh;	/* Common msg header */
	u32	status;
};

/*
 * Flash read response
 */
struct bfi_flash_read_rsp_s {
	struct bfi_mhdr_s mh;	/* Common msg header */
	u32	type;       /* partition type */
	u8	instance;   /* partition instance */
	u8	rsv[3];
	u32	status;
	u32	length;
};

/*
 * Flash write response
 */
struct bfi_flash_write_rsp_s {
	struct bfi_mhdr_s mh;	/* Common msg header */
	u32	type;       /* partition type */
	u8	instance;   /* partition instance */
	u8	rsv[3];
	u32	status;
	u32	length;
};

/*
 * Flash erase response
 */
struct bfi_flash_erase_rsp_s {
	struct bfi_mhdr_s mh;	/* Common msg header */
	u32	type;		/* partition type */
	u8	instance;	/* partition instance */
	u8	rsv[3];
	u32	status;
};

/*
 * Flash event notification
 */
struct bfi_flash_event_s {
	struct bfi_mhdr_s	mh;	/* Common msg header */
	bfa_status_t		status;
	u32			param;
};

/*
 *----------------------------------------------------------------------
 *				DIAG
 *----------------------------------------------------------------------
 */
enum bfi_diag_h2i {
	BFI_DIAG_H2I_PORTBEACON = 1,
	BFI_DIAG_H2I_LOOPBACK = 2,
	BFI_DIAG_H2I_FWPING = 3,
	BFI_DIAG_H2I_TEMPSENSOR = 4,
	BFI_DIAG_H2I_LEDTEST = 5,
	BFI_DIAG_H2I_QTEST      = 6,
	BFI_DIAG_H2I_DPORT	= 7,
};

enum bfi_diag_i2h {
	BFI_DIAG_I2H_PORTBEACON = BFA_I2HM(BFI_DIAG_H2I_PORTBEACON),
	BFI_DIAG_I2H_LOOPBACK = BFA_I2HM(BFI_DIAG_H2I_LOOPBACK),
	BFI_DIAG_I2H_FWPING = BFA_I2HM(BFI_DIAG_H2I_FWPING),
	BFI_DIAG_I2H_TEMPSENSOR = BFA_I2HM(BFI_DIAG_H2I_TEMPSENSOR),
	BFI_DIAG_I2H_LEDTEST = BFA_I2HM(BFI_DIAG_H2I_LEDTEST),
	BFI_DIAG_I2H_QTEST      = BFA_I2HM(BFI_DIAG_H2I_QTEST),
	BFI_DIAG_I2H_DPORT	= BFA_I2HM(BFI_DIAG_H2I_DPORT),
	BFI_DIAG_I2H_DPORT_SCN	= BFA_I2HM(8),
};

#define BFI_DIAG_MAX_SGES	2
#define BFI_DIAG_DMA_BUF_SZ	(2 * 1024)
#define BFI_BOOT_MEMTEST_RES_ADDR 0x900
#define BFI_BOOT_MEMTEST_RES_SIG  0xA0A1A2A3

struct bfi_diag_lb_req_s {
	struct bfi_mhdr_s mh;
	u32	loopcnt;
	u32	pattern;
	u8	lb_mode;        /*!< bfa_port_opmode_t */
	u8	speed;          /*!< bfa_port_speed_t */
	u8	rsvd[2];
};

struct bfi_diag_lb_rsp_s {
	struct bfi_mhdr_s  mh;          /* 4 bytes */
	struct bfa_diag_loopback_result_s res; /* 16 bytes */
};

struct bfi_diag_fwping_req_s {
	struct bfi_mhdr_s mh;	/* 4 bytes */
	struct bfi_alen_s alen; /* 12 bytes */
	u32	data;           /* user input data pattern */
	u32	count;          /* user input dma count */
	u8	qtag;           /* track CPE vc */
	u8	rsv[3];
};

struct bfi_diag_fwping_rsp_s {
	struct bfi_mhdr_s  mh;          /* 4 bytes */
	u32	data;           /* user input data pattern    */
	u8	qtag;           /* track CPE vc               */
	u8	dma_status;     /* dma status                 */
	u8	rsv[2];
};

/*
 * Temperature Sensor
 */
struct bfi_diag_ts_req_s {
	struct bfi_mhdr_s mh;	/* 4 bytes */
	u16	temp;           /* 10-bit A/D value */
	u16	brd_temp;       /* 9-bit board temp */
	u8	status;
	u8	ts_junc;        /* show junction tempsensor   */
	u8	ts_brd;         /* show board tempsensor      */
	u8	rsv;
};
#define bfi_diag_ts_rsp_t struct bfi_diag_ts_req_s

struct bfi_diag_ledtest_req_s {
	struct bfi_mhdr_s  mh;  /* 4 bytes */
	u8	cmd;
	u8	color;
	u8	portid;
	u8	led;    /* bitmap of LEDs to be tested */
	u16	freq;   /* no. of blinks every 10 secs */
	u8	rsv[2];
};

/* notify host led operation is done */
struct bfi_diag_ledtest_rsp_s {
	struct bfi_mhdr_s  mh;  /* 4 bytes */
};

struct bfi_diag_portbeacon_req_s {
	struct bfi_mhdr_s  mh;  /* 4 bytes */
	u32	period; /* beaconing period */
	u8	beacon; /* 1: beacon on */
	u8	rsvd[3];
};

/* notify host the beacon is off */
struct bfi_diag_portbeacon_rsp_s {
	struct bfi_mhdr_s  mh;  /* 4 bytes */
};

struct bfi_diag_qtest_req_s {
	struct bfi_mhdr_s	mh;             /* 4 bytes */
	u32	data[BFI_LMSG_PL_WSZ]; /* fill up tcm prefetch area */
};
#define bfi_diag_qtest_rsp_t struct bfi_diag_qtest_req_s

/*
 *	D-port test
 */
enum bfi_dport_req {
	BFI_DPORT_DISABLE	= 0,	/* disable dport request	*/
	BFI_DPORT_ENABLE	= 1,	/* enable dport request		*/
	BFI_DPORT_START		= 2,	/* start dport request	*/
	BFI_DPORT_SHOW		= 3,	/* show dport request	*/
	BFI_DPORT_DYN_DISABLE	= 4,	/* disable dynamic dport request */
};

enum bfi_dport_scn {
	BFI_DPORT_SCN_TESTSTART		= 1,
	BFI_DPORT_SCN_TESTCOMP		= 2,
	BFI_DPORT_SCN_SFP_REMOVED	= 3,
	BFI_DPORT_SCN_DDPORT_ENABLE	= 4,
	BFI_DPORT_SCN_DDPORT_DISABLE	= 5,
	BFI_DPORT_SCN_FCPORT_DISABLE	= 6,
	BFI_DPORT_SCN_SUBTESTSTART	= 7,
	BFI_DPORT_SCN_TESTSKIP		= 8,
	BFI_DPORT_SCN_DDPORT_DISABLED	= 9,
};

struct bfi_diag_dport_req_s {
	struct bfi_mhdr_s	mh;	/* 4 bytes                      */
	u8			req;	/* request 1: enable 0: disable	*/
	u8			rsvd[3];
	u32			lpcnt;
	u32			payload;
};

struct bfi_diag_dport_rsp_s {
	struct bfi_mhdr_s	mh;	/* header 4 bytes		*/
	bfa_status_t		status;	/* reply status			*/
	wwn_t			pwwn;	/* switch port wwn. 8 bytes	*/
	wwn_t			nwwn;	/* switch node wwn. 8 bytes	*/
};

struct bfi_diag_dport_scn_teststart_s {
	wwn_t	pwwn;	/* switch port wwn. 8 bytes */
	wwn_t	nwwn;	/* switch node wwn. 8 bytes */
	u8	type;	/* bfa_diag_dport_test_type_e */
	u8	mode;	/* bfa_diag_dport_test_opmode */
	u8	rsvd[2];
	u32	numfrm; /* from switch uint in 1M */
};

struct bfi_diag_dport_scn_testcomp_s {
	u8	status; /* bfa_diag_dport_test_status_e */
	u8	speed;  /* bfa_port_speed_t  */
	u16	numbuffer; /* from switch  */
	u8	subtest_status[DPORT_TEST_MAX];  /* 4 bytes */
	u32	latency;   /* from switch  */
	u32	distance;  /* from swtich unit in meters  */
			/* Buffers required to saturate the link */
	u16	frm_sz;	/* from switch for buf_reqd */
	u8	rsvd[2];
};

struct bfi_diag_dport_scn_s {		/* max size == RDS_RMESZ	*/
	struct bfi_mhdr_s	mh;	/* header 4 bytes		*/
	u8			state;  /* new state			*/
	u8			rsvd[3];
	union {
		struct bfi_diag_dport_scn_teststart_s teststart;
		struct bfi_diag_dport_scn_testcomp_s testcomp;
	} info;
};

union bfi_diag_dport_msg_u {
	struct bfi_diag_dport_req_s	req;
	struct bfi_diag_dport_rsp_s	rsp;
	struct bfi_diag_dport_scn_s	scn;
};

/*
 *	PHY module specific
 */
enum bfi_phy_h2i_msgs_e {
	BFI_PHY_H2I_QUERY_REQ = 1,
	BFI_PHY_H2I_STATS_REQ = 2,
	BFI_PHY_H2I_WRITE_REQ = 3,
	BFI_PHY_H2I_READ_REQ = 4,
};

enum bfi_phy_i2h_msgs_e {
	BFI_PHY_I2H_QUERY_RSP = BFA_I2HM(1),
	BFI_PHY_I2H_STATS_RSP = BFA_I2HM(2),
	BFI_PHY_I2H_WRITE_RSP = BFA_I2HM(3),
	BFI_PHY_I2H_READ_RSP = BFA_I2HM(4),
};

/*
 * External PHY query request
 */
struct bfi_phy_query_req_s {
	struct bfi_mhdr_s	mh;             /* Common msg header */
	u8			instance;
	u8			rsv[3];
	struct bfi_alen_s	alen;
};

/*
 * External PHY stats request
 */
struct bfi_phy_stats_req_s {
	struct bfi_mhdr_s	mh;             /* Common msg header */
	u8			instance;
	u8			rsv[3];
	struct bfi_alen_s	alen;
};

/*
 * External PHY write request
 */
struct bfi_phy_write_req_s {
	struct bfi_mhdr_s	mh;             /* Common msg header */
	u8		instance;
	u8		last;
	u8		rsv[2];
	u32		offset;
	u32		length;
	struct bfi_alen_s	alen;
};

/*
 * External PHY read request
 */
struct bfi_phy_read_req_s {
	struct bfi_mhdr_s	mh;	/* Common msg header */
	u8		instance;
	u8		rsv[3];
	u32		offset;
	u32		length;
	struct bfi_alen_s	alen;
};

/*
 * External PHY query response
 */
struct bfi_phy_query_rsp_s {
	struct bfi_mhdr_s	mh;	/* Common msg header */
	u32			status;
};

/*
 * External PHY stats response
 */
struct bfi_phy_stats_rsp_s {
	struct bfi_mhdr_s	mh;	/* Common msg header */
	u32			status;
};

/*
 * External PHY read response
 */
struct bfi_phy_read_rsp_s {
	struct bfi_mhdr_s	mh;	/* Common msg header */
	u32			status;
	u32		length;
};

/*
 * External PHY write response
 */
struct bfi_phy_write_rsp_s {
	struct bfi_mhdr_s	mh;	/* Common msg header */
	u32			status;
	u32			length;
};

enum bfi_fru_h2i_msgs {
	BFI_FRUVPD_H2I_WRITE_REQ = 1,
	BFI_FRUVPD_H2I_READ_REQ = 2,
	BFI_TFRU_H2I_WRITE_REQ = 3,
	BFI_TFRU_H2I_READ_REQ = 4,
};

enum bfi_fru_i2h_msgs {
	BFI_FRUVPD_I2H_WRITE_RSP = BFA_I2HM(1),
	BFI_FRUVPD_I2H_READ_RSP = BFA_I2HM(2),
	BFI_TFRU_I2H_WRITE_RSP = BFA_I2HM(3),
	BFI_TFRU_I2H_READ_RSP = BFA_I2HM(4),
};

/*
 * FRU write request
 */
struct bfi_fru_write_req_s {
	struct bfi_mhdr_s	mh;	/* Common msg header */
	u8			last;
	u8			rsv_1[3];
	u8			trfr_cmpl;
	u8			rsv_2[3];
	u32			offset;
	u32			length;
	struct bfi_alen_s	alen;
};

/*
 * FRU read request
 */
struct bfi_fru_read_req_s {
	struct bfi_mhdr_s	mh;	/* Common msg header */
	u32			offset;
	u32			length;
	struct bfi_alen_s	alen;
};

/*
 * FRU response
 */
struct bfi_fru_rsp_s {
	struct bfi_mhdr_s	mh;	/* Common msg header */
	u32			status;
	u32			length;
};
#pragma pack()

#endif /* __BFI_H__ */
