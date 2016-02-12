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

#include "bfad_drv.h"
#include "bfa_ioc.h"
#include "bfi_reg.h"
#include "bfa_defs.h"

BFA_TRC_FILE(CNA, IOC_CT);

#define bfa_ioc_ct_sync_pos(__ioc)      \
		((uint32_t) (1 << bfa_ioc_pcifn(__ioc)))
#define BFA_IOC_SYNC_REQD_SH    16
#define bfa_ioc_ct_get_sync_ackd(__val) (__val & 0x0000ffff)
#define bfa_ioc_ct_clear_sync_ackd(__val)       (__val & 0xffff0000)
#define bfa_ioc_ct_get_sync_reqd(__val) (__val >> BFA_IOC_SYNC_REQD_SH)
#define bfa_ioc_ct_sync_reqd_pos(__ioc) \
			(bfa_ioc_ct_sync_pos(__ioc) << BFA_IOC_SYNC_REQD_SH)

/*
 * forward declarations
 */
static bfa_boolean_t bfa_ioc_ct_firmware_lock(struct bfa_ioc_s *ioc);
static void bfa_ioc_ct_firmware_unlock(struct bfa_ioc_s *ioc);
static void bfa_ioc_ct_notify_fail(struct bfa_ioc_s *ioc);
static void bfa_ioc_ct_ownership_reset(struct bfa_ioc_s *ioc);
static bfa_boolean_t bfa_ioc_ct_sync_start(struct bfa_ioc_s *ioc);
static void bfa_ioc_ct_sync_join(struct bfa_ioc_s *ioc);
static void bfa_ioc_ct_sync_leave(struct bfa_ioc_s *ioc);
static void bfa_ioc_ct_sync_ack(struct bfa_ioc_s *ioc);
static bfa_boolean_t bfa_ioc_ct_sync_complete(struct bfa_ioc_s *ioc);
static void bfa_ioc_ct_set_cur_ioc_fwstate(
			struct bfa_ioc_s *ioc, enum bfi_ioc_state fwstate);
static enum bfi_ioc_state bfa_ioc_ct_get_cur_ioc_fwstate(struct bfa_ioc_s *ioc);
static void bfa_ioc_ct_set_alt_ioc_fwstate(
			struct bfa_ioc_s *ioc, enum bfi_ioc_state fwstate);
static enum bfi_ioc_state bfa_ioc_ct_get_alt_ioc_fwstate(struct bfa_ioc_s *ioc);

static struct bfa_ioc_hwif_s hwif_ct;
static struct bfa_ioc_hwif_s hwif_ct2;

/*
 * Return true if firmware of current driver matches the running firmware.
 */
static bfa_boolean_t
bfa_ioc_ct_firmware_lock(struct bfa_ioc_s *ioc)
{
	enum bfi_ioc_state ioc_fwstate;
	u32 usecnt;
	struct bfi_ioc_image_hdr_s fwhdr;

	bfa_ioc_sem_get(ioc->ioc_regs.ioc_usage_sem_reg);
	usecnt = readl(ioc->ioc_regs.ioc_usage_reg);

	/*
	 * If usage count is 0, always return TRUE.
	 */
	if (usecnt == 0) {
		writel(1, ioc->ioc_regs.ioc_usage_reg);
		readl(ioc->ioc_regs.ioc_usage_sem_reg);
		writel(1, ioc->ioc_regs.ioc_usage_sem_reg);
		writel(0, ioc->ioc_regs.ioc_fail_sync);
		bfa_trc(ioc, usecnt);
		return BFA_TRUE;
	}

	ioc_fwstate = readl(ioc->ioc_regs.ioc_fwstate);
	bfa_trc(ioc, ioc_fwstate);

	/*
	 * Use count cannot be non-zero and chip in uninitialized state.
	 */
	WARN_ON(ioc_fwstate == BFI_IOC_UNINIT);

	/*
	 * Check if another driver with a different firmware is active
	 */
	bfa_ioc_fwver_get(ioc, &fwhdr);
	if (!bfa_ioc_fwver_cmp(ioc, &fwhdr)) {
		readl(ioc->ioc_regs.ioc_usage_sem_reg);
		writel(1, ioc->ioc_regs.ioc_usage_sem_reg);
		bfa_trc(ioc, usecnt);
		return BFA_FALSE;
	}

	/*
	 * Same firmware version. Increment the reference count.
	 */
	usecnt++;
	writel(usecnt, ioc->ioc_regs.ioc_usage_reg);
	readl(ioc->ioc_regs.ioc_usage_sem_reg);
	writel(1, ioc->ioc_regs.ioc_usage_sem_reg);
	bfa_trc(ioc, usecnt);
	return BFA_TRUE;
}

static void
bfa_ioc_ct_firmware_unlock(struct bfa_ioc_s *ioc)
{
	u32 usecnt;

	/*
	 * decrement usage count
	 */
	bfa_ioc_sem_get(ioc->ioc_regs.ioc_usage_sem_reg);
	usecnt = readl(ioc->ioc_regs.ioc_usage_reg);
	WARN_ON(usecnt <= 0);

	usecnt--;
	writel(usecnt, ioc->ioc_regs.ioc_usage_reg);
	bfa_trc(ioc, usecnt);

	readl(ioc->ioc_regs.ioc_usage_sem_reg);
	writel(1, ioc->ioc_regs.ioc_usage_sem_reg);
}

/*
 * Notify other functions on HB failure.
 */
static void
bfa_ioc_ct_notify_fail(struct bfa_ioc_s *ioc)
{
	if (bfa_ioc_is_cna(ioc)) {
		writel(__FW_INIT_HALT_P, ioc->ioc_regs.ll_halt);
		writel(__FW_INIT_HALT_P, ioc->ioc_regs.alt_ll_halt);
		/* Wait for halt to take effect */
		readl(ioc->ioc_regs.ll_halt);
		readl(ioc->ioc_regs.alt_ll_halt);
	} else {
		writel(~0U, ioc->ioc_regs.err_set);
		readl(ioc->ioc_regs.err_set);
	}
}

