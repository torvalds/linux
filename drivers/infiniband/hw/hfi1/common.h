/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef _COMMON_H
#define _COMMON_H

#include <rdma/hfi/hfi1_user.h>

/*
 * This file contains defines, structures, etc. that are used
 * to communicate between kernel and user code.
 */

/* version of protocol header (known to chip also). In the long run,
 * we should be able to generate and accept a range of version numbers;
 * for now we only accept one, and it's compiled in.
 */
#define IPS_PROTO_VERSION 2

/*
 * These are compile time constants that you may want to enable or disable
 * if you are trying to debug problems with code or performance.
 * HFI1_VERBOSE_TRACING define as 1 if you want additional tracing in
 * fast path code
 * HFI1_TRACE_REGWRITES define as 1 if you want register writes to be
 * traced in fast path code
 * _HFI1_TRACING define as 0 if you want to remove all tracing in a
 * compilation unit
 */

/*
 * If a packet's QP[23:16] bits match this value, then it is
 * a PSM packet and the hardware will expect a KDETH header
 * following the BTH.
 */
#define DEFAULT_KDETH_QP 0x80

/* driver/hw feature set bitmask */
#define HFI1_CAP_USER_SHIFT      24
#define HFI1_CAP_MASK            ((1UL << HFI1_CAP_USER_SHIFT) - 1)
/* locked flag - if set, only HFI1_CAP_WRITABLE_MASK bits can be set */
#define HFI1_CAP_LOCKED_SHIFT    63
#define HFI1_CAP_LOCKED_MASK     0x1ULL
#define HFI1_CAP_LOCKED_SMASK    (HFI1_CAP_LOCKED_MASK << HFI1_CAP_LOCKED_SHIFT)
/* extra bits used between kernel and user processes */
#define HFI1_CAP_MISC_SHIFT      (HFI1_CAP_USER_SHIFT * 2)
#define HFI1_CAP_MISC_MASK       ((1ULL << (HFI1_CAP_LOCKED_SHIFT - \
					   HFI1_CAP_MISC_SHIFT)) - 1)

#define HFI1_CAP_KSET(cap) ({ hfi1_cap_mask |= HFI1_CAP_##cap; hfi1_cap_mask; })
#define HFI1_CAP_KCLEAR(cap)						\
	({								\
		hfi1_cap_mask &= ~HFI1_CAP_##cap;			\
		hfi1_cap_mask;						\
	})
#define HFI1_CAP_USET(cap)						\
	({								\
		hfi1_cap_mask |= (HFI1_CAP_##cap << HFI1_CAP_USER_SHIFT); \
		hfi1_cap_mask;						\
		})
#define HFI1_CAP_UCLEAR(cap)						\
	({								\
		hfi1_cap_mask &= ~(HFI1_CAP_##cap << HFI1_CAP_USER_SHIFT); \
		hfi1_cap_mask;						\
	})
#define HFI1_CAP_SET(cap)						\
	({								\
		hfi1_cap_mask |= (HFI1_CAP_##cap | (HFI1_CAP_##cap <<	\
						  HFI1_CAP_USER_SHIFT)); \
		hfi1_cap_mask;						\
	})
#define HFI1_CAP_CLEAR(cap)						\
	({								\
		hfi1_cap_mask &= ~(HFI1_CAP_##cap |			\
				  (HFI1_CAP_##cap << HFI1_CAP_USER_SHIFT)); \
		hfi1_cap_mask;						\
	})
#define HFI1_CAP_LOCK()							\
	({ hfi1_cap_mask |= HFI1_CAP_LOCKED_SMASK; hfi1_cap_mask; })
#define HFI1_CAP_LOCKED() (!!(hfi1_cap_mask & HFI1_CAP_LOCKED_SMASK))
/*
 * The set of capability bits that can be changed after initial load
 * This set is the same for kernel and user contexts. However, for
 * user contexts, the set can be further filtered by using the
 * HFI1_CAP_RESERVED_MASK bits.
 */
#define HFI1_CAP_WRITABLE_MASK   (HFI1_CAP_SDMA_AHG |			\
				  HFI1_CAP_HDRSUPP |			\
				  HFI1_CAP_MULTI_PKT_EGR |		\
				  HFI1_CAP_NODROP_RHQ_FULL |		\
				  HFI1_CAP_NODROP_EGR_FULL |		\
				  HFI1_CAP_ALLOW_PERM_JKEY |		\
				  HFI1_CAP_STATIC_RATE_CTRL |		\
				  HFI1_CAP_PRINT_UNIMPL |		\
				  HFI1_CAP_TID_UNMAP)
/*
 * A set of capability bits that are "global" and are not allowed to be
 * set in the user bitmask.
 */
#define HFI1_CAP_RESERVED_MASK   ((HFI1_CAP_SDMA |			\
				  HFI1_CAP_USE_SDMA_HEAD |		\
				  HFI1_CAP_EXTENDED_PSN |		\
				  HFI1_CAP_PRINT_UNIMPL |		\
				  HFI1_CAP_NO_INTEGRITY |		\
				  HFI1_CAP_PKEY_CHECK) <<		\
				 HFI1_CAP_USER_SHIFT)
