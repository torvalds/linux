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

#include "bfad_drv.h"
#include "bfa_ioc.h"
#include "bfi_reg.h"
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
static void bfa_ioc_cb_notify_fail(struct bfa_ioc_s *ioc);
static void bfa_ioc_cb_ownership_reset(struct bfa_ioc_s *ioc);
static bfa_boolean_t bfa_ioc_cb_sync_start(struct bfa_ioc_s *ioc);
static void bfa_ioc_cb_sync_join(struct bfa_ioc_s *ioc);
static void bfa_ioc_cb_sync_leave(struct bfa_ioc_s *ioc);
static void bfa_ioc_cb_sync_ack(struct bfa_ioc_s *ioc);
static bfa_boolean_t bfa_ioc_cb_sync_complete(struct bfa_ioc_s *ioc);

static struct bfa_ioc_hwif_s hwif_cb;

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
	hwif_cb.ioc_notify_fail = bfa_ioc_cb_notify_fail;
	hwif_cb.ioc_ownership_reset = bfa_ioc_cb_ownership_reset;
	hwif_cb.ioc_sync_start = bfa_ioc_cb_sync_start;
	hwif_cb.ioc_sync_join = bfa_ioc_cb_sync_join;
	hwif_cb.ioc_sync_leave = bfa_ioc_cb_sync_leave;
	hwif_cb.ioc_sync_ack = bfa_ioc_cb_sync_ack;
	hwif_cb.ioc_sync_complete = bfa_ioc_cb_sync_complete;

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
bfa_ioc_cb_notify_fail(struct bfa_ioc_s *ioc)
{
	writel(~0U, ioc->ioc_regs.err_set);
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
		ioc->ioc_regs.alt_ioc_fwstate = rb + BFA_IOC1_STATE_REG;
	} else {
		ioc->ioc_regs.heartbeat = (rb + BFA_IOC1_HBEAT_REG);
		ioc->ioc_regs.ioc_fwstate = (rb + BFA_IOC1_STATE_REG);
		ioc->ioc_regs.alt_ioc_fwstate = (rb + BFA_IOC0_STATE_REG);
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
	ioc->ioc_regs.app_pll_fast_ctl_reg = (rb + APP_PLL_LCLK_CTL_REG);
	ioc->ioc_regs.app_pll_slow_ctl_reg = (rb + APP_PLL_SCLK_CTL_REG);

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
 * Synchronized IOC failure processing routines
 */
static bfa_boolean_t
bfa_ioc_cb_sync_start(struct bfa_ioc_s *ioc)
{
	return bfa_ioc_cb_sync_complete(ioc);
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
	writel(1, ioc->ioc_regs.ioc_sem_reg);
}

/*
 * Synchronized IOC failure processing routines
 */
static void
bfa_ioc_cb_sync_join(struct bfa_ioc_s *ioc)
{
}

static void
bfa_ioc_cb_sync_leave(struct bfa_ioc_s *ioc)
{
}

static void
bfa_ioc_cb_sync_ack(struct bfa_ioc_s *ioc)
{
	writel(BFI_IOC_FAIL, ioc->ioc_regs.ioc_fwstate);
}

static bfa_boolean_t
bfa_ioc_cb_sync_complete(struct bfa_ioc_s *ioc)
{
	uint32_t fwstate, alt_fwstate;
	fwstate = readl(ioc->ioc_regs.ioc_fwstate);

	/*
	 * At this point, this IOC is hoding the hw sem in the
	 * start path (fwcheck) OR in the disable/enable path
	 * OR to check if the other IOC has acknowledged failure.
	 *
	 * So, this IOC can be in UNINIT, INITING, DISABLED, FAIL
	 * or in MEMTEST states. In a normal scenario, this IOC
	 * can not be in OP state when this function is called.
	 *
	 * However, this IOC could still be in OP state when
	 * the OS driver is starting up, if the OptROM code has
	 * left it in that state.
	 *
	 * If we had marked this IOC's fwstate as BFI_IOC_FAIL
	 * in the failure case and now, if the fwstate is not
	 * BFI_IOC_FAIL it implies that the other PCI fn have
	 * reinitialized the ASIC or this IOC got disabled, so
	 * return TRUE.
	 */
	if (fwstate == BFI_IOC_UNINIT ||
		fwstate == BFI_IOC_INITING ||
		fwstate == BFI_IOC_DISABLED ||
		fwstate == BFI_IOC_MEMTEST ||
		fwstate == BFI_IOC_OP)
		return BFA_TRUE;
	else {
		alt_fwstate = readl(ioc->ioc_regs.alt_ioc_fwstate);
		if (alt_fwstate == BFI_IOC_FAIL ||
			alt_fwstate == BFI_IOC_DISABLED ||
			alt_fwstate == BFI_IOC_UNINIT ||
			alt_fwstate == BFI_IOC_INITING ||
			alt_fwstate == BFI_IOC_MEMTEST)
			return BFA_TRUE;
		else
			return BFA_FALSE;
	}
}

bfa_status_t
bfa_ioc_cb_pll_init(void __iomem *rb, enum bfi_asic_mode fcmode)
{
	u32	pll_sclk, pll_fclk;

	pll_sclk = __APP_PLL_SCLK_ENABLE | __APP_PLL_SCLK_LRESETN |
		__APP_PLL_SCLK_P0_1(3U) |
		__APP_PLL_SCLK_JITLMT0_1(3U) |
		__APP_PLL_SCLK_CNTLMT0_1(3U);
	pll_fclk = __APP_PLL_LCLK_ENABLE | __APP_PLL_LCLK_LRESETN |
		__APP_PLL_LCLK_RSEL200500 | __APP_PLL_LCLK_P0_1(3U) |
		__APP_PLL_LCLK_JITLMT0_1(3U) |
		__APP_PLL_LCLK_CNTLMT0_1(3U);
	writel(BFI_IOC_UNINIT, (rb + BFA_IOC0_STATE_REG));
	writel(BFI_IOC_UNINIT, (rb + BFA_IOC1_STATE_REG));
	writel(0xffffffffU, (rb + HOSTFN0_INT_MSK));
	writel(0xffffffffU, (rb + HOSTFN1_INT_MSK));
	writel(0xffffffffU, (rb + HOSTFN0_INT_STATUS));
	writel(0xffffffffU, (rb + HOSTFN1_INT_STATUS));
	writel(0xffffffffU, (rb + HOSTFN0_INT_MSK));
	writel(0xffffffffU, (rb + HOSTFN1_INT_MSK));
	writel(__APP_PLL_SCLK_LOGIC_SOFT_RESET, rb + APP_PLL_SCLK_CTL_REG);
	writel(__APP_PLL_SCLK_BYPASS | __APP_PLL_SCLK_LOGIC_SOFT_RESET,
			rb + APP_PLL_SCLK_CTL_REG);
	writel(__APP_PLL_LCLK_LOGIC_SOFT_RESET, rb + APP_PLL_LCLK_CTL_REG);
	writel(__APP_PLL_LCLK_BYPASS | __APP_PLL_LCLK_LOGIC_SOFT_RESET,
			rb + APP_PLL_LCLK_CTL_REG);
	udelay(2);
	writel(__APP_PLL_SCLK_LOGIC_SOFT_RESET, rb + APP_PLL_SCLK_CTL_REG);
	writel(__APP_PLL_LCLK_LOGIC_SOFT_RESET, rb + APP_PLL_LCLK_CTL_REG);
	writel(pll_sclk | __APP_PLL_SCLK_LOGIC_SOFT_RESET,
			rb + APP_PLL_SCLK_CTL_REG);
	writel(pll_fclk | __APP_PLL_LCLK_LOGIC_SOFT_RESET,
			rb + APP_PLL_LCLK_CTL_REG);
	udelay(2000);
	writel(0xffffffffU, (rb + HOSTFN0_INT_STATUS));
	writel(0xffffffffU, (rb + HOSTFN1_INT_STATUS));
	writel(pll_sclk, (rb + APP_PLL_SCLK_CTL_REG));
	writel(pll_fclk, (rb + APP_PLL_LCLK_CTL_REG));

	return BFA_STATUS_OK;
}
