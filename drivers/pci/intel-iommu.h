/*
 * Copyright (c) 2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Copyright (C) 2006-2008 Intel Corporation
 * Author: Ashok Raj <ashok.raj@intel.com>
 * Author: Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 */

#ifndef _INTEL_IOMMU_H_
#define _INTEL_IOMMU_H_

#include <linux/types.h>
#include <linux/msi.h>
#include <linux/sysdev.h>
#include "iova.h"
#include <linux/io.h>
#include "dma_remapping.h"

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
#define	DMAR_AFLOG_REG	0x58	/* Advanced Fault control */
#define	DMAR_PMEN_REG	0x64	/* Enable Protected Memory Region */
#define	DMAR_PLMBASE_REG 0x68	/* PMRR Low addr */
#define	DMAR_PLMLIMIT_REG 0x6c	/* PMRR low limit */
#define	DMAR_PHMBASE_REG 0x70	/* pmrr high base addr */
#define	DMAR_PHMLIMIT_REG 0x78	/* pmrr high limit */

#define OFFSET_STRIDE		(9)
/*
#define dmar_readl(dmar, reg) readl(dmar + reg)
#define dmar_readq(dmar, reg) ({ \
		u32 lo, hi; \
		lo = readl(dmar + reg); \
		hi = readl(dmar + reg + 4); \
		(((u64) hi) << 32) + lo; })
*/
static inline u64 dmar_readq(void __iomem *addr)
{
	u32 lo, hi;
	lo = readl(addr);
	hi = readl(addr + 4);
	return (((u64) hi) << 32) + lo;
}

static inline void dmar_writeq(void __iomem *addr, u64 val)
{
	writel((u32)val, addr);
	writel((u32)(val >> 32), addr + 4);
}

#define DMAR_VER_MAJOR(v)		(((v) & 0xf0) >> 4)
#define DMAR_VER_MINOR(v)		((v) & 0x0f)

/*
 * Decoding Capability Register
 */
#define cap_read_drain(c)	(((c) >> 55) & 1)
#define cap_write_drain(c)	(((c) >> 54) & 1)
#define cap_max_amask_val(c)	(((c) >> 48) & 0x3f)
#define cap_num_fault_regs(c)	((((c) >> 40) & 0xff) + 1)
#define cap_pgsel_inv(c)	(((c) >> 39) & 1)

#define cap_super_page_val(c)	(((c) >> 34) & 0xf)
#define cap_super_offset(c)	(((find_first_bit(&cap_super_page_val(c), 4)) \
					* OFFSET_STRIDE) + 21)

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

#define ecap_niotlb_iunits(e)	((((e) >> 24) & 0xff) + 1)
#define ecap_iotlb_offset(e) 	((((e) >> 8) & 0x3ff) * 16)
#define ecap_max_iotlb_offset(e) \
	(ecap_iotlb_offset(e) + ecap_niotlb_iunits(e) * 16)
#define ecap_coherent(e)	((e) & 0x1)


/* IOTLB_REG */
#define DMA_TLB_GLOBAL_FLUSH (((u64)1) << 60)
#define DMA_TLB_DSI_FLUSH (((u64)2) << 60)
#define DMA_TLB_PSI_FLUSH (((u64)3) << 60)
#define DMA_TLB_IIRG(type) ((type >> 60) & 7)
#define DMA_TLB_IAIG(val) (((val) >> 57) & 7)
#define DMA_TLB_READ_DRAIN (((u64)1) << 49)
#define DMA_TLB_WRITE_DRAIN (((u64)1) << 48)
#define DMA_TLB_DID(id)	(((u64)((id) & 0xffff)) << 32)
#define DMA_TLB_IVT (((u64)1) << 63)
#define DMA_TLB_IH_NONLEAF (((u64)1) << 6)
#define DMA_TLB_MAX_SIZE (0x3f)

/* PMEN_REG */
#define DMA_PMEN_EPM (((u32)1)<<31)
#define DMA_PMEN_PRS (((u32)1)<<0)

/* GCMD_REG */
#define DMA_GCMD_TE (((u32)1) << 31)
#define DMA_GCMD_SRTP (((u32)1) << 30)
#define DMA_GCMD_SFL (((u32)1) << 29)
#define DMA_GCMD_EAFL (((u32)1) << 28)
#define DMA_GCMD_WBF (((u32)1) << 27)

/* GSTS_REG */
#define DMA_GSTS_TES (((u32)1) << 31)
#define DMA_GSTS_RTPS (((u32)1) << 30)
#define DMA_GSTS_FLS (((u32)1) << 29)
#define DMA_GSTS_AFLS (((u32)1) << 28)
#define DMA_GSTS_WBFS (((u32)1) << 27)

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

/* FECTL_REG */
#define DMA_FECTL_IM (((u32)1) << 31)

/* FSTS_REG */
#define DMA_FSTS_PPF ((u32)2)
#define DMA_FSTS_PFO ((u32)1)
#define dma_fsts_fault_record_index(s) (((s) >> 8) & 0xff)

/* FRCD_REG, 32 bits access */
#define DMA_FRCD_F (((u32)1) << 31)
#define dma_frcd_type(d) ((d >> 30) & 1)
#define dma_frcd_fault_reason(c) (c & 0xff)
#define dma_frcd_source_id(c) (c & 0xffff)
#define dma_frcd_page_addr(d) (d & (((u64)-1) << 12)) /* low 64 bit */

struct intel_iommu {
	void __iomem	*reg; /* Pointer to hardware regs, virtual addr */
	u64		cap;
	u64		ecap;
	int		seg;
	u32		gcmd; /* Holds TE, EAFL. Don't need SRTP, SFL, WBF */
	spinlock_t	register_lock; /* protect register handling */
	int		seq_id;	/* sequence id of the iommu */

#ifdef CONFIG_DMAR
	unsigned long 	*domain_ids; /* bitmap of domains */
	struct dmar_domain **domains; /* ptr to domains */
	spinlock_t	lock; /* protect context, domain ids */
	struct root_entry *root_entry; /* virtual address */

	unsigned int irq;
	unsigned char name[7];    /* Device Name */
	struct msi_msg saved_msg;
	struct sys_device sysdev;
#endif
};

extern struct dmar_drhd_unit * dmar_find_matched_drhd_unit(struct pci_dev *dev);

extern int alloc_iommu(struct dmar_drhd_unit *drhd);
extern void free_iommu(struct intel_iommu *iommu);

#endif