/*
 * Host to LPU mailbox message addresses
 */
static struct { u32 hfn_mbox, lpu_mbox, hfn_pgn; } ct_fnreg[] = {
	{ HOSTFN0_LPU_MBOX0_0, LPU_HOSTFN0_MBOX0_0, HOST_PAGE_NUM_FN0 },
	{ HOSTFN1_LPU_MBOX0_8, LPU_HOSTFN1_MBOX0_8, HOST_PAGE_NUM_FN1 },
	{ HOSTFN2_LPU_MBOX0_0, LPU_HOSTFN2_MBOX0_0, HOST_PAGE_NUM_FN2 },
	{ HOSTFN3_LPU_MBOX0_8, LPU_HOSTFN3_MBOX0_8, HOST_PAGE_NUM_FN3 }
};

/*
 * Host <-> LPU mailbox command/status registers - port 0
 */
static struct { u32 hfn, lpu; } ct_p0reg[] = {
	{ HOSTFN0_LPU0_CMD_STAT, LPU0_HOSTFN0_CMD_STAT },
	{ HOSTFN1_LPU0_CMD_STAT, LPU0_HOSTFN1_CMD_STAT },
	{ HOSTFN2_LPU0_CMD_STAT, LPU0_HOSTFN2_CMD_STAT },
	{ HOSTFN3_LPU0_CMD_STAT, LPU0_HOSTFN3_CMD_STAT }
};

/*
 * Host <-> LPU mailbox command/status registers - port 1
 */
static struct { u32 hfn, lpu; } ct_p1reg[] = {
	{ HOSTFN0_LPU1_CMD_STAT, LPU1_HOSTFN0_CMD_STAT },
	{ HOSTFN1_LPU1_CMD_STAT, LPU1_HOSTFN1_CMD_STAT },
	{ HOSTFN2_LPU1_CMD_STAT, LPU1_HOSTFN2_CMD_STAT },
	{ HOSTFN3_LPU1_CMD_STAT, LPU1_HOSTFN3_CMD_STAT }
};

static struct { uint32_t hfn_mbox, lpu_mbox, hfn_pgn, hfn, lpu, lpu_read; }
	ct2_reg[] = {
	{ CT2_HOSTFN_LPU0_MBOX0, CT2_LPU0_HOSTFN_MBOX0, CT2_HOSTFN_PAGE_NUM,
	  CT2_HOSTFN_LPU0_CMD_STAT, CT2_LPU0_HOSTFN_CMD_STAT,
	  CT2_HOSTFN_LPU0_READ_STAT},
	{ CT2_HOSTFN_LPU1_MBOX0, CT2_LPU1_HOSTFN_MBOX0, CT2_HOSTFN_PAGE_NUM,
	  CT2_HOSTFN_LPU1_CMD_STAT, CT2_LPU1_HOSTFN_CMD_STAT,
	  CT2_HOSTFN_LPU1_READ_STAT},
};

static void
bfa_ioc_ct_reg_init(struct bfa_ioc_s *ioc)
{
	void __iomem *rb;
	int		pcifn = bfa_ioc_pcifn(ioc);

	rb = bfa_ioc_bar0(ioc);

	ioc->ioc_regs.hfn_mbox = rb + ct_fnreg[pcifn].hfn_mbox;
	ioc->ioc_regs.lpu_mbox = rb + ct_fnreg[pcifn].lpu_mbox;
	ioc->ioc_regs.host_page_num_fn = rb + ct_fnreg[pcifn].hfn_pgn;

	if (ioc->port_id == 0) {
		ioc->ioc_regs.heartbeat = rb + BFA_IOC0_HBEAT_REG;
		ioc->ioc_regs.ioc_fwstate = rb + BFA_IOC0_STATE_REG;
		ioc->ioc_regs.alt_ioc_fwstate = rb + BFA_IOC1_STATE_REG;
		ioc->ioc_regs.hfn_mbox_cmd = rb + ct_p0reg[pcifn].hfn;
		ioc->ioc_regs.lpu_mbox_cmd = rb + ct_p0reg[pcifn].lpu;
		ioc->ioc_regs.ll_halt = rb + FW_INIT_HALT_P0;
		ioc->ioc_regs.alt_ll_halt = rb + FW_INIT_HALT_P1;
	} else {
		ioc->ioc_regs.heartbeat = (rb + BFA_IOC1_HBEAT_REG);
		ioc->ioc_regs.ioc_fwstate = (rb + BFA_IOC1_STATE_REG);
		ioc->ioc_regs.alt_ioc_fwstate = rb + BFA_IOC0_STATE_REG;
		ioc->ioc_regs.hfn_mbox_cmd = rb + ct_p1reg[pcifn].hfn;
		ioc->ioc_regs.lpu_mbox_cmd = rb + ct_p1reg[pcifn].lpu;
		ioc->ioc_regs.ll_halt = rb + FW_INIT_HALT_P1;
		ioc->ioc_regs.alt_ll_halt = rb + FW_INIT_HALT_P0;
	}

	/*
	 * PSS control registers
	 */
	ioc->ioc_regs.pss_ctl_reg = (rb + PSS_CTL_REG);
	ioc->ioc_regs.pss_err_status_reg = (rb + PSS_ERR_STATUS_REG);
	ioc->ioc_regs.app_pll_fast_ctl_reg = (rb + APP_PLL_LCLK_CTL_REG);
	ioc->ioc_regs.app_pll_slow_ctl_reg = (rb + APP_PLL_SCLK_CTL_REG);

	/*
	 * IOC semaphore registers and serialization
	 */
	ioc->ioc_regs.ioc_sem_reg = (rb + HOST_SEM0_REG);
	ioc->ioc_regs.ioc_usage_sem_reg = (rb + HOST_SEM1_REG);
	ioc->ioc_regs.ioc_init_sem_reg = (rb + HOST_SEM2_REG);
	ioc->ioc_regs.ioc_usage_reg = (rb + BFA_FW_USE_COUNT);
	ioc->ioc_regs.ioc_fail_sync = (rb + BFA_IOC_FAIL_SYNC);

	/*
	 * sram memory access
	 */
	ioc->ioc_regs.smem_page_start = (rb + PSS_SMEM_PAGE_START);
	ioc->ioc_regs.smem_pg0 = BFI_IOC_SMEM_PG0_CT;

	/*
	 * err set reg : for notification of hb failure in fcmode
	 */
	ioc->ioc_regs.err_set = (rb + ERR_SET_REG);
}

