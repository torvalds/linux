/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
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

#include <bfa.h>
#include <bfa_ioc.h>
#include <bfa_fwimg_priv.h>
#include <cna/bfa_cna_trcmod.h>
#include <cs/bfa_debug.h>
#include <bfi/bfi_ioc.h>
#include <bfi/bfi_ctreg.h>
#include <log/bfa_log_hal.h>
#include <defs/bfa_defs_pci.h>

BFA_TRC_FILE(CNA, IOC_CT);

/*
 * forward declarations
 */
static bfa_status_t bfa_ioc_ct_pll_init(struct bfa_ioc_s *ioc);
static bfa_boolean_t bfa_ioc_ct_firmware_lock(struct bfa_ioc_s *ioc);
static void bfa_ioc_ct_firmware_unlock(struct bfa_ioc_s *ioc);
static u32* bfa_ioc_ct_fwimg_get_chunk(struct bfa_ioc_s *ioc,
					u32 off);
static u32 bfa_ioc_ct_fwimg_get_size(struct bfa_ioc_s *ioc);
static void bfa_ioc_ct_reg_init(struct bfa_ioc_s *ioc);
static void bfa_ioc_ct_map_port(struct bfa_ioc_s *ioc);
static void bfa_ioc_ct_isr_mode_set(struct bfa_ioc_s *ioc, bfa_boolean_t msix);
static void bfa_ioc_ct_notify_hbfail(struct bfa_ioc_s *ioc);
static void bfa_ioc_ct_ownership_reset(struct bfa_ioc_s *ioc);

struct bfa_ioc_hwif_s hwif_ct = {
	bfa_ioc_ct_pll_init,
	bfa_ioc_ct_firmware_lock,
	bfa_ioc_ct_firmware_unlock,
	bfa_ioc_ct_fwimg_get_chunk,
	bfa_ioc_ct_fwimg_get_size,
	bfa_ioc_ct_reg_init,
	bfa_ioc_ct_map_port,
	bfa_ioc_ct_isr_mode_set,
	bfa_ioc_ct_notify_hbfail,
	bfa_ioc_ct_ownership_reset,
};

/**
 * Called from bfa_ioc_attach() to map asic specific calls.
 */
void
bfa_ioc_set_ct_hwif(struct bfa_ioc_s *ioc)
{
	ioc->ioc_hwif = &hwif_ct;
}

static u32*
bfa_ioc_ct_fwimg_get_chunk(struct bfa_ioc_s *ioc, u32 off)
{
	return bfi_image_ct_get_chunk(off);
}

static u32
bfa_ioc_ct_fwimg_get_size(struct bfa_ioc_s *ioc)
{
	return bfi_image_ct_size;
}

/**
 * Return true if firmware of current driver matches the running firmware.
 */
static bfa_boolean_t
bfa_ioc_ct_firmware_lock(struct bfa_ioc_s *ioc)
{
	enum bfi_ioc_state ioc_fwstate;
	u32 usecnt;
	struct bfi_ioc_image_hdr_s fwhdr;

	/**
	 * Firmware match check is relevant only for CNA.
	 */
	if (!ioc->cna)
		return BFA_TRUE;

	/**
	 * If bios boot (flash based) -- do not increment usage count
	 */
	if (bfa_ioc_ct_fwimg_get_size(ioc) < BFA_IOC_FWIMG_MINSZ)
		return BFA_TRUE;

	bfa_ioc_sem_get(ioc->ioc_regs.ioc_usage_sem_reg);
	usecnt = bfa_reg_read(ioc->ioc_regs.ioc_usage_reg);

	/**
	 * If usage count is 0, always return TRUE.
	 */
	if (usecnt == 0) {
		bfa_reg_write(ioc->ioc_regs.ioc_usage_reg, 1);
		bfa_ioc_sem_release(ioc->ioc_regs.ioc_usage_sem_reg);
		bfa_trc(ioc, usecnt);
		return BFA_TRUE;
	}

	ioc_fwstate = bfa_reg_read(ioc->ioc_regs.ioc_fwstate);
	bfa_trc(ioc, ioc_fwstate);

	/**
	 * Use count cannot be non-zero and chip in uninitialized state.
	 */
	bfa_assert(ioc_fwstate != BFI_IOC_UNINIT);

	/**
	 * Check if another driver with a different firmware is active
	 */
	bfa_ioc_fwver_get(ioc, &fwhdr);
	if (!bfa_ioc_fwver_cmp(ioc, &fwhdr)) {
		bfa_ioc_sem_release(ioc->ioc_regs.ioc_usage_sem_reg);
		bfa_trc(ioc, usecnt);
		return BFA_FALSE;
	}

	/**
	 * Same firmware version. Increment the reference count.
	 */
	usecnt++;
	bfa_reg_write(ioc->ioc_regs.ioc_usage_reg, usecnt);
	bfa_ioc_sem_release(ioc->ioc_regs.ioc_usage_sem_reg);
	bfa_trc(ioc, usecnt);
	return BFA_TRUE;
}

