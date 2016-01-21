/*
 * IOMMU API for ARM architected SMMUv3 implementations.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Copyright (C) 2015 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 *
 * This driver is powered by bad coffee and bombay mix.
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/iommu.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include "io-pgtable.h"

/* MMIO registers */
#define ARM_SMMU_IDR0			0x0
#define IDR0_ST_LVL_SHIFT		27
#define IDR0_ST_LVL_MASK		0x3
#define IDR0_ST_LVL_2LVL		(1 << IDR0_ST_LVL_SHIFT)
#define IDR0_STALL_MODEL		(3 << 24)
#define IDR0_TTENDIAN_SHIFT		21
#define IDR0_TTENDIAN_MASK		0x3
#define IDR0_TTENDIAN_LE		(2 << IDR0_TTENDIAN_SHIFT)
#define IDR0_TTENDIAN_BE		(3 << IDR0_TTENDIAN_SHIFT)
#define IDR0_TTENDIAN_MIXED		(0 << IDR0_TTENDIAN_SHIFT)
#define IDR0_CD2L			(1 << 19)
#define IDR0_VMID16			(1 << 18)
#define IDR0_PRI			(1 << 16)
#define IDR0_SEV			(1 << 14)
#define IDR0_MSI			(1 << 13)
#define IDR0_ASID16			(1 << 12)
#define IDR0_ATS			(1 << 10)
#define IDR0_HYP			(1 << 9)
#define IDR0_COHACC			(1 << 4)
#define IDR0_TTF_SHIFT			2
#define IDR0_TTF_MASK			0x3
#define IDR0_TTF_AARCH64		(2 << IDR0_TTF_SHIFT)
#define IDR0_TTF_AARCH32_64		(3 << IDR0_TTF_SHIFT)
#define IDR0_S1P			(1 << 1)
#define IDR0_S2P			(1 << 0)

#define ARM_SMMU_IDR1			0x4
#define IDR1_TABLES_PRESET		(1 << 30)
#define IDR1_QUEUES_PRESET		(1 << 29)
#define IDR1_REL			(1 << 28)
#define IDR1_CMDQ_SHIFT			21
#define IDR1_CMDQ_MASK			0x1f
#define IDR1_EVTQ_SHIFT			16
#define IDR1_EVTQ_MASK			0x1f
#define IDR1_PRIQ_SHIFT			11
#define IDR1_PRIQ_MASK			0x1f
#define IDR1_SSID_SHIFT			6
#define IDR1_SSID_MASK			0x1f
#define IDR1_SID_SHIFT			0
#define IDR1_SID_MASK			0x3f

#define ARM_SMMU_IDR5			0x14
#define IDR5_STALL_MAX_SHIFT		16
#define IDR5_STALL_MAX_MASK		0xffff
#define IDR5_GRAN64K			(1 << 6)
#define IDR5_GRAN16K			(1 << 5)
#define IDR5_GRAN4K			(1 << 4)
#define IDR5_OAS_SHIFT			0
#define IDR5_OAS_MASK			0x7
#define IDR5_OAS_32_BIT			(0 << IDR5_OAS_SHIFT)
#define IDR5_OAS_36_BIT			(1 << IDR5_OAS_SHIFT)
#define IDR5_OAS_40_BIT			(2 << IDR5_OAS_SHIFT)
#define IDR5_OAS_42_BIT			(3 << IDR5_OAS_SHIFT)
#define IDR5_OAS_44_BIT			(4 << IDR5_OAS_SHIFT)
#define IDR5_OAS_48_BIT			(5 << IDR5_OAS_SHIFT)

#define ARM_SMMU_CR0			0x20
#define CR0_CMDQEN			(1 << 3)
#define CR0_EVTQEN			(1 << 2)
#define CR0_PRIQEN			(1 << 1)
#define CR0_SMMUEN			(1 << 0)

#define ARM_SMMU_CR0ACK			0x24

#define ARM_SMMU_CR1			0x28
#define CR1_SH_NSH			0
#define CR1_SH_OSH			2
#define CR1_SH_ISH			3
#define CR1_CACHE_NC			0
#define CR1_CACHE_WB			1
#define CR1_CACHE_WT			2
#define CR1_TABLE_SH_SHIFT		10
#define CR1_TABLE_OC_SHIFT		8
#define CR1_TABLE_IC_SHIFT		6
#define CR1_QUEUE_SH_SHIFT		4
#define CR1_QUEUE_OC_SHIFT		2
#define CR1_QUEUE_IC_SHIFT		0

#define ARM_SMMU_CR2			0x2c
#define CR2_PTM				(1 << 2)
#define CR2_RECINVSID			(1 << 1)
#define CR2_E2H				(1 << 0)

#define ARM_SMMU_IRQ_CTRL		0x50
#define IRQ_CTRL_EVTQ_IRQEN		(1 << 2)
#define IRQ_CTRL_PRIQ_IRQEN		(1 << 1)
#define IRQ_CTRL_GERROR_IRQEN		(1 << 0)

#define ARM_SMMU_IRQ_CTRLACK		0x54

#define ARM_SMMU_GERROR			0x60
#define GERROR_SFM_ERR			(1 << 8)
#define GERROR_MSI_GERROR_ABT_ERR	(1 << 7)
#define GERROR_MSI_PRIQ_ABT_ERR		(1 << 6)
#define GERROR_MSI_EVTQ_ABT_ERR		(1 << 5)
#define GERROR_MSI_CMDQ_ABT_ERR		(1 << 4)
#define GERROR_PRIQ_ABT_ERR		(1 << 3)
#define GERROR_EVTQ_ABT_ERR		(1 << 2)
#define GERROR_CMDQ_ERR			(1 << 0)
#define GERROR_ERR_MASK			0xfd

#define ARM_SMMU_GERRORN		0x64

#define ARM_SMMU_GERROR_IRQ_CFG0	0x68
#define ARM_SMMU_GERROR_IRQ_CFG1	0x70
#define ARM_SMMU_GERROR_IRQ_CFG2	0x74

#define ARM_SMMU_STRTAB_BASE		0x80
#define STRTAB_BASE_RA			(1UL << 62)
#define STRTAB_BASE_ADDR_SHIFT		6
#define STRTAB_BASE_ADDR_MASK		0x3ffffffffffUL

#define ARM_SMMU_STRTAB_BASE_CFG	0x88
#define STRTAB_BASE_CFG_LOG2SIZE_SHIFT	0
#define STRTAB_BASE_CFG_LOG2SIZE_MASK	0x3f
#define STRTAB_BASE_CFG_SPLIT_SHIFT	6
#define STRTAB_BASE_CFG_SPLIT_MASK	0x1f
#define STRTAB_BASE_CFG_FMT_SHIFT	16
#define STRTAB_BASE_CFG_FMT_MASK	0x3
#define STRTAB_BASE_CFG_FMT_LINEAR	(0 << STRTAB_BASE_CFG_FMT_SHIFT)
#define STRTAB_BASE_CFG_FMT_2LVL	(1 << STRTAB_BASE_CFG_FMT_SHIFT)

#define ARM_SMMU_CMDQ_BASE		0x90
#define ARM_SMMU_CMDQ_PROD		0x98
#define ARM_SMMU_CMDQ_CONS		0x9c

#define ARM_SMMU_EVTQ_BASE		0xa0
#define ARM_SMMU_EVTQ_PROD		0x100a8
#define ARM_SMMU_EVTQ_CONS		0x100ac
#define ARM_SMMU_EVTQ_IRQ_CFG0		0xb0
#define ARM_SMMU_EVTQ_IRQ_CFG1		0xb8
#define ARM_SMMU_EVTQ_IRQ_CFG2		0xbc

#define ARM_SMMU_PRIQ_BASE		0xc0
#define ARM_SMMU_PRIQ_PROD		0x100c8
#define ARM_SMMU_PRIQ_CONS		0x100cc
#define ARM_SMMU_PRIQ_IRQ_CFG0		0xd0
#define ARM_SMMU_PRIQ_IRQ_CFG1		0xd8
#define ARM_SMMU_PRIQ_IRQ_CFG2		0xdc

/* Common MSI config fields */
#define MSI_CFG0_ADDR_SHIFT		2
#define MSI_CFG0_ADDR_MASK		0x3fffffffffffUL
#define MSI_CFG2_SH_SHIFT		4
#define MSI_CFG2_SH_NSH			(0UL << MSI_CFG2_SH_SHIFT)
#define MSI_CFG2_SH_OSH			(2UL << MSI_CFG2_SH_SHIFT)
#define MSI_CFG2_SH_ISH			(3UL << MSI_CFG2_SH_SHIFT)
#define MSI_CFG2_MEMATTR_SHIFT		0
#define MSI_CFG2_MEMATTR_DEVICE_nGnRE	(0x1 << MSI_CFG2_MEMATTR_SHIFT)

#define Q_IDX(q, p)			((p) & ((1 << (q)->max_n_shift) - 1))
#define Q_WRP(q, p)			((p) & (1 << (q)->max_n_shift))
#define Q_OVERFLOW_FLAG			(1 << 31)
#define Q_OVF(q, p)			((p) & Q_OVERFLOW_FLAG)
#define Q_ENT(q, p)			((q)->base +			\
					 Q_IDX(q, p) * (q)->ent_dwords)

#define Q_BASE_RWA			(1UL << 62)
#define Q_BASE_ADDR_SHIFT		5
#define Q_BASE_ADDR_MASK		0xfffffffffffUL
#define Q_BASE_LOG2SIZE_SHIFT		0
#define Q_BASE_LOG2SIZE_MASK		0x1fUL

/*
 * Stream table.
 *
 * Linear: Enough to cover 1 << IDR1.SIDSIZE entries
 * 2lvl: 128k L1 entries,
 *       256 lazy entries per table (each table covers a PCI bus)
 */
#define STRTAB_L1_SZ_SHIFT		20
#define STRTAB_SPLIT			8

#define STRTAB_L1_DESC_DWORDS		1
#define STRTAB_L1_DESC_SPAN_SHIFT	0
#define STRTAB_L1_DESC_SPAN_MASK	0x1fUL
#define STRTAB_L1_DESC_L2PTR_SHIFT	6
#define STRTAB_L1_DESC_L2PTR_MASK	0x3ffffffffffUL

#define STRTAB_STE_DWORDS		8
#define STRTAB_STE_0_V			(1UL << 0)
#define STRTAB_STE_0_CFG_SHIFT		1
#define STRTAB_STE_0_CFG_MASK		0x7UL
#define STRTAB_STE_0_CFG_ABORT		(0UL << STRTAB_STE_0_CFG_SHIFT)
#define STRTAB_STE_0_CFG_BYPASS		(4UL << STRTAB_STE_0_CFG_SHIFT)
#define STRTAB_STE_0_CFG_S1_TRANS	(5UL << STRTAB_STE_0_CFG_SHIFT)
#define STRTAB_STE_0_CFG_S2_TRANS	(6UL << STRTAB_STE_0_CFG_SHIFT)

#define STRTAB_STE_0_S1FMT_SHIFT	4
#define STRTAB_STE_0_S1FMT_LINEAR	(0UL << STRTAB_STE_0_S1FMT_SHIFT)
#define STRTAB_STE_0_S1CTXPTR_SHIFT	6
#define STRTAB_STE_0_S1CTXPTR_MASK	0x3ffffffffffUL
#define STRTAB_STE_0_S1CDMAX_SHIFT	59
#define STRTAB_STE_0_S1CDMAX_MASK	0x1fUL

#define STRTAB_STE_1_S1C_CACHE_NC	0UL
#define STRTAB_STE_1_S1C_CACHE_WBRA	1UL
#define STRTAB_STE_1_S1C_CACHE_WT	2UL
#define STRTAB_STE_1_S1C_CACHE_WB	3UL
#define STRTAB_STE_1_S1C_SH_NSH		0UL
#define STRTAB_STE_1_S1C_SH_OSH		2UL
#define STRTAB_STE_1_S1C_SH_ISH		3UL
#define STRTAB_STE_1_S1CIR_SHIFT	2
#define STRTAB_STE_1_S1COR_SHIFT	4
#define STRTAB_STE_1_S1CSH_SHIFT	6

#define STRTAB_STE_1_S1STALLD		(1UL << 27)

#define STRTAB_STE_1_EATS_ABT		0UL
#define STRTAB_STE_1_EATS_TRANS		1UL
#define STRTAB_STE_1_EATS_S1CHK		2UL
#define STRTAB_STE_1_EATS_SHIFT		28

#define STRTAB_STE_1_STRW_NSEL1		0UL
#define STRTAB_STE_1_STRW_EL2		2UL
#define STRTAB_STE_1_STRW_SHIFT		30

#define STRTAB_STE_2_S2VMID_SHIFT	0
#define STRTAB_STE_2_S2VMID_MASK	0xffffUL
#define STRTAB_STE_2_VTCR_SHIFT		32
#define STRTAB_STE_2_VTCR_MASK		0x7ffffUL
#define STRTAB_STE_2_S2AA64		(1UL << 51)
#define STRTAB_STE_2_S2ENDI		(1UL << 52)
#define STRTAB_STE_2_S2PTW		(1UL << 54)
#define STRTAB_STE_2_S2R		(1UL << 58)

#define STRTAB_STE_3_S2TTB_SHIFT	4
#define STRTAB_STE_3_S2TTB_MASK		0xfffffffffffUL

