/*
 * Linux network driver for Brocade Converged Network Adapter.
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
/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 */

#include "bfa_ioc.h"
#include "cna.h"
#include "bfi.h"
#include "bfi_ctreg.h"
#include "bfa_defs.h"

/*
 * forward declarations
 */
static bool bfa_ioc_ct_firmware_lock(struct bfa_ioc *ioc);
static void bfa_ioc_ct_firmware_unlock(struct bfa_ioc *ioc);
static void bfa_ioc_ct_reg_init(struct bfa_ioc *ioc);
static void bfa_ioc_ct_map_port(struct bfa_ioc *ioc);
static void bfa_ioc_ct_isr_mode_set(struct bfa_ioc *ioc, bool msix);
static void bfa_ioc_ct_notify_hbfail(struct bfa_ioc *ioc);
static void bfa_ioc_ct_ownership_reset(struct bfa_ioc *ioc);
static enum bfa_status bfa_ioc_ct_pll_init(void __iomem *rb, bool fcmode);

static struct bfa_ioc_hwif nw_hwif_ct;

/**
 * Called from bfa_ioc_attach() to map asic specific calls.
 */
void
bfa_nw_ioc_set_ct_hwif(struct bfa_ioc *ioc)
{
	nw_hwif_ct.ioc_pll_init = bfa_ioc_ct_pll_init;
	nw_hwif_ct.ioc_firmware_lock = bfa_ioc_ct_firmware_lock;
	nw_hwif_ct.ioc_firmware_unlock = bfa_ioc_ct_firmware_unlock;
	nw_hwif_ct.ioc_reg_init = bfa_ioc_ct_reg_init;
	nw_hwif_ct.ioc_map_port = bfa_ioc_ct_map_port;
	nw_hwif_ct.ioc_isr_mode_set = bfa_ioc_ct_isr_mode_set;
	nw_hwif_ct.ioc_notify_hbfail = bfa_ioc_ct_notify_hbfail;
	nw_hwif_ct.ioc_ownership_reset = bfa_ioc_ct_ownership_reset;

	ioc->ioc_hwif = &nw_hwif_ct;
}

/**
 * Return true if firmware of current driver matches the running firmware.
 */
static bool
bfa_ioc_ct_firmware_lock(struct bfa_ioc *ioc)
{
	enum bfi_ioc_state ioc_fwstate;
	u32 usecnt;
	struct bfi_ioc_image_hdr fwhdr;

	/**
	 * Firmware match check is relevant only for CNA.
	 */
	if (!ioc->cna)
		return true;

	/**
	 * If bios boot (flash based) -- do not increment usage count
	 */
	if (bfa_cb_image_get_size(BFA_IOC_FWIMG_TYPE(ioc)) <
						BFA_IOC_FWIMG_MINSZ)
		return true;

	bfa_nw_ioc_sem_get(ioc->ioc_regs.ioc_usage_sem_reg);
	usecnt = readl(ioc->ioc_regs.ioc_usage_reg);

	/**
	 * If usage count is 0, always return TRUE.
	 */
	if (usecnt == 0) {
		writel(1, ioc->ioc_regs.ioc_usage_reg);
		bfa_nw_ioc_sem_release(ioc->ioc_regs.ioc_usage_sem_reg);
		return true;
	}

	ioc_fwstate = readl(ioc->ioc_regs.ioc_fwstate);

	/**
	 * Use count cannot be non-zero and chip in uninitialized state.
	 */
	BUG_ON(!(ioc_fwstate != BFI_IOC_UNINIT));

	/**
	 * Check if another driver with a different firmware is active
	 */
	bfa_nw_ioc_fwver_get(ioc, &fwhdr);
	if (!bfa_nw_ioc_fwver_cmp(ioc, &fwhdr)) {
		bfa_nw_ioc_sem_release(ioc->ioc_regs.ioc_usage_sem_reg);
		return false;
	}

	/**
	 * Same firmware version. Increment the reference count.
	 */
	usecnt++;
	writel(usecnt, ioc->ioc_regs.ioc_usage_reg);
	bfa_nw_ioc_sem_release(ioc->ioc_regs.ioc_usage_sem_reg);
	return true;
}

