/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2006-2015, Intel Corporation.
 *
 * Authors: Ashok Raj <ashok.raj@intel.com>
 *          Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *          David Woodhouse <David.Woodhouse@intel.com>
 */

#ifndef _INTEL_IOMMU_H_
#define _INTEL_IOMMU_H_

#include <linux/types.h>
#include <linux/iova.h>
#include <linux/io.h>
#include <linux/idr.h>
#include <linux/mmu_notifier.h>
#include <linux/list.h>
#include <linux/iommu.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/dmar.h>
#include <linux/bitfield.h>
#include <linux/xarray.h>
#include <linux/perf_event.h>
#include <linux/pci.h>

#include <asm/cacheflush.h>
#include <asm/iommu.h>
#include <uapi/linux/iommufd.h>

/*
 * VT-d hardware uses 4KiB page size regardless of host page size.
 */
#define VTD_PAGE_SHIFT		(12)
#define VTD_PAGE_SIZE		(1UL << VTD_PAGE_SHIFT)
#define VTD_PAGE_MASK		(((u64)-1) << VTD_PAGE_SHIFT)
#define VTD_PAGE_ALIGN(addr)	(((addr) + VTD_PAGE_SIZE - 1) & VTD_PAGE_MASK)

#define IOVA_PFN(addr)		((addr) >> PAGE_SHIFT)

#define VTD_STRIDE_SHIFT        (9)
#define VTD_STRIDE_MASK         (((u64)-1) << VTD_STRIDE_SHIFT)

#define DMA_PTE_READ		BIT_ULL(0)
#define DMA_PTE_WRITE		BIT_ULL(1)
#define DMA_PTE_LARGE_PAGE	BIT_ULL(7)
#define DMA_PTE_SNP		BIT_ULL(11)

#define DMA_FL_PTE_PRESENT	BIT_ULL(0)
#define DMA_FL_PTE_US		BIT_ULL(2)
#define DMA_FL_PTE_ACCESS	BIT_ULL(5)
#define DMA_FL_PTE_DIRTY	BIT_ULL(6)

#define DMA_SL_PTE_DIRTY_BIT	9
#define DMA_SL_PTE_DIRTY	BIT_ULL(DMA_SL_PTE_DIRTY_BIT)

#define ADDR_WIDTH_5LEVEL	(57)
#define ADDR_WIDTH_4LEVEL	(48)

#define CONTEXT_TT_MULTI_LEVEL	0
#define CONTEXT_TT_DEV_IOTLB	1
#define CONTEXT_TT_PASS_THROUGH 2
#define CONTEXT_PASIDE		BIT_ULL(3)

/*
 * Intel IOMMU register specification per version 1.0 public spec.
 */
#define	DMAR_VER_REG	0x0	/* Arch version supported by this IOMMU */
#define	DMAR_CAP_REG	0x8	/* Hardware supported capabilities */
#define	DMAR_ECAP_REG	0x10	/* Extended capabilities supported */
#define	DMAR_GCMD_REG	0x18	/* Global command register */
#define	DMAR_GSTS_REG	0x1c	/* Global status register */
#define	DMAR_RTADDR_REG	0x20	/* Root entry table */
#define	DMAR_CCMD_REG	0x28	/* Context command reg */
#define	DMAR_FSTS_REG	0x34	/* Fault Status register */
#define	DMAR_FECTL_REG	0x38	/* Fault control register */
#define	DMAR_FEDATA_REG	0x3c	/* Fault event interrupt data register */
#define	DMAR_FEADDR_REG	0x40	/* Fault event interrupt addr register */
#define	DMAR_FEUADDR_REG 0x44	/* Upper address register */
#define	DMAR_PMEN_REG	0x64	/* Enable Protected Memory Region */
#define	DMAR_PLMBASE_REG 0x68	/* PMRR Low addr */
#define	DMAR_PLMLIMIT_REG 0x6c	/* PMRR low limit */
#define	DMAR_PHMBASE_REG 0x70	/* pmrr high base addr */
#define	DMAR_PHMLIMIT_REG 0x78	/* pmrr high limit */
#define DMAR_IQH_REG	0x80	/* Invalidation queue head register */
#define DMAR_IQT_REG	0x88	/* Invalidation queue tail register */
#define DMAR_IQ_SHIFT	4	/* Invalidation queue head/tail shift */
#define DMAR_IQA_REG	0x90	/* Invalidation queue addr register */
#define DMAR_ICS_REG	0x9c	/* Invalidation complete status register */
#define DMAR_IQER_REG	0xb0	/* Invalidation queue error record register */
#define DMAR_IRTA_REG	0xb8    /* Interrupt remapping table addr register */
#define DMAR_PQH_REG	0xc0	/* Page request queue head register */
#define DMAR_PQT_REG	0xc8	/* Page request queue tail register */
#define DMAR_PQA_REG	0xd0	/* Page request queue address register */
#define DMAR_PRS_REG	0xdc	/* Page request status register */
#define DMAR_PECTL_REG	0xe0	/* Page request event control register */
#define	DMAR_PEDATA_REG	0xe4	/* Page request event interrupt data register */
#define	DMAR_PEADDR_REG	0xe8	/* Page request event interrupt addr register */
#define	DMAR_PEUADDR_REG 0xec	/* Page request event Upper address register */
#define DMAR_MTRRCAP_REG 0x100	/* MTRR capability register */
#define DMAR_MTRRDEF_REG 0x108	/* MTRR default type register */
#define DMAR_MTRR_FIX64K_00000_REG 0x120 /* MTRR Fixed range registers */
#define DMAR_MTRR_FIX16K_80000_REG 0x128
#define DMAR_MTRR_FIX16K_A0000_REG 0x130
#define DMAR_MTRR_FIX4K_C0000_REG 0x138
#define DMAR_MTRR_FIX4K_C8000_REG 0x140
#define DMAR_MTRR_FIX4K_D0000_REG 0x148
#define DMAR_MTRR_FIX4K_D8000_REG 0x150
#define DMAR_MTRR_FIX4K_E0000_REG 0x158
#define DMAR_MTRR_FIX4K_E8000_REG 0x160
#define DMAR_MTRR_FIX4K_F0000_REG 0x168
#define DMAR_MTRR_FIX4K_F8000_REG 0x170
#define DMAR_MTRR_PHYSBASE0_REG 0x180 /* MTRR Variable range registers */
#define DMAR_MTRR_PHYSMASK0_REG 0x188
#define DMAR_MTRR_PHYSBASE1_REG 0x190
#define DMAR_MTRR_PHYSMASK1_REG 0x198
#define DMAR_MTRR_PHYSBASE2_REG 0x1a0
#define DMAR_MTRR_PHYSMASK2_REG 0x1a8
#define DMAR_MTRR_PHYSBASE3_REG 0x1b0
#define DMAR_MTRR_PHYSMASK3_REG 0x1b8
#define DMAR_MTRR_PHYSBASE4_REG 0x1c0
#define DMAR_MTRR_PHYSMASK4_REG 0x1c8
#define DMAR_MTRR_PHYSBASE5_REG 0x1d0
#define DMAR_MTRR_PHYSMASK5_REG 0x1d8
#define DMAR_MTRR_PHYSBASE6_REG 0x1e0
#define DMAR_MTRR_PHYSMASK6_REG 0x1e8
#define DMAR_MTRR_PHYSBASE7_REG 0x1f0
#define DMAR_MTRR_PHYSMASK7_REG 0x1f8
#define DMAR_MTRR_PHYSBASE8_REG 0x200
#define DMAR_MTRR_PHYSMASK8_REG 0x208
#define DMAR_MTRR_PHYSBASE9_REG 0x210
#define DMAR_MTRR_PHYSMASK9_REG 0x218
#define DMAR_PERFCAP_REG	0x300
#define DMAR_PERFCFGOFF_REG	0x310
#define DMAR_PERFOVFOFF_REG	0x318
#define DMAR_PERFCNTROFF_REG	0x31c
#define DMAR_PERFINTRSTS_REG	0x324
#define DMAR_PERFINTRCTL_REG	0x328
#define DMAR_PERFEVNTCAP_REG	0x380
#define DMAR_ECMD_REG		0x400
#define DMAR_ECEO_REG		0x408
#define DMAR_ECRSP_REG		0x410
#define DMAR_ECCAP_REG		0x430

