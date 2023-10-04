// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#include <linux/circ_buf.h>
#include <linux/highmem.h>

#include "ivpu_drv.h"
#include "ivpu_hw_reg_io.h"
#include "ivpu_mmu.h"
#include "ivpu_mmu_context.h"
#include "ivpu_pm.h"

#define IVPU_MMU_REG_IDR0		      0x00200000u
#define IVPU_MMU_REG_IDR1		      0x00200004u
#define IVPU_MMU_REG_IDR3		      0x0020000cu
#define IVPU_MMU_REG_IDR5		      0x00200014u
#define IVPU_MMU_REG_CR0		      0x00200020u
#define IVPU_MMU_REG_CR0ACK		      0x00200024u
#define IVPU_MMU_REG_CR1		      0x00200028u
#define IVPU_MMU_REG_CR2		      0x0020002cu
#define IVPU_MMU_REG_IRQ_CTRL		      0x00200050u
#define IVPU_MMU_REG_IRQ_CTRLACK	      0x00200054u

#define IVPU_MMU_REG_GERROR		      0x00200060u
#define IVPU_MMU_REG_GERROR_CMDQ_MASK	      BIT_MASK(0)
#define IVPU_MMU_REG_GERROR_EVTQ_ABT_MASK     BIT_MASK(2)
#define IVPU_MMU_REG_GERROR_PRIQ_ABT_MASK     BIT_MASK(3)
#define IVPU_MMU_REG_GERROR_MSI_CMDQ_ABT_MASK BIT_MASK(4)
#define IVPU_MMU_REG_GERROR_MSI_EVTQ_ABT_MASK BIT_MASK(5)
#define IVPU_MMU_REG_GERROR_MSI_PRIQ_ABT_MASK BIT_MASK(6)
#define IVPU_MMU_REG_GERROR_MSI_ABT_MASK      BIT_MASK(7)

#define IVPU_MMU_REG_GERRORN		      0x00200064u

#define IVPU_MMU_REG_STRTAB_BASE	      0x00200080u
#define IVPU_MMU_REG_STRTAB_BASE_CFG	      0x00200088u
#define IVPU_MMU_REG_CMDQ_BASE		      0x00200090u
#define IVPU_MMU_REG_CMDQ_PROD		      0x00200098u
#define IVPU_MMU_REG_CMDQ_CONS		      0x0020009cu
#define IVPU_MMU_REG_EVTQ_BASE		      0x002000a0u
#define IVPU_MMU_REG_EVTQ_PROD		      0x002000a8u
#define IVPU_MMU_REG_EVTQ_CONS		      0x002000acu
#define IVPU_MMU_REG_EVTQ_PROD_SEC	      (0x002000a8u + SZ_64K)
#define IVPU_MMU_REG_EVTQ_CONS_SEC	      (0x002000acu + SZ_64K)
#define IVPU_MMU_REG_CMDQ_CONS_ERR_MASK	      GENMASK(30, 24)

#define IVPU_MMU_IDR0_REF		0x080f3e0f
#define IVPU_MMU_IDR0_REF_SIMICS	0x080f3e1f
#define IVPU_MMU_IDR1_REF		0x0e739d18
#define IVPU_MMU_IDR3_REF		0x0000003c
#define IVPU_MMU_IDR5_REF		0x00040070
#define IVPU_MMU_IDR5_REF_SIMICS	0x00000075
#define IVPU_MMU_IDR5_REF_FPGA		0x00800075

#define IVPU_MMU_CDTAB_ENT_SIZE		64
#define IVPU_MMU_CDTAB_ENT_COUNT_LOG2	8 /* 256 entries */
#define IVPU_MMU_CDTAB_ENT_COUNT	((u32)1 << IVPU_MMU_CDTAB_ENT_COUNT_LOG2)

#define IVPU_MMU_STREAM_ID0		0
#define IVPU_MMU_STREAM_ID3		3

#define IVPU_MMU_STRTAB_ENT_SIZE	64
#define IVPU_MMU_STRTAB_ENT_COUNT	4
#define IVPU_MMU_STRTAB_CFG_LOG2SIZE	2
#define IVPU_MMU_STRTAB_CFG		IVPU_MMU_STRTAB_CFG_LOG2SIZE

#define IVPU_MMU_Q_COUNT_LOG2		4 /* 16 entries */
#define IVPU_MMU_Q_COUNT		((u32)1 << IVPU_MMU_Q_COUNT_LOG2)
#define IVPU_MMU_Q_WRAP_BIT		(IVPU_MMU_Q_COUNT << 1)
#define IVPU_MMU_Q_WRAP_MASK		(IVPU_MMU_Q_WRAP_BIT - 1)
#define IVPU_MMU_Q_IDX_MASK		(IVPU_MMU_Q_COUNT - 1)
#define IVPU_MMU_Q_IDX(val)		((val) & IVPU_MMU_Q_IDX_MASK)

#define IVPU_MMU_CMDQ_CMD_SIZE		16
#define IVPU_MMU_CMDQ_SIZE		(IVPU_MMU_Q_COUNT * IVPU_MMU_CMDQ_CMD_SIZE)

#define IVPU_MMU_EVTQ_CMD_SIZE		32
#define IVPU_MMU_EVTQ_SIZE		(IVPU_MMU_Q_COUNT * IVPU_MMU_EVTQ_CMD_SIZE)

#define IVPU_MMU_CMD_OPCODE		GENMASK(7, 0)

#define IVPU_MMU_CMD_SYNC_0_CS		GENMASK(13, 12)
#define IVPU_MMU_CMD_SYNC_0_MSH		GENMASK(23, 22)
#define IVPU_MMU_CMD_SYNC_0_MSI_ATTR	GENMASK(27, 24)
#define IVPU_MMU_CMD_SYNC_0_MSI_ATTR	GENMASK(27, 24)
#define IVPU_MMU_CMD_SYNC_0_MSI_DATA	GENMASK(63, 32)

