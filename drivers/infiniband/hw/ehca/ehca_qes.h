/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  Hardware request structures
 *
 *  Authors: Waleri Fomin <fomin@de.ibm.com>
 *           Reinhard Ernst <rernst@de.ibm.com>
 *           Christoph Raisch <raisch@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef _EHCA_QES_H_
#define _EHCA_QES_H_

#include "ehca_tools.h"

/* virtual scatter gather entry to specify remote adresses with length */
struct ehca_vsgentry {
	u64 vaddr;
	u32 lkey;
	u32 length;
};

#define GRH_FLAG_MASK        EHCA_BMASK_IBM( 7,  7)
#define GRH_IPVERSION_MASK   EHCA_BMASK_IBM( 0,  3)
#define GRH_TCLASS_MASK      EHCA_BMASK_IBM( 4, 12)
#define GRH_FLOWLABEL_MASK   EHCA_BMASK_IBM(13, 31)
#define GRH_PAYLEN_MASK      EHCA_BMASK_IBM(32, 47)
#define GRH_NEXTHEADER_MASK  EHCA_BMASK_IBM(48, 55)
#define GRH_HOPLIMIT_MASK    EHCA_BMASK_IBM(56, 63)

/*
 * Unreliable Datagram Address Vector Format
 * see IBTA Vol1 chapter 8.3 Global Routing Header
 */
struct ehca_ud_av {
	u8 sl;
	u8 lnh;
	u16 dlid;
	u8 reserved1;
	u8 reserved2;
	u8 reserved3;
	u8 slid_path_bits;
	u8 reserved4;
	u8 ipd;
	u8 reserved5;
	u8 pmtu;
	u32 reserved6;
	u64 reserved7;
	union {
		struct {
			u64 word_0; /* always set to 6  */
			/*should be 0x1B for IB transport */
			u64 word_1;
			u64 word_2;
			u64 word_3;
			u64 word_4;
		} grh;
		struct {
			u32 wd_0;
			u32 wd_1;
			/* DWord_1 --> SGID */

			u32 sgid_wd3;
			u32 sgid_wd2;

			u32 sgid_wd1;
			u32 sgid_wd0;
			/* DWord_3 --> DGID */

			u32 dgid_wd3;
			u32 dgid_wd2;

			u32 dgid_wd1;
			u32 dgid_wd0;
		} grh_l;
	};
};

/* maximum number of sg entries allowed in a WQE */
#define MAX_WQE_SG_ENTRIES 252

#define WQE_OPTYPE_SEND             0x80
#define WQE_OPTYPE_RDMAREAD         0x40
#define WQE_OPTYPE_RDMAWRITE        0x20
#define WQE_OPTYPE_CMPSWAP          0x10
#define WQE_OPTYPE_FETCHADD         0x08
#define WQE_OPTYPE_BIND             0x04

#define WQE_WRFLAG_REQ_SIGNAL_COM   0x80
#define WQE_WRFLAG_FENCE            0x40
#define WQE_WRFLAG_IMM_DATA_PRESENT 0x20
#define WQE_WRFLAG_SOLIC_EVENT      0x10

#define WQEF_CACHE_HINT             0x80
#define WQEF_CACHE_HINT_RD_WR       0x40
#define WQEF_TIMED_WQE              0x20
#define WQEF_PURGE                  0x08
#define WQEF_HIGH_NIBBLE            0xF0

#define MW_BIND_ACCESSCTRL_R_WRITE   0x40
#define MW_BIND_ACCESSCTRL_R_READ    0x20
#define MW_BIND_ACCESSCTRL_R_ATOMIC  0x10

struct ehca_wqe {
	u64 work_request_id;
	u8 optype;
	u8 wr_flag;
	u16 pkeyi;
	u8 wqef;
	u8 nr_of_data_seg;
	u16 wqe_provided_slid;
	u32 destination_qp_number;
	u32 resync_psn_sqp;
	u32 local_ee_context_qkey;
	u32 immediate_data;
	union {
		struct {
			u64 remote_virtual_adress;
			u32 rkey;
			u32 reserved;
			u64 atomic_1st_op_dma_len;
			u64 atomic_2nd_op;
			struct ehca_vsgentry sg_list[MAX_WQE_SG_ENTRIES];

		} nud;
		struct {
			u64 ehca_ud_av_ptr;
			u64 reserved1;
			u64 reserved2;
			u64 reserved3;
			struct ehca_vsgentry sg_list[MAX_WQE_SG_ENTRIES];
		} ud_avp;
		struct {
			struct ehca_ud_av ud_av;
			struct ehca_vsgentry sg_list[MAX_WQE_SG_ENTRIES -
						     2];
		} ud_av;
		struct {
			u64 reserved0;
			u64 reserved1;
			u64 reserved2;
			u64 reserved3;
			struct ehca_vsgentry sg_list[MAX_WQE_SG_ENTRIES];
		} all_rcv;

		struct {
			u64 reserved;
			u32 rkey;
			u32 old_rkey;
			u64 reserved1;
			u64 reserved2;
			u64 virtual_address;
			u32 reserved3;
			u32 length;
			u32 reserved4;
			u16 reserved5;
			u8 reserved6;
			u8 lr_ctl;
			u32 lkey;
			u32 reserved7;
			u64 reserved8;
			u64 reserved9;
			u64 reserved10;
			u64 reserved11;
		} bind;
		struct {
			u64 reserved12;
			u64 reserved13;
			u32 size;
			u32 start;
		} inline_data;
	} u;

};

#define WC_SEND_RECEIVE EHCA_BMASK_IBM(0, 0)
#define WC_IMM_DATA     EHCA_BMASK_IBM(1, 1)
#define WC_GRH_PRESENT  EHCA_BMASK_IBM(2, 2)
#define WC_SE_BIT       EHCA_BMASK_IBM(3, 3)
#define WC_STATUS_ERROR_BIT 0x80000000
#define WC_STATUS_REMOTE_ERROR_FLAGS 0x0000F800
#define WC_STATUS_PURGE_BIT 0x10
#define WC_SEND_RECEIVE_BIT 0x80

struct ehca_cqe {
	u64 work_request_id;
	u8 optype;
	u8 w_completion_flags;
	u16 reserved1;
	u32 nr_bytes_transferred;
	u32 immediate_data;
	u32 local_qp_number;
	u8 freed_resource_count;
	u8 service_level;
	u16 wqe_count;
	u32 qp_token;
	u32 qkey_ee_token;
	u32 remote_qp_number;
	u16 dlid;
	u16 rlid;
	u16 reserved2;
	u16 pkey_index;
	u32 cqe_timestamp;
	u32 wqe_timestamp;
	u8 wqe_timestamp_valid;
	u8 reserved3;
	u8 reserved4;
	u8 cqe_flags;
	u32 status;
};

struct ehca_eqe {
	u64 entry;
};

struct ehca_mrte {
	u64 starting_va;
	u64 length; /* length of memory region in bytes*/
	u32 pd;
	u8 key_instance;
	u8 pagesize;
	u8 mr_control;
	u8 local_remote_access_ctrl;
	u8 reserved[0x20 - 0x18];
	u64 at_pointer[4];
};
#endif /*_EHCA_QES_H_*/
