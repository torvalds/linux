/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
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

#include "bfa_ioc.h"
#include "bfi_cbreg.h"
#include "bfa_defs.h"

BFA_TRC_FILE(CNA, IOC_CB);

/*
 * forward declarations
 */
static bfa_boolean_t bfa_ioc_cb_firmware_lock(struct bfa_ioc_s *ioc);
static void bfa_ioc_cb_firmware_unlock(struct bfa_ioc_s *ioc);
static void bfa_ioc_cb_reg_init(struct bfa_ioc_s *ioc);
static void bfa_ioc_cb_map_port(struct bfa_ioc_s *ioc);
static void bfa_ioc_cb_isr_mode_set(struct bfa_ioc_s *ioc, bfa_boolean_t msix);
static void bfa_ioc_cb_notify_hbfail(struct bfa_ioc_s *ioc);
static void bfa_ioc_cb_ownership_reset(struct bfa_ioc_s *ioc);

struct bfa_ioc_hwif_s hwif_cb;

/*
 * Called from bfa_ioc_attach() to map asic specific calls.
 */
void
bfa_ioc_set_cb_hwif(struct bfa_ioc_s *ioc)
{
	hwif_cb.ioc_pll_init = bfa_ioc_cb_pll_init;
	hwif_cb.ioc_firmware_lock = bfa_ioc_cb_firmware_lock;
	hwif_cb.ioc_firmware_unlock = bfa_ioc_cb_firmware_unlock;
	hwif_cb.ioc_reg_init = bfa_ioc_cb_reg_init;
	hwif_cb.ioc_map_port = bfa_ioc_cb_map_port;
	hwif_cb.ioc_isr_mode_set = bfa_ioc_cb_isr_mode_set;
	hwif_cb.ioc_notify_hbfail = bfa_ioc_cb_notify_hbfail;
	hwif_cb.ioc_ownership_reset = bfa_ioc_cb_ownership_reset;

	ioc->ioc_hwif = &hwif_cb;
}

/*
 * Return true if firmware of current driver matches the running firmware.
 */
static bfa_boolean_t
bfa_ioc_cb_firmware_lock(struct bfa_ioc_s *ioc)
{
	return BFA_TRUE;
}

static void
bfa_ioc_cb_firmware_unlock(struct bfa_ioc_s *ioc)
{
}

/*
 * Notify other functions on HB failure.
 */
static void
bfa_ioc_cb_notify_hbfail(struct bfa_ioc_s *ioc)
{
	writel(__PSS_ERR_STATUS_SET, ioc->ioc_regs.err_set);
	readl(ioc->ioc_regs.err_set);
}

/*
 * Host to LPU mailbox message addresses
 */
static struct { u32 hfn_mbox, lpu_mbox, hfn_pgn; } iocreg_fnreg[] = {
	{ HOSTFN0_LPU_MBOX0_0, LPU_HOSTFN0_MBOX0_0, HOST_PAGE_NUM_FN0 },
	{ HOSTFN1_LPU_MBOX0_8, LPU_HOSTFN1_MBOX0_8, HOST_PAGE_NUM_FN1 }
};

/*
 * Host <-> LPU mailbox command/status registers
 */
static struct { u32 hfn, lpu; } iocreg_mbcmd[] = {

	{ HOSTFN0_LPU0_CMD_STAT, LPU0_HOSTFN0_CMD_STAT },
	{ HOSTFN1_LPU1_CMD_STAT, LPU1_HOSTFN1_CMD_STAT }
};

static void
bfa_ioc_cb_reg_init(struct bfa_ioc_s *ioc)
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
	} else {
		ioc->ioc_regs.heartbeat = (rb + BFA_IOC1_HBEAT_REG);
		ioc->ioc_regs.ioc_fwstate = (rb + BFA_IOC1_STATE_REG);
	}

	/*
	 * Host <-> LPU mailbox command/status registers
	 */
	ioc->ioc_regs.hfn_mbox_cmd = rb + iocreg_mbcmd[pcifn].hfn;
	ioc->ioc_regs.lpu_mbox_cmd = rb + iocreg_mbcmd[pcifn].lpu;

	/*
	 * PSS control registers
	 */
	ioc->ioc_regs.pss_ctl_reg = (rb + PSS_CTL_REG);
	ioc->ioc_regs.pss_err_status_reg = (rb + PSS_ERR_STATUS_REG);
	ioc->ioc_regs.app_pll_fast_ctl_reg = (rb + APP_PLL_400_CTL_REG);
	ioc->ioc_regs.app_pll_slow_ctl_reg = (rb + APP_PLL_212_CTL_REG);

	/*
	 * IOC semaphore registers and serialization
	 */
	ioc->ioc_regs.ioc_sem_reg = (rb + HOST_SEM0_REG);
	ioc->ioc_regs.ioc_init_sem_reg = (rb + HOST_SEM2_REG);

	/*
	 * sram memory access
	 */
	ioc->ioc_regs.smem_page_start = (rb + PSS_SMEM_PAGE_START);
	ioc->ioc_regs.smem_pg0 = BFI_IOC_SMEM_PG0_CB;

	/*
	 * err set reg : for notification of hb failure
	 */
	ioc->ioc_regs.err_set = (rb + ERR_SET_REG);
}