/* Context descriptor (stage-1 only) */
#define CTXDESC_CD_DWORDS		8
#define CTXDESC_CD_0_TCR_T0SZ_SHIFT	0
#define ARM64_TCR_T0SZ_SHIFT		0
#define ARM64_TCR_T0SZ_MASK		0x1fUL
#define CTXDESC_CD_0_TCR_TG0_SHIFT	6
#define ARM64_TCR_TG0_SHIFT		14
#define ARM64_TCR_TG0_MASK		0x3UL
#define CTXDESC_CD_0_TCR_IRGN0_SHIFT	8
#define ARM64_TCR_IRGN0_SHIFT		8
#define ARM64_TCR_IRGN0_MASK		0x3UL
#define CTXDESC_CD_0_TCR_ORGN0_SHIFT	10
#define ARM64_TCR_ORGN0_SHIFT		10
#define ARM64_TCR_ORGN0_MASK		0x3UL
#define CTXDESC_CD_0_TCR_SH0_SHIFT	12
#define ARM64_TCR_SH0_SHIFT		12
#define ARM64_TCR_SH0_MASK		0x3UL
#define CTXDESC_CD_0_TCR_EPD0_SHIFT	14
#define ARM64_TCR_EPD0_SHIFT		7
#define ARM64_TCR_EPD0_MASK		0x1UL
#define CTXDESC_CD_0_TCR_EPD1_SHIFT	30
#define ARM64_TCR_EPD1_SHIFT		23
#define ARM64_TCR_EPD1_MASK		0x1UL

#define CTXDESC_CD_0_ENDI		(1UL << 15)
#define CTXDESC_CD_0_V			(1UL << 31)

#define CTXDESC_CD_0_TCR_IPS_SHIFT	32
#define ARM64_TCR_IPS_SHIFT		32
#define ARM64_TCR_IPS_MASK		0x7UL
#define CTXDESC_CD_0_TCR_TBI0_SHIFT	38
#define ARM64_TCR_TBI0_SHIFT		37
#define ARM64_TCR_TBI0_MASK		0x1UL

#define CTXDESC_CD_0_AA64		(1UL << 41)
#define CTXDESC_CD_0_R			(1UL << 45)
#define CTXDESC_CD_0_A			(1UL << 46)
#define CTXDESC_CD_0_ASET_SHIFT		47
#define CTXDESC_CD_0_ASET_SHARED	(0UL << CTXDESC_CD_0_ASET_SHIFT)
#define CTXDESC_CD_0_ASET_PRIVATE	(1UL << CTXDESC_CD_0_ASET_SHIFT)
#define CTXDESC_CD_0_ASID_SHIFT		48
#define CTXDESC_CD_0_ASID_MASK		0xffffUL

#define CTXDESC_CD_1_TTB0_SHIFT		4
#define CTXDESC_CD_1_TTB0_MASK		0xfffffffffffUL

#define CTXDESC_CD_3_MAIR_SHIFT		0

/* Convert between AArch64 (CPU) TCR format and SMMU CD format */
#define ARM_SMMU_TCR2CD(tcr, fld)					\
	(((tcr) >> ARM64_TCR_##fld##_SHIFT & ARM64_TCR_##fld##_MASK)	\
	 << CTXDESC_CD_0_TCR_##fld##_SHIFT)

/* Command queue */
#define CMDQ_ENT_DWORDS			2
#define CMDQ_MAX_SZ_SHIFT		8

#define CMDQ_ERR_SHIFT			24
#define CMDQ_ERR_MASK			0x7f
#define CMDQ_ERR_CERROR_NONE_IDX	0
#define CMDQ_ERR_CERROR_ILL_IDX		1
#define CMDQ_ERR_CERROR_ABT_IDX		2

#define CMDQ_0_OP_SHIFT			0
#define CMDQ_0_OP_MASK			0xffUL
#define CMDQ_0_SSV			(1UL << 11)

#define CMDQ_PREFETCH_0_SID_SHIFT	32
#define CMDQ_PREFETCH_1_SIZE_SHIFT	0
#define CMDQ_PREFETCH_1_ADDR_MASK	~0xfffUL

#define CMDQ_CFGI_0_SID_SHIFT		32
#define CMDQ_CFGI_0_SID_MASK		0xffffffffUL
#define CMDQ_CFGI_1_LEAF		(1UL << 0)
#define CMDQ_CFGI_1_RANGE_SHIFT		0
#define CMDQ_CFGI_1_RANGE_MASK		0x1fUL

#define CMDQ_TLBI_0_VMID_SHIFT		32
#define CMDQ_TLBI_0_ASID_SHIFT		48
#define CMDQ_TLBI_1_LEAF		(1UL << 0)
#define CMDQ_TLBI_1_VA_MASK		~0xfffUL
#define CMDQ_TLBI_1_IPA_MASK		0xfffffffff000UL

#define CMDQ_PRI_0_SSID_SHIFT		12
#define CMDQ_PRI_0_SSID_MASK		0xfffffUL
#define CMDQ_PRI_0_SID_SHIFT		32
#define CMDQ_PRI_0_SID_MASK		0xffffffffUL
#define CMDQ_PRI_1_GRPID_SHIFT		0
#define CMDQ_PRI_1_GRPID_MASK		0x1ffUL
#define CMDQ_PRI_1_RESP_SHIFT		12
#define CMDQ_PRI_1_RESP_DENY		(0UL << CMDQ_PRI_1_RESP_SHIFT)
#define CMDQ_PRI_1_RESP_FAIL		(1UL << CMDQ_PRI_1_RESP_SHIFT)
#define CMDQ_PRI_1_RESP_SUCC		(2UL << CMDQ_PRI_1_RESP_SHIFT)

#define CMDQ_SYNC_0_CS_SHIFT		12
#define CMDQ_SYNC_0_CS_NONE		(0UL << CMDQ_SYNC_0_CS_SHIFT)
#define CMDQ_SYNC_0_CS_SEV		(2UL << CMDQ_SYNC_0_CS_SHIFT)

/* Event queue */
#define EVTQ_ENT_DWORDS			4
#define EVTQ_MAX_SZ_SHIFT		7

#define EVTQ_0_ID_SHIFT			0
#define EVTQ_0_ID_MASK			0xffUL

/* PRI queue */
#define PRIQ_ENT_DWORDS			2
#define PRIQ_MAX_SZ_SHIFT		8

#define PRIQ_0_SID_SHIFT		0
#define PRIQ_0_SID_MASK			0xffffffffUL
#define PRIQ_0_SSID_SHIFT		32
#define PRIQ_0_SSID_MASK		0xfffffUL
#define PRIQ_0_OF			(1UL << 57)
#define PRIQ_0_PERM_PRIV		(1UL << 58)
#define PRIQ_0_PERM_EXEC		(1UL << 59)
#define PRIQ_0_PERM_READ		(1UL << 60)
#define PRIQ_0_PERM_WRITE		(1UL << 61)
#define PRIQ_0_PRG_LAST			(1UL << 62)
#define PRIQ_0_SSID_V			(1UL << 63)

#define PRIQ_1_PRG_IDX_SHIFT		0
#define PRIQ_1_PRG_IDX_MASK		0x1ffUL
#define PRIQ_1_ADDR_SHIFT		12
#define PRIQ_1_ADDR_MASK		0xfffffffffffffUL

/* High-level queue structures */
#define ARM_SMMU_POLL_TIMEOUT_US	100

static bool disable_bypass;
module_param_named(disable_bypass, disable_bypass, bool, S_IRUGO);
MODULE_PARM_DESC(disable_bypass,
	"Disable bypass streams such that incoming transactions from devices that are not attached to an iommu domain will report an abort back to the device and will not be allowed to pass through the SMMU.");

enum pri_resp {
	PRI_RESP_DENY,
	PRI_RESP_FAIL,
	PRI_RESP_SUCC,
};

enum arm_smmu_msi_index {
	EVTQ_MSI_INDEX,
	GERROR_MSI_INDEX,
	PRIQ_MSI_INDEX,
	ARM_SMMU_MAX_MSIS,
};

static phys_addr_t arm_smmu_msi_cfg[ARM_SMMU_MAX_MSIS][3] = {
	[EVTQ_MSI_INDEX] = {
		ARM_SMMU_EVTQ_IRQ_CFG0,
		ARM_SMMU_EVTQ_IRQ_CFG1,
		ARM_SMMU_EVTQ_IRQ_CFG2,
	},
	[GERROR_MSI_INDEX] = {
		ARM_SMMU_GERROR_IRQ_CFG0,
		ARM_SMMU_GERROR_IRQ_CFG1,
		ARM_SMMU_GERROR_IRQ_CFG2,
	},
	[PRIQ_MSI_INDEX] = {
		ARM_SMMU_PRIQ_IRQ_CFG0,
		ARM_SMMU_PRIQ_IRQ_CFG1,
		ARM_SMMU_PRIQ_IRQ_CFG2,
	},
};

struct arm_smmu_cmdq_ent {
	/* Common fields */
	u8				opcode;
	bool				substream_valid;

	/* Command-specific fields */
	union {
		#define CMDQ_OP_PREFETCH_CFG	0x1
		struct {
			u32			sid;
			u8			size;
			u64			addr;
		} prefetch;

		#define CMDQ_OP_CFGI_STE	0x3
		#define CMDQ_OP_CFGI_ALL	0x4
		struct {
			u32			sid;
			union {
				bool		leaf;
				u8		span;
			};
		} cfgi;

		#define CMDQ_OP_TLBI_NH_ASID	0x11
		#define CMDQ_OP_TLBI_NH_VA	0x12
		#define CMDQ_OP_TLBI_EL2_ALL	0x20
		#define CMDQ_OP_TLBI_S12_VMALL	0x28
		#define CMDQ_OP_TLBI_S2_IPA	0x2a
		#define CMDQ_OP_TLBI_NSNH_ALL	0x30
		struct {
			u16			asid;
			u16			vmid;
			bool			leaf;
			u64			addr;
		} tlbi;

		#define CMDQ_OP_PRI_RESP	0x41
		struct {
			u32			sid;
			u32			ssid;
			u16			grpid;
			enum pri_resp		resp;
		} pri;

		#define CMDQ_OP_CMD_SYNC	0x46
	};
};

struct arm_smmu_queue {
	int				irq; /* Wired interrupt */

	__le64				*base;
	dma_addr_t			base_dma;
	u64				q_base;

	size_t				ent_dwords;
	u32				max_n_shift;
	u32				prod;
	u32				cons;

	u32 __iomem			*prod_reg;
	u32 __iomem			*cons_reg;
};

struct arm_smmu_cmdq {
	struct arm_smmu_queue		q;
	spinlock_t			lock;
};

struct arm_smmu_evtq {
	struct arm_smmu_queue		q;
	u32				max_stalls;
};

struct arm_smmu_priq {
	struct arm_smmu_queue		q;
};

/* High-level stream table and context descriptor structures */
struct arm_smmu_strtab_l1_desc {
	u8				span;

	__le64				*l2ptr;
	dma_addr_t			l2ptr_dma;
};

struct arm_smmu_s1_cfg {
	__le64				*cdptr;
	dma_addr_t			cdptr_dma;

	struct arm_smmu_ctx_desc {
		u16	asid;
		u64	ttbr;
		u64	tcr;
		u64	mair;
	}				cd;
};

struct arm_smmu_s2_cfg {
	u16				vmid;
	u64				vttbr;
	u64				vtcr;
};

struct arm_smmu_strtab_ent {
	bool				valid;

	bool				bypass;	/* Overrides s1/s2 config */
	struct arm_smmu_s1_cfg		*s1_cfg;
	struct arm_smmu_s2_cfg		*s2_cfg;
};

struct arm_smmu_strtab_cfg {
	__le64				*strtab;
	dma_addr_t			strtab_dma;
	struct arm_smmu_strtab_l1_desc	*l1_desc;
	unsigned int			num_l1_ents;

	u64				strtab_base;
	u32				strtab_base_cfg;
};

/* An SMMUv3 instance */
struct arm_smmu_device {
	struct device			*dev;
	void __iomem			*base;

#define ARM_SMMU_FEAT_2_LVL_STRTAB	(1 << 0)
#define ARM_SMMU_FEAT_2_LVL_CDTAB	(1 << 1)
#define ARM_SMMU_FEAT_TT_LE		(1 << 2)
#define ARM_SMMU_FEAT_TT_BE		(1 << 3)
#define ARM_SMMU_FEAT_PRI		(1 << 4)
#define ARM_SMMU_FEAT_ATS		(1 << 5)
#define ARM_SMMU_FEAT_SEV		(1 << 6)
#define ARM_SMMU_FEAT_MSI		(1 << 7)
#define ARM_SMMU_FEAT_COHERENCY		(1 << 8)
#define ARM_SMMU_FEAT_TRANS_S1		(1 << 9)
#define ARM_SMMU_FEAT_TRANS_S2		(1 << 10)
#define ARM_SMMU_FEAT_STALLS		(1 << 11)
#define ARM_SMMU_FEAT_HYP		(1 << 12)
	u32				features;

#define ARM_SMMU_OPT_SKIP_PREFETCH	(1 << 0)
	u32				options;

	struct arm_smmu_cmdq		cmdq;
	struct arm_smmu_evtq		evtq;
	struct arm_smmu_priq		priq;

	int				gerr_irq;

	unsigned long			ias; /* IPA */
	unsigned long			oas; /* PA */

#define ARM_SMMU_MAX_ASIDS		(1 << 16)
	unsigned int			asid_bits;
	DECLARE_BITMAP(asid_map, ARM_SMMU_MAX_ASIDS);

#define ARM_SMMU_MAX_VMIDS		(1 << 16)
	unsigned int			vmid_bits;
	DECLARE_BITMAP(vmid_map, ARM_SMMU_MAX_VMIDS);

	unsigned int			ssid_bits;
	unsigned int			sid_bits;

	struct arm_smmu_strtab_cfg	strtab_cfg;
};

/* SMMU private data for an IOMMU group */
struct arm_smmu_group {
	struct arm_smmu_device		*smmu;
	struct arm_smmu_domain		*domain;
	int				num_sids;
	u32				*sids;
	struct arm_smmu_strtab_ent	ste;
};

/* SMMU private data for an IOMMU domain */
enum arm_smmu_domain_stage {
	ARM_SMMU_DOMAIN_S1 = 0,
	ARM_SMMU_DOMAIN_S2,
	ARM_SMMU_DOMAIN_NESTED,
};

struct arm_smmu_domain {
	struct arm_smmu_device		*smmu;
	struct mutex			init_mutex; /* Protects smmu pointer */

	struct io_pgtable_ops		*pgtbl_ops;
	spinlock_t			pgtbl_lock;

	enum arm_smmu_domain_stage	stage;
	union {
		struct arm_smmu_s1_cfg	s1_cfg;
		struct arm_smmu_s2_cfg	s2_cfg;
	};

	struct iommu_domain		domain;
};

struct arm_smmu_option_prop {
	u32 opt;
	const char *prop;
};

static struct arm_smmu_option_prop arm_smmu_options[] = {
	{ ARM_SMMU_OPT_SKIP_PREFETCH, "hisilicon,broken-prefetch-cmd" },
	{ 0, NULL},
};

static struct arm_smmu_domain *to_smmu_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct arm_smmu_domain, domain);
}

