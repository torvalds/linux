/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2010 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __T4_HW_H
#define __T4_HW_H

#include <linux/types.h>

enum {
	NCHAN          = 4,     /* # of HW channels */
	MAX_MTU        = 9600,  /* max MAC MTU, excluding header + FCS */
	EEPROMSIZE     = 17408, /* Serial EEPROM physical size */
	EEPROMVSIZE    = 32768, /* Serial EEPROM virtual address space size */
	EEPROMPFSIZE   = 1024,  /* EEPROM writable area size for PFn, n>0 */
	RSS_NENTRIES   = 2048,  /* # of entries in RSS mapping table */
	TCB_SIZE       = 128,   /* TCB size */
	NMTUS          = 16,    /* size of MTU table */
	NCCTRL_WIN     = 32,    /* # of congestion control windows */
	NEXACT_MAC     = 336,   /* # of exact MAC address filters */
	L2T_SIZE       = 4096,  /* # of L2T entries */
	MBOX_LEN       = 64,    /* mailbox size in bytes */
	TRACE_LEN      = 112,   /* length of trace data and mask */
	FILTER_OPT_LEN = 36,    /* filter tuple width for optional components */
	NWOL_PAT       = 8,     /* # of WoL patterns */
	WOL_PAT_LEN    = 128,   /* length of WoL patterns */
};

enum {
	SF_PAGE_SIZE = 256,           /* serial flash page size */
};

enum { RSP_TYPE_FLBUF, RSP_TYPE_CPL, RSP_TYPE_INTR }; /* response entry types */

enum { MBOX_OWNER_NONE, MBOX_OWNER_FW, MBOX_OWNER_DRV };    /* mailbox owners */

enum {
	SGE_MAX_WR_LEN = 512,     /* max WR size in bytes */
	SGE_NTIMERS = 6,          /* # of interrupt holdoff timer values */
	SGE_NCOUNTERS = 4,        /* # of interrupt packet counter values */

	SGE_TIMER_RSTRT_CNTR = 6, /* restart RX packet threshold counter */
	SGE_TIMER_UPD_CIDX = 7,   /* update cidx only */

	SGE_EQ_IDXSIZE = 64,      /* egress queue pidx/cidx unit size */

	SGE_INTRDST_PCI = 0,      /* interrupt destination is PCI-E */
	SGE_INTRDST_IQ = 1,       /*   destination is an ingress queue */

	SGE_UPDATEDEL_NONE = 0,   /* ingress queue pidx update delivery */
	SGE_UPDATEDEL_INTR = 1,   /*   interrupt */
	SGE_UPDATEDEL_STPG = 2,   /*   status page */
	SGE_UPDATEDEL_BOTH = 3,   /*   interrupt and status page */

	SGE_HOSTFCMODE_NONE = 0,  /* egress queue cidx updates */
	SGE_HOSTFCMODE_IQ = 1,    /*   sent to ingress queue */
	SGE_HOSTFCMODE_STPG = 2,  /*   sent to status page */
	SGE_HOSTFCMODE_BOTH = 3,  /*   ingress queue and status page */

	SGE_FETCHBURSTMIN_16B = 0,/* egress queue descriptor fetch minimum */
	SGE_FETCHBURSTMIN_32B = 1,
	SGE_FETCHBURSTMIN_64B = 2,
	SGE_FETCHBURSTMIN_128B = 3,

	SGE_FETCHBURSTMAX_64B = 0,/* egress queue descriptor fetch maximum */
	SGE_FETCHBURSTMAX_128B = 1,
	SGE_FETCHBURSTMAX_256B = 2,
	SGE_FETCHBURSTMAX_512B = 3,

	SGE_CIDXFLUSHTHRESH_1 = 0,/* egress queue cidx flush threshold */
	SGE_CIDXFLUSHTHRESH_2 = 1,
	SGE_CIDXFLUSHTHRESH_4 = 2,
	SGE_CIDXFLUSHTHRESH_8 = 3,
	SGE_CIDXFLUSHTHRESH_16 = 4,
	SGE_CIDXFLUSHTHRESH_32 = 5,
	SGE_CIDXFLUSHTHRESH_64 = 6,
	SGE_CIDXFLUSHTHRESH_128 = 7,

	SGE_INGPADBOUNDARY_SHIFT = 5,/* ingress queue pad boundary */
};

struct sge_qstat {                /* data written to SGE queue status entries */
	__be32 qid;
	__be16 cidx;
	__be16 pidx;
};

/*
 * Structure for last 128 bits of response descriptors
 */
struct rsp_ctrl {
	__be32 hdrbuflen_pidx;
	__be32 pldbuflen_qid;
	union {
		u8 type_gen;
		__be64 last_flit;
	};
};

#define RSPD_NEWBUF 0x80000000U
#define RSPD_LEN(x) (((x) >> 0) & 0x7fffffffU)
#define RSPD_QID(x) RSPD_LEN(x)

#define RSPD_GEN(x)  ((x) >> 7)
#define RSPD_TYPE(x) (((x) >> 4) & 3)

#define QINTR_CNT_EN       0x1
#define QINTR_TIMER_IDX(x) ((x) << 1)
#define QINTR_TIMER_IDX_GET(x) (((x) >> 1) & 0x7)
#endif /* __T4_HW_H */