/*
 * Set of capabilities that need to be enabled for kernel context in
 * order to be allowed for user contexts, as well.
 */
#define HFI1_CAP_MUST_HAVE_KERN (HFI1_CAP_STATIC_RATE_CTRL)
/* Default enabled capabilities (both kernel and user) */
#define HFI1_CAP_MASK_DEFAULT    (HFI1_CAP_HDRSUPP |			\
				 HFI1_CAP_NODROP_RHQ_FULL |		\
				 HFI1_CAP_NODROP_EGR_FULL |		\
				 HFI1_CAP_SDMA |			\
				 HFI1_CAP_PRINT_UNIMPL |		\
				 HFI1_CAP_STATIC_RATE_CTRL |		\
				 HFI1_CAP_PKEY_CHECK |			\
				 HFI1_CAP_MULTI_PKT_EGR |		\
				 HFI1_CAP_EXTENDED_PSN |		\
				 ((HFI1_CAP_HDRSUPP |			\
				   HFI1_CAP_MULTI_PKT_EGR |		\
				   HFI1_CAP_STATIC_RATE_CTRL |		\
				   HFI1_CAP_PKEY_CHECK |		\
				   HFI1_CAP_EARLY_CREDIT_RETURN) <<	\
				  HFI1_CAP_USER_SHIFT))
/*
 * A bitmask of kernel/global capabilities that should be communicated
 * to user level processes.
 */
#define HFI1_CAP_K2U (HFI1_CAP_SDMA |			\
		     HFI1_CAP_EXTENDED_PSN |		\
		     HFI1_CAP_PKEY_CHECK |		\
		     HFI1_CAP_NO_INTEGRITY)

#define HFI1_USER_SWVERSION ((HFI1_USER_SWMAJOR << HFI1_SWMAJOR_SHIFT) | \
			     HFI1_USER_SWMINOR)

#ifndef HFI1_KERN_TYPE
#define HFI1_KERN_TYPE 0
#endif

/*
 * Similarly, this is the kernel version going back to the user.  It's
 * slightly different, in that we want to tell if the driver was built as
 * part of a Intel release, or from the driver from openfabrics.org,
 * kernel.org, or a standard distribution, for support reasons.
 * The high bit is 0 for non-Intel and 1 for Intel-built/supplied.
 *
 * It's returned by the driver to the user code during initialization in the
 * spi_sw_version field of hfi1_base_info, so the user code can in turn
 * check for compatibility with the kernel.
*/
#define HFI1_KERN_SWVERSION ((HFI1_KERN_TYPE << 31) | HFI1_USER_SWVERSION)

/*
 * Define the driver version number.  This is something that refers only
 * to the driver itself, not the software interfaces it supports.
 */
#ifndef HFI1_DRIVER_VERSION_BASE
#define HFI1_DRIVER_VERSION_BASE "0.9-294"
#endif

/* create the final driver version string */
#ifdef HFI1_IDSTR
#define HFI1_DRIVER_VERSION HFI1_DRIVER_VERSION_BASE " " HFI1_IDSTR
#else
#define HFI1_DRIVER_VERSION HFI1_DRIVER_VERSION_BASE
#endif

/*
 * Diagnostics can send a packet by writing the following
 * struct to the diag packet special file.
 *
 * This allows a custom PBC qword, so that special modes and deliberate
 * changes to CRCs can be used.
 */