static void
bfa_ioc_ct_firmware_unlock(struct bfa_ioc *ioc)
{
	u32 usecnt;

	/**
	 * Firmware lock is relevant only for CNA.
	 */
	if (!ioc->cna)
		return;

	/**
	 * If bios boot (flash based) -- do not decrement usage count
	 */
	if (bfa_cb_image_get_size(BFA_IOC_FWIMG_TYPE(ioc)) <
						BFA_IOC_FWIMG_MINSZ)
		return;

	/**
	 * decrement usage count
	 */
	bfa_nw_ioc_sem_get(ioc->ioc_regs.ioc_usage_sem_reg);
	usecnt = readl(ioc->ioc_regs.ioc_usage_reg);
	BUG_ON(!(usecnt > 0));

	usecnt--;
	writel(usecnt, ioc->ioc_regs.ioc_usage_reg);

	bfa_nw_ioc_sem_release(ioc->ioc_regs.ioc_usage_sem_reg);
}

/**
 * Notify other functions on HB failure.
 */
static void
bfa_ioc_ct_notify_hbfail(struct bfa_ioc *ioc)
{
	if (ioc->cna) {
		writel(__FW_INIT_HALT_P, ioc->ioc_regs.ll_halt);
		/* Wait for halt to take effect */
		readl(ioc->ioc_regs.ll_halt);
	} else {
		writel(__PSS_ERR_STATUS_SET, ioc->ioc_regs.err_set);
		readl(ioc->ioc_regs.err_set);
	}
}

/**
 * Host to LPU mailbox message addresses
 */
static struct { u32 hfn_mbox, lpu_mbox, hfn_pgn; } iocreg_fnreg[] = {
	{ HOSTFN0_LPU_MBOX0_0, LPU_HOSTFN0_MBOX0_0, HOST_PAGE_NUM_FN0 },
	{ HOSTFN1_LPU_MBOX0_8, LPU_HOSTFN1_MBOX0_8, HOST_PAGE_NUM_FN1 },
	{ HOSTFN2_LPU_MBOX0_0, LPU_HOSTFN2_MBOX0_0, HOST_PAGE_NUM_FN2 },
	{ HOSTFN3_LPU_MBOX0_8, LPU_HOSTFN3_MBOX0_8, HOST_PAGE_NUM_FN3 }
};

/**
 * Host <-> LPU mailbox command/status registers - port 0
 */
static struct { u32 hfn, lpu; } iocreg_mbcmd_p0[] = {
	{ HOSTFN0_LPU0_MBOX0_CMD_STAT, LPU0_HOSTFN0_MBOX0_CMD_STAT },
	{ HOSTFN1_LPU0_MBOX0_CMD_STAT, LPU0_HOSTFN1_MBOX0_CMD_STAT },
	{ HOSTFN2_LPU0_MBOX0_CMD_STAT, LPU0_HOSTFN2_MBOX0_CMD_STAT },
	{ HOSTFN3_LPU0_MBOX0_CMD_STAT, LPU0_HOSTFN3_MBOX0_CMD_STAT }
};

/**
 * Host <-> LPU mailbox command/status registers - port 1
 */
static struct { u32 hfn, lpu; } iocreg_mbcmd_p1[] = {
	{ HOSTFN0_LPU1_MBOX0_CMD_STAT, LPU1_HOSTFN0_MBOX0_CMD_STAT },
	{ HOSTFN1_LPU1_MBOX0_CMD_STAT, LPU1_HOSTFN1_MBOX0_CMD_STAT },
	{ HOSTFN2_LPU1_MBOX0_CMD_STAT, LPU1_HOSTFN2_MBOX0_CMD_STAT },
	{ HOSTFN3_LPU1_MBOX0_CMD_STAT, LPU1_HOSTFN3_MBOX0_CMD_STAT }
};