#define DMAR_IQER_REG_IQEI(reg)		FIELD_GET(GENMASK_ULL(3, 0), reg)
#define DMAR_IQER_REG_ITESID(reg)	FIELD_GET(GENMASK_ULL(47, 32), reg)
#define DMAR_IQER_REG_ICESID(reg)	FIELD_GET(GENMASK_ULL(63, 48), reg)

#define OFFSET_STRIDE		(9)

#define dmar_readq(a) readq(a)
#define dmar_writeq(a,v) writeq(v,a)
#define dmar_readl(a) readl(a)
#define dmar_writel(a, v) writel(v, a)

#define DMAR_VER_MAJOR(v)		(((v) & 0xf0) >> 4)
#define DMAR_VER_MINOR(v)		((v) & 0x0f)

/*
 * Decoding Capability Register
 */
#define cap_esrtps(c)		(((c) >> 63) & 1)
#define cap_esirtps(c)		(((c) >> 62) & 1)
#define cap_ecmds(c)		(((c) >> 61) & 1)
#define cap_fl5lp_support(c)	(((c) >> 60) & 1)
#define cap_pi_support(c)	(((c) >> 59) & 1)
#define cap_fl1gp_support(c)	(((c) >> 56) & 1)
#define cap_read_drain(c)	(((c) >> 55) & 1)
#define cap_write_drain(c)	(((c) >> 54) & 1)
#define cap_max_amask_val(c)	(((c) >> 48) & 0x3f)
#define cap_num_fault_regs(c)	((((c) >> 40) & 0xff) + 1)
#define cap_pgsel_inv(c)	(((c) >> 39) & 1)

#define cap_super_page_val(c)	(((c) >> 34) & 0xf)

#define cap_fault_reg_offset(c)	((((c) >> 24) & 0x3ff) * 16)
#define cap_max_fault_reg_offset(c) \
	(cap_fault_reg_offset(c) + cap_num_fault_regs(c) * 16)

#define cap_zlr(c)		(((c) >> 22) & 1)
#define cap_isoch(c)		(((c) >> 23) & 1)
#define cap_mgaw(c)		((((c) >> 16) & 0x3f) + 1)
#define cap_sagaw(c)		(((c) >> 8) & 0x1f)
#define cap_caching_mode(c)	(((c) >> 7) & 1)
#define cap_phmr(c)		(((c) >> 6) & 1)
#define cap_plmr(c)		(((c) >> 5) & 1)
#define cap_rwbf(c)		(((c) >> 4) & 1)
#define cap_afl(c)		(((c) >> 3) & 1)
#define cap_ndoms(c)		(((unsigned long)1) << (4 + 2 * ((c) & 0x7)))
/*
 * Extended Capability Register
 */

#define ecap_pms(e)		(((e) >> 51) & 0x1)
#define ecap_rps(e)		(((e) >> 49) & 0x1)
#define ecap_smpwc(e)		(((e) >> 48) & 0x1)
#define ecap_flts(e)		(((e) >> 47) & 0x1)
#define ecap_slts(e)		(((e) >> 46) & 0x1)
#define ecap_slads(e)		(((e) >> 45) & 0x1)
#define ecap_smts(e)		(((e) >> 43) & 0x1)
#define ecap_dit(e)		(((e) >> 41) & 0x1)
#define ecap_pds(e)		(((e) >> 42) & 0x1)
#define ecap_pasid(e)		(((e) >> 40) & 0x1)
#define ecap_pss(e)		(((e) >> 35) & 0x1f)
#define ecap_eafs(e)		(((e) >> 34) & 0x1)
#define ecap_nwfs(e)		(((e) >> 33) & 0x1)
#define ecap_srs(e)		(((e) >> 31) & 0x1)
#define ecap_ers(e)		(((e) >> 30) & 0x1)
#define ecap_prs(e)		(((e) >> 29) & 0x1)
#define ecap_broken_pasid(e)	(((e) >> 28) & 0x1)
#define ecap_dis(e)		(((e) >> 27) & 0x1)
#define ecap_nest(e)		(((e) >> 26) & 0x1)
#define ecap_mts(e)		(((e) >> 25) & 0x1)
#define ecap_iotlb_offset(e) 	((((e) >> 8) & 0x3ff) * 16)
#define ecap_max_iotlb_offset(e) (ecap_iotlb_offset(e) + 16)
#define ecap_coherent(e)	((e) & 0x1)
#define ecap_qis(e)		((e) & 0x2)
#define ecap_pass_through(e)	(((e) >> 6) & 0x1)
#define ecap_eim_support(e)	(((e) >> 4) & 0x1)
#define ecap_ir_support(e)	(((e) >> 3) & 0x1)
#define ecap_dev_iotlb_support(e)	(((e) >> 2) & 0x1)
#define ecap_max_handle_mask(e) (((e) >> 20) & 0xf)
#define ecap_sc_support(e)	(((e) >> 7) & 0x1) /* Snooping Control */

/*
 * Decoding Perf Capability Register
 */
#define pcap_num_cntr(p)	((p) & 0xffff)
#define pcap_cntr_width(p)	(((p) >> 16) & 0x7f)
#define pcap_num_event_group(p)	(((p) >> 24) & 0x1f)
#define pcap_filters_mask(p)	(((p) >> 32) & 0x1f)
#define pcap_interrupt(p)	(((p) >> 50) & 0x1)
/* The counter stride is calculated as 2 ^ (x+10) bytes */
#define pcap_cntr_stride(p)	(1ULL << ((((p) >> 52) & 0x7) + 10))

/*
 * Decoding Perf Event Capability Register
 */
#define pecap_es(p)		((p) & 0xfffffff)

/* Virtual command interface capability */
#define vccap_pasid(v)		(((v) & DMA_VCS_PAS)) /* PASID allocation */

/* IOTLB_REG */
#define DMA_TLB_FLUSH_GRANU_OFFSET  60
#define DMA_TLB_GLOBAL_FLUSH (((u64)1) << 60)
#define DMA_TLB_DSI_FLUSH (((u64)2) << 60)
#define DMA_TLB_PSI_FLUSH (((u64)3) << 60)
#define DMA_TLB_IIRG(type) ((type >> 60) & 3)
#define DMA_TLB_IAIG(val) (((val) >> 57) & 3)
#define DMA_TLB_READ_DRAIN (((u64)1) << 49)
#define DMA_TLB_WRITE_DRAIN (((u64)1) << 48)
#define DMA_TLB_DID(id)	(((u64)((id) & 0xffff)) << 32)
#define DMA_TLB_IVT (((u64)1) << 63)
#define DMA_TLB_IH_NONLEAF (((u64)1) << 6)
#define DMA_TLB_MAX_SIZE (0x3f)

/* INVALID_DESC */
#define DMA_CCMD_INVL_GRANU_OFFSET  61
#define DMA_ID_TLB_GLOBAL_FLUSH	(((u64)1) << 4)
#define DMA_ID_TLB_DSI_FLUSH	(((u64)2) << 4)
#define DMA_ID_TLB_PSI_FLUSH	(((u64)3) << 4)
#define DMA_ID_TLB_READ_DRAIN	(((u64)1) << 7)
#define DMA_ID_TLB_WRITE_DRAIN	(((u64)1) << 6)
#define DMA_ID_TLB_DID(id)	(((u64)((id & 0xffff) << 16)))
#define DMA_ID_TLB_IH_NONLEAF	(((u64)1) << 6)
#define DMA_ID_TLB_ADDR(addr)	(addr)
#define DMA_ID_TLB_ADDR_MASK(mask)	(mask)