static void
bfa_ioc_ct2_reg_init(struct bfa_ioc_s *ioc)
{
	void __iomem *rb;
	int	port = bfa_ioc_portid(ioc);

	rb = bfa_ioc_bar0(ioc);

	ioc->ioc_regs.hfn_mbox = rb + ct2_reg[port].hfn_mbox;
	ioc->ioc_regs.lpu_mbox = rb + ct2_reg[port].lpu_mbox;
	ioc->ioc_regs.host_page_num_fn = rb + ct2_reg[port].hfn_pgn;
	ioc->ioc_regs.hfn_mbox_cmd = rb + ct2_reg[port].hfn;
	ioc->ioc_regs.lpu_mbox_cmd = rb + ct2_reg[port].lpu;
	ioc->ioc_regs.lpu_read_stat = rb + ct2_reg[port].lpu_read;

	if (port == 0) {
		ioc->ioc_regs.heartbeat = rb + CT2_BFA_IOC0_HBEAT_REG;
		ioc->ioc_regs.ioc_fwstate = rb + CT2_BFA_IOC0_STATE_REG;
		ioc->ioc_regs.alt_ioc_fwstate = rb + CT2_BFA_IOC1_STATE_REG;
		ioc->ioc_regs.ll_halt = rb + FW_INIT_HALT_P0;
		ioc->ioc_regs.alt_ll_halt = rb + FW_INIT_HALT_P1;
	} else {
		ioc->ioc_regs.heartbeat = (rb + CT2_BFA_IOC1_HBEAT_REG);
		ioc->ioc_regs.ioc_fwstate = (rb + CT2_BFA_IOC1_STATE_REG);
		ioc->ioc_regs.alt_ioc_fwstate = rb + CT2_BFA_IOC0_STATE_REG;
		ioc->ioc_regs.ll_halt = rb + FW_INIT_HALT_P1;
		ioc->ioc_regs.alt_ll_halt = rb + FW_INIT_HALT_P0;
	}

	/*
	 * PSS control registers
	 */
	ioc->ioc_regs.pss_ctl_reg = (rb + PSS_CTL_REG);
	ioc->ioc_regs.pss_err_status_reg = (rb + PSS_ERR_STATUS_REG);
	ioc->ioc_regs.app_pll_fast_ctl_reg = (rb + CT2_APP_PLL_LCLK_CTL_REG);
	ioc->ioc_regs.app_pll_slow_ctl_reg = (rb + CT2_APP_PLL_SCLK_CTL_REG);

	/*
	 * IOC semaphore registers and serialization
	 */
	ioc->ioc_regs.ioc_sem_reg = (rb + CT2_HOST_SEM0_REG);
	ioc->ioc_regs.ioc_usage_sem_reg = (rb + CT2_HOST_SEM1_REG);
	ioc->ioc_regs.ioc_init_sem_reg = (rb + CT2_HOST_SEM2_REG);
	ioc->ioc_regs.ioc_usage_reg = (rb + CT2_BFA_FW_USE_COUNT);
	ioc->ioc_regs.ioc_fail_sync = (rb + CT2_BFA_IOC_FAIL_SYNC);

	/*
	 * sram memory access
	 */
	ioc->ioc_regs.smem_page_start = (rb + PSS_SMEM_PAGE_START);
	ioc->ioc_regs.smem_pg0 = BFI_IOC_SMEM_PG0_CT;

	/*
	 * err set reg : for notification of hb failure in fcmode
	 */
	ioc->ioc_regs.err_set = (rb + ERR_SET_REG);
}

/*
 * Initialize IOC to port mapping.
 */

#define FNC_PERS_FN_SHIFT(__fn)	((__fn) * 8)
static void
bfa_ioc_ct_map_port(struct bfa_ioc_s *ioc)
{
	void __iomem *rb = ioc->pcidev.pci_bar_kva;
	u32	r32;

	/*
	 * For catapult, base port id on personality register and IOC type
	 */
	r32 = readl(rb + FNC_PERS_REG);
	r32 >>= FNC_PERS_FN_SHIFT(bfa_ioc_pcifn(ioc));
	ioc->port_id = (r32 & __F0_PORT_MAP_MK) >> __F0_PORT_MAP_SH;

	bfa_trc(ioc, bfa_ioc_pcifn(ioc));
	bfa_trc(ioc, ioc->port_id);
}

static void
bfa_ioc_ct2_map_port(struct bfa_ioc_s *ioc)
{
	void __iomem	*rb = ioc->pcidev.pci_bar_kva;
	u32	r32;

	r32 = readl(rb + CT2_HOSTFN_PERSONALITY0);
	ioc->port_id = ((r32 & __FC_LL_PORT_MAP__MK) >> __FC_LL_PORT_MAP__SH);

	bfa_trc(ioc, bfa_ioc_pcifn(ioc));
	bfa_trc(ioc, ioc->port_id);
}