#define _DIAG_PKT_VERS 1
struct diag_pkt {
	__u16 version;		/* structure version */
	__u16 unit;		/* which device */
	__u16 sw_index;		/* send sw index to use */
	__u16 len;		/* data length, in bytes */
	__u16 port;		/* port number */
	__u16 unused;
	__u32 flags;		/* call flags */
	__u64 data;		/* user data pointer */
	__u64 pbc;		/* PBC for the packet */
};

/* diag_pkt flags */
#define F_DIAGPKT_WAIT 0x1	/* wait until packet is sent */

/*
 * The next set of defines are for packet headers, and chip register
 * and memory bits that are visible to and/or used by user-mode software.
 */

/*
 * Receive Header Flags
 */
#define RHF_PKT_LEN_SHIFT	0
#define RHF_PKT_LEN_MASK	0xfffull
#define RHF_PKT_LEN_SMASK (RHF_PKT_LEN_MASK << RHF_PKT_LEN_SHIFT)

#define RHF_RCV_TYPE_SHIFT	12
#define RHF_RCV_TYPE_MASK	0x7ull
#define RHF_RCV_TYPE_SMASK (RHF_RCV_TYPE_MASK << RHF_RCV_TYPE_SHIFT)

#define RHF_USE_EGR_BFR_SHIFT	15
#define RHF_USE_EGR_BFR_MASK	0x1ull
#define RHF_USE_EGR_BFR_SMASK (RHF_USE_EGR_BFR_MASK << RHF_USE_EGR_BFR_SHIFT)

#define RHF_EGR_INDEX_SHIFT	16
#define RHF_EGR_INDEX_MASK	0x7ffull
#define RHF_EGR_INDEX_SMASK (RHF_EGR_INDEX_MASK << RHF_EGR_INDEX_SHIFT)

#define RHF_DC_INFO_SHIFT	27
#define RHF_DC_INFO_MASK	0x1ull
#define RHF_DC_INFO_SMASK (RHF_DC_INFO_MASK << RHF_DC_INFO_SHIFT)

#define RHF_RCV_SEQ_SHIFT	28
#define RHF_RCV_SEQ_MASK	0xfull
#define RHF_RCV_SEQ_SMASK (RHF_RCV_SEQ_MASK << RHF_RCV_SEQ_SHIFT)

#define RHF_EGR_OFFSET_SHIFT	32
#define RHF_EGR_OFFSET_MASK	0xfffull
#define RHF_EGR_OFFSET_SMASK (RHF_EGR_OFFSET_MASK << RHF_EGR_OFFSET_SHIFT)
#define RHF_HDRQ_OFFSET_SHIFT	44
#define RHF_HDRQ_OFFSET_MASK	0x1ffull
#define RHF_HDRQ_OFFSET_SMASK (RHF_HDRQ_OFFSET_MASK << RHF_HDRQ_OFFSET_SHIFT)
#define RHF_K_HDR_LEN_ERR	(0x1ull << 53)
#define RHF_DC_UNC_ERR		(0x1ull << 54)
#define RHF_DC_ERR		(0x1ull << 55)
#define RHF_RCV_TYPE_ERR_SHIFT	56
#define RHF_RCV_TYPE_ERR_MASK	0x7ul
#define RHF_RCV_TYPE_ERR_SMASK (RHF_RCV_TYPE_ERR_MASK << RHF_RCV_TYPE_ERR_SHIFT)
#define RHF_TID_ERR		(0x1ull << 59)
#define RHF_LEN_ERR		(0x1ull << 60)
#define RHF_ECC_ERR		(0x1ull << 61)
#define RHF_VCRC_ERR		(0x1ull << 62)
#define RHF_ICRC_ERR		(0x1ull << 63)

#define RHF_ERROR_SMASK 0xffe0000000000000ull		/* bits 63:53 */

/* RHF receive types */
#define RHF_RCV_TYPE_EXPECTED 0
#define RHF_RCV_TYPE_EAGER    1
#define RHF_RCV_TYPE_IB       2 /* normal IB, IB Raw, or IPv6 */
#define RHF_RCV_TYPE_ERROR    3
#define RHF_RCV_TYPE_BYPASS   4
#define RHF_RCV_TYPE_INVALID5 5
#define RHF_RCV_TYPE_INVALID6 6
#define RHF_RCV_TYPE_INVALID7 7