#define IVPU_MMU_CMD_CFGI_0_SSEC	BIT(10)
#define IVPU_MMU_CMD_CFGI_0_SSV		BIT(11)
#define IVPU_MMU_CMD_CFGI_0_SSID	GENMASK(31, 12)
#define IVPU_MMU_CMD_CFGI_0_SID		GENMASK(63, 32)
#define IVPU_MMU_CMD_CFGI_1_RANGE	GENMASK(4, 0)

#define IVPU_MMU_CMD_TLBI_0_ASID	GENMASK(63, 48)
#define IVPU_MMU_CMD_TLBI_0_VMID	GENMASK(47, 32)

#define CMD_PREFETCH_CFG		0x1
#define CMD_CFGI_STE			0x3
#define CMD_CFGI_ALL			0x4
#define CMD_CFGI_CD			0x5
#define CMD_CFGI_CD_ALL			0x6
#define CMD_TLBI_NH_ASID		0x11
#define CMD_TLBI_EL2_ALL		0x20
#define CMD_TLBI_NSNH_ALL		0x30
#define CMD_SYNC			0x46

#define IVPU_MMU_EVT_F_UUT		0x01
#define IVPU_MMU_EVT_C_BAD_STREAMID	0x02
#define IVPU_MMU_EVT_F_STE_FETCH	0x03
#define IVPU_MMU_EVT_C_BAD_STE		0x04
#define IVPU_MMU_EVT_F_BAD_ATS_TREQ	0x05
#define IVPU_MMU_EVT_F_STREAM_DISABLED	0x06
#define IVPU_MMU_EVT_F_TRANSL_FORBIDDEN	0x07
#define IVPU_MMU_EVT_C_BAD_SUBSTREAMID	0x08
#define IVPU_MMU_EVT_F_CD_FETCH		0x09
#define IVPU_MMU_EVT_C_BAD_CD		0x0a
#define IVPU_MMU_EVT_F_WALK_EABT	0x0b
#define IVPU_MMU_EVT_F_TRANSLATION	0x10
#define IVPU_MMU_EVT_F_ADDR_SIZE	0x11
#define IVPU_MMU_EVT_F_ACCESS		0x12
#define IVPU_MMU_EVT_F_PERMISSION	0x13
#define IVPU_MMU_EVT_F_TLB_CONFLICT	0x20
#define IVPU_MMU_EVT_F_CFG_CONFLICT	0x21
#define IVPU_MMU_EVT_E_PAGE_REQUEST	0x24
#define IVPU_MMU_EVT_F_VMS_FETCH	0x25

#define IVPU_MMU_EVT_OP_MASK		GENMASK_ULL(7, 0)
#define IVPU_MMU_EVT_SSID_MASK		GENMASK_ULL(31, 12)

#define IVPU_MMU_Q_BASE_RWA		BIT(62)
#define IVPU_MMU_Q_BASE_ADDR_MASK	GENMASK_ULL(51, 5)
#define IVPU_MMU_STRTAB_BASE_RA		BIT(62)
#define IVPU_MMU_STRTAB_BASE_ADDR_MASK	GENMASK_ULL(51, 6)

#define IVPU_MMU_IRQ_EVTQ_EN		BIT(2)
#define IVPU_MMU_IRQ_GERROR_EN		BIT(0)

#define IVPU_MMU_CR0_ATSCHK		BIT(4)
#define IVPU_MMU_CR0_CMDQEN		BIT(3)
#define IVPU_MMU_CR0_EVTQEN		BIT(2)
#define IVPU_MMU_CR0_PRIQEN		BIT(1)
#define IVPU_MMU_CR0_SMMUEN		BIT(0)

#define IVPU_MMU_CR1_TABLE_SH		GENMASK(11, 10)
#define IVPU_MMU_CR1_TABLE_OC		GENMASK(9, 8)
#define IVPU_MMU_CR1_TABLE_IC		GENMASK(7, 6)
#define IVPU_MMU_CR1_QUEUE_SH		GENMASK(5, 4)
#define IVPU_MMU_CR1_QUEUE_OC		GENMASK(3, 2)
#define IVPU_MMU_CR1_QUEUE_IC		GENMASK(1, 0)
#define IVPU_MMU_CACHE_NC		0
#define IVPU_MMU_CACHE_WB		1
#define IVPU_MMU_CACHE_WT		2
#define IVPU_MMU_SH_NSH			0
#define IVPU_MMU_SH_OSH			2
#define IVPU_MMU_SH_ISH			3

#define IVPU_MMU_CMDQ_OP		GENMASK_ULL(7, 0)

#define IVPU_MMU_CD_0_TCR_T0SZ		GENMASK_ULL(5, 0)
#define IVPU_MMU_CD_0_TCR_TG0		GENMASK_ULL(7, 6)
#define IVPU_MMU_CD_0_TCR_IRGN0		GENMASK_ULL(9, 8)
#define IVPU_MMU_CD_0_TCR_ORGN0		GENMASK_ULL(11, 10)
#define IVPU_MMU_CD_0_TCR_SH0		GENMASK_ULL(13, 12)
#define IVPU_MMU_CD_0_TCR_EPD0		BIT_ULL(14)
#define IVPU_MMU_CD_0_TCR_EPD1		BIT_ULL(30)
#define IVPU_MMU_CD_0_ENDI		BIT(15)
#define IVPU_MMU_CD_0_V			BIT(31)
#define IVPU_MMU_CD_0_TCR_IPS		GENMASK_ULL(34, 32)
#define IVPU_MMU_CD_0_TCR_TBI0		BIT_ULL(38)
#define IVPU_MMU_CD_0_AA64		BIT(41)
#define IVPU_MMU_CD_0_S			BIT(44)
#define IVPU_MMU_CD_0_R			BIT(45)
#define IVPU_MMU_CD_0_A			BIT(46)
#define IVPU_MMU_CD_0_ASET		BIT(47)
#define IVPU_MMU_CD_0_ASID		GENMASK_ULL(63, 48)