/*
 * Set interrupt mode for a function: INTX or MSIX
 */
static void
bfa_ioc_ct_isr_mode_set(struct bfa_ioc_s *ioc, bfa_boolean_t msix)
{
	void __iomem *rb = ioc->pcidev.pci_bar_kva;
	u32	r32, mode;

	r32 = readl(rb + FNC_PERS_REG);
	bfa_trc(ioc, r32);

	mode = (r32 >> FNC_PERS_FN_SHIFT(bfa_ioc_pcifn(ioc))) &
		__F0_INTX_STATUS;

	/*
	 * If already in desired mode, do not change anything
	 */
	if ((!msix && mode) || (msix && !mode))
		return;

	if (msix)
		mode = __F0_INTX_STATUS_MSIX;
	else
		mode = __F0_INTX_STATUS_INTA;

	r32 &= ~(__F0_INTX_STATUS << FNC_PERS_FN_SHIFT(bfa_ioc_pcifn(ioc)));
	r32 |= (mode << FNC_PERS_FN_SHIFT(bfa_ioc_pcifn(ioc)));
	bfa_trc(ioc, r32);

	writel(r32, rb + FNC_PERS_REG);
}

bfa_boolean_t
bfa_ioc_ct2_lpu_read_stat(struct bfa_ioc_s *ioc)
{
	u32	r32;

	r32 = readl(ioc->ioc_regs.lpu_read_stat);
	if (r32) {
		writel(1, ioc->ioc_regs.lpu_read_stat);
		return BFA_TRUE;
	}

	return BFA_FALSE;
}

/*
 * Cleanup hw semaphore and usecnt registers
 */
static void
bfa_ioc_ct_ownership_reset(struct bfa_ioc_s *ioc)
{

	bfa_ioc_sem_get(ioc->ioc_regs.ioc_usage_sem_reg);
	writel(0, ioc->ioc_regs.ioc_usage_reg);
	readl(ioc->ioc_regs.ioc_usage_sem_reg);
	writel(1, ioc->ioc_regs.ioc_usage_sem_reg);

	writel(0, ioc->ioc_regs.ioc_fail_sync);
	/*
	 * Read the hw sem reg to make sure that it is locked
	 * before we clear it. If it is not locked, writing 1
	 * will lock it instead of clearing it.
	 */
	readl(ioc->ioc_regs.ioc_sem_reg);
	writel(1, ioc->ioc_regs.ioc_sem_reg);
}

static bfa_boolean_t
bfa_ioc_ct_sync_start(struct bfa_ioc_s *ioc)
{
	uint32_t r32 = readl(ioc->ioc_regs.ioc_fail_sync);
	uint32_t sync_reqd = bfa_ioc_ct_get_sync_reqd(r32);

	/*
	 * Driver load time.  If the sync required bit for this PCI fn
	 * is set, it is due to an unclean exit by the driver for this
	 * PCI fn in the previous incarnation. Whoever comes here first
	 * should clean it up, no matter which PCI fn.
	 */

	if (sync_reqd & bfa_ioc_ct_sync_pos(ioc)) {
		writel(0, ioc->ioc_regs.ioc_fail_sync);
		writel(1, ioc->ioc_regs.ioc_usage_reg);
		writel(BFI_IOC_UNINIT, ioc->ioc_regs.ioc_fwstate);
		writel(BFI_IOC_UNINIT, ioc->ioc_regs.alt_ioc_fwstate);
		return BFA_TRUE;
	}

	return bfa_ioc_ct_sync_complete(ioc);
}

/*
 * Synchronized IOC failure processing routines
 */
static void
bfa_ioc_ct_sync_join(struct bfa_ioc_s *ioc)
{
	uint32_t r32 = readl(ioc->ioc_regs.ioc_fail_sync);
	uint32_t sync_pos = bfa_ioc_ct_sync_reqd_pos(ioc);

	writel((r32 | sync_pos), ioc->ioc_regs.ioc_fail_sync);
}

static void
bfa_ioc_ct_sync_leave(struct bfa_ioc_s *ioc)
{
	uint32_t r32 = readl(ioc->ioc_regs.ioc_fail_sync);
	uint32_t sync_msk = bfa_ioc_ct_sync_reqd_pos(ioc) |
					bfa_ioc_ct_sync_pos(ioc);

	writel((r32 & ~sync_msk), ioc->ioc_regs.ioc_fail_sync);
}

static void
bfa_ioc_ct_sync_ack(struct bfa_ioc_s *ioc)
{
	uint32_t r32 = readl(ioc->ioc_regs.ioc_fail_sync);

	writel((r32 | bfa_ioc_ct_sync_pos(ioc)),
		ioc->ioc_regs.ioc_fail_sync);
}

static bfa_boolean_t
bfa_ioc_ct_sync_complete(struct bfa_ioc_s *ioc)
{
	uint32_t r32 = readl(ioc->ioc_regs.ioc_fail_sync);
	uint32_t sync_reqd = bfa_ioc_ct_get_sync_reqd(r32);
	uint32_t sync_ackd = bfa_ioc_ct_get_sync_ackd(r32);
	uint32_t tmp_ackd;

	if (sync_ackd == 0)
		return BFA_TRUE;

	/*
	 * The check below is to see whether any other PCI fn
	 * has reinitialized the ASIC (reset sync_ackd bits)
	 * and failed again while this IOC was waiting for hw
	 * semaphore (in bfa_iocpf_sm_semwait()).
	 */
	tmp_ackd = sync_ackd;
	if ((sync_reqd &  bfa_ioc_ct_sync_pos(ioc)) &&
		!(sync_ackd & bfa_ioc_ct_sync_pos(ioc)))
		sync_ackd |= bfa_ioc_ct_sync_pos(ioc);

	if (sync_reqd == sync_ackd) {
		writel(bfa_ioc_ct_clear_sync_ackd(r32),
			ioc->ioc_regs.ioc_fail_sync);
		writel(BFI_IOC_FAIL, ioc->ioc_regs.ioc_fwstate);
		writel(BFI_IOC_FAIL, ioc->ioc_regs.alt_ioc_fwstate);
		return BFA_TRUE;
	}

	/*
	 * If another PCI fn reinitialized and failed again while
	 * this IOC was waiting for hw sem, the sync_ackd bit for
	 * this IOC need to be set again to allow reinitialization.
	 */
	if (tmp_ackd != sync_ackd)
		writel((r32 | sync_ackd), ioc->ioc_regs.ioc_fail_sync);

	return BFA_FALSE;
}