static void parse_driver_options(struct arm_smmu_device *smmu)
{
	int i = 0;

	do {
		if (of_property_read_bool(smmu->dev->of_node,
						arm_smmu_options[i].prop)) {
			smmu->options |= arm_smmu_options[i].opt;
			dev_notice(smmu->dev, "option %s\n",
				arm_smmu_options[i].prop);
		}
	} while (arm_smmu_options[++i].opt);
}

/* Low-level queue manipulation functions */
static bool queue_full(struct arm_smmu_queue *q)
{
	return Q_IDX(q, q->prod) == Q_IDX(q, q->cons) &&
	       Q_WRP(q, q->prod) != Q_WRP(q, q->cons);
}

static bool queue_empty(struct arm_smmu_queue *q)
{
	return Q_IDX(q, q->prod) == Q_IDX(q, q->cons) &&
	       Q_WRP(q, q->prod) == Q_WRP(q, q->cons);
}

static void queue_sync_cons(struct arm_smmu_queue *q)
{
	q->cons = readl_relaxed(q->cons_reg);
}

static void queue_inc_cons(struct arm_smmu_queue *q)
{
	u32 cons = (Q_WRP(q, q->cons) | Q_IDX(q, q->cons)) + 1;

	q->cons = Q_OVF(q, q->cons) | Q_WRP(q, cons) | Q_IDX(q, cons);
	writel(q->cons, q->cons_reg);
}

static int queue_sync_prod(struct arm_smmu_queue *q)
{
	int ret = 0;
	u32 prod = readl_relaxed(q->prod_reg);

	if (Q_OVF(q, prod) != Q_OVF(q, q->prod))
		ret = -EOVERFLOW;

	q->prod = prod;
	return ret;
}

static void queue_inc_prod(struct arm_smmu_queue *q)
{
	u32 prod = (Q_WRP(q, q->prod) | Q_IDX(q, q->prod)) + 1;

	q->prod = Q_OVF(q, q->prod) | Q_WRP(q, prod) | Q_IDX(q, prod);
	writel(q->prod, q->prod_reg);
}

static bool __queue_cons_before(struct arm_smmu_queue *q, u32 until)
{
	if (Q_WRP(q, q->cons) == Q_WRP(q, until))
		return Q_IDX(q, q->cons) < Q_IDX(q, until);

	return Q_IDX(q, q->cons) >= Q_IDX(q, until);
}

static int queue_poll_cons(struct arm_smmu_queue *q, u32 until, bool wfe)
{
	ktime_t timeout = ktime_add_us(ktime_get(), ARM_SMMU_POLL_TIMEOUT_US);

	while (queue_sync_cons(q), __queue_cons_before(q, until)) {
		if (ktime_compare(ktime_get(), timeout) > 0)
			return -ETIMEDOUT;

		if (wfe) {
			wfe();
		} else {
			cpu_relax();
			udelay(1);
		}
	}

	return 0;
}

static void queue_write(__le64 *dst, u64 *src, size_t n_dwords)
{
	int i;

	for (i = 0; i < n_dwords; ++i)
		*dst++ = cpu_to_le64(*src++);
}

static int queue_insert_raw(struct arm_smmu_queue *q, u64 *ent)
{
	if (queue_full(q))
		return -ENOSPC;

	queue_write(Q_ENT(q, q->prod), ent, q->ent_dwords);
	queue_inc_prod(q);
	return 0;
}

static void queue_read(__le64 *dst, u64 *src, size_t n_dwords)
{
	int i;

	for (i = 0; i < n_dwords; ++i)
		*dst++ = le64_to_cpu(*src++);
}

static int queue_remove_raw(struct arm_smmu_queue *q, u64 *ent)
{
	if (queue_empty(q))
		return -EAGAIN;

	queue_read(ent, Q_ENT(q, q->cons), q->ent_dwords);
	queue_inc_cons(q);
	return 0;
}

/* High-level queue accessors */
static int arm_smmu_cmdq_build_cmd(u64 *cmd, struct arm_smmu_cmdq_ent *ent)
{
	memset(cmd, 0, CMDQ_ENT_DWORDS << 3);
	cmd[0] |= (ent->opcode & CMDQ_0_OP_MASK) << CMDQ_0_OP_SHIFT;

	switch (ent->opcode) {
	case CMDQ_OP_TLBI_EL2_ALL:
	case CMDQ_OP_TLBI_NSNH_ALL:
		break;
	case CMDQ_OP_PREFETCH_CFG:
		cmd[0] |= (u64)ent->prefetch.sid << CMDQ_PREFETCH_0_SID_SHIFT;
		cmd[1] |= ent->prefetch.size << CMDQ_PREFETCH_1_SIZE_SHIFT;
		cmd[1] |= ent->prefetch.addr & CMDQ_PREFETCH_1_ADDR_MASK;
		break;
	case CMDQ_OP_CFGI_STE:
		cmd[0] |= (u64)ent->cfgi.sid << CMDQ_CFGI_0_SID_SHIFT;
		cmd[1] |= ent->cfgi.leaf ? CMDQ_CFGI_1_LEAF : 0;
		break;
	case CMDQ_OP_CFGI_ALL:
		/* Cover the entire SID range */
		cmd[1] |= CMDQ_CFGI_1_RANGE_MASK << CMDQ_CFGI_1_RANGE_SHIFT;
		break;
	case CMDQ_OP_TLBI_NH_VA:
		cmd[0] |= (u64)ent->tlbi.asid << CMDQ_TLBI_0_ASID_SHIFT;
		cmd[1] |= ent->tlbi.leaf ? CMDQ_TLBI_1_LEAF : 0;
		cmd[1] |= ent->tlbi.addr & CMDQ_TLBI_1_VA_MASK;
		break;
	case CMDQ_OP_TLBI_S2_IPA:
		cmd[0] |= (u64)ent->tlbi.vmid << CMDQ_TLBI_0_VMID_SHIFT;
		cmd[1] |= ent->tlbi.leaf ? CMDQ_TLBI_1_LEAF : 0;
		cmd[1] |= ent->tlbi.addr & CMDQ_TLBI_1_IPA_MASK;
		break;
	case CMDQ_OP_TLBI_NH_ASID:
		cmd[0] |= (u64)ent->tlbi.asid << CMDQ_TLBI_0_ASID_SHIFT;
		/* Fallthrough */
	case CMDQ_OP_TLBI_S12_VMALL:
		cmd[0] |= (u64)ent->tlbi.vmid << CMDQ_TLBI_0_VMID_SHIFT;
		break;
	case CMDQ_OP_PRI_RESP:
		cmd[0] |= ent->substream_valid ? CMDQ_0_SSV : 0;
		cmd[0] |= ent->pri.ssid << CMDQ_PRI_0_SSID_SHIFT;
		cmd[0] |= (u64)ent->pri.sid << CMDQ_PRI_0_SID_SHIFT;
		cmd[1] |= ent->pri.grpid << CMDQ_PRI_1_GRPID_SHIFT;
		switch (ent->pri.resp) {
		case PRI_RESP_DENY:
			cmd[1] |= CMDQ_PRI_1_RESP_DENY;
			break;
		case PRI_RESP_FAIL:
			cmd[1] |= CMDQ_PRI_1_RESP_FAIL;
			break;
		case PRI_RESP_SUCC:
			cmd[1] |= CMDQ_PRI_1_RESP_SUCC;
			break;
		default:
			return -EINVAL;
		}
		break;
	case CMDQ_OP_CMD_SYNC:
		cmd[0] |= CMDQ_SYNC_0_CS_SEV;
		break;
	default:
		return -ENOENT;
	}

	return 0;
}

static void arm_smmu_cmdq_skip_err(struct arm_smmu_device *smmu)
{
	static const char *cerror_str[] = {
		[CMDQ_ERR_CERROR_NONE_IDX]	= "No error",
		[CMDQ_ERR_CERROR_ILL_IDX]	= "Illegal command",
		[CMDQ_ERR_CERROR_ABT_IDX]	= "Abort on command fetch",
	};

	int i;
	u64 cmd[CMDQ_ENT_DWORDS];
	struct arm_smmu_queue *q = &smmu->cmdq.q;
	u32 cons = readl_relaxed(q->cons_reg);
	u32 idx = cons >> CMDQ_ERR_SHIFT & CMDQ_ERR_MASK;
	struct arm_smmu_cmdq_ent cmd_sync = {
		.opcode = CMDQ_OP_CMD_SYNC,
	};

	dev_err(smmu->dev, "CMDQ error (cons 0x%08x): %s\n", cons,
		cerror_str[idx]);

	switch (idx) {
	case CMDQ_ERR_CERROR_ILL_IDX:
		break;
	case CMDQ_ERR_CERROR_ABT_IDX:
		dev_err(smmu->dev, "retrying command fetch\n");
	case CMDQ_ERR_CERROR_NONE_IDX:
		return;
	}

	/*
	 * We may have concurrent producers, so we need to be careful
	 * not to touch any of the shadow cmdq state.
	 */
	queue_read(cmd, Q_ENT(q, idx), q->ent_dwords);
	dev_err(smmu->dev, "skipping command in error state:\n");
	for (i = 0; i < ARRAY_SIZE(cmd); ++i)
		dev_err(smmu->dev, "\t0x%016llx\n", (unsigned long long)cmd[i]);

	/* Convert the erroneous command into a CMD_SYNC */
	if (arm_smmu_cmdq_build_cmd(cmd, &cmd_sync)) {
		dev_err(smmu->dev, "failed to convert to CMD_SYNC\n");
		return;
	}

	queue_write(cmd, Q_ENT(q, idx), q->ent_dwords);
}

static void arm_smmu_cmdq_issue_cmd(struct arm_smmu_device *smmu,
				    struct arm_smmu_cmdq_ent *ent)
{
	u32 until;
	u64 cmd[CMDQ_ENT_DWORDS];
	bool wfe = !!(smmu->features & ARM_SMMU_FEAT_SEV);
	struct arm_smmu_queue *q = &smmu->cmdq.q;

	if (arm_smmu_cmdq_build_cmd(cmd, ent)) {
		dev_warn(smmu->dev, "ignoring unknown CMDQ opcode 0x%x\n",
			 ent->opcode);
		return;
	}

	spin_lock(&smmu->cmdq.lock);
	while (until = q->prod + 1, queue_insert_raw(q, cmd) == -ENOSPC) {
		/*
		 * Keep the queue locked, otherwise the producer could wrap
		 * twice and we could see a future consumer pointer that looks
		 * like it's behind us.
		 */
		if (queue_poll_cons(q, until, wfe))
			dev_err_ratelimited(smmu->dev, "CMDQ timeout\n");
	}

	if (ent->opcode == CMDQ_OP_CMD_SYNC && queue_poll_cons(q, until, wfe))
		dev_err_ratelimited(smmu->dev, "CMD_SYNC timeout\n");
	spin_unlock(&smmu->cmdq.lock);
}

/* Context descriptor manipulation functions */
static u64 arm_smmu_cpu_tcr_to_cd(u64 tcr)
{
	u64 val = 0;

	/* Repack the TCR. Just care about TTBR0 for now */
	val |= ARM_SMMU_TCR2CD(tcr, T0SZ);
	val |= ARM_SMMU_TCR2CD(tcr, TG0);
	val |= ARM_SMMU_TCR2CD(tcr, IRGN0);
	val |= ARM_SMMU_TCR2CD(tcr, ORGN0);
	val |= ARM_SMMU_TCR2CD(tcr, SH0);
	val |= ARM_SMMU_TCR2CD(tcr, EPD0);
	val |= ARM_SMMU_TCR2CD(tcr, EPD1);
	val |= ARM_SMMU_TCR2CD(tcr, IPS);
	val |= ARM_SMMU_TCR2CD(tcr, TBI0);

	return val;
}