#define IVPU_MMU_T0SZ_48BIT             16
#define IVPU_MMU_T0SZ_38BIT             26

#define IVPU_MMU_IPS_48BIT		5
#define IVPU_MMU_IPS_44BIT		4
#define IVPU_MMU_IPS_42BIT		3
#define IVPU_MMU_IPS_40BIT		2
#define IVPU_MMU_IPS_36BIT		1
#define IVPU_MMU_IPS_32BIT		0

#define IVPU_MMU_CD_1_TTB0_MASK		GENMASK_ULL(51, 4)

#define IVPU_MMU_STE_0_S1CDMAX		GENMASK_ULL(63, 59)
#define IVPU_MMU_STE_0_S1FMT		GENMASK_ULL(5, 4)
#define IVPU_MMU_STE_0_S1FMT_LINEAR	0
#define IVPU_MMU_STE_DWORDS		8
#define IVPU_MMU_STE_0_CFG_S1_TRANS	5
#define IVPU_MMU_STE_0_CFG		GENMASK_ULL(3, 1)
#define IVPU_MMU_STE_0_S1CTXPTR_MASK	GENMASK_ULL(51, 6)
#define IVPU_MMU_STE_0_V			BIT(0)

#define IVPU_MMU_STE_1_STRW_NSEL1	0ul
#define IVPU_MMU_STE_1_CONT		GENMASK_ULL(16, 13)
#define IVPU_MMU_STE_1_STRW		GENMASK_ULL(31, 30)
#define IVPU_MMU_STE_1_PRIVCFG		GENMASK_ULL(49, 48)
#define IVPU_MMU_STE_1_PRIVCFG_UNPRIV	2ul
#define IVPU_MMU_STE_1_INSTCFG		GENMASK_ULL(51, 50)
#define IVPU_MMU_STE_1_INSTCFG_DATA	2ul
#define IVPU_MMU_STE_1_MEV		BIT(19)
#define IVPU_MMU_STE_1_S1STALLD		BIT(27)
#define IVPU_MMU_STE_1_S1C_CACHE_NC	0ul
#define IVPU_MMU_STE_1_S1C_CACHE_WBRA	1ul
#define IVPU_MMU_STE_1_S1C_CACHE_WT	2ul
#define IVPU_MMU_STE_1_S1C_CACHE_WB	3ul
#define IVPU_MMU_STE_1_S1CIR		GENMASK_ULL(3, 2)
#define IVPU_MMU_STE_1_S1COR		GENMASK_ULL(5, 4)
#define IVPU_MMU_STE_1_S1CSH		GENMASK_ULL(7, 6)
#define IVPU_MMU_STE_1_S1DSS		GENMASK_ULL(1, 0)
#define IVPU_MMU_STE_1_S1DSS_TERMINATE	0x0

#define IVPU_MMU_REG_TIMEOUT_US		(10 * USEC_PER_MSEC)
#define IVPU_MMU_QUEUE_TIMEOUT_US	(100 * USEC_PER_MSEC)

#define IVPU_MMU_GERROR_ERR_MASK ((REG_FLD(IVPU_MMU_REG_GERROR, CMDQ)) | \
				  (REG_FLD(IVPU_MMU_REG_GERROR, EVTQ_ABT)) | \
				  (REG_FLD(IVPU_MMU_REG_GERROR, PRIQ_ABT)) | \
				  (REG_FLD(IVPU_MMU_REG_GERROR, MSI_CMDQ_ABT)) | \
				  (REG_FLD(IVPU_MMU_REG_GERROR, MSI_EVTQ_ABT)) | \
				  (REG_FLD(IVPU_MMU_REG_GERROR, MSI_PRIQ_ABT)) | \
				  (REG_FLD(IVPU_MMU_REG_GERROR, MSI_ABT)))

static char *ivpu_mmu_event_to_str(u32 cmd)
{
	switch (cmd) {
	case IVPU_MMU_EVT_F_UUT:
		return "Unsupported Upstream Transaction";
	case IVPU_MMU_EVT_C_BAD_STREAMID:
		return "Transaction StreamID out of range";
	case IVPU_MMU_EVT_F_STE_FETCH:
		return "Fetch of STE caused external abort";
	case IVPU_MMU_EVT_C_BAD_STE:
		return "Used STE invalid";
	case IVPU_MMU_EVT_F_BAD_ATS_TREQ:
		return "Address Request disallowed for a StreamID";
	case IVPU_MMU_EVT_F_STREAM_DISABLED:
		return "Transaction marks non-substream disabled";
	case IVPU_MMU_EVT_F_TRANSL_FORBIDDEN:
		return "MMU bypass is disallowed for this StreamID";
	case IVPU_MMU_EVT_C_BAD_SUBSTREAMID:
		return "Invalid StreamID";
	case IVPU_MMU_EVT_F_CD_FETCH:
		return "Fetch of CD caused external abort";
	case IVPU_MMU_EVT_C_BAD_CD:
		return "Fetched CD invalid";
	case IVPU_MMU_EVT_F_WALK_EABT:
		return " An external abort occurred fetching a TLB";
	case IVPU_MMU_EVT_F_TRANSLATION:
		return "Translation fault";
	case IVPU_MMU_EVT_F_ADDR_SIZE:
		return " Output address caused address size fault";
	case IVPU_MMU_EVT_F_ACCESS:
		return "Access flag fault";
	case IVPU_MMU_EVT_F_PERMISSION:
		return "Permission fault occurred on page access";
	case IVPU_MMU_EVT_F_TLB_CONFLICT:
		return "A TLB conflict";
	case IVPU_MMU_EVT_F_CFG_CONFLICT:
		return "A configuration cache conflict";
	case IVPU_MMU_EVT_E_PAGE_REQUEST:
		return "Page request hint from a client device";
	case IVPU_MMU_EVT_F_VMS_FETCH:
		return "Fetch of VMS caused external abort";
	default:
		return "Unknown CMDQ command";
	}
}