/**
 * Called from bfa_ioc_attach() to map asic specific calls.
 */
static void
bfa_ioc_set_ctx_hwif(struct bfa_ioc_s *ioc, struct bfa_ioc_hwif_s *hwif)
{
	hwif->ioc_firmware_lock = bfa_ioc_ct_firmware_lock;
	hwif->ioc_firmware_unlock = bfa_ioc_ct_firmware_unlock;
	hwif->ioc_notify_fail = bfa_ioc_ct_notify_fail;
	hwif->ioc_ownership_reset = bfa_ioc_ct_ownership_reset;
	hwif->ioc_sync_start = bfa_ioc_ct_sync_start;
	hwif->ioc_sync_join = bfa_ioc_ct_sync_join;
	hwif->ioc_sync_leave = bfa_ioc_ct_sync_leave;
	hwif->ioc_sync_ack = bfa_ioc_ct_sync_ack;
	hwif->ioc_sync_complete = bfa_ioc_ct_sync_complete;
	hwif->ioc_set_fwstate = bfa_ioc_ct_set_cur_ioc_fwstate;
	hwif->ioc_get_fwstate = bfa_ioc_ct_get_cur_ioc_fwstate;
	hwif->ioc_set_alt_fwstate = bfa_ioc_ct_set_alt_ioc_fwstate;
	hwif->ioc_get_alt_fwstate = bfa_ioc_ct_get_alt_ioc_fwstate;
}

/**
 * Called from bfa_ioc_attach() to map asic specific calls.
 */
void
bfa_ioc_set_ct_hwif(struct bfa_ioc_s *ioc)
{
	bfa_ioc_set_ctx_hwif(ioc, &hwif_ct);

	hwif_ct.ioc_pll_init = bfa_ioc_ct_pll_init;
	hwif_ct.ioc_reg_init = bfa_ioc_ct_reg_init;
	hwif_ct.ioc_map_port = bfa_ioc_ct_map_port;
	hwif_ct.ioc_isr_mode_set = bfa_ioc_ct_isr_mode_set;
	ioc->ioc_hwif = &hwif_ct;
}

/**
 * Called from bfa_ioc_attach() to map asic specific calls.
 */
void
bfa_ioc_set_ct2_hwif(struct bfa_ioc_s *ioc)
{
	bfa_ioc_set_ctx_hwif(ioc, &hwif_ct2);

	hwif_ct2.ioc_pll_init = bfa_ioc_ct2_pll_init;
	hwif_ct2.ioc_reg_init = bfa_ioc_ct2_reg_init;
	hwif_ct2.ioc_map_port = bfa_ioc_ct2_map_port;
	hwif_ct2.ioc_lpu_read_stat = bfa_ioc_ct2_lpu_read_stat;
	hwif_ct2.ioc_isr_mode_set = NULL;
	ioc->ioc_hwif = &hwif_ct2;
}

/*
 * Workaround for MSI-X resource allocation for catapult-2 with no asic block
 */
#define HOSTFN_MSIX_DEFAULT		64
#define HOSTFN_MSIX_VT_INDEX_MBOX_ERR	0x30138
#define HOSTFN_MSIX_VT_OFST_NUMVT	0x3013c
#define __MSIX_VT_NUMVT__MK		0x003ff800
#define __MSIX_VT_NUMVT__SH		11
#define __MSIX_VT_NUMVT_(_v)		((_v) << __MSIX_VT_NUMVT__SH)
#define __MSIX_VT_OFST_			0x000007ff
void
bfa_ioc_ct2_poweron(struct bfa_ioc_s *ioc)
{
	void __iomem *rb = ioc->pcidev.pci_bar_kva;
	u32	r32;

	r32 = readl(rb + HOSTFN_MSIX_VT_OFST_NUMVT);
	if (r32 & __MSIX_VT_NUMVT__MK) {
		writel(r32 & __MSIX_VT_OFST_,
			rb + HOSTFN_MSIX_VT_INDEX_MBOX_ERR);
		return;
	}

	writel(__MSIX_VT_NUMVT_(HOSTFN_MSIX_DEFAULT - 1) |
		HOSTFN_MSIX_DEFAULT * bfa_ioc_pcifn(ioc),
		rb + HOSTFN_MSIX_VT_OFST_NUMVT);
	writel(HOSTFN_MSIX_DEFAULT * bfa_ioc_pcifn(ioc),
		rb + HOSTFN_MSIX_VT_INDEX_MBOX_ERR);
}