static void
bfa_ioc_ct_firmware_unlock(struct bfa_ioc_s *ioc)
{
	u32 usecnt;

	/**
	 * Firmware lock is relevant only for CNA.
	 * If bios boot (flash based) -- do not decrement usage count
	 */
	if (!ioc->cna || bfa_ioc_ct_fwimg_get_size(ioc) < BFA_IOC_FWIMG_MINSZ)
		return;

	/**
	 * decrement usage count
	 */
	bfa_ioc_sem_get(ioc->ioc_regs.ioc_usage_sem_reg);
	usecnt = bfa_reg_read(ioc->ioc_regs.ioc_usage_reg);
	bfa_assert(usecnt > 0);

	usecnt--;
	bfa_reg_write(ioc->ioc_regs.ioc_usage_reg, usecnt);
	bfa_trc(ioc, usecnt);

	bfa_ioc_sem_release(ioc->ioc_regs.ioc_usage_sem_reg);
}

/**
 * Notify other functions on HB failure.
 */
static void
bfa_ioc_ct_notify_hbfail(struct bfa_ioc_s *ioc)
{
	if (ioc->cna) {
		bfa_reg_write(ioc->ioc_regs.ll_halt, __FW_INIT_HALT_P);
		/* Wait for halt to take effect */
		bfa_reg_read(ioc->ioc_regs.ll_halt);
	} else {
		bfa_reg_write(ioc->ioc_regs.err_set, __PSS_ERR_STATUS_SET);
		bfa_reg_read(ioc->ioc_regs.err_set);
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
bfa_ioc_ct_reg_init(struct bfa_ioc_s *ioc)
{
	bfa_os_addr_t	rb;
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
bfa_ioc_ct_map_port(struct bfa_ioc_s *ioc)
{
	bfa_os_addr_t	rb = ioc->pcidev.pci_bar_kva;
	u32	r32;

	/**
	 * For catapult, base port id on personality register and IOC type
	 */
	r32 = bfa_reg_read(rb + FNC_PERS_REG);
	r32 >>= FNC_PERS_FN_SHIFT(bfa_ioc_pcifn(ioc));
	ioc->port_id = (r32 & __F0_PORT_MAP_MK) >> __F0_PORT_MAP_SH;

	bfa_trc(ioc, bfa_ioc_pcifn(ioc));
	bfa_trc(ioc, ioc->port_id);
}

/**
 * Set interrupt mode for a function: INTX or MSIX
 */
static void
bfa_ioc_ct_isr_mode_set(struct bfa_ioc_s *ioc, bfa_boolean_t msix)
{
	bfa_os_addr_t	rb = ioc->pcidev.pci_bar_kva;
	u32	r32, mode;

	r32 = bfa_reg_read(rb + FNC_PERS_REG);
	bfa_trc(ioc, r32);

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
	bfa_trc(ioc, r32);

	bfa_reg_write(rb + FNC_PERS_REG, r32);
}

static bfa_status_t
bfa_ioc_ct_pll_init(struct bfa_ioc_s *ioc)
{
	bfa_os_addr_t	rb = ioc->pcidev.pci_bar_kva;
	u32	pll_sclk, pll_fclk, r32;

	/*
	 *  Hold semaphore so that nobody can access the chip during init.
	 */
	bfa_ioc_sem_get(ioc->ioc_regs.ioc_init_sem_reg);

	pll_sclk = __APP_PLL_312_LRESETN | __APP_PLL_312_ENARST |
		__APP_PLL_312_RSEL200500 | __APP_PLL_312_P0_1(3U) |
		__APP_PLL_312_JITLMT0_1(3U) |
		__APP_PLL_312_CNTLMT0_1(1U);
	pll_fclk = __APP_PLL_425_LRESETN | __APP_PLL_425_ENARST |
		__APP_PLL_425_RSEL200500 | __APP_PLL_425_P0_1(3U) |
		__APP_PLL_425_JITLMT0_1(3U) |
		__APP_PLL_425_CNTLMT0_1(1U);

	/**
	 *	For catapult, choose operational mode FC/FCoE
	 */
	if (ioc->fcmode) {
		bfa_reg_write((rb + OP_MODE), 0);
		bfa_reg_write((rb + ETH_MAC_SER_REG),
				__APP_EMS_CMLCKSEL |
				__APP_EMS_REFCKBUFEN2 |
				__APP_EMS_CHANNEL_SEL);
	} else {
		ioc->pllinit = BFA_TRUE;
		bfa_reg_write((rb + OP_MODE), __GLOBAL_FCOE_MODE);
		bfa_reg_write((rb + ETH_MAC_SER_REG),
				 __APP_EMS_REFCKBUFEN1);
	}

	bfa_reg_write((rb + BFA_IOC0_STATE_REG), BFI_IOC_UNINIT);
	bfa_reg_write((rb + BFA_IOC1_STATE_REG), BFI_IOC_UNINIT);

	bfa_reg_write((rb + HOSTFN0_INT_MSK), 0xffffffffU);
	bfa_reg_write((rb + HOSTFN1_INT_MSK), 0xffffffffU);
	bfa_reg_write((rb + HOSTFN0_INT_STATUS), 0xffffffffU);
	bfa_reg_write((rb + HOSTFN1_INT_STATUS), 0xffffffffU);
	bfa_reg_write((rb + HOSTFN0_INT_MSK), 0xffffffffU);
	bfa_reg_write((rb + HOSTFN1_INT_MSK), 0xffffffffU);

	bfa_reg_write(ioc->ioc_regs.app_pll_slow_ctl_reg, pll_sclk |
		__APP_PLL_312_LOGIC_SOFT_RESET);
	bfa_reg_write(ioc->ioc_regs.app_pll_fast_ctl_reg, pll_fclk |
		__APP_PLL_425_LOGIC_SOFT_RESET);
	bfa_reg_write(ioc->ioc_regs.app_pll_slow_ctl_reg, pll_sclk |
		__APP_PLL_312_LOGIC_SOFT_RESET | __APP_PLL_312_ENABLE);
	bfa_reg_write(ioc->ioc_regs.app_pll_fast_ctl_reg, pll_fclk |
		__APP_PLL_425_LOGIC_SOFT_RESET | __APP_PLL_425_ENABLE);

	/**
	 * Wait for PLLs to lock.
	 */
	bfa_reg_read(rb + HOSTFN0_INT_MSK);
	bfa_os_udelay(2000);
	bfa_reg_write((rb + HOSTFN0_INT_STATUS), 0xffffffffU);
	bfa_reg_write((rb + HOSTFN1_INT_STATUS), 0xffffffffU);

	bfa_reg_write(ioc->ioc_regs.app_pll_slow_ctl_reg, pll_sclk |
		__APP_PLL_312_ENABLE);
	bfa_reg_write(ioc->ioc_regs.app_pll_fast_ctl_reg, pll_fclk |
		__APP_PLL_425_ENABLE);

	bfa_reg_write((rb + MBIST_CTL_REG), __EDRAM_BISTR_START);
	bfa_os_udelay(1000);
	r32 = bfa_reg_read((rb + MBIST_STAT_REG));
	bfa_trc(ioc, r32);
	/*
	 *  release semaphore.
	 */
	bfa_ioc_sem_release(ioc->ioc_regs.ioc_init_sem_reg);

	return BFA_STATUS_OK;
}

/**
 * Cleanup hw semaphore and usecnt registers
 */
static void
bfa_ioc_ct_ownership_reset(struct bfa_ioc_s *ioc)
{

	if (ioc->cna) {
		bfa_ioc_sem_get(ioc->ioc_regs.ioc_usage_sem_reg);
		bfa_reg_write(ioc->ioc_regs.ioc_usage_reg, 0);
		bfa_ioc_sem_release(ioc->ioc_regs.ioc_usage_sem_reg);
	}

	/*
	 * Read the hw sem reg to make sure that it is locked
	 * before we clear it. If it is not locked, writing 1
	 * will lock it instead of clearing it.
	 */
	bfa_reg_read(ioc->ioc_regs.ioc_sem_reg);
	bfa_ioc_hw_sem_release(ioc);
}