static void ivpu_mmu_config_check(struct ivpu_device *vdev)
{
	u32 val_ref;
	u32 val;

	if (ivpu_is_simics(vdev))
		val_ref = IVPU_MMU_IDR0_REF_SIMICS;
	else
		val_ref = IVPU_MMU_IDR0_REF;

	val = REGV_RD32(IVPU_MMU_REG_IDR0);
	if (val != val_ref)
		ivpu_dbg(vdev, MMU, "IDR0 0x%x != IDR0_REF 0x%x\n", val, val_ref);

	val = REGV_RD32(IVPU_MMU_REG_IDR1);
	if (val != IVPU_MMU_IDR1_REF)
		ivpu_dbg(vdev, MMU, "IDR1 0x%x != IDR1_REF 0x%x\n", val, IVPU_MMU_IDR1_REF);

	val = REGV_RD32(IVPU_MMU_REG_IDR3);
	if (val != IVPU_MMU_IDR3_REF)
		ivpu_dbg(vdev, MMU, "IDR3 0x%x != IDR3_REF 0x%x\n", val, IVPU_MMU_IDR3_REF);

	if (ivpu_is_simics(vdev))
		val_ref = IVPU_MMU_IDR5_REF_SIMICS;
	else if (ivpu_is_fpga(vdev))
		val_ref = IVPU_MMU_IDR5_REF_FPGA;
	else
		val_ref = IVPU_MMU_IDR5_REF;

	val = REGV_RD32(IVPU_MMU_REG_IDR5);
	if (val != val_ref)
		ivpu_dbg(vdev, MMU, "IDR5 0x%x != IDR5_REF 0x%x\n", val, val_ref);
}

static int ivpu_mmu_cdtab_alloc(struct ivpu_device *vdev)
{
	struct ivpu_mmu_info *mmu = vdev->mmu;
	struct ivpu_mmu_cdtab *cdtab = &mmu->cdtab;
	size_t size = IVPU_MMU_CDTAB_ENT_COUNT * IVPU_MMU_CDTAB_ENT_SIZE;

	cdtab->base = dmam_alloc_coherent(vdev->drm.dev, size, &cdtab->dma, GFP_KERNEL);
	if (!cdtab->base)
		return -ENOMEM;

	ivpu_dbg(vdev, MMU, "CDTAB alloc: dma=%pad size=%zu\n", &cdtab->dma, size);

	return 0;
}

static int ivpu_mmu_strtab_alloc(struct ivpu_device *vdev)
{
	struct ivpu_mmu_info *mmu = vdev->mmu;
	struct ivpu_mmu_strtab *strtab = &mmu->strtab;
	size_t size = IVPU_MMU_STRTAB_ENT_COUNT * IVPU_MMU_STRTAB_ENT_SIZE;

	strtab->base = dmam_alloc_coherent(vdev->drm.dev, size, &strtab->dma, GFP_KERNEL);
	if (!strtab->base)
		return -ENOMEM;

	strtab->base_cfg = IVPU_MMU_STRTAB_CFG;
	strtab->dma_q = IVPU_MMU_STRTAB_BASE_RA;
	strtab->dma_q |= strtab->dma & IVPU_MMU_STRTAB_BASE_ADDR_MASK;

	ivpu_dbg(vdev, MMU, "STRTAB alloc: dma=%pad dma_q=%pad size=%zu\n",
		 &strtab->dma, &strtab->dma_q, size);

	return 0;
}

static int ivpu_mmu_cmdq_alloc(struct ivpu_device *vdev)
{
	struct ivpu_mmu_info *mmu = vdev->mmu;
	struct ivpu_mmu_queue *q = &mmu->cmdq;

	q->base = dmam_alloc_coherent(vdev->drm.dev, IVPU_MMU_CMDQ_SIZE, &q->dma, GFP_KERNEL);
	if (!q->base)
		return -ENOMEM;

	q->dma_q = IVPU_MMU_Q_BASE_RWA;
	q->dma_q |= q->dma & IVPU_MMU_Q_BASE_ADDR_MASK;
	q->dma_q |= IVPU_MMU_Q_COUNT_LOG2;

	ivpu_dbg(vdev, MMU, "CMDQ alloc: dma=%pad dma_q=%pad size=%u\n",
		 &q->dma, &q->dma_q, IVPU_MMU_CMDQ_SIZE);

	return 0;
}

static int ivpu_mmu_evtq_alloc(struct ivpu_device *vdev)
{
	struct ivpu_mmu_info *mmu = vdev->mmu;
	struct ivpu_mmu_queue *q = &mmu->evtq;

	q->base = dmam_alloc_coherent(vdev->drm.dev, IVPU_MMU_EVTQ_SIZE, &q->dma, GFP_KERNEL);
	if (!q->base)
		return -ENOMEM;

	q->dma_q = IVPU_MMU_Q_BASE_RWA;
	q->dma_q |= q->dma & IVPU_MMU_Q_BASE_ADDR_MASK;
	q->dma_q |= IVPU_MMU_Q_COUNT_LOG2;

	ivpu_dbg(vdev, MMU, "EVTQ alloc: dma=%pad dma_q=%pad size=%u\n",
		 &q->dma, &q->dma_q, IVPU_MMU_EVTQ_SIZE);

	return 0;
}

static int ivpu_mmu_structs_alloc(struct ivpu_device *vdev)
{
	int ret;

	ret = ivpu_mmu_cdtab_alloc(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to allocate cdtab: %d\n", ret);
		return ret;
	}

	ret = ivpu_mmu_strtab_alloc(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to allocate strtab: %d\n", ret);
		return ret;
	}

	ret = ivpu_mmu_cmdq_alloc(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to allocate cmdq: %d\n", ret);
		return ret;
	}

	ret = ivpu_mmu_evtq_alloc(vdev);
	if (ret)
		ivpu_err(vdev, "Failed to allocate evtq: %d\n", ret);

	return ret;
}