/* PMEN_REG */
#define DMA_PMEN_EPM (((u32)1)<<31)
#define DMA_PMEN_PRS (((u32)1)<<0)

/* GCMD_REG */
#define DMA_GCMD_TE (((u32)1) << 31)
#define DMA_GCMD_SRTP (((u32)1) << 30)
#define DMA_GCMD_SFL (((u32)1) << 29)
#define DMA_GCMD_EAFL (((u32)1) << 28)
#define DMA_GCMD_WBF (((u32)1) << 27)
#define DMA_GCMD_QIE (((u32)1) << 26)
#define DMA_GCMD_SIRTP (((u32)1) << 24)
#define DMA_GCMD_IRE (((u32) 1) << 25)
#define DMA_GCMD_CFI (((u32) 1) << 23)

/* GSTS_REG */
#define DMA_GSTS_TES (((u32)1) << 31)
#define DMA_GSTS_RTPS (((u32)1) << 30)
#define DMA_GSTS_FLS (((u32)1) << 29)
#define DMA_GSTS_AFLS (((u32)1) << 28)
#define DMA_GSTS_WBFS (((u32)1) << 27)
#define DMA_GSTS_QIES (((u32)1) << 26)
#define DMA_GSTS_IRTPS (((u32)1) << 24)
#define DMA_GSTS_IRES (((u32)1) << 25)
#define DMA_GSTS_CFIS (((u32)1) << 23)

/* DMA_RTADDR_REG */
#define DMA_RTADDR_SMT (((u64)1) << 10)

/* CCMD_REG */
#define DMA_CCMD_ICC (((u64)1) << 63)
#define DMA_CCMD_GLOBAL_INVL (((u64)1) << 61)
#define DMA_CCMD_DOMAIN_INVL (((u64)2) << 61)
#define DMA_CCMD_DEVICE_INVL (((u64)3) << 61)
#define DMA_CCMD_FM(m) (((u64)((m) & 0x3)) << 32)
#define DMA_CCMD_MASK_NOBIT 0
#define DMA_CCMD_MASK_1BIT 1
#define DMA_CCMD_MASK_2BIT 2
#define DMA_CCMD_MASK_3BIT 3
#define DMA_CCMD_SID(s) (((u64)((s) & 0xffff)) << 16)
#define DMA_CCMD_DID(d) ((u64)((d) & 0xffff))

/* ECMD_REG */
#define DMA_MAX_NUM_ECMD		256
#define DMA_MAX_NUM_ECMDCAP		(DMA_MAX_NUM_ECMD / 64)
#define DMA_ECMD_REG_STEP		8
#define DMA_ECMD_ENABLE			0xf0
#define DMA_ECMD_DISABLE		0xf1
#define DMA_ECMD_FREEZE			0xf4
#define DMA_ECMD_UNFREEZE		0xf5
#define DMA_ECMD_OA_SHIFT		16
#define DMA_ECMD_ECRSP_IP		0x1
#define DMA_ECMD_ECCAP3			3
#define DMA_ECMD_ECCAP3_ECNTS		BIT_ULL(48)
#define DMA_ECMD_ECCAP3_DCNTS		BIT_ULL(49)
#define DMA_ECMD_ECCAP3_FCNTS		BIT_ULL(52)
#define DMA_ECMD_ECCAP3_UFCNTS		BIT_ULL(53)
#define DMA_ECMD_ECCAP3_ESSENTIAL	(DMA_ECMD_ECCAP3_ECNTS |	\
					 DMA_ECMD_ECCAP3_DCNTS |	\
					 DMA_ECMD_ECCAP3_FCNTS |	\
					 DMA_ECMD_ECCAP3_UFCNTS)

/* FECTL_REG */
#define DMA_FECTL_IM (((u32)1) << 31)

/* FSTS_REG */
#define DMA_FSTS_PFO (1 << 0) /* Primary Fault Overflow */
#define DMA_FSTS_PPF (1 << 1) /* Primary Pending Fault */
#define DMA_FSTS_IQE (1 << 4) /* Invalidation Queue Error */
#define DMA_FSTS_ICE (1 << 5) /* Invalidation Completion Error */
#define DMA_FSTS_ITE (1 << 6) /* Invalidation Time-out Error */
#define DMA_FSTS_PRO (1 << 7) /* Page Request Overflow */
#define dma_fsts_fault_record_index(s) (((s) >> 8) & 0xff)

/* FRCD_REG, 32 bits access */
#define DMA_FRCD_F (((u32)1) << 31)
#define dma_frcd_type(d) ((d >> 30) & 1)
#define dma_frcd_fault_reason(c) (c & 0xff)
#define dma_frcd_source_id(c) (c & 0xffff)
#define dma_frcd_pasid_value(c) (((c) >> 8) & 0xfffff)
#define dma_frcd_pasid_present(c) (((c) >> 31) & 1)
/* low 64 bit */
#define dma_frcd_page_addr(d) (d & (((u64)-1) << PAGE_SHIFT))

/* PRS_REG */
#define DMA_PRS_PPR	((u32)1)
#define DMA_PRS_PRO	((u32)2)

#define DMA_VCS_PAS	((u64)1)

/* PERFINTRSTS_REG */
#define DMA_PERFINTRSTS_PIS	((u32)1)

#define IOMMU_WAIT_OP(iommu, offset, op, cond, sts)			\
do {									\
	cycles_t start_time = get_cycles();				\
	while (1) {							\
		sts = op(iommu->reg + offset);				\
		if (cond)						\
			break;						\
		if (DMAR_OPERATION_TIMEOUT < (get_cycles() - start_time))\
			panic("DMAR hardware is malfunctioning\n");	\
		cpu_relax();						\
	}								\
} while (0)

#define QI_LENGTH	256	/* queue length */

enum {
	QI_FREE,
	QI_IN_USE,
	QI_DONE,
	QI_ABORT
};

#define QI_CC_TYPE		0x1
#define QI_IOTLB_TYPE		0x2
#define QI_DIOTLB_TYPE		0x3
#define QI_IEC_TYPE		0x4
#define QI_IWD_TYPE		0x5
#define QI_EIOTLB_TYPE		0x6
#define QI_PC_TYPE		0x7
#define QI_DEIOTLB_TYPE		0x8
#define QI_PGRP_RESP_TYPE	0x9
#define QI_PSTRM_RESP_TYPE	0xa

#define QI_IEC_SELECTIVE	(((u64)1) << 4)
#define QI_IEC_IIDEX(idx)	(((u64)(idx & 0xffff) << 32))
#define QI_IEC_IM(m)		(((u64)(m & 0x1f) << 27))

#define QI_IWD_STATUS_DATA(d)	(((u64)d) << 32)
#define QI_IWD_STATUS_WRITE	(((u64)1) << 5)
#define QI_IWD_FENCE		(((u64)1) << 6)
#define QI_IWD_PRQ_DRAIN	(((u64)1) << 7)

#define QI_IOTLB_DID(did) 	(((u64)did) << 16)
#define QI_IOTLB_DR(dr) 	(((u64)dr) << 7)
#define QI_IOTLB_DW(dw) 	(((u64)dw) << 6)
#define QI_IOTLB_GRAN(gran) 	(((u64)gran) >> (DMA_TLB_FLUSH_GRANU_OFFSET-4))
#define QI_IOTLB_ADDR(addr)	(((u64)addr) & VTD_PAGE_MASK)
#define QI_IOTLB_IH(ih)		(((u64)ih) << 6)
#define QI_IOTLB_AM(am)		(((u8)am) & 0x3f)