bfa_status_t
bfa_ioc_ct_pll_init(void __iomem *rb, enum bfi_asic_mode mode)
{
	u32	pll_sclk, pll_fclk, r32;
	bfa_boolean_t fcmode = (mode == BFI_ASIC_MODE_FC);

	pll_sclk = __APP_PLL_SCLK_LRESETN | __APP_PLL_SCLK_ENARST |
		__APP_PLL_SCLK_RSEL200500 | __APP_PLL_SCLK_P0_1(3U) |
		__APP_PLL_SCLK_JITLMT0_1(3U) |
		__APP_PLL_SCLK_CNTLMT0_1(1U);
	pll_fclk = __APP_PLL_LCLK_LRESETN | __APP_PLL_LCLK_ENARST |
		__APP_PLL_LCLK_RSEL200500 | __APP_PLL_LCLK_P0_1(3U) |
		__APP_PLL_LCLK_JITLMT0_1(3U) |
		__APP_PLL_LCLK_CNTLMT0_1(1U);

	if (fcmode) {
		writel(0, (rb + OP_MODE));
		writel(__APP_EMS_CMLCKSEL | __APP_EMS_REFCKBUFEN2 |
			 __APP_EMS_CHANNEL_SEL, (rb + ETH_MAC_SER_REG));
	} else {
		writel(__GLOBAL_FCOE_MODE, (rb + OP_MODE));
		writel(__APP_EMS_REFCKBUFEN1, (rb + ETH_MAC_SER_REG));
	}
	writel(BFI_IOC_UNINIT, (rb + BFA_IOC0_STATE_REG));
	writel(BFI_IOC_UNINIT, (rb + BFA_IOC1_STATE_REG));
	writel(0xffffffffU, (rb + HOSTFN0_INT_MSK));
	writel(0xffffffffU, (rb + HOSTFN1_INT_MSK));
	writel(0xffffffffU, (rb + HOSTFN0_INT_STATUS));
	writel(0xffffffffU, (rb + HOSTFN1_INT_STATUS));
	writel(0xffffffffU, (rb + HOSTFN0_INT_MSK));
	writel(0xffffffffU, (rb + HOSTFN1_INT_MSK));
	writel(pll_sclk | __APP_PLL_SCLK_LOGIC_SOFT_RESET,
			rb + APP_PLL_SCLK_CTL_REG);
	writel(pll_fclk | __APP_PLL_LCLK_LOGIC_SOFT_RESET,
			rb + APP_PLL_LCLK_CTL_REG);
	writel(pll_sclk | __APP_PLL_SCLK_LOGIC_SOFT_RESET |
		__APP_PLL_SCLK_ENABLE, rb + APP_PLL_SCLK_CTL_REG);
	writel(pll_fclk | __APP_PLL_LCLK_LOGIC_SOFT_RESET |
		__APP_PLL_LCLK_ENABLE, rb + APP_PLL_LCLK_CTL_REG);
	readl(rb + HOSTFN0_INT_MSK);
	udelay(2000);
	writel(0xffffffffU, (rb + HOSTFN0_INT_STATUS));
	writel(0xffffffffU, (rb + HOSTFN1_INT_STATUS));
	writel(pll_sclk | __APP_PLL_SCLK_ENABLE, rb + APP_PLL_SCLK_CTL_REG);
	writel(pll_fclk | __APP_PLL_LCLK_ENABLE, rb + APP_PLL_LCLK_CTL_REG);

	if (!fcmode) {
		writel(__PMM_1T_RESET_P, (rb + PMM_1T_RESET_REG_P0));
		writel(__PMM_1T_RESET_P, (rb + PMM_1T_RESET_REG_P1));
	}
	r32 = readl((rb + PSS_CTL_REG));
	r32 &= ~__PSS_LMEM_RESET;
	writel(r32, (rb + PSS_CTL_REG));
	udelay(1000);
	if (!fcmode) {
		writel(0, (rb + PMM_1T_RESET_REG_P0));
		writel(0, (rb + PMM_1T_RESET_REG_P1));
	}

	writel(__EDRAM_BISTR_START, (rb + MBIST_CTL_REG));
	udelay(1000);
	r32 = readl((rb + MBIST_STAT_REG));
	writel(0, (rb + MBIST_CTL_REG));
	return BFA_STATUS_OK;
}

static void
bfa_ioc_ct2_sclk_init(void __iomem *rb)
{
	u32 r32;

	/*
	 * put s_clk PLL and PLL FSM in reset
	 */
	r32 = readl((rb + CT2_APP_PLL_SCLK_CTL_REG));
	r32 &= ~(__APP_PLL_SCLK_ENABLE | __APP_PLL_SCLK_LRESETN);
	r32 |= (__APP_PLL_SCLK_ENARST | __APP_PLL_SCLK_BYPASS |
		__APP_PLL_SCLK_LOGIC_SOFT_RESET);
	writel(r32, (rb + CT2_APP_PLL_SCLK_CTL_REG));

	/*
	 * Ignore mode and program for the max clock (which is FC16)
	 * Firmware/NFC will do the PLL init appropiately
	 */
	r32 = readl((rb + CT2_APP_PLL_SCLK_CTL_REG));
	r32 &= ~(__APP_PLL_SCLK_REFCLK_SEL | __APP_PLL_SCLK_CLK_DIV2);
	writel(r32, (rb + CT2_APP_PLL_SCLK_CTL_REG));

	/*
	 * while doing PLL init dont clock gate ethernet subsystem
	 */
	r32 = readl((rb + CT2_CHIP_MISC_PRG));
	writel(r32 | __ETH_CLK_ENABLE_PORT0, (rb + CT2_CHIP_MISC_PRG));

	r32 = readl((rb + CT2_PCIE_MISC_REG));
	writel(r32 | __ETH_CLK_ENABLE_PORT1, (rb + CT2_PCIE_MISC_REG));

	/*
	 * set sclk value
	 */
	r32 = readl((rb + CT2_APP_PLL_SCLK_CTL_REG));
	r32 &= (__P_SCLK_PLL_LOCK | __APP_PLL_SCLK_REFCLK_SEL |
		__APP_PLL_SCLK_CLK_DIV2);
	writel(r32 | 0x1061731b, (rb + CT2_APP_PLL_SCLK_CTL_REG));

	/*
	 * poll for s_clk lock or delay 1ms
	 */
	udelay(1000);
}