static int ivpu_mmu_reg_write(struct ivpu_device *vdev, u32 reg, u32 val)
{
	u32 reg_ack = reg + 4; /* ACK register is 4B after base register */
	u32 val_ack;
	int ret;

	REGV_WR32(reg, val);

	ret = REGV_POLL(reg_ack, val_ack, (val == val_ack), IVPU_MMU_REG_TIMEOUT_US);
	if (ret)
		ivpu_err(vdev, "Failed to write register 0x%x\n", reg);

	return ret;
}

static int ivpu_mmu_irqs_setup(struct ivpu_device *vdev)
{
	u32 irq_ctrl = IVPU_MMU_IRQ_EVTQ_EN | IVPU_MMU_IRQ_GERROR_EN;
	int ret;

	ret = ivpu_mmu_reg_write(vdev, IVPU_MMU_REG_IRQ_CTRL, 0);
	if (ret)
		return ret;

	return ivpu_mmu_reg_write(vdev, IVPU_MMU_REG_IRQ_CTRL, irq_ctrl);
}

static int ivpu_mmu_cmdq_wait_for_cons(struct ivpu_device *vdev)
{
	struct ivpu_mmu_queue *cmdq = &vdev->mmu->cmdq;

	return REGV_POLL(IVPU_MMU_REG_CMDQ_CONS, cmdq->cons, (cmdq->prod == cmdq->cons),
			 IVPU_MMU_QUEUE_TIMEOUT_US);
}

static int ivpu_mmu_cmdq_cmd_write(struct ivpu_device *vdev, const char *name, u64 data0, u64 data1)
{
	struct ivpu_mmu_queue *q = &vdev->mmu->cmdq;
	u64 *queue_buffer = q->base;
	int idx = IVPU_MMU_Q_IDX(q->prod) * (IVPU_MMU_CMDQ_CMD_SIZE / sizeof(*queue_buffer));

	if (!CIRC_SPACE(IVPU_MMU_Q_IDX(q->prod), IVPU_MMU_Q_IDX(q->cons), IVPU_MMU_Q_COUNT)) {
		ivpu_err(vdev, "Failed to write MMU CMD %s\n", name);
		return -EBUSY;
	}

	queue_buffer[idx] = data0;
	queue_buffer[idx + 1] = data1;
	q->prod = (q->prod + 1) & IVPU_MMU_Q_WRAP_MASK;

	ivpu_dbg(vdev, MMU, "CMD write: %s data: 0x%llx 0x%llx\n", name, data0, data1);

	return 0;
}

static int ivpu_mmu_cmdq_sync(struct ivpu_device *vdev)
{
	struct ivpu_mmu_queue *q = &vdev->mmu->cmdq;
	u64 val;
	int ret;

	val = FIELD_PREP(IVPU_MMU_CMD_OPCODE, CMD_SYNC) |
	      FIELD_PREP(IVPU_MMU_CMD_SYNC_0_CS, 0x2) |
	      FIELD_PREP(IVPU_MMU_CMD_SYNC_0_MSH, 0x3) |
	      FIELD_PREP(IVPU_MMU_CMD_SYNC_0_MSI_ATTR, 0xf);

	ret = ivpu_mmu_cmdq_cmd_write(vdev, "SYNC", val, 0);
	if (ret)
		return ret;

	clflush_cache_range(q->base, IVPU_MMU_CMDQ_SIZE);
	REGV_WR32(IVPU_MMU_REG_CMDQ_PROD, q->prod);

	ret = ivpu_mmu_cmdq_wait_for_cons(vdev);
	if (ret)
		ivpu_err(vdev, "Timed out waiting for consumer: %d\n", ret);

	return ret;
}

static int ivpu_mmu_cmdq_write_cfgi_all(struct ivpu_device *vdev)
{
	u64 data0 = FIELD_PREP(IVPU_MMU_CMD_OPCODE, CMD_CFGI_ALL);
	u64 data1 = FIELD_PREP(IVPU_MMU_CMD_CFGI_1_RANGE, 0x1f);

	return ivpu_mmu_cmdq_cmd_write(vdev, "CFGI_ALL", data0, data1);
}

static int ivpu_mmu_cmdq_write_tlbi_nh_asid(struct ivpu_device *vdev, u16 ssid)
{
	u64 val = FIELD_PREP(IVPU_MMU_CMD_OPCODE, CMD_TLBI_NH_ASID) |
		  FIELD_PREP(IVPU_MMU_CMD_TLBI_0_ASID, ssid);

	return ivpu_mmu_cmdq_cmd_write(vdev, "TLBI_NH_ASID", val, 0);
}

static int ivpu_mmu_cmdq_write_tlbi_nsnh_all(struct ivpu_device *vdev)
{
	u64 val = FIELD_PREP(IVPU_MMU_CMD_OPCODE, CMD_TLBI_NSNH_ALL);

	return ivpu_mmu_cmdq_cmd_write(vdev, "TLBI_NSNH_ALL", val, 0);
}