#define QI_CC_FM(fm)		(((u64)fm) << 48)
#define QI_CC_SID(sid)		(((u64)sid) << 32)
#define QI_CC_DID(did)		(((u64)did) << 16)
#define QI_CC_GRAN(gran)	(((u64)gran) >> (DMA_CCMD_INVL_GRANU_OFFSET-4))

#define QI_DEV_IOTLB_SID(sid)	((u64)((sid) & 0xffff) << 32)
#define QI_DEV_IOTLB_QDEP(qdep)	(((qdep) & 0x1f) << 16)
#define QI_DEV_IOTLB_ADDR(addr)	((u64)(addr) & VTD_PAGE_MASK)
#define QI_DEV_IOTLB_PFSID(pfsid) (((u64)(pfsid & 0xf) << 12) | \
				   ((u64)((pfsid >> 4) & 0xfff) << 52))
#define QI_DEV_IOTLB_SIZE	1
#define QI_DEV_IOTLB_MAX_INVS	32

#define QI_PC_PASID(pasid)	(((u64)pasid) << 32)
#define QI_PC_DID(did)		(((u64)did) << 16)
#define QI_PC_GRAN(gran)	(((u64)gran) << 4)

/* PASID cache invalidation granu */
#define QI_PC_ALL_PASIDS	0
#define QI_PC_PASID_SEL		1
#define QI_PC_GLOBAL		3

#define QI_EIOTLB_ADDR(addr)	((u64)(addr) & VTD_PAGE_MASK)
#define QI_EIOTLB_IH(ih)	(((u64)ih) << 6)
#define QI_EIOTLB_AM(am)	(((u64)am) & 0x3f)
#define QI_EIOTLB_PASID(pasid) 	(((u64)pasid) << 32)
#define QI_EIOTLB_DID(did)	(((u64)did) << 16)
#define QI_EIOTLB_GRAN(gran) 	(((u64)gran) << 4)

/* QI Dev-IOTLB inv granu */
#define QI_DEV_IOTLB_GRAN_ALL		1
#define QI_DEV_IOTLB_GRAN_PASID_SEL	0

#define QI_DEV_EIOTLB_ADDR(a)	((u64)(a) & VTD_PAGE_MASK)
#define QI_DEV_EIOTLB_SIZE	(((u64)1) << 11)
#define QI_DEV_EIOTLB_PASID(p)	((u64)((p) & 0xfffff) << 32)
#define QI_DEV_EIOTLB_SID(sid)	((u64)((sid) & 0xffff) << 16)
#define QI_DEV_EIOTLB_QDEP(qd)	((u64)((qd) & 0x1f) << 4)
#define QI_DEV_EIOTLB_PFSID(pfsid) (((u64)(pfsid & 0xf) << 12) | \
				    ((u64)((pfsid >> 4) & 0xfff) << 52))
#define QI_DEV_EIOTLB_MAX_INVS	32

/* Page group response descriptor QW0 */
#define QI_PGRP_PASID_P(p)	(((u64)(p)) << 4)
#define QI_PGRP_RESP_CODE(res)	(((u64)(res)) << 12)
#define QI_PGRP_DID(rid)	(((u64)(rid)) << 16)
#define QI_PGRP_PASID(pasid)	(((u64)(pasid)) << 32)

/* Page group response descriptor QW1 */
#define QI_PGRP_IDX(idx)	(((u64)(idx)) << 3)


#define QI_RESP_SUCCESS		0x0
#define QI_RESP_INVALID		0x1
#define QI_RESP_FAILURE		0xf

#define QI_GRAN_NONG_PASID		2
#define QI_GRAN_PSI_PASID		3

#define qi_shift(iommu)		(DMAR_IQ_SHIFT + !!ecap_smts((iommu)->ecap))

struct qi_desc {
	u64 qw0;
	u64 qw1;
	u64 qw2;
	u64 qw3;
};

struct q_inval {
	raw_spinlock_t  q_lock;
	void		*desc;          /* invalidation queue */
	int             *desc_status;   /* desc status */
	int             free_head;      /* first free entry */
	int             free_tail;      /* last free entry */
	int             free_cnt;
};

/* Page Request Queue depth */
#define PRQ_ORDER	4
#define PRQ_SIZE	(SZ_4K << PRQ_ORDER)
#define PRQ_RING_MASK	(PRQ_SIZE - 0x20)
#define PRQ_DEPTH	(PRQ_SIZE >> 5)

struct dmar_pci_notify_info;

#ifdef CONFIG_IRQ_REMAP
#define INTR_REMAP_TABLE_REG_SIZE	0xf
#define INTR_REMAP_TABLE_REG_SIZE_MASK  0xf

#define INTR_REMAP_TABLE_ENTRIES	65536

struct irq_domain;

struct ir_table {
	struct irte *base;
	unsigned long *bitmap;
};

void intel_irq_remap_add_device(struct dmar_pci_notify_info *info);
#else
static inline void
intel_irq_remap_add_device(struct dmar_pci_notify_info *info) { }
#endif

struct iommu_flush {
	void (*flush_context)(struct intel_iommu *iommu, u16 did, u16 sid,
			      u8 fm, u64 type);
	void (*flush_iotlb)(struct intel_iommu *iommu, u16 did, u64 addr,
			    unsigned int size_order, u64 type);
};

enum {
	SR_DMAR_FECTL_REG,
	SR_DMAR_FEDATA_REG,
	SR_DMAR_FEADDR_REG,
	SR_DMAR_FEUADDR_REG,
	MAX_SR_DMAR_REGS
};

#define VTD_FLAG_TRANS_PRE_ENABLED	(1 << 0)
#define VTD_FLAG_IRQ_REMAP_PRE_ENABLED	(1 << 1)
#define VTD_FLAG_SVM_CAPABLE		(1 << 2)

#define sm_supported(iommu)	(intel_iommu_sm && ecap_smts((iommu)->ecap))
#define pasid_supported(iommu)	(sm_supported(iommu) &&			\
				 ecap_pasid((iommu)->ecap))
#define ssads_supported(iommu) (sm_supported(iommu) &&                 \
				ecap_slads((iommu)->ecap) &&           \
				ecap_smpwc(iommu->ecap))
#define nested_supported(iommu)	(sm_supported(iommu) &&			\
				 ecap_nest((iommu)->ecap))

struct pasid_entry;
struct pasid_state_entry;
struct page_req_dsc;

/*
 * 0: Present
 * 1-11: Reserved
 * 12-63: Context Ptr (12 - (haw-1))
 * 64-127: Reserved
 */
struct root_entry {
	u64     lo;
	u64     hi;
};

/*
 * low 64 bits:
 * 0: present
 * 1: fault processing disable
 * 2-3: translation type
 * 12-63: address space root
 * high 64 bits:
 * 0-2: address width
 * 3-6: aval
 * 8-23: domain id
 */
struct context_entry {
	u64 lo;
	u64 hi;
};

struct iommu_domain_info {
	struct intel_iommu *iommu;
	unsigned int refcnt;		/* Refcount of devices per iommu */
	u16 did;			/* Domain ids per IOMMU. Use u16 since
					 * domain ids are 16 bit wide according
					 * to VT-d spec, section 9.3 */
};

/*
 * We start simply by using a fixed size for the batched descriptors. This
 * size is currently sufficient for our needs. Future improvements could
 * involve dynamically allocating the batch buffer based on actual demand,
 * allowing us to adjust the batch size for optimal performance in different
 * scenarios.
 */
#define QI_MAX_BATCHED_DESC_COUNT 16
struct qi_batch {
	struct qi_desc descs[QI_MAX_BATCHED_DESC_COUNT];
	unsigned int index;
};

struct dmar_domain {
	int	nid;			/* node id */
	struct xarray iommu_array;	/* Attached IOMMU array */