static void arm_smmu_write_ctx_desc(struct arm_smmu_device *smmu,
				    struct arm_smmu_s1_cfg *cfg)
{
	u64 val;

	/*
	 * We don't need to issue any invalidation here, as we'll invalidate
	 * the STE when installing the new entry anyway.
	 */
	val = arm_smmu_cpu_tcr_to_cd(cfg->cd.tcr) |
#ifdef __BIG_ENDIAN
	      CTXDESC_CD_0_ENDI |
#endif
	      CTXDESC_CD_0_R | CTXDESC_CD_0_A | CTXDESC_CD_0_ASET_PRIVATE |
	      CTXDESC_CD_0_AA64 | (u64)cfg->cd.asid << CTXDESC_CD_0_ASID_SHIFT |
	      CTXDESC_CD_0_V;
	cfg->cdptr[0] = cpu_to_le64(val);

	val = cfg->cd.ttbr & CTXDESC_CD_1_TTB0_MASK << CTXDESC_CD_1_TTB0_SHIFT;
	cfg->cdptr[1] = cpu_to_le64(val);

	cfg->cdptr[3] = cpu_to_le64(cfg->cd.mair << CTXDESC_CD_3_MAIR_SHIFT);
}

/* Stream table manipulation functions */
static void
arm_smmu_write_strtab_l1_desc(__le64 *dst, struct arm_smmu_strtab_l1_desc *desc)
{
	u64 val = 0;

	val |= (desc->span & STRTAB_L1_DESC_SPAN_MASK)
		<< STRTAB_L1_DESC_SPAN_SHIFT;
	val |= desc->l2ptr_dma &
	       STRTAB_L1_DESC_L2PTR_MASK << STRTAB_L1_DESC_L2PTR_SHIFT;

	*dst = cpu_to_le64(val);
}

static void arm_smmu_sync_ste_for_sid(struct arm_smmu_device *smmu, u32 sid)
{
	struct arm_smmu_cmdq_ent cmd = {
		.opcode	= CMDQ_OP_CFGI_STE,
		.cfgi	= {
			.sid	= sid,
			.leaf	= true,
		},
	};

	arm_smmu_cmdq_issue_cmd(smmu, &cmd);
	cmd.opcode = CMDQ_OP_CMD_SYNC;
	arm_smmu_cmdq_issue_cmd(smmu, &cmd);
}

static void arm_smmu_write_strtab_ent(struct arm_smmu_device *smmu, u32 sid,
				      __le64 *dst, struct arm_smmu_strtab_ent *ste)
{
	/*
	 * This is hideously complicated, but we only really care about
	 * three cases at the moment:
	 *
	 * 1. Invalid (all zero) -> bypass  (init)
	 * 2. Bypass -> translation (attach)
	 * 3. Translation -> bypass (detach)
	 *
	 * Given that we can't update the STE atomically and the SMMU
	 * doesn't read the thing in a defined order, that leaves us
	 * with the following maintenance requirements:
	 *
	 * 1. Update Config, return (init time STEs aren't live)
	 * 2. Write everything apart from dword 0, sync, write dword 0, sync
	 * 3. Update Config, sync
	 */
	u64 val = le64_to_cpu(dst[0]);
	bool ste_live = false;
	struct arm_smmu_cmdq_ent prefetch_cmd = {
		.opcode		= CMDQ_OP_PREFETCH_CFG,
		.prefetch	= {
			.sid	= sid,
		},
	};

	if (val & STRTAB_STE_0_V) {
		u64 cfg;

		cfg = val & STRTAB_STE_0_CFG_MASK << STRTAB_STE_0_CFG_SHIFT;
		switch (cfg) {
		case STRTAB_STE_0_CFG_BYPASS:
			break;
		case STRTAB_STE_0_CFG_S1_TRANS:
		case STRTAB_STE_0_CFG_S2_TRANS:
			ste_live = true;
			break;
		default:
			BUG(); /* STE corruption */
		}
	}

	/* Nuke the existing Config, as we're going to rewrite it */
	val &= ~(STRTAB_STE_0_CFG_MASK << STRTAB_STE_0_CFG_SHIFT);

	if (ste->valid)
		val |= STRTAB_STE_0_V;
	else
		val &= ~STRTAB_STE_0_V;

	if (ste->bypass) {
		val |= disable_bypass ? STRTAB_STE_0_CFG_ABORT
				      : STRTAB_STE_0_CFG_BYPASS;
		dst[0] = cpu_to_le64(val);
		dst[2] = 0; /* Nuke the VMID */
		if (ste_live)
			arm_smmu_sync_ste_for_sid(smmu, sid);
		return;
	}

	if (ste->s1_cfg) {
		BUG_ON(ste_live);
		dst[1] = cpu_to_le64(
			 STRTAB_STE_1_S1C_CACHE_WBRA
			 << STRTAB_STE_1_S1CIR_SHIFT |
			 STRTAB_STE_1_S1C_CACHE_WBRA
			 << STRTAB_STE_1_S1COR_SHIFT |
			 STRTAB_STE_1_S1C_SH_ISH << STRTAB_STE_1_S1CSH_SHIFT |
			 STRTAB_STE_1_S1STALLD |
#ifdef CONFIG_PCI_ATS
			 STRTAB_STE_1_EATS_TRANS << STRTAB_STE_1_EATS_SHIFT |
#endif
			 STRTAB_STE_1_STRW_NSEL1 << STRTAB_STE_1_STRW_SHIFT);

		val |= (ste->s1_cfg->cdptr_dma & STRTAB_STE_0_S1CTXPTR_MASK
		        << STRTAB_STE_0_S1CTXPTR_SHIFT) |
			STRTAB_STE_0_CFG_S1_TRANS;

	}

	if (ste->s2_cfg) {
		BUG_ON(ste_live);
		dst[2] = cpu_to_le64(
			 ste->s2_cfg->vmid << STRTAB_STE_2_S2VMID_SHIFT |
			 (ste->s2_cfg->vtcr & STRTAB_STE_2_VTCR_MASK)
			  << STRTAB_STE_2_VTCR_SHIFT |
#ifdef __BIG_ENDIAN
			 STRTAB_STE_2_S2ENDI |
#endif
			 STRTAB_STE_2_S2PTW | STRTAB_STE_2_S2AA64 |
			 STRTAB_STE_2_S2R);

		dst[3] = cpu_to_le64(ste->s2_cfg->vttbr &
			 STRTAB_STE_3_S2TTB_MASK << STRTAB_STE_3_S2TTB_SHIFT);

		val |= STRTAB_STE_0_CFG_S2_TRANS;
	}

	arm_smmu_sync_ste_for_sid(smmu, sid);
	dst[0] = cpu_to_le64(val);
	arm_smmu_sync_ste_for_sid(smmu, sid);

	/* It's likely that we'll want to use the new STE soon */
	if (!(smmu->options & ARM_SMMU_OPT_SKIP_PREFETCH))
		arm_smmu_cmdq_issue_cmd(smmu, &prefetch_cmd);
}

static void arm_smmu_init_bypass_stes(u64 *strtab, unsigned int nent)
{
	unsigned int i;
	struct arm_smmu_strtab_ent ste = {
		.valid	= true,
		.bypass	= true,
	};

	for (i = 0; i < nent; ++i) {
		arm_smmu_write_strtab_ent(NULL, -1, strtab, &ste);
		strtab += STRTAB_STE_DWORDS;
	}
}

static int arm_smmu_init_l2_strtab(struct arm_smmu_device *smmu, u32 sid)
{
	size_t size;
	void *strtab;
	struct arm_smmu_strtab_cfg *cfg = &smmu->strtab_cfg;
	struct arm_smmu_strtab_l1_desc *desc = &cfg->l1_desc[sid >> STRTAB_SPLIT];

	if (desc->l2ptr)
		return 0;

	size = 1 << (STRTAB_SPLIT + ilog2(STRTAB_STE_DWORDS) + 3);
	strtab = &cfg->strtab[(sid >> STRTAB_SPLIT) * STRTAB_L1_DESC_DWORDS];

	desc->span = STRTAB_SPLIT + 1;
	desc->l2ptr = dma_zalloc_coherent(smmu->dev, size, &desc->l2ptr_dma,
					  GFP_KERNEL);
	if (!desc->l2ptr) {
		dev_err(smmu->dev,
			"failed to allocate l2 stream table for SID %u\n",
			sid);
		return -ENOMEM;
	}

	arm_smmu_init_bypass_stes(desc->l2ptr, 1 << STRTAB_SPLIT);
	arm_smmu_write_strtab_l1_desc(strtab, desc);
	return 0;
}

/* IRQ and event handlers */
static irqreturn_t arm_smmu_evtq_thread(int irq, void *dev)
{
	int i;
	struct arm_smmu_device *smmu = dev;
	struct arm_smmu_queue *q = &smmu->evtq.q;
	u64 evt[EVTQ_ENT_DWORDS];

	while (!queue_remove_raw(q, evt)) {
		u8 id = evt[0] >> EVTQ_0_ID_SHIFT & EVTQ_0_ID_MASK;

		dev_info(smmu->dev, "event 0x%02x received:\n", id);
		for (i = 0; i < ARRAY_SIZE(evt); ++i)
			dev_info(smmu->dev, "\t0x%016llx\n",
				 (unsigned long long)evt[i]);
	}

	/* Sync our overflow flag, as we believe we're up to speed */
	q->cons = Q_OVF(q, q->prod) | Q_WRP(q, q->cons) | Q_IDX(q, q->cons);
	return IRQ_HANDLED;
}

static irqreturn_t arm_smmu_evtq_handler(int irq, void *dev)
{
	irqreturn_t ret = IRQ_WAKE_THREAD;
	struct arm_smmu_device *smmu = dev;
	struct arm_smmu_queue *q = &smmu->evtq.q;

	/*
	 * Not much we can do on overflow, so scream and pretend we're
	 * trying harder.
	 */
	if (queue_sync_prod(q) == -EOVERFLOW)
		dev_err(smmu->dev, "EVTQ overflow detected -- events lost\n");
	else if (queue_empty(q))
		ret = IRQ_NONE;

	return ret;
}

static irqreturn_t arm_smmu_priq_thread(int irq, void *dev)
{
	struct arm_smmu_device *smmu = dev;
	struct arm_smmu_queue *q = &smmu->priq.q;
	u64 evt[PRIQ_ENT_DWORDS];

	while (!queue_remove_raw(q, evt)) {
		u32 sid, ssid;
		u16 grpid;
		bool ssv, last;

		sid = evt[0] >> PRIQ_0_SID_SHIFT & PRIQ_0_SID_MASK;
		ssv = evt[0] & PRIQ_0_SSID_V;
		ssid = ssv ? evt[0] >> PRIQ_0_SSID_SHIFT & PRIQ_0_SSID_MASK : 0;
		last = evt[0] & PRIQ_0_PRG_LAST;
		grpid = evt[1] >> PRIQ_1_PRG_IDX_SHIFT & PRIQ_1_PRG_IDX_MASK;

		dev_info(smmu->dev, "unexpected PRI request received:\n");
		dev_info(smmu->dev,
			 "\tsid 0x%08x.0x%05x: [%u%s] %sprivileged %s%s%s access at iova 0x%016llx\n",
			 sid, ssid, grpid, last ? "L" : "",
			 evt[0] & PRIQ_0_PERM_PRIV ? "" : "un",
			 evt[0] & PRIQ_0_PERM_READ ? "R" : "",
			 evt[0] & PRIQ_0_PERM_WRITE ? "W" : "",
			 evt[0] & PRIQ_0_PERM_EXEC ? "X" : "",
			 evt[1] & PRIQ_1_ADDR_MASK << PRIQ_1_ADDR_SHIFT);

		if (last) {
			struct arm_smmu_cmdq_ent cmd = {
				.opcode			= CMDQ_OP_PRI_RESP,
				.substream_valid	= ssv,
				.pri			= {
					.sid	= sid,
					.ssid	= ssid,
					.grpid	= grpid,
					.resp	= PRI_RESP_DENY,
				},
			};

			arm_smmu_cmdq_issue_cmd(smmu, &cmd);
		}
	}

	/* Sync our overflow flag, as we believe we're up to speed */
	q->cons = Q_OVF(q, q->prod) | Q_WRP(q, q->cons) | Q_IDX(q, q->cons);
	return IRQ_HANDLED;
}

static irqreturn_t arm_smmu_priq_handler(int irq, void *dev)
{
	irqreturn_t ret = IRQ_WAKE_THREAD;
	struct arm_smmu_device *smmu = dev;
	struct arm_smmu_queue *q = &smmu->priq.q;

	/* PRIQ overflow indicates a programming error */
	if (queue_sync_prod(q) == -EOVERFLOW)
		dev_err(smmu->dev, "PRIQ overflow detected -- requests lost\n");
	else if (queue_empty(q))
		ret = IRQ_NONE;

	return ret;
}

static irqreturn_t arm_smmu_cmdq_sync_handler(int irq, void *dev)
{
	/* We don't actually use CMD_SYNC interrupts for anything */
	return IRQ_HANDLED;
}

static int arm_smmu_device_disable(struct arm_smmu_device *smmu);