static int ivpu_mmu_reset(struct ivpu_device *vdev)
{
	struct ivpu_mmu_info *mmu = vdev->mmu;
	u32 val;
	int ret;

	memset(mmu->cmdq.base, 0, IVPU_MMU_CMDQ_SIZE);
	clflush_cache_range(mmu->cmdq.base, IVPU_MMU_CMDQ_SIZE);
	mmu->cmdq.prod = 0;
	mmu->cmdq.cons = 0;

	memset(mmu->evtq.base, 0, IVPU_MMU_EVTQ_SIZE);
	clflush_cache_range(mmu->evtq.base, IVPU_MMU_EVTQ_SIZE);
	mmu->evtq.prod = 0;
	mmu->evtq.cons = 0;

	ret = ivpu_mmu_reg_write(vdev, IVPU_MMU_REG_CR0, 0);
	if (ret)
		return ret;

	val = FIELD_PREP(IVPU_MMU_CR1_TABLE_SH, IVPU_MMU_SH_ISH) |
	      FIELD_PREP(IVPU_MMU_CR1_TABLE_OC, IVPU_MMU_CACHE_WB) |
	      FIELD_PREP(IVPU_MMU_CR1_TABLE_IC, IVPU_MMU_CACHE_WB) |
	      FIELD_PREP(IVPU_MMU_CR1_QUEUE_SH, IVPU_MMU_SH_ISH) |
	      FIELD_PREP(IVPU_MMU_CR1_QUEUE_OC, IVPU_MMU_CACHE_WB) |
	      FIELD_PREP(IVPU_MMU_CR1_QUEUE_IC, IVPU_MMU_CACHE_WB);
	REGV_WR32(IVPU_MMU_REG_CR1, val);

	REGV_WR64(IVPU_MMU_REG_STRTAB_BASE, mmu->strtab.dma_q);
	REGV_WR32(IVPU_MMU_REG_STRTAB_BASE_CFG, mmu->strtab.base_cfg);

	REGV_WR64(IVPU_MMU_REG_CMDQ_BASE, mmu->cmdq.dma_q);
	REGV_WR32(IVPU_MMU_REG_CMDQ_PROD, 0);
	REGV_WR32(IVPU_MMU_REG_CMDQ_CONS, 0);

	val = IVPU_MMU_CR0_CMDQEN;
	ret = ivpu_mmu_reg_write(vdev, IVPU_MMU_REG_CR0, val);
	if (ret)
		return ret;

	ret = ivpu_mmu_cmdq_write_cfgi_all(vdev);
	if (ret)
		return ret;

	ret = ivpu_mmu_cmdq_write_tlbi_nsnh_all(vdev);
	if (ret)
		return ret;

	ret = ivpu_mmu_cmdq_sync(vdev);
	if (ret)
		return ret;

	REGV_WR64(IVPU_MMU_REG_EVTQ_BASE, mmu->evtq.dma_q);
	REGV_WR32(IVPU_MMU_REG_EVTQ_PROD_SEC, 0);
	REGV_WR32(IVPU_MMU_REG_EVTQ_CONS_SEC, 0);

	val |= IVPU_MMU_CR0_EVTQEN;
	ret = ivpu_mmu_reg_write(vdev, IVPU_MMU_REG_CR0, val);
	if (ret)
		return ret;

	val |= IVPU_MMU_CR0_ATSCHK;
	ret = ivpu_mmu_reg_write(vdev, IVPU_MMU_REG_CR0, val);
	if (ret)
		return ret;

	ret = ivpu_mmu_irqs_setup(vdev);
	if (ret)
		return ret;

	val |= IVPU_MMU_CR0_SMMUEN;
	return ivpu_mmu_reg_write(vdev, IVPU_MMU_REG_CR0, val);
}

static void ivpu_mmu_strtab_link_cd(struct ivpu_device *vdev, u32 sid)
{
	struct ivpu_mmu_info *mmu = vdev->mmu;
	struct ivpu_mmu_strtab *strtab = &mmu->strtab;
	struct ivpu_mmu_cdtab *cdtab = &mmu->cdtab;
	u64 *entry = strtab->base + (sid * IVPU_MMU_STRTAB_ENT_SIZE);
	u64 str[2];

	str[0] = FIELD_PREP(IVPU_MMU_STE_0_CFG, IVPU_MMU_STE_0_CFG_S1_TRANS) |
		 FIELD_PREP(IVPU_MMU_STE_0_S1CDMAX, IVPU_MMU_CDTAB_ENT_COUNT_LOG2) |
		 FIELD_PREP(IVPU_MMU_STE_0_S1FMT, IVPU_MMU_STE_0_S1FMT_LINEAR) |
		 IVPU_MMU_STE_0_V |
		 (cdtab->dma & IVPU_MMU_STE_0_S1CTXPTR_MASK);

	str[1] = FIELD_PREP(IVPU_MMU_STE_1_S1DSS, IVPU_MMU_STE_1_S1DSS_TERMINATE) |
		 FIELD_PREP(IVPU_MMU_STE_1_S1CIR, IVPU_MMU_STE_1_S1C_CACHE_NC) |
		 FIELD_PREP(IVPU_MMU_STE_1_S1COR, IVPU_MMU_STE_1_S1C_CACHE_NC) |
		 FIELD_PREP(IVPU_MMU_STE_1_S1CSH, IVPU_MMU_SH_NSH) |
		 FIELD_PREP(IVPU_MMU_STE_1_PRIVCFG, IVPU_MMU_STE_1_PRIVCFG_UNPRIV) |
		 FIELD_PREP(IVPU_MMU_STE_1_INSTCFG, IVPU_MMU_STE_1_INSTCFG_DATA) |
		 FIELD_PREP(IVPU_MMU_STE_1_STRW, IVPU_MMU_STE_1_STRW_NSEL1) |
		 FIELD_PREP(IVPU_MMU_STE_1_CONT, IVPU_MMU_STRTAB_CFG_LOG2SIZE) |
		 IVPU_MMU_STE_1_MEV |
		 IVPU_MMU_STE_1_S1STALLD;

	WRITE_ONCE(entry[1], str[1]);
	WRITE_ONCE(entry[0], str[0]);

	clflush_cache_range(entry, IVPU_MMU_STRTAB_ENT_SIZE);

	ivpu_dbg(vdev, MMU, "STRTAB write entry (SSID=%u): 0x%llx, 0x%llx\n", sid, str[0], str[1]);
}