/*
 * Initialize IOC to port mapping.
 */

static void
bfa_ioc_cb_map_port(struct bfa_ioc_s *ioc)
{
	/*
	 * For crossbow, port id is same as pci function.
	 */
	ioc->port_id = bfa_ioc_pcifn(ioc);

	bfa_trc(ioc, ioc->port_id);
}

/*
 * Set interrupt mode for a function: INTX or MSIX
 */
static void
bfa_ioc_cb_isr_mode_set(struct bfa_ioc_s *ioc, bfa_boolean_t msix)
{
}

/*
 * Cleanup hw semaphore and usecnt registers
 */
static void
bfa_ioc_cb_ownership_reset(struct bfa_ioc_s *ioc)
{

	/*
	 * Read the hw sem reg to make sure that it is locked
	 * before we clear it. If it is not locked, writing 1
	 * will lock it instead of clearing it.
	 */
	readl(ioc->ioc_regs.ioc_sem_reg);
	bfa_ioc_hw_sem_release(ioc);
}



bfa_status_t
bfa_ioc_cb_pll_init(void __iomem *rb, bfa_boolean_t fcmode)
{
	u32	pll_sclk, pll_fclk;

	pll_sclk = __APP_PLL_212_ENABLE | __APP_PLL_212_LRESETN |
		__APP_PLL_212_P0_1(3U) |
		__APP_PLL_212_JITLMT0_1(3U) |
		__APP_PLL_212_CNTLMT0_1(3U);
	pll_fclk = __APP_PLL_400_ENABLE | __APP_PLL_400_LRESETN |
		__APP_PLL_400_RSEL200500 | __APP_PLL_400_P0_1(3U) |
		__APP_PLL_400_JITLMT0_1(3U) |
		__APP_PLL_400_CNTLMT0_1(3U);
	writel(BFI_IOC_UNINIT, (rb + BFA_IOC0_STATE_REG));
	writel(BFI_IOC_UNINIT, (rb + BFA_IOC1_STATE_REG));
	writel(0xffffffffU, (rb + HOSTFN0_INT_MSK));
	writel(0xffffffffU, (rb + HOSTFN1_INT_MSK));
	writel(0xffffffffU, (rb + HOSTFN0_INT_STATUS));
	writel(0xffffffffU, (rb + HOSTFN1_INT_STATUS));
	writel(0xffffffffU, (rb + HOSTFN0_INT_MSK));
	writel(0xffffffffU, (rb + HOSTFN1_INT_MSK));
	writel(__APP_PLL_212_LOGIC_SOFT_RESET, rb + APP_PLL_212_CTL_REG);
	writel(__APP_PLL_212_BYPASS | __APP_PLL_212_LOGIC_SOFT_RESET,
			rb + APP_PLL_212_CTL_REG);
	writel(__APP_PLL_400_LOGIC_SOFT_RESET, rb + APP_PLL_400_CTL_REG);
	writel(__APP_PLL_400_BYPASS | __APP_PLL_400_LOGIC_SOFT_RESET,
			rb + APP_PLL_400_CTL_REG);
	udelay(2);
	writel(__APP_PLL_212_LOGIC_SOFT_RESET, rb + APP_PLL_212_CTL_REG);
	writel(__APP_PLL_400_LOGIC_SOFT_RESET, rb + APP_PLL_400_CTL_REG);
	writel(pll_sclk | __APP_PLL_212_LOGIC_SOFT_RESET,
			rb + APP_PLL_212_CTL_REG);
	writel(pll_fclk | __APP_PLL_400_LOGIC_SOFT_RESET,
			rb + APP_PLL_400_CTL_REG);
	udelay(2000);
	writel(0xffffffffU, (rb + HOSTFN0_INT_STATUS));
	writel(0xffffffffU, (rb + HOSTFN1_INT_STATUS));
	writel(pll_sclk, (rb + APP_PLL_212_CTL_REG));
	writel(pll_fclk, (rb + APP_PLL_400_CTL_REG));

	return BFA_STATUS_OK;
}