static irqreturn_t arm_smmu_gerror_handler(int irq, void *dev)
{
	u32 gerror, gerrorn;
	struct arm_smmu_device *smmu = dev;

	gerror = readl_relaxed(smmu->base + ARM_SMMU_GERROR);
	gerrorn = readl_relaxed(smmu->base + ARM_SMMU_GERRORN);

	gerror ^= gerrorn;
	if (!(gerror & GERROR_ERR_MASK))
		return IRQ_NONE; /* No errors pending */

	dev_warn(smmu->dev,
		 "unexpected global error reported (0x%08x), this could be serious\n",
		 gerror);

	if (gerror & GERROR_SFM_ERR) {
		dev_err(smmu->dev, "device has entered Service Failure Mode!\n");
		arm_smmu_device_disable(smmu);
	}

	if (gerror & GERROR_MSI_GERROR_ABT_ERR)
		dev_warn(smmu->dev, "GERROR MSI write aborted\n");

	if (gerror & GERROR_MSI_PRIQ_ABT_ERR) {
		dev_warn(smmu->dev, "PRIQ MSI write aborted\n");
		arm_smmu_priq_handler(irq, smmu->dev);
	}

	if (gerror & GERROR_MSI_EVTQ_ABT_ERR) {
		dev_warn(smmu->dev, "EVTQ MSI write aborted\n");
		arm_smmu_evtq_handler(irq, smmu->dev);
	}

	if (gerror & GERROR_MSI_CMDQ_ABT_ERR) {
		dev_warn(smmu->dev, "CMDQ MSI write aborted\n");
		arm_smmu_cmdq_sync_handler(irq, smmu->dev);
	}

	if (gerror & GERROR_PRIQ_ABT_ERR)
		dev_err(smmu->dev, "PRIQ write aborted -- events may have been lost\n");

	if (gerror & GERROR_EVTQ_ABT_ERR)
		dev_err(smmu->dev, "EVTQ write aborted -- events may have been lost\n");

	if (gerror & GERROR_CMDQ_ERR)
		arm_smmu_cmdq_skip_err(smmu);

	writel(gerror, smmu->base + ARM_SMMU_GERRORN);
	return IRQ_HANDLED;
}

/* IO_PGTABLE API */
static void __arm_smmu_tlb_sync(struct arm_smmu_device *smmu)
{
	struct arm_smmu_cmdq_ent cmd;

	cmd.opcode = CMDQ_OP_CMD_SYNC;
	arm_smmu_cmdq_issue_cmd(smmu, &cmd);
}

static void arm_smmu_tlb_sync(void *cookie)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	__arm_smmu_tlb_sync(smmu_domain->smmu);
}

static void arm_smmu_tlb_inv_context(void *cookie)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_cmdq_ent cmd;

	if (smmu_domain->stage == ARM_SMMU_DOMAIN_S1) {
		cmd.opcode	= CMDQ_OP_TLBI_NH_ASID;
		cmd.tlbi.asid	= smmu_domain->s1_cfg.cd.asid;
		cmd.tlbi.vmid	= 0;
	} else {
		cmd.opcode	= CMDQ_OP_TLBI_S12_VMALL;
		cmd.tlbi.vmid	= smmu_domain->s2_cfg.vmid;
	}

	arm_smmu_cmdq_issue_cmd(smmu, &cmd);
	__arm_smmu_tlb_sync(smmu);
}

static void arm_smmu_tlb_inv_range_nosync(unsigned long iova, size_t size,
					  bool leaf, void *cookie)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_cmdq_ent cmd = {
		.tlbi = {
			.leaf	= leaf,
			.addr	= iova,
		},
	};

	if (smmu_domain->stage == ARM_SMMU_DOMAIN_S1) {
		cmd.opcode	= CMDQ_OP_TLBI_NH_VA;
		cmd.tlbi.asid	= smmu_domain->s1_cfg.cd.asid;
	} else {
		cmd.opcode	= CMDQ_OP_TLBI_S2_IPA;
		cmd.tlbi.vmid	= smmu_domain->s2_cfg.vmid;
	}

	arm_smmu_cmdq_issue_cmd(smmu, &cmd);
}

static struct iommu_gather_ops arm_smmu_gather_ops = {
	.tlb_flush_all	= arm_smmu_tlb_inv_context,
	.tlb_add_flush	= arm_smmu_tlb_inv_range_nosync,
	.tlb_sync	= arm_smmu_tlb_sync,
};

/* IOMMU API */
static bool arm_smmu_capable(enum iommu_cap cap)
{
	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		return true;
	case IOMMU_CAP_INTR_REMAP:
		return true; /* MSIs are just memory writes */
	case IOMMU_CAP_NOEXEC:
		return true;
	default:
		return false;
	}
}

static struct iommu_domain *arm_smmu_domain_alloc(unsigned type)
{
	struct arm_smmu_domain *smmu_domain;

	if (type != IOMMU_DOMAIN_UNMANAGED)
		return NULL;

	/*
	 * Allocate the domain and initialise some of its data structures.
	 * We can't really do anything meaningful until we've added a
	 * master.
	 */
	smmu_domain = kzalloc(sizeof(*smmu_domain), GFP_KERNEL);
	if (!smmu_domain)
		return NULL;

	mutex_init(&smmu_domain->init_mutex);
	spin_lock_init(&smmu_domain->pgtbl_lock);
	return &smmu_domain->domain;
}

static int arm_smmu_bitmap_alloc(unsigned long *map, int span)
{
	int idx, size = 1 << span;

	do {
		idx = find_first_zero_bit(map, size);
		if (idx == size)
			return -ENOSPC;
	} while (test_and_set_bit(idx, map));

	return idx;
}

static void arm_smmu_bitmap_free(unsigned long *map, int idx)
{
	clear_bit(idx, map);
}

static void arm_smmu_domain_free(struct iommu_domain *domain)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_device *smmu = smmu_domain->smmu;

	free_io_pgtable_ops(smmu_domain->pgtbl_ops);

	/* Free the CD and ASID, if we allocated them */
	if (smmu_domain->stage == ARM_SMMU_DOMAIN_S1) {
		struct arm_smmu_s1_cfg *cfg = &smmu_domain->s1_cfg;

		if (cfg->cdptr) {
			dma_free_coherent(smmu_domain->smmu->dev,
					  CTXDESC_CD_DWORDS << 3,
					  cfg->cdptr,
					  cfg->cdptr_dma);

			arm_smmu_bitmap_free(smmu->asid_map, cfg->cd.asid);
		}
	} else {
		struct arm_smmu_s2_cfg *cfg = &smmu_domain->s2_cfg;
		if (cfg->vmid)
			arm_smmu_bitmap_free(smmu->vmid_map, cfg->vmid);
	}

	kfree(smmu_domain);
}

static int arm_smmu_domain_finalise_s1(struct arm_smmu_domain *smmu_domain,
				       struct io_pgtable_cfg *pgtbl_cfg)
{
	int ret;
	int asid;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_s1_cfg *cfg = &smmu_domain->s1_cfg;

	asid = arm_smmu_bitmap_alloc(smmu->asid_map, smmu->asid_bits);
	if (IS_ERR_VALUE(asid))
		return asid;

	cfg->cdptr = dma_zalloc_coherent(smmu->dev, CTXDESC_CD_DWORDS << 3,
					 &cfg->cdptr_dma, GFP_KERNEL);
	if (!cfg->cdptr) {
		dev_warn(smmu->dev, "failed to allocate context descriptor\n");
		ret = -ENOMEM;
		goto out_free_asid;
	}

	cfg->cd.asid	= (u16)asid;
	cfg->cd.ttbr	= pgtbl_cfg->arm_lpae_s1_cfg.ttbr[0];
	cfg->cd.tcr	= pgtbl_cfg->arm_lpae_s1_cfg.tcr;
	cfg->cd.mair	= pgtbl_cfg->arm_lpae_s1_cfg.mair[0];
	return 0;

out_free_asid:
	arm_smmu_bitmap_free(smmu->asid_map, asid);
	return ret;
}

static int arm_smmu_domain_finalise_s2(struct arm_smmu_domain *smmu_domain,
				       struct io_pgtable_cfg *pgtbl_cfg)
{
	int vmid;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_s2_cfg *cfg = &smmu_domain->s2_cfg;

	vmid = arm_smmu_bitmap_alloc(smmu->vmid_map, smmu->vmid_bits);
	if (IS_ERR_VALUE(vmid))
		return vmid;

	cfg->vmid	= (u16)vmid;
	cfg->vttbr	= pgtbl_cfg->arm_lpae_s2_cfg.vttbr;
	cfg->vtcr	= pgtbl_cfg->arm_lpae_s2_cfg.vtcr;
	return 0;
}

static struct iommu_ops arm_smmu_ops;

static int arm_smmu_domain_finalise(struct iommu_domain *domain)
{
	int ret;
	unsigned long ias, oas;
	enum io_pgtable_fmt fmt;
	struct io_pgtable_cfg pgtbl_cfg;
	struct io_pgtable_ops *pgtbl_ops;
	int (*finalise_stage_fn)(struct arm_smmu_domain *,
				 struct io_pgtable_cfg *);
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_device *smmu = smmu_domain->smmu;

	/* Restrict the stage to what we can actually support */
	if (!(smmu->features & ARM_SMMU_FEAT_TRANS_S1))
		smmu_domain->stage = ARM_SMMU_DOMAIN_S2;
	if (!(smmu->features & ARM_SMMU_FEAT_TRANS_S2))
		smmu_domain->stage = ARM_SMMU_DOMAIN_S1;

	switch (smmu_domain->stage) {
	case ARM_SMMU_DOMAIN_S1:
		ias = VA_BITS;
		oas = smmu->ias;
		fmt = ARM_64_LPAE_S1;
		finalise_stage_fn = arm_smmu_domain_finalise_s1;
		break;
	case ARM_SMMU_DOMAIN_NESTED:
	case ARM_SMMU_DOMAIN_S2:
		ias = smmu->ias;
		oas = smmu->oas;
		fmt = ARM_64_LPAE_S2;
		finalise_stage_fn = arm_smmu_domain_finalise_s2;
		break;
	default:
		return -EINVAL;
	}

	pgtbl_cfg = (struct io_pgtable_cfg) {
		.pgsize_bitmap	= arm_smmu_ops.pgsize_bitmap,
		.ias		= ias,
		.oas		= oas,
		.tlb		= &arm_smmu_gather_ops,
		.iommu_dev	= smmu->dev,
	};

	pgtbl_ops = alloc_io_pgtable_ops(fmt, &pgtbl_cfg, smmu_domain);
	if (!pgtbl_ops)
		return -ENOMEM;

	arm_smmu_ops.pgsize_bitmap = pgtbl_cfg.pgsize_bitmap;
	smmu_domain->pgtbl_ops = pgtbl_ops;

	ret = finalise_stage_fn(smmu_domain, &pgtbl_cfg);
	if (IS_ERR_VALUE(ret))
		free_io_pgtable_ops(pgtbl_ops);

	return ret;
}

static struct arm_smmu_group *arm_smmu_group_get(struct device *dev)
{
	struct iommu_group *group;
	struct arm_smmu_group *smmu_group;

	group = iommu_group_get(dev);
	if (!group)
		return NULL;

	smmu_group = iommu_group_get_iommudata(group);
	iommu_group_put(group);
	return smmu_group;
}

static __le64 *arm_smmu_get_step_for_sid(struct arm_smmu_device *smmu, u32 sid)
{
	__le64 *step;
	struct arm_smmu_strtab_cfg *cfg = &smmu->strtab_cfg;

	if (smmu->features & ARM_SMMU_FEAT_2_LVL_STRTAB) {
		struct arm_smmu_strtab_l1_desc *l1_desc;
		int idx;

		/* Two-level walk */
		idx = (sid >> STRTAB_SPLIT) * STRTAB_L1_DESC_DWORDS;
		l1_desc = &cfg->l1_desc[idx];
		idx = (sid & ((1 << STRTAB_SPLIT) - 1)) * STRTAB_STE_DWORDS;
		step = &l1_desc->l2ptr[idx];
	} else {
		/* Simple linear lookup */
		step = &cfg->strtab[sid * STRTAB_STE_DWORDS];
	}

	return step;
}

static int arm_smmu_install_ste_for_group(struct arm_smmu_group *smmu_group)
{
	int i;
	struct arm_smmu_domain *smmu_domain = smmu_group->domain;
	struct arm_smmu_strtab_ent *ste = &smmu_group->ste;
	struct arm_smmu_device *smmu = smmu_group->smmu;

	if (smmu_domain->stage == ARM_SMMU_DOMAIN_S1) {
		ste->s1_cfg = &smmu_domain->s1_cfg;
		ste->s2_cfg = NULL;
		arm_smmu_write_ctx_desc(smmu, ste->s1_cfg);
	} else {
		ste->s1_cfg = NULL;
		ste->s2_cfg = &smmu_domain->s2_cfg;
	}

	for (i = 0; i < smmu_group->num_sids; ++i) {
		u32 sid = smmu_group->sids[i];
		__le64 *step = arm_smmu_get_step_for_sid(smmu, sid);

		arm_smmu_write_strtab_ent(smmu, sid, step, ste);
	}

	return 0;
}

static int arm_smmu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	int ret = 0;
	struct arm_smmu_device *smmu;
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_group *smmu_group = arm_smmu_group_get(dev);

	if (!smmu_group)
		return -ENOENT;

	/* Already attached to a different domain? */
	if (smmu_group->domain && smmu_group->domain != smmu_domain)
		return -EEXIST;

	smmu = smmu_group->smmu;
	mutex_lock(&smmu_domain->init_mutex);

	if (!smmu_domain->smmu) {
		smmu_domain->smmu = smmu;
		ret = arm_smmu_domain_finalise(domain);
		if (ret) {
			smmu_domain->smmu = NULL;
			goto out_unlock;
		}
	} else if (smmu_domain->smmu != smmu) {
		dev_err(dev,
			"cannot attach to SMMU %s (upstream of %s)\n",
			dev_name(smmu_domain->smmu->dev),
			dev_name(smmu->dev));
		ret = -ENXIO;
		goto out_unlock;
	}

	/* Group already attached to this domain? */
	if (smmu_group->domain)
		goto out_unlock;

	smmu_group->domain	= smmu_domain;
	smmu_group->ste.bypass	= false;

	ret = arm_smmu_install_ste_for_group(smmu_group);
	if (IS_ERR_VALUE(ret))
		smmu_group->domain = NULL;