static void
bfa_ioc_ct2_lclk_init(void __iomem *rb)
{
	u32 r32;

	/*
	 * put l_clk PLL and PLL FSM in reset
	 */
	r32 = readl((rb + CT2_APP_PLL_LCLK_CTL_REG));
	r32 &= ~(__APP_PLL_LCLK_ENABLE | __APP_PLL_LCLK_LRESETN);
	r32 |= (__APP_PLL_LCLK_ENARST | __APP_PLL_LCLK_BYPASS |
		__APP_PLL_LCLK_LOGIC_SOFT_RESET);
	writel(r32, (rb + CT2_APP_PLL_LCLK_CTL_REG));

	/*
	 * set LPU speed (set for FC16 which will work for other modes)
	 */
	r32 = readl((rb + CT2_CHIP_MISC_PRG));
	writel(r32, (rb + CT2_CHIP_MISC_PRG));

	/*
	 * set LPU half speed (set for FC16 which will work for other modes)
	 */
	r32 = readl((rb + CT2_APP_PLL_LCLK_CTL_REG));
	writel(r32, (rb + CT2_APP_PLL_LCLK_CTL_REG));

	/*
	 * set lclk for mode (set for FC16)
	 */
	r32 = readl((rb + CT2_APP_PLL_LCLK_CTL_REG));
	r32 &= (__P_LCLK_PLL_LOCK | __APP_LPUCLK_HALFSPEED);
	r32 |= 0x20c1731b;
	writel(r32, (rb + CT2_APP_PLL_LCLK_CTL_REG));

	/*
	 * poll for s_clk lock or delay 1ms
	 */
	udelay(1000);
}

static void
bfa_ioc_ct2_mem_init(void __iomem *rb)
{
	u32	r32;

	r32 = readl((rb + PSS_CTL_REG));
	r32 &= ~__PSS_LMEM_RESET;
	writel(r32, (rb + PSS_CTL_REG));
	udelay(1000);

	writel(__EDRAM_BISTR_START, (rb + CT2_MBIST_CTL_REG));
	udelay(1000);
	writel(0, (rb + CT2_MBIST_CTL_REG));
}

void
bfa_ioc_ct2_mac_reset(void __iomem *rb)
{
	/* put port0, port1 MAC & AHB in reset */
	writel((__CSI_MAC_RESET | __CSI_MAC_AHB_RESET),
		rb + CT2_CSI_MAC_CONTROL_REG(0));
	writel((__CSI_MAC_RESET | __CSI_MAC_AHB_RESET),
		rb + CT2_CSI_MAC_CONTROL_REG(1));
}

static void
bfa_ioc_ct2_enable_flash(void __iomem *rb)
{
	u32 r32;

	r32 = readl((rb + PSS_GPIO_OUT_REG));
	writel(r32 & ~1, (rb + PSS_GPIO_OUT_REG));
	r32 = readl((rb + PSS_GPIO_OE_REG));
	writel(r32 | 1, (rb + PSS_GPIO_OE_REG));
}

#define CT2_NFC_MAX_DELAY	1000
#define CT2_NFC_PAUSE_MAX_DELAY 4000
#define CT2_NFC_VER_VALID	0x147
#define CT2_NFC_STATE_RUNNING   0x20000001
#define BFA_IOC_PLL_POLL	1000000

static bfa_boolean_t
bfa_ioc_ct2_nfc_halted(void __iomem *rb)
{
	u32	r32;

	r32 = readl(rb + CT2_NFC_CSR_SET_REG);
	if (r32 & __NFC_CONTROLLER_HALTED)
		return BFA_TRUE;

	return BFA_FALSE;
}

static void
bfa_ioc_ct2_nfc_halt(void __iomem *rb)
{
	int	i;

	writel(__HALT_NFC_CONTROLLER, rb + CT2_NFC_CSR_SET_REG);
	for (i = 0; i < CT2_NFC_MAX_DELAY; i++) {
		if (bfa_ioc_ct2_nfc_halted(rb))
			break;
		udelay(1000);
	}
	WARN_ON(!bfa_ioc_ct2_nfc_halted(rb));
}

static void
bfa_ioc_ct2_nfc_resume(void __iomem *rb)
{
	u32	r32;
	int i;

	writel(__HALT_NFC_CONTROLLER, rb + CT2_NFC_CSR_CLR_REG);
	for (i = 0; i < CT2_NFC_MAX_DELAY; i++) {
		r32 = readl(rb + CT2_NFC_CSR_SET_REG);
		if (!(r32 & __NFC_CONTROLLER_HALTED))
			return;
		udelay(1000);
	}
	WARN_ON(1);
}

static void
bfa_ioc_ct2_clk_reset(void __iomem *rb)
{
	u32 r32;

	bfa_ioc_ct2_sclk_init(rb);
	bfa_ioc_ct2_lclk_init(rb);

	/*
	 * release soft reset on s_clk & l_clk
	 */
	r32 = readl((rb + CT2_APP_PLL_SCLK_CTL_REG));
	writel(r32 & ~__APP_PLL_SCLK_LOGIC_SOFT_RESET,
			(rb + CT2_APP_PLL_SCLK_CTL_REG));

	r32 = readl((rb + CT2_APP_PLL_LCLK_CTL_REG));
	writel(r32 & ~__APP_PLL_LCLK_LOGIC_SOFT_RESET,
			(rb + CT2_APP_PLL_LCLK_CTL_REG));

}