	u8 iommu_coherency: 1;		/* indicate coherency of iommu access */
	u8 force_snooping : 1;		/* Create IOPTEs with snoop control */
	u8 set_pte_snp:1;
	u8 use_first_level:1;		/* DMA translation for the domain goes
					 * through the first level page table,
					 * otherwise, goes through the second
					 * level.
					 */
	u8 dirty_tracking:1;		/* Dirty tracking is enabled */
	u8 nested_parent:1;		/* Has other domains nested on it */
	u8 has_mappings:1;		/* Has mappings configured through
					 * iommu_map() interface.
					 */
	u8 iotlb_sync_map:1;		/* Need to flush IOTLB cache or write
					 * buffer when creating mappings.
					 */

	spinlock_t lock;		/* Protect device tracking lists */
	struct list_head devices;	/* all devices' list */
	struct list_head dev_pasids;	/* all attached pasids */

	spinlock_t cache_lock;		/* Protect the cache tag list */
	struct list_head cache_tags;	/* Cache tag list */
	struct qi_batch *qi_batch;	/* Batched QI descriptors */

	int		iommu_superpage;/* Level of superpages supported:
					   0 == 4KiB (no superpages), 1 == 2MiB,
					   2 == 1GiB, 3 == 512GiB, 4 == 1TiB */
	union {
		/* DMA remapping domain */
		struct {
			/* virtual address */
			struct dma_pte	*pgd;
			/* max guest address width */
			int		gaw;
			/*
			 * adjusted guest address width:
			 *   0: level 2 30-bit
			 *   1: level 3 39-bit
			 *   2: level 4 48-bit
			 *   3: level 5 57-bit
			 */
			int		agaw;
			/* maximum mapped address */
			u64		max_addr;
			/* Protect the s1_domains list */
			spinlock_t	s1_lock;
			/* Track s1_domains nested on this domain */
			struct list_head s1_domains;
		};

		/* Nested user domain */
		struct {
			/* parent page table which the user domain is nested on */
			struct dmar_domain *s2_domain;
			/* page table attributes */
			struct iommu_hwpt_vtd_s1 s1_cfg;
			/* link to parent domain siblings */
			struct list_head s2_link;
		};

		/* SVA domain */
		struct {
			struct mmu_notifier notifier;
		};
	};

	struct iommu_domain domain;	/* generic domain data structure for
					   iommu core */
};

/*
 * In theory, the VT-d 4.0 spec can support up to 2 ^ 16 counters.
 * But in practice, there are only 14 counters for the existing
 * platform. Setting the max number of counters to 64 should be good
 * enough for a long time. Also, supporting more than 64 counters
 * requires more extras, e.g., extra freeze and overflow registers,
 * which is not necessary for now.
 */
#define IOMMU_PMU_IDX_MAX		64

struct iommu_pmu {
	struct intel_iommu	*iommu;
	u32			num_cntr;	/* Number of counters */
	u32			num_eg;		/* Number of event group */
	u32			cntr_width;	/* Counter width */
	u32			cntr_stride;	/* Counter Stride */
	u32			filter;		/* Bitmask of filter support */
	void __iomem		*base;		/* the PerfMon base address */
	void __iomem		*cfg_reg;	/* counter configuration base address */
	void __iomem		*cntr_reg;	/* counter 0 address*/
	void __iomem		*overflow;	/* overflow status register */

	u64			*evcap;		/* Indicates all supported events */
	u32			**cntr_evcap;	/* Supported events of each counter. */

	struct pmu		pmu;
	DECLARE_BITMAP(used_mask, IOMMU_PMU_IDX_MAX);
	struct perf_event	*event_list[IOMMU_PMU_IDX_MAX];
	unsigned char		irq_name[16];
};

#define IOMMU_IRQ_ID_OFFSET_PRQ		(DMAR_UNITS_SUPPORTED)
#define IOMMU_IRQ_ID_OFFSET_PERF	(2 * DMAR_UNITS_SUPPORTED)

struct intel_iommu {
	void __iomem	*reg; /* Pointer to hardware regs, virtual addr */
	u64 		reg_phys; /* physical address of hw register set */
	u64		reg_size; /* size of hw register set */
	u64		cap;
	u64		ecap;
	u64		vccap;
	u64		ecmdcap[DMA_MAX_NUM_ECMDCAP];
	u32		gcmd; /* Holds TE, EAFL. Don't need SRTP, SFL, WBF */
	raw_spinlock_t	register_lock; /* protect register handling */
	int		seq_id;	/* sequence id of the iommu */
	int		agaw; /* agaw of this iommu */
	int		msagaw; /* max sagaw of this iommu */
	unsigned int	irq, pr_irq, perf_irq;
	u16		segment;     /* PCI segment# */
	unsigned char	name[16];    /* Device Name */

#ifdef CONFIG_INTEL_IOMMU
	/* mutex to protect domain_ida */
	struct mutex	did_lock;
	struct ida	domain_ida; /* domain id allocator */
	unsigned long	*copied_tables; /* bitmap of copied tables */
	spinlock_t	lock; /* protect context, domain ids */
	struct root_entry *root_entry; /* virtual address */

	struct iommu_flush flush;
#endif
	struct page_req_dsc *prq;
	unsigned char prq_name[16];    /* Name for PRQ interrupt */
	unsigned long prq_seq_number;
	struct completion prq_complete;
	struct iopf_queue *iopf_queue;
	unsigned char iopfq_name[16];
	/* Synchronization between fault report and iommu device release. */
	struct mutex iopf_lock;
	struct q_inval  *qi;            /* Queued invalidation info */
	u32 iommu_state[MAX_SR_DMAR_REGS]; /* Store iommu states between suspend and resume.*/

	/* rb tree for all probed devices */
	struct rb_root device_rbtree;
	/* protect the device_rbtree */
	spinlock_t device_rbtree_lock;

#ifdef CONFIG_IRQ_REMAP
	struct ir_table *ir_table;	/* Interrupt remapping info */
	struct irq_domain *ir_domain;
#endif
	struct iommu_device iommu;  /* IOMMU core code handle */
	int		node;
	u32		flags;      /* Software defined flags */

	struct dmar_drhd_unit *drhd;
	void *perf_statistic;

	struct iommu_pmu *pmu;
};

/* PCI domain-device relationship */
struct device_domain_info {
	struct list_head link;	/* link to domain siblings */
	u32 segment;		/* PCI segment number */
	u8 bus;			/* PCI bus number */
	u8 devfn;		/* PCI devfn number */
	u16 pfsid;		/* SRIOV physical function source ID */
	u8 pasid_supported:3;
	u8 pasid_enabled:1;
	u8 pri_supported:1;
	u8 pri_enabled:1;
	u8 ats_supported:1;
	u8 ats_enabled:1;
	u8 dtlb_extra_inval:1;	/* Quirk for devices need extra flush */
	u8 domain_attached:1;	/* Device has domain attached */
	u8 ats_qdep;
	unsigned int iopf_refcount;
	struct device *dev; /* it's NULL for PCIe-to-PCI bridge */
	struct intel_iommu *iommu; /* IOMMU used by this device */
	struct dmar_domain *domain; /* pointer to domain */
	struct pasid_table *pasid_table; /* pasid table */
	/* device tracking node(lookup by PCI RID) */
	struct rb_node node;
#ifdef CONFIG_INTEL_IOMMU_DEBUGFS
	struct dentry *debugfs_dentry; /* pointer to device directory dentry */
#endif
};

struct dev_pasid_info {
	struct list_head link_domain;	/* link to domain siblings */
	struct device *dev;
	ioasid_t pasid;
#ifdef CONFIG_INTEL_IOMMU_DEBUGFS
	struct dentry *debugfs_dentry; /* pointer to pasid directory dentry */
#endif
};

static inline void __iommu_flush_cache(
	struct intel_iommu *iommu, void *addr, int size)
{
	if (!ecap_coherent(iommu->ecap))
		clflush_cache_range(addr, size);
}

/* Convert generic struct iommu_domain to private struct dmar_domain */
static inline struct dmar_domain *to_dmar_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct dmar_domain, domain);
}