out_unlock:
	mutex_unlock(&smmu_domain->init_mutex);
	return ret;
}

static void arm_smmu_detach_dev(struct iommu_domain *domain, struct device *dev)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_group *smmu_group = arm_smmu_group_get(dev);

	BUG_ON(!smmu_domain);
	BUG_ON(!smmu_group);

	mutex_lock(&smmu_domain->init_mutex);
	BUG_ON(smmu_group->domain != smmu_domain);

	smmu_group->ste.bypass = true;
	if (IS_ERR_VALUE(arm_smmu_install_ste_for_group(smmu_group)))
		dev_warn(dev, "failed to install bypass STE\n");

	smmu_group->domain = NULL;
	mutex_unlock(&smmu_domain->init_mutex);
}

static int arm_smmu_map(struct iommu_domain *domain, unsigned long iova,
			phys_addr_t paddr, size_t size, int prot)
{
	int ret;
	unsigned long flags;
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct io_pgtable_ops *ops = smmu_domain->pgtbl_ops;

	if (!ops)
		return -ENODEV;

	spin_lock_irqsave(&smmu_domain->pgtbl_lock, flags);
	ret = ops->map(ops, iova, paddr, size, prot);
	spin_unlock_irqrestore(&smmu_domain->pgtbl_lock, flags);
	return ret;
}

static size_t
arm_smmu_unmap(struct iommu_domain *domain, unsigned long iova, size_t size)
{
	size_t ret;
	unsigned long flags;
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct io_pgtable_ops *ops = smmu_domain->pgtbl_ops;

	if (!ops)
		return 0;

	spin_lock_irqsave(&smmu_domain->pgtbl_lock, flags);
	ret = ops->unmap(ops, iova, size);
	spin_unlock_irqrestore(&smmu_domain->pgtbl_lock, flags);
	return ret;
}

static phys_addr_t
arm_smmu_iova_to_phys(struct iommu_domain *domain, dma_addr_t iova)
{
	phys_addr_t ret;
	unsigned long flags;
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct io_pgtable_ops *ops = smmu_domain->pgtbl_ops;

	if (!ops)
		return 0;

	spin_lock_irqsave(&smmu_domain->pgtbl_lock, flags);
	ret = ops->iova_to_phys(ops, iova);
	spin_unlock_irqrestore(&smmu_domain->pgtbl_lock, flags);

	return ret;
}

static int __arm_smmu_get_pci_sid(struct pci_dev *pdev, u16 alias, void *sidp)
{
	*(u32 *)sidp = alias;
	return 0; /* Continue walking */
}

static void __arm_smmu_release_pci_iommudata(void *data)
{
	kfree(data);
}

static struct arm_smmu_device *arm_smmu_get_for_pci_dev(struct pci_dev *pdev)
{
	struct device_node *of_node;
	struct platform_device *smmu_pdev;
	struct arm_smmu_device *smmu = NULL;
	struct pci_bus *bus = pdev->bus;

	/* Walk up to the root bus */
	while (!pci_is_root_bus(bus))
		bus = bus->parent;

	/* Follow the "iommus" phandle from the host controller */
	of_node = of_parse_phandle(bus->bridge->parent->of_node, "iommus", 0);
	if (!of_node)
		return NULL;

	/* See if we can find an SMMU corresponding to the phandle */
	smmu_pdev = of_find_device_by_node(of_node);
	if (smmu_pdev)
		smmu = platform_get_drvdata(smmu_pdev);

	of_node_put(of_node);
	return smmu;
}

static bool arm_smmu_sid_in_range(struct arm_smmu_device *smmu, u32 sid)
{
	unsigned long limit = smmu->strtab_cfg.num_l1_ents;

	if (smmu->features & ARM_SMMU_FEAT_2_LVL_STRTAB)
		limit *= 1UL << STRTAB_SPLIT;

	return sid < limit;
}

static int arm_smmu_add_device(struct device *dev)
{
	int i, ret;
	u32 sid, *sids;
	struct pci_dev *pdev;
	struct iommu_group *group;
	struct arm_smmu_group *smmu_group;
	struct arm_smmu_device *smmu;

	/* We only support PCI, for now */
	if (!dev_is_pci(dev))
		return -ENODEV;

	pdev = to_pci_dev(dev);
	group = iommu_group_get_for_dev(dev);
	if (IS_ERR(group))
		return PTR_ERR(group);

	smmu_group = iommu_group_get_iommudata(group);
	if (!smmu_group) {
		smmu = arm_smmu_get_for_pci_dev(pdev);
		if (!smmu) {
			ret = -ENOENT;
			goto out_put_group;
		}

		smmu_group = kzalloc(sizeof(*smmu_group), GFP_KERNEL);
		if (!smmu_group) {
			ret = -ENOMEM;
			goto out_put_group;
		}

		smmu_group->ste.valid	= true;
		smmu_group->smmu	= smmu;
		iommu_group_set_iommudata(group, smmu_group,
					  __arm_smmu_release_pci_iommudata);
	} else {
		smmu = smmu_group->smmu;
	}

	/* Assume SID == RID until firmware tells us otherwise */
	pci_for_each_dma_alias(pdev, __arm_smmu_get_pci_sid, &sid);
	for (i = 0; i < smmu_group->num_sids; ++i) {
		/* If we already know about this SID, then we're done */
		if (smmu_group->sids[i] == sid)
			return 0;
	}

	/* Check the SID is in range of the SMMU and our stream table */
	if (!arm_smmu_sid_in_range(smmu, sid)) {
		ret = -ERANGE;
		goto out_put_group;
	}

	/* Ensure l2 strtab is initialised */
	if (smmu->features & ARM_SMMU_FEAT_2_LVL_STRTAB) {
		ret = arm_smmu_init_l2_strtab(smmu, sid);
		if (ret)
			goto out_put_group;
	}

	/* Resize the SID array for the group */
	smmu_group->num_sids++;
	sids = krealloc(smmu_group->sids, smmu_group->num_sids * sizeof(*sids),
			GFP_KERNEL);
	if (!sids) {
		smmu_group->num_sids--;
		ret = -ENOMEM;
		goto out_put_group;
	}

	/* Add the new SID */
	sids[smmu_group->num_sids - 1] = sid;
	smmu_group->sids = sids;
	return 0;

out_put_group:
	iommu_group_put(group);
	return ret;
}

static void arm_smmu_remove_device(struct device *dev)
{
	iommu_group_remove_device(dev);
}

static int arm_smmu_domain_get_attr(struct iommu_domain *domain,
				    enum iommu_attr attr, void *data)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);

	switch (attr) {
	case DOMAIN_ATTR_NESTING:
		*(int *)data = (smmu_domain->stage == ARM_SMMU_DOMAIN_NESTED);
		return 0;
	default:
		return -ENODEV;
	}
}

static int arm_smmu_domain_set_attr(struct iommu_domain *domain,
				    enum iommu_attr attr, void *data)
{
	int ret = 0;
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);

	mutex_lock(&smmu_domain->init_mutex);

	switch (attr) {
	case DOMAIN_ATTR_NESTING:
		if (smmu_domain->smmu) {
			ret = -EPERM;
			goto out_unlock;
		}

		if (*(int *)data)
			smmu_domain->stage = ARM_SMMU_DOMAIN_NESTED;
		else
			smmu_domain->stage = ARM_SMMU_DOMAIN_S1;

		break;
	default:
		ret = -ENODEV;
	}

out_unlock:
	mutex_unlock(&smmu_domain->init_mutex);
	return ret;
}

static struct iommu_ops arm_smmu_ops = {
	.capable		= arm_smmu_capable,
	.domain_alloc		= arm_smmu_domain_alloc,
	.domain_free		= arm_smmu_domain_free,
	.attach_dev		= arm_smmu_attach_dev,
	.detach_dev		= arm_smmu_detach_dev,
	.map			= arm_smmu_map,
	.unmap			= arm_smmu_unmap,
	.iova_to_phys		= arm_smmu_iova_to_phys,
	.add_device		= arm_smmu_add_device,
	.remove_device		= arm_smmu_remove_device,
	.device_group		= pci_device_group,
	.domain_get_attr	= arm_smmu_domain_get_attr,
	.domain_set_attr	= arm_smmu_domain_set_attr,
	.pgsize_bitmap		= -1UL, /* Restricted during device attach */
};

/* Probing and initialisation functions */
static int arm_smmu_init_one_queue(struct arm_smmu_device *smmu,
				   struct arm_smmu_queue *q,
				   unsigned long prod_off,
				   unsigned long cons_off,
				   size_t dwords)
{
	size_t qsz = ((1 << q->max_n_shift) * dwords) << 3;

	q->base = dma_alloc_coherent(smmu->dev, qsz, &q->base_dma, GFP_KERNEL);
	if (!q->base) {
		dev_err(smmu->dev, "failed to allocate queue (0x%zx bytes)\n",
			qsz);
		return -ENOMEM;
	}

	q->prod_reg	= smmu->base + prod_off;
	q->cons_reg	= smmu->base + cons_off;
	q->ent_dwords	= dwords;

	q->q_base  = Q_BASE_RWA;
	q->q_base |= q->base_dma & Q_BASE_ADDR_MASK << Q_BASE_ADDR_SHIFT;
	q->q_base |= (q->max_n_shift & Q_BASE_LOG2SIZE_MASK)
		     << Q_BASE_LOG2SIZE_SHIFT;

	q->prod = q->cons = 0;
	return 0;
}

static void arm_smmu_free_one_queue(struct arm_smmu_device *smmu,
				    struct arm_smmu_queue *q)
{
	size_t qsz = ((1 << q->max_n_shift) * q->ent_dwords) << 3;

	dma_free_coherent(smmu->dev, qsz, q->base, q->base_dma);
}

static void arm_smmu_free_queues(struct arm_smmu_device *smmu)
{
	arm_smmu_free_one_queue(smmu, &smmu->cmdq.q);
	arm_smmu_free_one_queue(smmu, &smmu->evtq.q);

	if (smmu->features & ARM_SMMU_FEAT_PRI)
		arm_smmu_free_one_queue(smmu, &smmu->priq.q);
}

static int arm_smmu_init_queues(struct arm_smmu_device *smmu)
{
	int ret;

	/* cmdq */
	spin_lock_init(&smmu->cmdq.lock);
	ret = arm_smmu_init_one_queue(smmu, &smmu->cmdq.q, ARM_SMMU_CMDQ_PROD,
				      ARM_SMMU_CMDQ_CONS, CMDQ_ENT_DWORDS);
	if (ret)
		goto out;

	/* evtq */
	ret = arm_smmu_init_one_queue(smmu, &smmu->evtq.q, ARM_SMMU_EVTQ_PROD,
				      ARM_SMMU_EVTQ_CONS, EVTQ_ENT_DWORDS);
	if (ret)
		goto out_free_cmdq;

	/* priq */
	if (!(smmu->features & ARM_SMMU_FEAT_PRI))
		return 0;

	ret = arm_smmu_init_one_queue(smmu, &smmu->priq.q, ARM_SMMU_PRIQ_PROD,
				      ARM_SMMU_PRIQ_CONS, PRIQ_ENT_DWORDS);
	if (ret)
		goto out_free_evtq;

	return 0;

out_free_evtq:
	arm_smmu_free_one_queue(smmu, &smmu->evtq.q);
out_free_cmdq:
	arm_smmu_free_one_queue(smmu, &smmu->cmdq.q);
out:
	return ret;
}

static void arm_smmu_free_l2_strtab(struct arm_smmu_device *smmu)
{
	int i;
	size_t size;
	struct arm_smmu_strtab_cfg *cfg = &smmu->strtab_cfg;

	size = 1 << (STRTAB_SPLIT + ilog2(STRTAB_STE_DWORDS) + 3);
	for (i = 0; i < cfg->num_l1_ents; ++i) {
		struct arm_smmu_strtab_l1_desc *desc = &cfg->l1_desc[i];

		if (!desc->l2ptr)
			continue;

		dma_free_coherent(smmu->dev, size, desc->l2ptr,
				  desc->l2ptr_dma);
	}
}

static int arm_smmu_init_l1_strtab(struct arm_smmu_device *smmu)
{
	unsigned int i;
	struct arm_smmu_strtab_cfg *cfg = &smmu->strtab_cfg;
	size_t size = sizeof(*cfg->l1_desc) * cfg->num_l1_ents;
	void *strtab = smmu->strtab_cfg.strtab;

	cfg->l1_desc = devm_kzalloc(smmu->dev, size, GFP_KERNEL);
	if (!cfg->l1_desc) {
		dev_err(smmu->dev, "failed to allocate l1 stream table desc\n");
		return -ENOMEM;
	}

	for (i = 0; i < cfg->num_l1_ents; ++i) {
		arm_smmu_write_strtab_l1_desc(strtab, &cfg->l1_desc[i]);
		strtab += STRTAB_L1_DESC_DWORDS << 3;
	}

	return 0;
}