static int ivpu_mmu_strtab_init(struct ivpu_device *vdev)
{
	ivpu_mmu_strtab_link_cd(vdev, IVPU_MMU_STREAM_ID0);
	ivpu_mmu_strtab_link_cd(vdev, IVPU_MMU_STREAM_ID3);

	return 0;
}

int ivpu_mmu_invalidate_tlb(struct ivpu_device *vdev, u16 ssid)
{
	struct ivpu_mmu_info *mmu = vdev->mmu;
	int ret = 0;

	mutex_lock(&mmu->lock);
	if (!mmu->on)
		goto unlock;

	ret = ivpu_mmu_cmdq_write_tlbi_nh_asid(vdev, ssid);
	if (ret)
		goto unlock;

	ret = ivpu_mmu_cmdq_sync(vdev);
unlock:
	mutex_unlock(&mmu->lock);
	return ret;
}

static int ivpu_mmu_cd_add(struct ivpu_device *vdev, u32 ssid, u64 cd_dma)
{
	struct ivpu_mmu_info *mmu = vdev->mmu;
	struct ivpu_mmu_cdtab *cdtab = &mmu->cdtab;
	u64 *entry;
	u64 cd[4];
	int ret = 0;

	if (ssid > IVPU_MMU_CDTAB_ENT_COUNT)
		return -EINVAL;

	entry = cdtab->base + (ssid * IVPU_MMU_CDTAB_ENT_SIZE);

	if (cd_dma != 0) {
		cd[0] = FIELD_PREP(IVPU_MMU_CD_0_TCR_T0SZ, IVPU_MMU_T0SZ_48BIT) |
			FIELD_PREP(IVPU_MMU_CD_0_TCR_TG0, 0) |
			FIELD_PREP(IVPU_MMU_CD_0_TCR_IRGN0, 0) |
			FIELD_PREP(IVPU_MMU_CD_0_TCR_ORGN0, 0) |
			FIELD_PREP(IVPU_MMU_CD_0_TCR_SH0, 0) |
			FIELD_PREP(IVPU_MMU_CD_0_TCR_IPS, IVPU_MMU_IPS_48BIT) |
			FIELD_PREP(IVPU_MMU_CD_0_ASID, ssid) |
			IVPU_MMU_CD_0_TCR_EPD1 |
			IVPU_MMU_CD_0_AA64 |
			IVPU_MMU_CD_0_R |
			IVPU_MMU_CD_0_ASET |
			IVPU_MMU_CD_0_V;
		cd[1] = cd_dma & IVPU_MMU_CD_1_TTB0_MASK;
		cd[2] = 0;
		cd[3] = 0x0000000000007444;

		/* For global context generate memory fault on VPU */
		if (ssid == IVPU_GLOBAL_CONTEXT_MMU_SSID)
			cd[0] |= IVPU_MMU_CD_0_A;
	} else {
		memset(cd, 0, sizeof(cd));
	}

	WRITE_ONCE(entry[1], cd[1]);
	WRITE_ONCE(entry[2], cd[2]);
	WRITE_ONCE(entry[3], cd[3]);
	WRITE_ONCE(entry[0], cd[0]);

	clflush_cache_range(entry, IVPU_MMU_CDTAB_ENT_SIZE);

	ivpu_dbg(vdev, MMU, "CDTAB %s entry (SSID=%u, dma=%pad): 0x%llx, 0x%llx, 0x%llx, 0x%llx\n",
		 cd_dma ? "write" : "clear", ssid, &cd_dma, cd[0], cd[1], cd[2], cd[3]);

	mutex_lock(&mmu->lock);
	if (!mmu->on)
		goto unlock;

	ret = ivpu_mmu_cmdq_write_cfgi_all(vdev);
	if (ret)
		goto unlock;

	ret = ivpu_mmu_cmdq_sync(vdev);
unlock:
	mutex_unlock(&mmu->lock);
	return ret;
}

static int ivpu_mmu_cd_add_gbl(struct ivpu_device *vdev)
{
	int ret;

	ret = ivpu_mmu_cd_add(vdev, 0, vdev->gctx.pgtable.pgd_dma);
	if (ret)
		ivpu_err(vdev, "Failed to add global CD entry: %d\n", ret);

	return ret;
}

static int ivpu_mmu_cd_add_user(struct ivpu_device *vdev, u32 ssid, dma_addr_t cd_dma)
{
	int ret;

	if (ssid == 0) {
		ivpu_err(vdev, "Invalid SSID: %u\n", ssid);
		return -EINVAL;
	}

	ret = ivpu_mmu_cd_add(vdev, ssid, cd_dma);
	if (ret)
		ivpu_err(vdev, "Failed to add CD entry SSID=%u: %d\n", ssid, ret);

	return ret;
}

int ivpu_mmu_init(struct ivpu_device *vdev)
{
	struct ivpu_mmu_info *mmu = vdev->mmu;
	int ret;

	ivpu_dbg(vdev, MMU, "Init..\n");

	drmm_mutex_init(&vdev->drm, &mmu->lock);
	ivpu_mmu_config_check(vdev);

	ret = ivpu_mmu_structs_alloc(vdev);
	if (ret)
		return ret;

	ret = ivpu_mmu_strtab_init(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to initialize strtab: %d\n", ret);
		return ret;
	}

	ret = ivpu_mmu_cd_add_gbl(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to initialize strtab: %d\n", ret);
		return ret;
	}

	ret = ivpu_mmu_enable(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to resume MMU: %d\n", ret);
		return ret;
	}

	ivpu_dbg(vdev, MMU, "Init done\n");

	return 0;
}