/*
 * Domain ID 0 and 1 are reserved:
 *
 * If Caching mode is set, then invalid translations are tagged
 * with domain-id 0, hence we need to pre-allocate it. We also
 * use domain-id 0 as a marker for non-allocated domain-id, so
 * make sure it is not used for a real domain.
 *
 * Vt-d spec rev3.0 (section 6.2.3.1) requires that each pasid
 * entry for first-level or pass-through translation modes should
 * be programmed with a domain id different from those used for
 * second-level or nested translation. We reserve a domain id for
 * this purpose. This domain id is also used for identity domain
 * in legacy mode.
 */
#define FLPT_DEFAULT_DID		1
#define IDA_START_DID			2

/* Retrieve the domain ID which has allocated to the domain */
static inline u16
domain_id_iommu(struct dmar_domain *domain, struct intel_iommu *iommu)
{
	struct iommu_domain_info *info =
			xa_load(&domain->iommu_array, iommu->seq_id);

	return info->did;
}

static inline u16
iommu_domain_did(struct iommu_domain *domain, struct intel_iommu *iommu)
{
	if (domain->type == IOMMU_DOMAIN_SVA ||
	    domain->type == IOMMU_DOMAIN_IDENTITY)
		return FLPT_DEFAULT_DID;
	return domain_id_iommu(to_dmar_domain(domain), iommu);
}

static inline bool dev_is_real_dma_subdevice(struct device *dev)
{
	return dev && dev_is_pci(dev) &&
	       pci_real_dma_dev(to_pci_dev(dev)) != to_pci_dev(dev);
}

/*
 * 0: readable
 * 1: writable
 * 2-6: reserved
 * 7: super page
 * 8-10: available
 * 11: snoop behavior
 * 12-63: Host physical address
 */
struct dma_pte {
	u64 val;
};

static inline void dma_clear_pte(struct dma_pte *pte)
{
	pte->val = 0;
}

static inline u64 dma_pte_addr(struct dma_pte *pte)
{
#ifdef CONFIG_64BIT
	return pte->val & VTD_PAGE_MASK;
#else
	/* Must have a full atomic 64-bit read */
	return  __cmpxchg64(&pte->val, 0ULL, 0ULL) & VTD_PAGE_MASK;
#endif
}

static inline bool dma_pte_present(struct dma_pte *pte)
{
	return (pte->val & 3) != 0;
}

static inline bool dma_sl_pte_test_and_clear_dirty(struct dma_pte *pte,
						   unsigned long flags)
{
	if (flags & IOMMU_DIRTY_NO_CLEAR)
		return (pte->val & DMA_SL_PTE_DIRTY) != 0;

	return test_and_clear_bit(DMA_SL_PTE_DIRTY_BIT,
				  (unsigned long *)&pte->val);
}

static inline bool dma_pte_superpage(struct dma_pte *pte)
{
	return (pte->val & DMA_PTE_LARGE_PAGE);
}

static inline bool first_pte_in_page(struct dma_pte *pte)
{
	return IS_ALIGNED((unsigned long)pte, VTD_PAGE_SIZE);
}

static inline int nr_pte_to_next_page(struct dma_pte *pte)
{
	return first_pte_in_page(pte) ? BIT_ULL(VTD_STRIDE_SHIFT) :
		(struct dma_pte *)ALIGN((unsigned long)pte, VTD_PAGE_SIZE) - pte;
}

static inline bool context_present(struct context_entry *context)
{
	return (context->lo & 1);
}

#define LEVEL_STRIDE		(9)
#define LEVEL_MASK		(((u64)1 << LEVEL_STRIDE) - 1)
#define MAX_AGAW_WIDTH		(64)
#define MAX_AGAW_PFN_WIDTH	(MAX_AGAW_WIDTH - VTD_PAGE_SHIFT)

static inline int agaw_to_level(int agaw)
{
	return agaw + 2;
}

static inline int agaw_to_width(int agaw)
{
	return min_t(int, 30 + agaw * LEVEL_STRIDE, MAX_AGAW_WIDTH);
}

static inline int width_to_agaw(int width)
{
	return DIV_ROUND_UP(width - 30, LEVEL_STRIDE);
}

static inline unsigned int level_to_offset_bits(int level)
{
	return (level - 1) * LEVEL_STRIDE;
}

static inline int pfn_level_offset(u64 pfn, int level)
{
	return (pfn >> level_to_offset_bits(level)) & LEVEL_MASK;
}

static inline u64 level_mask(int level)
{
	return -1ULL << level_to_offset_bits(level);
}

static inline u64 level_size(int level)
{
	return 1ULL << level_to_offset_bits(level);
}

static inline u64 align_to_level(u64 pfn, int level)
{
	return (pfn + level_size(level) - 1) & level_mask(level);
}

static inline unsigned long lvl_to_nr_pages(unsigned int lvl)
{
	return 1UL << min_t(int, (lvl - 1) * LEVEL_STRIDE, MAX_AGAW_PFN_WIDTH);
}

static inline void context_set_present(struct context_entry *context)
{
	context->lo |= 1;
}

static inline void context_set_fault_enable(struct context_entry *context)
{
	context->lo &= (((u64)-1) << 2) | 1;
}

static inline void context_set_translation_type(struct context_entry *context,
						unsigned long value)
{
	context->lo &= (((u64)-1) << 4) | 3;
	context->lo |= (value & 3) << 2;
}

static inline void context_set_address_root(struct context_entry *context,
					    unsigned long value)
{
	context->lo &= ~VTD_PAGE_MASK;
	context->lo |= value & VTD_PAGE_MASK;
}

static inline void context_set_address_width(struct context_entry *context,
					     unsigned long value)
{
	context->hi |= value & 7;
}

static inline void context_set_domain_id(struct context_entry *context,
					 unsigned long value)
{
	context->hi |= (value & ((1 << 16) - 1)) << 8;
}

static inline void context_set_pasid(struct context_entry *context)
{
	context->lo |= CONTEXT_PASIDE;
}

static inline int context_domain_id(struct context_entry *c)
{
	return((c->hi >> 8) & 0xffff);
}

static inline void context_clear_entry(struct context_entry *context)
{
	context->lo = 0;
	context->hi = 0;
}

#ifdef CONFIG_INTEL_IOMMU
static inline bool context_copied(struct intel_iommu *iommu, u8 bus, u8 devfn)
{
	if (!iommu->copied_tables)
		return false;

	return test_bit(((long)bus << 8) | devfn, iommu->copied_tables);
}

static inline void
set_context_copied(struct intel_iommu *iommu, u8 bus, u8 devfn)
{
	set_bit(((long)bus << 8) | devfn, iommu->copied_tables);
}

static inline void
clear_context_copied(struct intel_iommu *iommu, u8 bus, u8 devfn)
{
	clear_bit(((long)bus << 8) | devfn, iommu->copied_tables);
}
#endif /* CONFIG_INTEL_IOMMU */

/*
 * Set the RID_PASID field of a scalable mode context entry. The
 * IOMMU hardware will use the PASID value set in this field for
 * DMA translations of DMA requests without PASID.
 */
static inline void
context_set_sm_rid2pasid(struct context_entry *context, unsigned long pasid)
{
	context->hi |= pasid & ((1 << 20) - 1);
}

/*
 * Set the DTE(Device-TLB Enable) field of a scalable mode context
 * entry.
 */
static inline void context_set_sm_dte(struct context_entry *context)
{
	context->lo |= BIT_ULL(2);
}

/*
 * Set the PRE(Page Request Enable) field of a scalable mode context
 * entry.
 */
static inline void context_set_sm_pre(struct context_entry *context)
{
	context->lo |= BIT_ULL(4);
}

/*
 * Clear the PRE(Page Request Enable) field of a scalable mode context
 * entry.
 */