static void
bfa_ioc_ct_reg_init(struct bfa_ioc *ioc)
{
	void __iomem *rb;
	int		pcifn = bfa_ioc_pcifn(ioc);

	rb = bfa_ioc_bar0(ioc);

	ioc->ioc_regs.hfn_mbox = rb + iocreg_fnreg[pcifn].hfn_mbox;
	ioc->ioc_regs.lpu_mbox = rb + iocreg_fnreg[pcifn].lpu_mbox;
	ioc->ioc_regs.host_page_num_fn = rb + iocreg_fnreg[pcifn].hfn_pgn;

	if (ioc->port_id == 0) {
		ioc->ioc_regs.heartbeat = rb + BFA_IOC0_HBEAT_REG;
		ioc->ioc_regs.ioc_fwstate = rb + BFA_IOC0_STATE_REG;
		ioc->ioc_regs.hfn_mbox_cmd = rb + iocreg_mbcmd_p0[pcifn].hfn;
		ioc->ioc_regs.lpu_mbox_cmd = rb + iocreg_mbcmd_p0[pcifn].lpu;
		ioc->ioc_regs.ll_halt = rb + FW_INIT_HALT_P0;
	} else {
		ioc->ioc_regs.heartbeat = (rb + BFA_IOC1_HBEAT_REG);
		ioc->ioc_regs.ioc_fwstate = (rb + BFA_IOC1_STATE_REG);
		ioc->ioc_regs.hfn_mbox_cmd = rb + iocreg_mbcmd_p1[pcifn].hfn;
		ioc->ioc_regs.lpu_mbox_cmd = rb + iocreg_mbcmd_p1[pcifn].lpu;
		ioc->ioc_regs.ll_halt = rb + FW_INIT_HALT_P1;
	}

	/*
	 * PSS control registers
	 */
	ioc->ioc_regs.pss_ctl_reg = (rb + PSS_CTL_REG);
	ioc->ioc_regs.pss_err_status_reg = (rb + PSS_ERR_STATUS_REG);
	ioc->ioc_regs.app_pll_fast_ctl_reg = (rb + APP_PLL_425_CTL_REG);
	ioc->ioc_regs.app_pll_slow_ctl_reg = (rb + APP_PLL_312_CTL_REG);

	/*
	 * IOC semaphore registers and serialization
	 */
	ioc->ioc_regs.ioc_sem_reg = (rb + HOST_SEM0_REG);
	ioc->ioc_regs.ioc_usage_sem_reg = (rb + HOST_SEM1_REG);
	ioc->ioc_regs.ioc_init_sem_reg = (rb + HOST_SEM2_REG);
	ioc->ioc_regs.ioc_usage_reg = (rb + BFA_FW_USE_COUNT);

	/**
	 * sram memory access
	 */
	ioc->ioc_regs.smem_page_start = (rb + PSS_SMEM_PAGE_START);
	ioc->ioc_regs.smem_pg0 = BFI_IOC_SMEM_PG0_CT;

	/*
	 * err set reg : for notification of hb failure in fcmode
	 */
	ioc->ioc_regs.err_set = (rb + ERR_SET_REG);
}

/**
 * Initialize IOC to port mapping.
 */

#define FNC_PERS_FN_SHIFT(__fn)	((__fn) * 8)
static void
bfa_ioc_ct_map_port(struct bfa_ioc *ioc)
{
	void __iomem *rb = ioc->pcidev.pci_bar_kva;
	u32	r32;

	/**
	 * For catapult, base port id on personality register and IOC type
	 */
	r32 = readl(rb + FNC_PERS_REG);
	r32 >>= FNC_PERS_FN_SHIFT(bfa_ioc_pcifn(ioc));
	ioc->port_id = (r32 & __F0_PORT_MAP_MK) >> __F0_PORT_MAP_SH;

}

/**
 * Set interrupt mode for a function: INTX or MSIX
 */
static void
bfa_ioc_ct_isr_mode_set(struct bfa_ioc *ioc, bool msix)
{
	void __iomem *rb = ioc->pcidev.pci_bar_kva;
	u32	r32, mode;

	r32 = readl(rb + FNC_PERS_REG);

	mode = (r32 >> FNC_PERS_FN_SHIFT(bfa_ioc_pcifn(ioc))) &
		__F0_INTX_STATUS;

	/**
	 * If already in desired mode, do not change anything
	 */
	if (!msix && mode)
		return;

	if (msix)
		mode = __F0_INTX_STATUS_MSIX;
	else
		mode = __F0_INTX_STATUS_INTA;

	r32 &= ~(__F0_INTX_STATUS << FNC_PERS_FN_SHIFT(bfa_ioc_pcifn(ioc)));
	r32 |= (mode << FNC_PERS_FN_SHIFT(bfa_ioc_pcifn(ioc)));

	writel(r32, rb + FNC_PERS_REG);
}