static int arm_smmu_init_strtab_2lvl(struct arm_smmu_device *smmu)
{
	void *strtab;
	u64 reg;
	u32 size, l1size;
	int ret;
	struct arm_smmu_strtab_cfg *cfg = &smmu->strtab_cfg;

	/*
	 * If we can resolve everything with a single L2 table, then we
	 * just need a single L1 descriptor. Otherwise, calculate the L1
	 * size, capped to the SIDSIZE.
	 */
	if (smmu->sid_bits < STRTAB_SPLIT) {
		size = 0;
	} else {
		size = STRTAB_L1_SZ_SHIFT - (ilog2(STRTAB_L1_DESC_DWORDS) + 3);
		size = min(size, smmu->sid_bits - STRTAB_SPLIT);
	}
	cfg->num_l1_ents = 1 << size;

	size += STRTAB_SPLIT;
	if (size < smmu->sid_bits)
		dev_warn(smmu->dev,
			 "2-level strtab only covers %u/%u bits of SID\n",
			 size, smmu->sid_bits);

	l1size = cfg->num_l1_ents * (STRTAB_L1_DESC_DWORDS << 3);
	strtab = dma_zalloc_coherent(smmu->dev, l1size, &cfg->strtab_dma,
				     GFP_KERNEL);
	if (!strtab) {
		dev_err(smmu->dev,
			"failed to allocate l1 stream table (%u bytes)\n",
			size);
		return -ENOMEM;
	}
	cfg->strtab = strtab;

	/* Configure strtab_base_cfg for 2 levels */
	reg  = STRTAB_BASE_CFG_FMT_2LVL;
	reg |= (size & STRTAB_BASE_CFG_LOG2SIZE_MASK)
		<< STRTAB_BASE_CFG_LOG2SIZE_SHIFT;
	reg |= (STRTAB_SPLIT & STRTAB_BASE_CFG_SPLIT_MASK)
		<< STRTAB_BASE_CFG_SPLIT_SHIFT;
	cfg->strtab_base_cfg = reg;

	ret = arm_smmu_init_l1_strtab(smmu);
	if (ret)
		dma_free_coherent(smmu->dev,
				  l1size,
				  strtab,
				  cfg->strtab_dma);
	return ret;
}

static int arm_smmu_init_strtab_linear(struct arm_smmu_device *smmu)
{
	void *strtab;
	u64 reg;
	u32 size;
	struct arm_smmu_strtab_cfg *cfg = &smmu->strtab_cfg;

	size = (1 << smmu->sid_bits) * (STRTAB_STE_DWORDS << 3);
	strtab = dma_zalloc_coherent(smmu->dev, size, &cfg->strtab_dma,
				     GFP_KERNEL);
	if (!strtab) {
		dev_err(smmu->dev,
			"failed to allocate linear stream table (%u bytes)\n",
			size);
		return -ENOMEM;
	}
	cfg->strtab = strtab;
	cfg->num_l1_ents = 1 << smmu->sid_bits;

	/* Configure strtab_base_cfg for a linear table covering all SIDs */
	reg  = STRTAB_BASE_CFG_FMT_LINEAR;
	reg |= (smmu->sid_bits & STRTAB_BASE_CFG_LOG2SIZE_MASK)
		<< STRTAB_BASE_CFG_LOG2SIZE_SHIFT;
	cfg->strtab_base_cfg = reg;

	arm_smmu_init_bypass_stes(strtab, cfg->num_l1_ents);
	return 0;
}

static int arm_smmu_init_strtab(struct arm_smmu_device *smmu)
{
	u64 reg;
	int ret;

	if (smmu->features & ARM_SMMU_FEAT_2_LVL_STRTAB)
		ret = arm_smmu_init_strtab_2lvl(smmu);
	else
		ret = arm_smmu_init_strtab_linear(smmu);

	if (ret)
		return ret;

	/* Set the strtab base address */
	reg  = smmu->strtab_cfg.strtab_dma &
	       STRTAB_BASE_ADDR_MASK << STRTAB_BASE_ADDR_SHIFT;
	reg |= STRTAB_BASE_RA;
	smmu->strtab_cfg.strtab_base = reg;

	/* Allocate the first VMID for stage-2 bypass STEs */
	set_bit(0, smmu->vmid_map);
	return 0;
}

static void arm_smmu_free_strtab(struct arm_smmu_device *smmu)
{
	struct arm_smmu_strtab_cfg *cfg = &smmu->strtab_cfg;
	u32 size = cfg->num_l1_ents;

	if (smmu->features & ARM_SMMU_FEAT_2_LVL_STRTAB) {
		arm_smmu_free_l2_strtab(smmu);
		size *= STRTAB_L1_DESC_DWORDS << 3;
	} else {
		size *= STRTAB_STE_DWORDS * 3;
	}

	dma_free_coherent(smmu->dev, size, cfg->strtab, cfg->strtab_dma);
}

static int arm_smmu_init_structures(struct arm_smmu_device *smmu)
{
	int ret;

	ret = arm_smmu_init_queues(smmu);
	if (ret)
		return ret;

	ret = arm_smmu_init_strtab(smmu);
	if (ret)
		goto out_free_queues;

	return 0;

out_free_queues:
	arm_smmu_free_queues(smmu);
	return ret;
}

static void arm_smmu_free_structures(struct arm_smmu_device *smmu)
{
	arm_smmu_free_strtab(smmu);
	arm_smmu_free_queues(smmu);
}

static int arm_smmu_write_reg_sync(struct arm_smmu_device *smmu, u32 val,
				   unsigned int reg_off, unsigned int ack_off)
{
	u32 reg;

	writel_relaxed(val, smmu->base + reg_off);
	return readl_relaxed_poll_timeout(smmu->base + ack_off, reg, reg == val,
					  1, ARM_SMMU_POLL_TIMEOUT_US);
}

static void arm_smmu_free_msis(void *data)
{
	struct device *dev = data;
	platform_msi_domain_free_irqs(dev);
}

static void arm_smmu_write_msi_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	phys_addr_t doorbell;
	struct device *dev = msi_desc_to_dev(desc);
	struct arm_smmu_device *smmu = dev_get_drvdata(dev);
	phys_addr_t *cfg = arm_smmu_msi_cfg[desc->platform.msi_index];

	doorbell = (((u64)msg->address_hi) << 32) | msg->address_lo;
	doorbell &= MSI_CFG0_ADDR_MASK << MSI_CFG0_ADDR_SHIFT;

	writeq_relaxed(doorbell, smmu->base + cfg[0]);
	writel_relaxed(msg->data, smmu->base + cfg[1]);
	writel_relaxed(MSI_CFG2_MEMATTR_DEVICE_nGnRE, smmu->base + cfg[2]);
}

static void arm_smmu_setup_msis(struct arm_smmu_device *smmu)
{
	struct msi_desc *desc;
	int ret, nvec = ARM_SMMU_MAX_MSIS;
	struct device *dev = smmu->dev;

	/* Clear the MSI address regs */
	writeq_relaxed(0, smmu->base + ARM_SMMU_GERROR_IRQ_CFG0);
	writeq_relaxed(0, smmu->base + ARM_SMMU_EVTQ_IRQ_CFG0);

	if (smmu->features & ARM_SMMU_FEAT_PRI)
		writeq_relaxed(0, smmu->base + ARM_SMMU_PRIQ_IRQ_CFG0);
	else
		nvec--;

	if (!(smmu->features & ARM_SMMU_FEAT_MSI))
		return;

	/* Allocate MSIs for evtq, gerror and priq. Ignore cmdq */
	ret = platform_msi_domain_alloc_irqs(dev, nvec, arm_smmu_write_msi_msg);
	if (ret) {
		dev_warn(dev, "failed to allocate MSIs\n");
		return;
	}

	for_each_msi_entry(desc, dev) {
		switch (desc->platform.msi_index) {
		case EVTQ_MSI_INDEX:
			smmu->evtq.q.irq = desc->irq;
			break;
		case GERROR_MSI_INDEX:
			smmu->gerr_irq = desc->irq;
			break;
		case PRIQ_MSI_INDEX:
			smmu->priq.q.irq = desc->irq;
			break;
		default:	/* Unknown */
			continue;
		}
	}

	/* Add callback to free MSIs on teardown */
	devm_add_action(dev, arm_smmu_free_msis, dev);
}

static int arm_smmu_setup_irqs(struct arm_smmu_device *smmu)
{
	int ret, irq;
	u32 irqen_flags = IRQ_CTRL_EVTQ_IRQEN | IRQ_CTRL_GERROR_IRQEN;

	/* Disable IRQs first */
	ret = arm_smmu_write_reg_sync(smmu, 0, ARM_SMMU_IRQ_CTRL,
				      ARM_SMMU_IRQ_CTRLACK);
	if (ret) {
		dev_err(smmu->dev, "failed to disable irqs\n");
		return ret;
	}

	arm_smmu_setup_msis(smmu);

	/* Request interrupt lines */
	irq = smmu->evtq.q.irq;
	if (irq) {
		ret = devm_request_threaded_irq(smmu->dev, irq,
						arm_smmu_evtq_handler,
						arm_smmu_evtq_thread,
						0, "arm-smmu-v3-evtq", smmu);
		if (IS_ERR_VALUE(ret))
			dev_warn(smmu->dev, "failed to enable evtq irq\n");
	}

	irq = smmu->cmdq.q.irq;
	if (irq) {
		ret = devm_request_irq(smmu->dev, irq,
				       arm_smmu_cmdq_sync_handler, 0,
				       "arm-smmu-v3-cmdq-sync", smmu);
		if (IS_ERR_VALUE(ret))
			dev_warn(smmu->dev, "failed to enable cmdq-sync irq\n");
	}

	irq = smmu->gerr_irq;
	if (irq) {
		ret = devm_request_irq(smmu->dev, irq, arm_smmu_gerror_handler,
				       0, "arm-smmu-v3-gerror", smmu);
		if (IS_ERR_VALUE(ret))
			dev_warn(smmu->dev, "failed to enable gerror irq\n");
	}

	if (smmu->features & ARM_SMMU_FEAT_PRI) {
		irq = smmu->priq.q.irq;
		if (irq) {
			ret = devm_request_threaded_irq(smmu->dev, irq,
							arm_smmu_priq_handler,
							arm_smmu_priq_thread,
							0, "arm-smmu-v3-priq",
							smmu);
			if (IS_ERR_VALUE(ret))
				dev_warn(smmu->dev,
					 "failed to enable priq irq\n");
			else
				irqen_flags |= IRQ_CTRL_PRIQ_IRQEN;
		}
	}

	/* Enable interrupt generation on the SMMU */
	ret = arm_smmu_write_reg_sync(smmu, irqen_flags,
				      ARM_SMMU_IRQ_CTRL, ARM_SMMU_IRQ_CTRLACK);
	if (ret)
		dev_warn(smmu->dev, "failed to enable irqs\n");

	return 0;
}

static int arm_smmu_device_disable(struct arm_smmu_device *smmu)
{
	int ret;

	ret = arm_smmu_write_reg_sync(smmu, 0, ARM_SMMU_CR0, ARM_SMMU_CR0ACK);
	if (ret)
		dev_err(smmu->dev, "failed to clear cr0\n");

	return ret;
}