/* RHF receive type error - expected packet errors */
#define RHF_RTE_EXPECTED_FLOW_SEQ_ERR	0x2
#define RHF_RTE_EXPECTED_FLOW_GEN_ERR	0x4

/* RHF receive type error - eager packet errors */
#define RHF_RTE_EAGER_NO_ERR		0x0

/* RHF receive type error - IB packet errors */
#define RHF_RTE_IB_NO_ERR		0x0

/* RHF receive type error - error packet errors */
#define RHF_RTE_ERROR_NO_ERR		0x0
#define RHF_RTE_ERROR_OP_CODE_ERR	0x1
#define RHF_RTE_ERROR_KHDR_MIN_LEN_ERR	0x2
#define RHF_RTE_ERROR_KHDR_HCRC_ERR	0x3
#define RHF_RTE_ERROR_KHDR_KVER_ERR	0x4
#define RHF_RTE_ERROR_CONTEXT_ERR	0x5
#define RHF_RTE_ERROR_KHDR_TID_ERR	0x6

/* RHF receive type error - bypass packet errors */
#define RHF_RTE_BYPASS_NO_ERR		0x0

/* IB - LRH header constants */
#define HFI1_LRH_GRH 0x0003      /* 1. word of IB LRH - next header: GRH */
#define HFI1_LRH_BTH 0x0002      /* 1. word of IB LRH - next header: BTH */

/* misc. */
#define SIZE_OF_CRC 1

#define LIM_MGMT_P_KEY       0x7FFF
#define FULL_MGMT_P_KEY      0xFFFF

#define DEFAULT_P_KEY LIM_MGMT_P_KEY
#define HFI1_FECN_SHIFT 31
#define HFI1_FECN_MASK 1
#define HFI1_FECN_SMASK BIT(HFI1_FECN_SHIFT)
#define HFI1_BECN_SHIFT 30
#define HFI1_BECN_MASK 1
#define HFI1_BECN_SMASK BIT(HFI1_BECN_SHIFT)

#define HFI1_PSM_IOC_BASE_SEQ 0x0

static inline __u64 rhf_to_cpu(const __le32 *rbuf)
{
	return __le64_to_cpu(*((__le64 *)rbuf));
}

static inline u64 rhf_err_flags(u64 rhf)
{
	return rhf & RHF_ERROR_SMASK;
}

static inline u32 rhf_rcv_type(u64 rhf)
{
	return (rhf >> RHF_RCV_TYPE_SHIFT) & RHF_RCV_TYPE_MASK;
}

static inline u32 rhf_rcv_type_err(u64 rhf)
{
	return (rhf >> RHF_RCV_TYPE_ERR_SHIFT) & RHF_RCV_TYPE_ERR_MASK;
}

/* return size is in bytes, not DWORDs */
static inline u32 rhf_pkt_len(u64 rhf)
{
	return ((rhf & RHF_PKT_LEN_SMASK) >> RHF_PKT_LEN_SHIFT) << 2;
}

static inline u32 rhf_egr_index(u64 rhf)
{
	return (rhf >> RHF_EGR_INDEX_SHIFT) & RHF_EGR_INDEX_MASK;
}

static inline u32 rhf_rcv_seq(u64 rhf)
{
	return (rhf >> RHF_RCV_SEQ_SHIFT) & RHF_RCV_SEQ_MASK;
}

/* returned offset is in DWORDS */
static inline u32 rhf_hdrq_offset(u64 rhf)
{
	return (rhf >> RHF_HDRQ_OFFSET_SHIFT) & RHF_HDRQ_OFFSET_MASK;
}

static inline u64 rhf_use_egr_bfr(u64 rhf)
{
	return rhf & RHF_USE_EGR_BFR_SMASK;
}

static inline u64 rhf_dc_info(u64 rhf)
{
	return rhf & RHF_DC_INFO_SMASK;
}

static inline u32 rhf_egr_buf_offset(u64 rhf)
{
	return (rhf >> RHF_EGR_OFFSET_SHIFT) & RHF_EGR_OFFSET_MASK;
}
#endif /* _COMMON_H */
