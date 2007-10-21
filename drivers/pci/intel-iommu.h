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
 * Copyright (C) Ashok Raj <ashok.raj@intel.com>
 * Copyright (C) Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 */

#ifndef _INTEL_IOMMU_H_
#define _INTEL_IOMMU_H_

#include <linux/types.h>
#include <linux/msi.h>
#include "iova.h"
#include <linux/io.h>

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
static inline u64 dmar_readq(void *addr)
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

/*
 * 0: Present
 * 1-11: Reserved
 * 12-63: Context Ptr (12 - (haw-1))
 * 64-127: Reserved
 */
struct root_entry {
	u64	val;
	u64	rsvd1;
};
#define ROOT_ENTRY_NR (PAGE_SIZE_4K/sizeof(struct root_entry))
static inline bool root_present(struct root_entry *root)
{
	return (root->val & 1);
}
static inline void set_root_present(struct root_entry *root)
{
	root->val |= 1;
}
static inline void set_root_value(struct root_entry *root, unsigned long value)
{
	root->val |= value & PAGE_MASK_4K;
}

struct context_entry;
static inline struct context_entry *
get_context_addr_from_root(struct root_entry *root)
{
	return (struct context_entry *)
		(root_present(root)?phys_to_virt(
		root->val & PAGE_MASK_4K):
		NULL);
}

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
#define context_present(c) ((c).lo & 1)
#define context_fault_disable(c) (((c).lo >> 1) & 1)
#define context_translation_type(c) (((c).lo >> 2) & 3)
#define context_address_root(c) ((c).lo & PAGE_MASK_4K)
#define context_address_width(c) ((c).hi &  7)
#define context_domain_id(c) (((c).hi >> 8) & ((1 << 16) - 1))

#define context_set_present(c) do {(c).lo |= 1;} while (0)
#define context_set_fault_enable(c) \
	do {(c).lo &= (((u64)-1) << 2) | 1;} while (0)
#define context_set_translation_type(c, val) \
	do { \
		(c).lo &= (((u64)-1) << 4) | 3; \
		(c).lo |= ((val) & 3) << 2; \
	} while (0)
#define CONTEXT_TT_MULTI_LEVEL 0
#define context_set_address_root(c, val) \
	do {(c).lo |= (val) & PAGE_MASK_4K;} while (0)
#define context_set_address_width(c, val) do {(c).hi |= (val) & 7;} while (0)
#define context_set_domain_id(c, val) \
	do {(c).hi |= ((val) & ((1 << 16) - 1)) << 8;} while (0)
#define context_clear_entry(c) do {(c).lo = 0; (c).hi = 0;} while (0)

/*
 * 0: readable
 * 1: writable
 * 2-6: reserved
 * 7: super page
 * 8-11: available
 * 12-63: Host physcial address
 */
struct dma_pte {
	u64 val;
};
#define dma_clear_pte(p)	do {(p).val = 0;} while (0)

#define DMA_PTE_READ (1)
#define DMA_PTE_WRITE (2)

#define dma_set_pte_readable(p) do {(p).val |= DMA_PTE_READ;} while (0)
#define dma_set_pte_writable(p) do {(p).val |= DMA_PTE_WRITE;} while (0)
#define dma_set_pte_prot(p, prot) \
		do {(p).val = ((p).val & ~3) | ((prot) & 3); } while (0)
#define dma_pte_addr(p) ((p).val & PAGE_MASK_4K)
#define dma_set_pte_addr(p, addr) do {\
		(p).val |= ((addr) & PAGE_MASK_4K); } while (0)
#define dma_pte_present(p) (((p).val & 3) != 0)

struct intel_iommu;

struct dmar_domain {
	int	id;			/* domain id */
	struct intel_iommu *iommu;	/* back pointer to owning iommu */

	struct list_head devices; 	/* all devices' list */
	struct iova_domain iovad;	/* iova's that belong to this domain */

	struct dma_pte	*pgd;		/* virtual address */
	spinlock_t	mapping_lock;	/* page table lock */
	int		gaw;		/* max guest address width */

	/* adjusted guest address width, 0 is level 2 30-bit */
	int		agaw;

#define DOMAIN_FLAG_MULTIPLE_DEVICES 1
	int		flags;
};

/* PCI domain-device relationship */
struct device_domain_info {
	struct list_head link;	/* link to domain siblings */
	struct list_head global; /* link to global list */
	u8 bus;			/* PCI bus numer */
	u8 devfn;		/* PCI devfn number */
	struct pci_dev *dev; /* it's NULL for PCIE-to-PCI bridge */
	struct dmar_domain *domain; /* pointer to domain */
};

extern int init_dmars(void);

struct intel_iommu {
	void __iomem	*reg; /* Pointer to hardware regs, virtual addr */
	u64		cap;
	u64		ecap;
	unsigned long 	*domain_ids; /* bitmap of domains */
	struct dmar_domain **domains; /* ptr to domains */
	int		seg;
	u32		gcmd; /* Holds TE, EAFL. Don't need SRTP, SFL, WBF */
	spinlock_t	lock; /* protect context, domain ids */
	spinlock_t	register_lock; /* protect register handling */
	struct root_entry *root_entry; /* virtual address */

	unsigned int irq;
	unsigned char name[7];    /* Device Name */
	struct msi_msg saved_msg;
	struct sys_device sysdev;
};

#endif
