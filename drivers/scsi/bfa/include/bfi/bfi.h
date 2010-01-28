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

#ifndef __BFI_H__
#define __BFI_H__

#include <bfa_os_inc.h>
#include <defs/bfa_defs_status.h>

#pragma pack(1)

/**
 * Msg header common to all msgs
 */
struct bfi_mhdr_s {
	u8         msg_class;	/*  @ref bfi_mclass_t	    */
	u8         msg_id;		/*  msg opcode with in the class   */
	union {
		struct {
			u8         rsvd;
			u8         lpu_id;	/*  msg destination	    */
		} h2i;
		u16        i2htok;	/*  token in msgs to host	    */
	} mtag;
};

#define bfi_h2i_set(_mh, _mc, _op, _lpuid) do {		\
	(_mh).msg_class 		= (_mc);      \
	(_mh).msg_id			= (_op);      \
	(_mh).mtag.h2i.lpu_id	= (_lpuid);      \
} while (0)

#define bfi_i2h_set(_mh, _mc, _op, _i2htok) do {		\
	(_mh).msg_class 		= (_mc);      \
	(_mh).msg_id			= (_op);      \
	(_mh).mtag.i2htok		= (_i2htok);      \
} while (0)

/*
 * Message opcodes: 0-127 to firmware, 128-255 to host
 */
#define BFI_I2H_OPCODE_BASE	128
#define BFA_I2HM(_x) 			((_x) + BFI_I2H_OPCODE_BASE)

/**
 ****************************************************************************
 *
 * Scatter Gather Element and Page definition
 *
 ****************************************************************************
 */

#define BFI_SGE_INLINE	1
#define BFI_SGE_INLINE_MAX	(BFI_SGE_INLINE + 1)

/**
 * SG Flags
 */
enum {
	BFI_SGE_DATA	= 0,	/*  data address, not last	     */
	BFI_SGE_DATA_CPL	= 1,	/*  data addr, last in current page */
	BFI_SGE_DATA_LAST	= 3,	/*  data address, last		     */
	BFI_SGE_LINK	= 2,	/*  link address		     */
	BFI_SGE_PGDLEN	= 2,	/*  cumulative data length for page */
};

/**
 * DMA addresses
 */
union bfi_addr_u {
	struct {
		u32        addr_lo;
		u32        addr_hi;
	} a32;
};

/**
 * Scatter Gather Element
 */
struct bfi_sge_s {
#ifdef __BIGENDIAN
	u32        flags:2,
			rsvd:2,
			sg_len:28;
#else
	u32        sg_len:28,
			rsvd:2,
			flags:2;
#endif
	union bfi_addr_u sga;
};

/**
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

/**
 * Mailbox message structure
 */
#define BFI_MBMSG_SZ		7
struct bfi_mbmsg_s {
	struct bfi_mhdr_s	mh;
	u32		pl[BFI_MBMSG_SZ];
};

/**
 * Message Classes
 */
enum bfi_mclass {
	BFI_MC_IOC		= 1,	/*  IO Controller (IOC)	    */
	BFI_MC_DIAG		= 2,	/*  Diagnostic Msgs		    */
	BFI_MC_FLASH		= 3,	/*  Flash message class	    */
	BFI_MC_CEE		= 4,
	BFI_MC_FC_PORT		= 5,	/*  FC port		   	    */
	BFI_MC_IOCFC		= 6,	/*  FC - IO Controller (IOC)	    */
	BFI_MC_LL		= 7,	/*  Link Layer		 	    */
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
	BFI_MC_SBOOT		= 19,	/*  SAN boot services		    */
	BFI_MC_IPFC		= 20,	/*  IP over FC Msgs		    */
	BFI_MC_PORT		= 21,	/*  Physical port		    */
	BFI_MC_MAX		= 32
};

#define BFI_IOC_MAX_CQS		4
#define BFI_IOC_MAX_CQS_ASIC	8
#define BFI_IOC_MSGLEN_MAX	32	/* 32 bytes */

#pragma pack()

#endif /* __BFI_H__ */