static inline void context_clear_sm_pre(struct context_entry *context)
{
	context->lo &= ~BIT_ULL(4);
}

/* Returns a number of VTD pages, but aligned to MM page size */
static inline unsigned long aligned_nrpages(unsigned long host_addr, size_t size)
{
	host_addr &= ~PAGE_MASK;
	return PAGE_ALIGN(host_addr + size) >> VTD_PAGE_SHIFT;
}

/* Return a size from number of VTD pages. */
static inline unsigned long nrpages_to_size(unsigned long npages)
{
	return npages << VTD_PAGE_SHIFT;
}

static inline void qi_desc_iotlb(struct intel_iommu *iommu, u16 did, u64 addr,
				 unsigned int size_order, u64 type,
				 struct qi_desc *desc)
{
	u8 dw = 0, dr = 0;
	int ih = 0;

	if (cap_write_drain(iommu->cap))
		dw = 1;

	if (cap_read_drain(iommu->cap))
		dr = 1;

	desc->qw0 = QI_IOTLB_DID(did) | QI_IOTLB_DR(dr) | QI_IOTLB_DW(dw)
		| QI_IOTLB_GRAN(type) | QI_IOTLB_TYPE;
	desc->qw1 = QI_IOTLB_ADDR(addr) | QI_IOTLB_IH(ih)
		| QI_IOTLB_AM(size_order);
	desc->qw2 = 0;
	desc->qw3 = 0;
}

static inline void qi_desc_dev_iotlb(u16 sid, u16 pfsid, u16 qdep, u64 addr,
				     unsigned int mask, struct qi_desc *desc)
{
	if (mask) {
		addr |= (1ULL << (VTD_PAGE_SHIFT + mask - 1)) - 1;
		desc->qw1 = QI_DEV_IOTLB_ADDR(addr) | QI_DEV_IOTLB_SIZE;
	} else {
		desc->qw1 = QI_DEV_IOTLB_ADDR(addr);
	}

	if (qdep >= QI_DEV_IOTLB_MAX_INVS)
		qdep = 0;

	desc->qw0 = QI_DEV_IOTLB_SID(sid) | QI_DEV_IOTLB_QDEP(qdep) |
		   QI_DIOTLB_TYPE | QI_DEV_IOTLB_PFSID(pfsid);
	desc->qw2 = 0;
	desc->qw3 = 0;
}

static inline void qi_desc_piotlb(u16 did, u32 pasid, u64 addr,
				  unsigned long npages, bool ih,
				  struct qi_desc *desc)
{
	if (npages == -1) {
		desc->qw0 = QI_EIOTLB_PASID(pasid) |
				QI_EIOTLB_DID(did) |
				QI_EIOTLB_GRAN(QI_GRAN_NONG_PASID) |
				QI_EIOTLB_TYPE;
		desc->qw1 = 0;
	} else {
		int mask = ilog2(__roundup_pow_of_two(npages));
		unsigned long align = (1ULL << (VTD_PAGE_SHIFT + mask));

		if (WARN_ON_ONCE(!IS_ALIGNED(addr, align)))
			addr = ALIGN_DOWN(addr, align);

		desc->qw0 = QI_EIOTLB_PASID(pasid) |
				QI_EIOTLB_DID(did) |
				QI_EIOTLB_GRAN(QI_GRAN_PSI_PASID) |
				QI_EIOTLB_TYPE;
		desc->qw1 = QI_EIOTLB_ADDR(addr) |
				QI_EIOTLB_IH(ih) |
				QI_EIOTLB_AM(mask);
	}
}

static inline void qi_desc_dev_iotlb_pasid(u16 sid, u16 pfsid, u32 pasid,
					   u16 qdep, u64 addr,
					   unsigned int size_order,
					   struct qi_desc *desc)
{
	unsigned long mask = 1UL << (VTD_PAGE_SHIFT + size_order - 1);

	desc->qw0 = QI_DEV_EIOTLB_PASID(pasid) | QI_DEV_EIOTLB_SID(sid) |
		QI_DEV_EIOTLB_QDEP(qdep) | QI_DEIOTLB_TYPE |
		QI_DEV_IOTLB_PFSID(pfsid);

	/*
	 * If S bit is 0, we only flush a single page. If S bit is set,
	 * The least significant zero bit indicates the invalidation address
	 * range. VT-d spec 6.5.2.6.
	 * e.g. address bit 12[0] indicates 8KB, 13[0] indicates 16KB.
	 * size order = 0 is PAGE_SIZE 4KB
	 * Max Invs Pending (MIP) is set to 0 for now until we have DIT in
	 * ECAP.
	 */
	if (!IS_ALIGNED(addr, VTD_PAGE_SIZE << size_order))
		pr_warn_ratelimited("Invalidate non-aligned address %llx, order %d\n",
				    addr, size_order);

	/* Take page address */
	desc->qw1 = QI_DEV_EIOTLB_ADDR(addr);

	if (size_order) {
		/*
		 * Existing 0s in address below size_order may be the least
		 * significant bit, we must set them to 1s to avoid having
		 * smaller size than desired.
		 */
		desc->qw1 |= GENMASK_ULL(size_order + VTD_PAGE_SHIFT - 1,
					VTD_PAGE_SHIFT);
		/* Clear size_order bit to indicate size */
		desc->qw1 &= ~mask;
		/* Set the S bit to indicate flushing more than 1 page */
		desc->qw1 |= QI_DEV_EIOTLB_SIZE;
	}
}

/* Convert value to context PASID directory size field coding. */
#define context_pdts(pds)	(((pds) & 0x7) << 9)

struct dmar_drhd_unit *dmar_find_matched_drhd_unit(struct pci_dev *dev);

int dmar_enable_qi(struct intel_iommu *iommu);
void dmar_disable_qi(struct intel_iommu *iommu);
int dmar_reenable_qi(struct intel_iommu *iommu);
void qi_global_iec(struct intel_iommu *iommu);

void qi_flush_context(struct intel_iommu *iommu, u16 did,
		      u16 sid, u8 fm, u64 type);
void qi_flush_iotlb(struct intel_iommu *iommu, u16 did, u64 addr,
		    unsigned int size_order, u64 type);
void qi_flush_dev_iotlb(struct intel_iommu *iommu, u16 sid, u16 pfsid,
			u16 qdep, u64 addr, unsigned mask);

void qi_flush_piotlb(struct intel_iommu *iommu, u16 did, u32 pasid, u64 addr,
		     unsigned long npages, bool ih);

void qi_flush_dev_iotlb_pasid(struct intel_iommu *iommu, u16 sid, u16 pfsid,
			      u32 pasid, u16 qdep, u64 addr,
			      unsigned int size_order);
void quirk_extra_dev_tlb_flush(struct device_domain_info *info,
			       unsigned long address, unsigned long pages,
			       u32 pasid, u16 qdep);
void qi_flush_pasid_cache(struct intel_iommu *iommu, u16 did, u64 granu,
			  u32 pasid);

int qi_submit_sync(struct intel_iommu *iommu, struct qi_desc *desc,
		   unsigned int count, unsigned long options);

void __iommu_flush_iotlb(struct intel_iommu *iommu, u16 did, u64 addr,
			 unsigned int size_order, u64 type);
/*
 * Options used in qi_submit_sync:
 * QI_OPT_WAIT_DRAIN - Wait for PRQ drain completion, spec 6.5.2.8.
 */
#define QI_OPT_WAIT_DRAIN		BIT(0)

int domain_attach_iommu(struct dmar_domain *domain, struct intel_iommu *iommu);
void domain_detach_iommu(struct dmar_domain *domain, struct intel_iommu *iommu);
void device_block_translation(struct device *dev);
int paging_domain_compatible(struct iommu_domain *domain, struct device *dev);

struct dev_pasid_info *
domain_add_dev_pasid(struct iommu_domain *domain,
		     struct device *dev, ioasid_t pasid);