int ivpu_mmu_enable(struct ivpu_device *vdev)
{
	struct ivpu_mmu_info *mmu = vdev->mmu;
	int ret;

	mutex_lock(&mmu->lock);

	mmu->on = true;

	ret = ivpu_mmu_reset(vdev);
	if (ret) {
		ivpu_err(vdev, "Failed to reset MMU: %d\n", ret);
		goto err;
	}

	ret = ivpu_mmu_cmdq_write_cfgi_all(vdev);
	if (ret)
		goto err;

	ret = ivpu_mmu_cmdq_write_tlbi_nsnh_all(vdev);
	if (ret)
		goto err;

	ret = ivpu_mmu_cmdq_sync(vdev);
	if (ret)
		goto err;

	mutex_unlock(&mmu->lock);

	return 0;
err:
	mmu->on = false;
	mutex_unlock(&mmu->lock);
	return ret;
}

void ivpu_mmu_disable(struct ivpu_device *vdev)
{
	struct ivpu_mmu_info *mmu = vdev->mmu;

	mutex_lock(&mmu->lock);
	mmu->on = false;
	mutex_unlock(&mmu->lock);
}

static void ivpu_mmu_dump_event(struct ivpu_device *vdev, u32 *event)
{
	u32 ssid = FIELD_GET(IVPU_MMU_EVT_SSID_MASK, event[0]);
	u32 op = FIELD_GET(IVPU_MMU_EVT_OP_MASK, event[0]);
	u64 fetch_addr = ((u64)event[7]) << 32 | event[6];
	u64 in_addr = ((u64)event[5]) << 32 | event[4];
	u32 sid = event[1];

	ivpu_err(vdev, "MMU EVTQ: 0x%x (%s) SSID: %d SID: %d, e[2] %08x, e[3] %08x, in addr: 0x%llx, fetch addr: 0x%llx\n",
		 op, ivpu_mmu_event_to_str(op), ssid, sid, event[2], event[3], in_addr, fetch_addr);
}

static u32 *ivpu_mmu_get_event(struct ivpu_device *vdev)
{
	struct ivpu_mmu_queue *evtq = &vdev->mmu->evtq;
	u32 idx = IVPU_MMU_Q_IDX(evtq->cons);
	u32 *evt = evtq->base + (idx * IVPU_MMU_EVTQ_CMD_SIZE);

	evtq->prod = REGV_RD32(IVPU_MMU_REG_EVTQ_PROD_SEC);
	if (!CIRC_CNT(IVPU_MMU_Q_IDX(evtq->prod), IVPU_MMU_Q_IDX(evtq->cons), IVPU_MMU_Q_COUNT))
		return NULL;

	clflush_cache_range(evt, IVPU_MMU_EVTQ_CMD_SIZE);

	evtq->cons = (evtq->cons + 1) & IVPU_MMU_Q_WRAP_MASK;
	REGV_WR32(IVPU_MMU_REG_EVTQ_CONS_SEC, evtq->cons);

	return evt;
}

void ivpu_mmu_irq_evtq_handler(struct ivpu_device *vdev)
{
	bool schedule_recovery = false;
	u32 *event;
	u32 ssid;

	ivpu_dbg(vdev, IRQ, "MMU event queue\n");

	while ((event = ivpu_mmu_get_event(vdev)) != NULL) {
		ivpu_mmu_dump_event(vdev, event);

		ssid = FIELD_GET(IVPU_MMU_EVT_SSID_MASK, event[0]);
		if (ssid == IVPU_GLOBAL_CONTEXT_MMU_SSID)
			schedule_recovery = true;
		else
			ivpu_mmu_user_context_mark_invalid(vdev, ssid);
	}

	if (schedule_recovery)
		ivpu_pm_schedule_recovery(vdev);
}

void ivpu_mmu_irq_gerr_handler(struct ivpu_device *vdev)
{
	u32 gerror_val, gerrorn_val, active;

	ivpu_dbg(vdev, IRQ, "MMU error\n");

	gerror_val = REGV_RD32(IVPU_MMU_REG_GERROR);
	gerrorn_val = REGV_RD32(IVPU_MMU_REG_GERRORN);

	active = gerror_val ^ gerrorn_val;
	if (!(active & IVPU_MMU_GERROR_ERR_MASK))
		return;

	if (REG_TEST_FLD(IVPU_MMU_REG_GERROR, MSI_ABT, active))
		ivpu_warn_ratelimited(vdev, "MMU MSI ABT write aborted\n");

	if (REG_TEST_FLD(IVPU_MMU_REG_GERROR, MSI_PRIQ_ABT, active))
		ivpu_warn_ratelimited(vdev, "MMU PRIQ MSI ABT write aborted\n");

	if (REG_TEST_FLD(IVPU_MMU_REG_GERROR, MSI_EVTQ_ABT, active))
		ivpu_warn_ratelimited(vdev, "MMU EVTQ MSI ABT write aborted\n");

	if (REG_TEST_FLD(IVPU_MMU_REG_GERROR, MSI_CMDQ_ABT, active))
		ivpu_warn_ratelimited(vdev, "MMU CMDQ MSI ABT write aborted\n");

	if (REG_TEST_FLD(IVPU_MMU_REG_GERROR, PRIQ_ABT, active))
		ivpu_err_ratelimited(vdev, "MMU PRIQ write aborted\n");

	if (REG_TEST_FLD(IVPU_MMU_REG_GERROR, EVTQ_ABT, active))
		ivpu_err_ratelimited(vdev, "MMU EVTQ write aborted\n");

	if (REG_TEST_FLD(IVPU_MMU_REG_GERROR, CMDQ, active))
		ivpu_err_ratelimited(vdev, "MMU CMDQ write aborted\n");

	REGV_WR32(IVPU_MMU_REG_GERRORN, gerror_val);
}

int ivpu_mmu_set_pgtable(struct ivpu_device *vdev, int ssid, struct ivpu_mmu_pgtable *pgtable)
{
	return ivpu_mmu_cd_add_user(vdev, ssid, pgtable->pgd_dma);
}

void ivpu_mmu_clear_pgtable(struct ivpu_device *vdev, int ssid)
{
	ivpu_mmu_cd_add_user(vdev, ssid, 0); /* 0 will clear CD entry */
}