static void
bfa_ioc_ct2_nfc_clk_reset(void __iomem *rb)
{
	u32 r32, i;

	r32 = readl((rb + PSS_CTL_REG));
	r32 |= (__PSS_LPU0_RESET | __PSS_LPU1_RESET);
	writel(r32, (rb + PSS_CTL_REG));

	writel(__RESET_AND_START_SCLK_LCLK_PLLS, rb + CT2_CSI_FW_CTL_SET_REG);

	for (i = 0; i < BFA_IOC_PLL_POLL; i++) {
		r32 = readl(rb + CT2_NFC_FLASH_STS_REG);

		if ((r32 & __FLASH_PLL_INIT_AND_RESET_IN_PROGRESS))
			break;
	}
	WARN_ON(!(r32 & __FLASH_PLL_INIT_AND_RESET_IN_PROGRESS));

	for (i = 0; i < BFA_IOC_PLL_POLL; i++) {
		r32 = readl(rb + CT2_NFC_FLASH_STS_REG);

		if (!(r32 & __FLASH_PLL_INIT_AND_RESET_IN_PROGRESS))
			break;
	}
	WARN_ON((r32 & __FLASH_PLL_INIT_AND_RESET_IN_PROGRESS));

	r32 = readl(rb + CT2_CSI_FW_CTL_REG);
	WARN_ON((r32 & __RESET_AND_START_SCLK_LCLK_PLLS));
}

static void
bfa_ioc_ct2_wait_till_nfc_running(void __iomem *rb)
{
	u32 r32;
	int i;

	if (bfa_ioc_ct2_nfc_halted(rb))
		bfa_ioc_ct2_nfc_resume(rb);
	for (i = 0; i < CT2_NFC_PAUSE_MAX_DELAY; i++) {
		r32 = readl(rb + CT2_NFC_STS_REG);
		if (r32 == CT2_NFC_STATE_RUNNING)
			return;
		udelay(1000);
	}

	r32 = readl(rb + CT2_NFC_STS_REG);
	WARN_ON(!(r32 == CT2_NFC_STATE_RUNNING));
}

bfa_status_t
bfa_ioc_ct2_pll_init(void __iomem *rb, enum bfi_asic_mode mode)
{
	u32 wgn, r32, nfc_ver;

	wgn = readl(rb + CT2_WGN_STATUS);

	if (wgn == (__WGN_READY | __GLBL_PF_VF_CFG_RDY)) {
		/*
		 * If flash is corrupted, enable flash explicitly
		 */
		bfa_ioc_ct2_clk_reset(rb);
		bfa_ioc_ct2_enable_flash(rb);

		bfa_ioc_ct2_mac_reset(rb);

		bfa_ioc_ct2_clk_reset(rb);
		bfa_ioc_ct2_enable_flash(rb);

	} else {
		nfc_ver = readl(rb + CT2_RSC_GPR15_REG);

		if ((nfc_ver >= CT2_NFC_VER_VALID) &&
		    (wgn == (__A2T_AHB_LOAD | __WGN_READY))) {

			bfa_ioc_ct2_wait_till_nfc_running(rb);

			bfa_ioc_ct2_nfc_clk_reset(rb);
		} else {
			bfa_ioc_ct2_nfc_halt(rb);

			bfa_ioc_ct2_clk_reset(rb);
			bfa_ioc_ct2_mac_reset(rb);
			bfa_ioc_ct2_clk_reset(rb);

		}
	}
	/*
	* The very first PCIe DMA Read done by LPU fails with a fatal error,
	* when Address Translation Cache (ATC) has been enabled by system BIOS.
	*
	* Workaround:
	* Disable Invalidated Tag Match Enable capability by setting the bit 26
	* of CHIP_MISC_PRG to 0, by default it is set to 1.
	*/
	r32 = readl(rb + CT2_CHIP_MISC_PRG);
	writel((r32 & 0xfbffffff), (rb + CT2_CHIP_MISC_PRG));

	/*
	 * Mask the interrupts and clear any
	 * pending interrupts left by BIOS/EFI
	 */

	writel(1, (rb + CT2_LPU0_HOSTFN_MBOX0_MSK));
	writel(1, (rb + CT2_LPU1_HOSTFN_MBOX0_MSK));

	/* For first time initialization, no need to clear interrupts */
	r32 = readl(rb + HOST_SEM5_REG);
	if (r32 & 0x1) {
		r32 = readl((rb + CT2_LPU0_HOSTFN_CMD_STAT));
		if (r32 == 1) {
			writel(1, (rb + CT2_LPU0_HOSTFN_CMD_STAT));
			readl((rb + CT2_LPU0_HOSTFN_CMD_STAT));
		}
		r32 = readl((rb + CT2_LPU1_HOSTFN_CMD_STAT));
		if (r32 == 1) {
			writel(1, (rb + CT2_LPU1_HOSTFN_CMD_STAT));
			readl((rb + CT2_LPU1_HOSTFN_CMD_STAT));
		}
	}

	bfa_ioc_ct2_mem_init(rb);

	writel(BFI_IOC_UNINIT, (rb + CT2_BFA_IOC0_STATE_REG));
	writel(BFI_IOC_UNINIT, (rb + CT2_BFA_IOC1_STATE_REG));

	return BFA_STATUS_OK;
}

static void
bfa_ioc_ct_set_cur_ioc_fwstate(struct bfa_ioc_s *ioc,
		enum bfi_ioc_state fwstate)
{
	writel(fwstate, ioc->ioc_regs.ioc_fwstate);
}

static enum bfi_ioc_state
bfa_ioc_ct_get_cur_ioc_fwstate(struct bfa_ioc_s *ioc)
{
	return (enum bfi_ioc_state)readl(ioc->ioc_regs.ioc_fwstate);
}

static void
bfa_ioc_ct_set_alt_ioc_fwstate(struct bfa_ioc_s *ioc,
		enum bfi_ioc_state fwstate)
{
	writel(fwstate, ioc->ioc_regs.alt_ioc_fwstate);
}

static enum bfi_ioc_state
bfa_ioc_ct_get_alt_ioc_fwstate(struct bfa_ioc_s *ioc)
{
	return (enum bfi_ioc_state) readl(ioc->ioc_regs.alt_ioc_fwstate);
}