void domain_remove_dev_pasid(struct iommu_domain *domain,
			     struct device *dev, ioasid_t pasid);

int __domain_setup_first_level(struct intel_iommu *iommu, struct device *dev,
			       ioasid_t pasid, u16 did, phys_addr_t fsptptr,
			       int flags, struct iommu_domain *old);

int dmar_ir_support(void);

void iommu_flush_write_buffer(struct intel_iommu *iommu);
struct iommu_domain *
intel_iommu_domain_alloc_nested(struct device *dev, struct iommu_domain *parent,
				u32 flags,
				const struct iommu_user_data *user_data);
struct device *device_rbtree_find(struct intel_iommu *iommu, u16 rid);

enum cache_tag_type {
	CACHE_TAG_IOTLB,
	CACHE_TAG_DEVTLB,
	CACHE_TAG_NESTING_IOTLB,
	CACHE_TAG_NESTING_DEVTLB,
};

struct cache_tag {
	struct list_head node;
	enum cache_tag_type type;
	struct intel_iommu *iommu;
	/*
	 * The @dev field represents the location of the cache. For IOTLB, it
	 * resides on the IOMMU hardware. @dev stores the device pointer to
	 * the IOMMU hardware. For DevTLB, it locates in the PCIe endpoint.
	 * @dev stores the device pointer to that endpoint.
	 */
	struct device *dev;
	u16 domain_id;
	ioasid_t pasid;
	unsigned int users;
};

int cache_tag_assign(struct dmar_domain *domain, u16 did, struct device *dev,
		     ioasid_t pasid, enum cache_tag_type type);
int cache_tag_assign_domain(struct dmar_domain *domain,
			    struct device *dev, ioasid_t pasid);
void cache_tag_unassign_domain(struct dmar_domain *domain,
			       struct device *dev, ioasid_t pasid);
void cache_tag_flush_range(struct dmar_domain *domain, unsigned long start,
			   unsigned long end, int ih);
void cache_tag_flush_all(struct dmar_domain *domain);
void cache_tag_flush_range_np(struct dmar_domain *domain, unsigned long start,
			      unsigned long end);

void intel_context_flush_no_pasid(struct device_domain_info *info,
				  struct context_entry *context, u16 did);

int intel_iommu_enable_prq(struct intel_iommu *iommu);
int intel_iommu_finish_prq(struct intel_iommu *iommu);
void intel_iommu_page_response(struct device *dev, struct iopf_fault *evt,
			       struct iommu_page_response *msg);
void intel_iommu_drain_pasid_prq(struct device *dev, u32 pasid);

int intel_iommu_enable_iopf(struct device *dev);
void intel_iommu_disable_iopf(struct device *dev);

static inline int iopf_for_domain_set(struct iommu_domain *domain,
				      struct device *dev)
{
	if (!domain || !domain->iopf_handler)
		return 0;

	return intel_iommu_enable_iopf(dev);
}

static inline void iopf_for_domain_remove(struct iommu_domain *domain,
					  struct device *dev)
{
	if (!domain || !domain->iopf_handler)
		return;

	intel_iommu_disable_iopf(dev);
}

static inline int iopf_for_domain_replace(struct iommu_domain *new,
					  struct iommu_domain *old,
					  struct device *dev)
{
	int ret;

	ret = iopf_for_domain_set(new, dev);
	if (ret)
		return ret;

	iopf_for_domain_remove(old, dev);

	return 0;
}

#ifdef CONFIG_INTEL_IOMMU_SVM
void intel_svm_check(struct intel_iommu *iommu);
struct iommu_domain *intel_svm_domain_alloc(struct device *dev,
					    struct mm_struct *mm);
#else
static inline void intel_svm_check(struct intel_iommu *iommu) {}
static inline struct iommu_domain *intel_svm_domain_alloc(struct device *dev,
							  struct mm_struct *mm)
{
	return ERR_PTR(-ENODEV);
}
#endif

#ifdef CONFIG_INTEL_IOMMU_DEBUGFS
void intel_iommu_debugfs_init(void);
void intel_iommu_debugfs_create_dev(struct device_domain_info *info);
void intel_iommu_debugfs_remove_dev(struct device_domain_info *info);
void intel_iommu_debugfs_create_dev_pasid(struct dev_pasid_info *dev_pasid);
void intel_iommu_debugfs_remove_dev_pasid(struct dev_pasid_info *dev_pasid);
#else
static inline void intel_iommu_debugfs_init(void) {}
static inline void intel_iommu_debugfs_create_dev(struct device_domain_info *info) {}
static inline void intel_iommu_debugfs_remove_dev(struct device_domain_info *info) {}
static inline void intel_iommu_debugfs_create_dev_pasid(struct dev_pasid_info *dev_pasid) {}
static inline void intel_iommu_debugfs_remove_dev_pasid(struct dev_pasid_info *dev_pasid) {}
#endif /* CONFIG_INTEL_IOMMU_DEBUGFS */

extern const struct attribute_group *intel_iommu_groups[];
struct context_entry *iommu_context_addr(struct intel_iommu *iommu, u8 bus,
					 u8 devfn, int alloc);

extern const struct iommu_ops intel_iommu_ops;
extern const struct iommu_domain_ops intel_fs_paging_domain_ops;
extern const struct iommu_domain_ops intel_ss_paging_domain_ops;

static inline bool intel_domain_is_fs_paging(struct dmar_domain *domain)
{
	return domain->domain.ops == &intel_fs_paging_domain_ops;
}

static inline bool intel_domain_is_ss_paging(struct dmar_domain *domain)
{
	return domain->domain.ops == &intel_ss_paging_domain_ops;
}

#ifdef CONFIG_INTEL_IOMMU
extern int intel_iommu_sm;
int iommu_calculate_agaw(struct intel_iommu *iommu);
int iommu_calculate_max_sagaw(struct intel_iommu *iommu);
int ecmd_submit_sync(struct intel_iommu *iommu, u8 ecmd, u64 oa, u64 ob);

static inline bool ecmd_has_pmu_essential(struct intel_iommu *iommu)
{
	return (iommu->ecmdcap[DMA_ECMD_ECCAP3] & DMA_ECMD_ECCAP3_ESSENTIAL) ==
		DMA_ECMD_ECCAP3_ESSENTIAL;
}

extern int dmar_disabled;
extern int intel_iommu_enabled;
#else
static inline int iommu_calculate_agaw(struct intel_iommu *iommu)
{
	return 0;
}
static inline int iommu_calculate_max_sagaw(struct intel_iommu *iommu)
{
	return 0;
}
#define dmar_disabled	(1)
#define intel_iommu_enabled (0)
#define intel_iommu_sm (0)
#endif

static inline const char *decode_prq_descriptor(char *str, size_t size,
		u64 dw0, u64 dw1, u64 dw2, u64 dw3)
{
	char *buf = str;
	int bytes;

	bytes = snprintf(buf, size,
			 "rid=0x%llx addr=0x%llx %c%c%c%c%c pasid=0x%llx index=0x%llx",
			 FIELD_GET(GENMASK_ULL(31, 16), dw0),
			 FIELD_GET(GENMASK_ULL(63, 12), dw1),
			 dw1 & BIT_ULL(0) ? 'r' : '-',
			 dw1 & BIT_ULL(1) ? 'w' : '-',
			 dw0 & BIT_ULL(52) ? 'x' : '-',
			 dw0 & BIT_ULL(53) ? 'p' : '-',
			 dw1 & BIT_ULL(2) ? 'l' : '-',
			 FIELD_GET(GENMASK_ULL(51, 32), dw0),
			 FIELD_GET(GENMASK_ULL(11, 3), dw1));

	/* Private Data */
	if (dw0 & BIT_ULL(9)) {
		size -= bytes;
		buf += bytes;
		snprintf(buf, size, " private=0x%llx/0x%llx\n", dw2, dw3);
	}

	return str;
}

#endif