static int arm_smmu_device_reset(struct arm_smmu_device *smmu)
{
	int ret;
	u32 reg, enables;
	struct arm_smmu_cmdq_ent cmd;

	/* Clear CR0 and sync (disables SMMU and queue processing) */
	reg = readl_relaxed(smmu->base + ARM_SMMU_CR0);
	if (reg & CR0_SMMUEN)
		dev_warn(smmu->dev, "SMMU currently enabled! Resetting...\n");

	ret = arm_smmu_device_disable(smmu);
	if (ret)
		return ret;

	/* CR1 (table and queue memory attributes) */
	reg = (CR1_SH_ISH << CR1_TABLE_SH_SHIFT) |
	      (CR1_CACHE_WB << CR1_TABLE_OC_SHIFT) |
	      (CR1_CACHE_WB << CR1_TABLE_IC_SHIFT) |
	      (CR1_SH_ISH << CR1_QUEUE_SH_SHIFT) |
	      (CR1_CACHE_WB << CR1_QUEUE_OC_SHIFT) |
	      (CR1_CACHE_WB << CR1_QUEUE_IC_SHIFT);
	writel_relaxed(reg, smmu->base + ARM_SMMU_CR1);

	/* CR2 (random crap) */
	reg = CR2_PTM | CR2_RECINVSID | CR2_E2H;
	writel_relaxed(reg, smmu->base + ARM_SMMU_CR2);

	/* Stream table */
	writeq_relaxed(smmu->strtab_cfg.strtab_base,
		       smmu->base + ARM_SMMU_STRTAB_BASE);
	writel_relaxed(smmu->strtab_cfg.strtab_base_cfg,
		       smmu->base + ARM_SMMU_STRTAB_BASE_CFG);

	/* Command queue */
	writeq_relaxed(smmu->cmdq.q.q_base, smmu->base + ARM_SMMU_CMDQ_BASE);
	writel_relaxed(smmu->cmdq.q.prod, smmu->base + ARM_SMMU_CMDQ_PROD);
	writel_relaxed(smmu->cmdq.q.cons, smmu->base + ARM_SMMU_CMDQ_CONS);

	enables = CR0_CMDQEN;
	ret = arm_smmu_write_reg_sync(smmu, enables, ARM_SMMU_CR0,
				      ARM_SMMU_CR0ACK);
	if (ret) {
		dev_err(smmu->dev, "failed to enable command queue\n");
		return ret;
	}

	/* Invalidate any cached configuration */
	cmd.opcode = CMDQ_OP_CFGI_ALL;
	arm_smmu_cmdq_issue_cmd(smmu, &cmd);
	cmd.opcode = CMDQ_OP_CMD_SYNC;
	arm_smmu_cmdq_issue_cmd(smmu, &cmd);

	/* Invalidate any stale TLB entries */
	if (smmu->features & ARM_SMMU_FEAT_HYP) {
		cmd.opcode = CMDQ_OP_TLBI_EL2_ALL;
		arm_smmu_cmdq_issue_cmd(smmu, &cmd);
	}

	cmd.opcode = CMDQ_OP_TLBI_NSNH_ALL;
	arm_smmu_cmdq_issue_cmd(smmu, &cmd);
	cmd.opcode = CMDQ_OP_CMD_SYNC;
	arm_smmu_cmdq_issue_cmd(smmu, &cmd);

	/* Event queue */
	writeq_relaxed(smmu->evtq.q.q_base, smmu->base + ARM_SMMU_EVTQ_BASE);
	writel_relaxed(smmu->evtq.q.prod, smmu->base + ARM_SMMU_EVTQ_PROD);
	writel_relaxed(smmu->evtq.q.cons, smmu->base + ARM_SMMU_EVTQ_CONS);

	enables |= CR0_EVTQEN;
	ret = arm_smmu_write_reg_sync(smmu, enables, ARM_SMMU_CR0,
				      ARM_SMMU_CR0ACK);
	if (ret) {
		dev_err(smmu->dev, "failed to enable event queue\n");
		return ret;
	}

	/* PRI queue */
	if (smmu->features & ARM_SMMU_FEAT_PRI) {
		writeq_relaxed(smmu->priq.q.q_base,
			       smmu->base + ARM_SMMU_PRIQ_BASE);
		writel_relaxed(smmu->priq.q.prod,
			       smmu->base + ARM_SMMU_PRIQ_PROD);
		writel_relaxed(smmu->priq.q.cons,
			       smmu->base + ARM_SMMU_PRIQ_CONS);

		enables |= CR0_PRIQEN;
		ret = arm_smmu_write_reg_sync(smmu, enables, ARM_SMMU_CR0,
					      ARM_SMMU_CR0ACK);
		if (ret) {
			dev_err(smmu->dev, "failed to enable PRI queue\n");
			return ret;
		}
	}

	ret = arm_smmu_setup_irqs(smmu);
	if (ret) {
		dev_err(smmu->dev, "failed to setup irqs\n");
		return ret;
	}

	/* Enable the SMMU interface */
	enables |= CR0_SMMUEN;
	ret = arm_smmu_write_reg_sync(smmu, enables, ARM_SMMU_CR0,
				      ARM_SMMU_CR0ACK);
	if (ret) {
		dev_err(smmu->dev, "failed to enable SMMU interface\n");
		return ret;
	}

	return 0;
}

static int arm_smmu_device_probe(struct arm_smmu_device *smmu)
{
	u32 reg;
	bool coherent;
	unsigned long pgsize_bitmap = 0;

	/* IDR0 */
	reg = readl_relaxed(smmu->base + ARM_SMMU_IDR0);

	/* 2-level structures */
	if ((reg & IDR0_ST_LVL_MASK << IDR0_ST_LVL_SHIFT) == IDR0_ST_LVL_2LVL)
		smmu->features |= ARM_SMMU_FEAT_2_LVL_STRTAB;

	if (reg & IDR0_CD2L)
		smmu->features |= ARM_SMMU_FEAT_2_LVL_CDTAB;

	/*
	 * Translation table endianness.
	 * We currently require the same endianness as the CPU, but this
	 * could be changed later by adding a new IO_PGTABLE_QUIRK.
	 */
	switch (reg & IDR0_TTENDIAN_MASK << IDR0_TTENDIAN_SHIFT) {
	case IDR0_TTENDIAN_MIXED:
		smmu->features |= ARM_SMMU_FEAT_TT_LE | ARM_SMMU_FEAT_TT_BE;
		break;
#ifdef __BIG_ENDIAN
	case IDR0_TTENDIAN_BE:
		smmu->features |= ARM_SMMU_FEAT_TT_BE;
		break;
#else
	case IDR0_TTENDIAN_LE:
		smmu->features |= ARM_SMMU_FEAT_TT_LE;
		break;
#endif
	default:
		dev_err(smmu->dev, "unknown/unsupported TT endianness!\n");
		return -ENXIO;
	}

	/* Boolean feature flags */
	if (IS_ENABLED(CONFIG_PCI_PRI) && reg & IDR0_PRI)
		smmu->features |= ARM_SMMU_FEAT_PRI;

	if (IS_ENABLED(CONFIG_PCI_ATS) && reg & IDR0_ATS)
		smmu->features |= ARM_SMMU_FEAT_ATS;

	if (reg & IDR0_SEV)
		smmu->features |= ARM_SMMU_FEAT_SEV;

	if (reg & IDR0_MSI)
		smmu->features |= ARM_SMMU_FEAT_MSI;

	if (reg & IDR0_HYP)
		smmu->features |= ARM_SMMU_FEAT_HYP;

	/*
	 * The dma-coherent property is used in preference to the ID
	 * register, but warn on mismatch.
	 */
	coherent = of_dma_is_coherent(smmu->dev->of_node);
	if (coherent)
		smmu->features |= ARM_SMMU_FEAT_COHERENCY;

	if (!!(reg & IDR0_COHACC) != coherent)
		dev_warn(smmu->dev, "IDR0.COHACC overridden by dma-coherent property (%s)\n",
			 coherent ? "true" : "false");

	if (reg & IDR0_STALL_MODEL)
		smmu->features |= ARM_SMMU_FEAT_STALLS;

	if (reg & IDR0_S1P)
		smmu->features |= ARM_SMMU_FEAT_TRANS_S1;

	if (reg & IDR0_S2P)
		smmu->features |= ARM_SMMU_FEAT_TRANS_S2;

	if (!(reg & (IDR0_S1P | IDR0_S2P))) {
		dev_err(smmu->dev, "no translation support!\n");
		return -ENXIO;
	}

	/* We only support the AArch64 table format at present */
	switch (reg & IDR0_TTF_MASK << IDR0_TTF_SHIFT) {
	case IDR0_TTF_AARCH32_64:
		smmu->ias = 40;
		/* Fallthrough */
	case IDR0_TTF_AARCH64:
		break;
	default:
		dev_err(smmu->dev, "AArch64 table format not supported!\n");
		return -ENXIO;
	}

	/* ASID/VMID sizes */
	smmu->asid_bits = reg & IDR0_ASID16 ? 16 : 8;
	smmu->vmid_bits = reg & IDR0_VMID16 ? 16 : 8;

	/* IDR1 */
	reg = readl_relaxed(smmu->base + ARM_SMMU_IDR1);
	if (reg & (IDR1_TABLES_PRESET | IDR1_QUEUES_PRESET | IDR1_REL)) {
		dev_err(smmu->dev, "embedded implementation not supported\n");
		return -ENXIO;
	}

	/* Queue sizes, capped at 4k */
	smmu->cmdq.q.max_n_shift = min((u32)CMDQ_MAX_SZ_SHIFT,
				       reg >> IDR1_CMDQ_SHIFT & IDR1_CMDQ_MASK);
	if (!smmu->cmdq.q.max_n_shift) {
		/* Odd alignment restrictions on the base, so ignore for now */
		dev_err(smmu->dev, "unit-length command queue not supported\n");
		return -ENXIO;
	}

	smmu->evtq.q.max_n_shift = min((u32)EVTQ_MAX_SZ_SHIFT,
				       reg >> IDR1_EVTQ_SHIFT & IDR1_EVTQ_MASK);
	smmu->priq.q.max_n_shift = min((u32)PRIQ_MAX_SZ_SHIFT,
				       reg >> IDR1_PRIQ_SHIFT & IDR1_PRIQ_MASK);

	/* SID/SSID sizes */
	smmu->ssid_bits = reg >> IDR1_SSID_SHIFT & IDR1_SSID_MASK;
	smmu->sid_bits = reg >> IDR1_SID_SHIFT & IDR1_SID_MASK;

	/* IDR5 */
	reg = readl_relaxed(smmu->base + ARM_SMMU_IDR5);

	/* Maximum number of outstanding stalls */
	smmu->evtq.max_stalls = reg >> IDR5_STALL_MAX_SHIFT
				& IDR5_STALL_MAX_MASK;

	/* Page sizes */
	if (reg & IDR5_GRAN64K)
		pgsize_bitmap |= SZ_64K | SZ_512M;
	if (reg & IDR5_GRAN16K)
		pgsize_bitmap |= SZ_16K | SZ_32M;
	if (reg & IDR5_GRAN4K)
		pgsize_bitmap |= SZ_4K | SZ_2M | SZ_1G;

	arm_smmu_ops.pgsize_bitmap &= pgsize_bitmap;

	/* Output address size */
	switch (reg & IDR5_OAS_MASK << IDR5_OAS_SHIFT) {
	case IDR5_OAS_32_BIT:
		smmu->oas = 32;
		break;
	case IDR5_OAS_36_BIT:
		smmu->oas = 36;
		break;
	case IDR5_OAS_40_BIT:
		smmu->oas = 40;
		break;
	case IDR5_OAS_42_BIT:
		smmu->oas = 42;
		break;
	case IDR5_OAS_44_BIT:
		smmu->oas = 44;
		break;
	default:
		dev_info(smmu->dev,
			"unknown output address size. Truncating to 48-bit\n");
		/* Fallthrough */
	case IDR5_OAS_48_BIT:
		smmu->oas = 48;
	}

	/* Set the DMA mask for our table walker */
	if (dma_set_mask_and_coherent(smmu->dev, DMA_BIT_MASK(smmu->oas)))
		dev_warn(smmu->dev,
			 "failed to set DMA mask for table walker\n");

	smmu->ias = max(smmu->ias, smmu->oas);

	dev_info(smmu->dev, "ias %lu-bit, oas %lu-bit (features 0x%08x)\n",
		 smmu->ias, smmu->oas, smmu->features);
	return 0;
}

static int arm_smmu_device_dt_probe(struct platform_device *pdev)
{
	int irq, ret;
	struct resource *res;
	struct arm_smmu_device *smmu;
	struct device *dev = &pdev->dev;

	smmu = devm_kzalloc(dev, sizeof(*smmu), GFP_KERNEL);
	if (!smmu) {
		dev_err(dev, "failed to allocate arm_smmu_device\n");
		return -ENOMEM;
	}
	smmu->dev = dev;

	/* Base address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (resource_size(res) + 1 < SZ_128K) {
		dev_err(dev, "MMIO region too small (%pr)\n", res);
		return -EINVAL;
	}

	smmu->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(smmu->base))
		return PTR_ERR(smmu->base);

	/* Interrupt lines */
	irq = platform_get_irq_byname(pdev, "eventq");
	if (irq > 0)
		smmu->evtq.q.irq = irq;

	irq = platform_get_irq_byname(pdev, "priq");
	if (irq > 0)
		smmu->priq.q.irq = irq;

	irq = platform_get_irq_byname(pdev, "cmdq-sync");
	if (irq > 0)
		smmu->cmdq.q.irq = irq;

	irq = platform_get_irq_byname(pdev, "gerror");
	if (irq > 0)
		smmu->gerr_irq = irq;

	parse_driver_options(smmu);

	/* Probe the h/w */
	ret = arm_smmu_device_probe(smmu);
	if (ret)
		return ret;

	/* Initialise in-memory data structures */
	ret = arm_smmu_init_structures(smmu);
	if (ret)
		return ret;

	/* Record our private device structure */
	platform_set_drvdata(pdev, smmu);

	/* Reset the device */
	ret = arm_smmu_device_reset(smmu);
	if (ret)
		goto out_free_structures;

	return 0;

out_free_structures:
	arm_smmu_free_structures(smmu);
	return ret;
}

static int arm_smmu_device_remove(struct platform_device *pdev)
{
	struct arm_smmu_device *smmu = platform_get_drvdata(pdev);

	arm_smmu_device_disable(smmu);
	arm_smmu_free_structures(smmu);
	return 0;
}

static struct of_device_id arm_smmu_of_match[] = {
	{ .compatible = "arm,smmu-v3", },
	{ },
};
MODULE_DEVICE_TABLE(of, arm_smmu_of_match);

static struct platform_driver arm_smmu_driver = {
	.driver	= {
		.name		= "arm-smmu-v3",
		.of_match_table	= of_match_ptr(arm_smmu_of_match),
	},
	.probe	= arm_smmu_device_dt_probe,
	.remove	= arm_smmu_device_remove,
};

static int __init arm_smmu_init(void)
{
	struct device_node *np;
	int ret;

	np = of_find_matching_node(NULL, arm_smmu_of_match);
	if (!np)
		return 0;

	of_node_put(np);

	ret = platform_driver_register(&arm_smmu_driver);
	if (ret)
		return ret;

	return bus_set_iommu(&pci_bus_type, &arm_smmu_ops);
}

static void __exit arm_smmu_exit(void)
{
	return platform_driver_unregister(&arm_smmu_driver);
}

subsys_initcall(arm_smmu_init);
module_exit(arm_smmu_exit);

MODULE_DESCRIPTION("IOMMU API for ARM architected SMMUv3 implementations");
MODULE_AUTHOR("Will Deacon <will.deacon@arm.com>");
MODULE_LICENSE("GPL v2");