/**
 * Cleanup hw semaphore and usecnt registers
 */
static void
bfa_ioc_ct_ownership_reset(struct bfa_ioc *ioc)
{
	if (ioc->cna) {
		bfa_nw_ioc_sem_get(ioc->ioc_regs.ioc_usage_sem_reg);
		writel(0, ioc->ioc_regs.ioc_usage_reg);
		bfa_nw_ioc_sem_release(ioc->ioc_regs.ioc_usage_sem_reg);
	}

	/*
	 * Read the hw sem reg to make sure that it is locked
	 * before we clear it. If it is not locked, writing 1
	 * will lock it instead of clearing it.
	 */
	readl(ioc->ioc_regs.ioc_sem_reg);
	bfa_nw_ioc_hw_sem_release(ioc);
}

static enum bfa_status
bfa_ioc_ct_pll_init(void __iomem *rb, bool fcmode)
{
	u32	pll_sclk, pll_fclk, r32;

	pll_sclk = __APP_PLL_312_LRESETN | __APP_PLL_312_ENARST |
		__APP_PLL_312_RSEL200500 | __APP_PLL_312_P0_1(3U) |
		__APP_PLL_312_JITLMT0_1(3U) |
		__APP_PLL_312_CNTLMT0_1(1U);
	pll_fclk = __APP_PLL_425_LRESETN | __APP_PLL_425_ENARST |
		__APP_PLL_425_RSEL200500 | __APP_PLL_425_P0_1(3U) |
		__APP_PLL_425_JITLMT0_1(3U) |
		__APP_PLL_425_CNTLMT0_1(1U);
	if (fcmode) {
		writel(0, (rb + OP_MODE));
		writel(__APP_EMS_CMLCKSEL |
				__APP_EMS_REFCKBUFEN2 |
				__APP_EMS_CHANNEL_SEL,
				(rb + ETH_MAC_SER_REG));
	} else {
		writel(__GLOBAL_FCOE_MODE, (rb + OP_MODE));
		writel(__APP_EMS_REFCKBUFEN1,
				(rb + ETH_MAC_SER_REG));
	}
	writel(BFI_IOC_UNINIT, (rb + BFA_IOC0_STATE_REG));
	writel(BFI_IOC_UNINIT, (rb + BFA_IOC1_STATE_REG));
	writel(0xffffffffU, (rb + HOSTFN0_INT_MSK));
	writel(0xffffffffU, (rb + HOSTFN1_INT_MSK));
	writel(0xffffffffU, (rb + HOSTFN0_INT_STATUS));
	writel(0xffffffffU, (rb + HOSTFN1_INT_STATUS));
	writel(0xffffffffU, (rb + HOSTFN0_INT_MSK));
	writel(0xffffffffU, (rb + HOSTFN1_INT_MSK));
	writel(pll_sclk |
		__APP_PLL_312_LOGIC_SOFT_RESET,
		rb + APP_PLL_312_CTL_REG);
	writel(pll_fclk |
		__APP_PLL_425_LOGIC_SOFT_RESET,
		rb + APP_PLL_425_CTL_REG);
	writel(pll_sclk |
		__APP_PLL_312_LOGIC_SOFT_RESET | __APP_PLL_312_ENABLE,
		rb + APP_PLL_312_CTL_REG);
	writel(pll_fclk |
		__APP_PLL_425_LOGIC_SOFT_RESET | __APP_PLL_425_ENABLE,
		rb + APP_PLL_425_CTL_REG);
	readl(rb + HOSTFN0_INT_MSK);
	udelay(2000);
	writel(0xffffffffU, (rb + HOSTFN0_INT_STATUS));
	writel(0xffffffffU, (rb + HOSTFN1_INT_STATUS));
	writel(pll_sclk |
		__APP_PLL_312_ENABLE,
		rb + APP_PLL_312_CTL_REG);
	writel(pll_fclk |
		__APP_PLL_425_ENABLE,
		rb + APP_PLL_425_CTL_REG);
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
