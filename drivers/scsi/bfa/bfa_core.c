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
#include "bfa_modules.h"
#include "bfi_reg.h"

BFA_TRC_FILE(HAL, CORE);

/*
 * BFA module list terminated by NULL
 */
static struct bfa_module_s *hal_mods[] = {
	&hal_mod_fcdiag,
	&hal_mod_sgpg,
	&hal_mod_fcport,
	&hal_mod_fcxp,
	&hal_mod_lps,
	&hal_mod_uf,
	&hal_mod_rport,
	&hal_mod_fcp,
	&hal_mod_dconf,
	NULL
};

/*
 * Message handlers for various modules.
 */
static bfa_isr_func_t  bfa_isrs[BFI_MC_MAX] = {
	bfa_isr_unhandled,	/* NONE */
	bfa_isr_unhandled,	/* BFI_MC_IOC */
	bfa_fcdiag_intr,	/* BFI_MC_DIAG */
	bfa_isr_unhandled,	/* BFI_MC_FLASH */
	bfa_isr_unhandled,	/* BFI_MC_CEE */
	bfa_fcport_isr,		/* BFI_MC_FCPORT */
	bfa_isr_unhandled,	/* BFI_MC_IOCFC */
	bfa_isr_unhandled,	/* BFI_MC_LL */
	bfa_uf_isr,		/* BFI_MC_UF */
	bfa_fcxp_isr,		/* BFI_MC_FCXP */
	bfa_lps_isr,		/* BFI_MC_LPS */
	bfa_rport_isr,		/* BFI_MC_RPORT */
	bfa_itn_isr,		/* BFI_MC_ITN */
	bfa_isr_unhandled,	/* BFI_MC_IOIM_READ */
	bfa_isr_unhandled,	/* BFI_MC_IOIM_WRITE */
	bfa_isr_unhandled,	/* BFI_MC_IOIM_IO */
	bfa_ioim_isr,		/* BFI_MC_IOIM */
	bfa_ioim_good_comp_isr,	/* BFI_MC_IOIM_IOCOM */
	bfa_tskim_isr,		/* BFI_MC_TSKIM */
	bfa_isr_unhandled,	/* BFI_MC_SBOOT */
	bfa_isr_unhandled,	/* BFI_MC_IPFC */
	bfa_isr_unhandled,	/* BFI_MC_PORT */
	bfa_isr_unhandled,	/* --------- */
	bfa_isr_unhandled,	/* --------- */
	bfa_isr_unhandled,	/* --------- */
	bfa_isr_unhandled,	/* --------- */
	bfa_isr_unhandled,	/* --------- */
	bfa_isr_unhandled,	/* --------- */
	bfa_isr_unhandled,	/* --------- */
	bfa_isr_unhandled,	/* --------- */
	bfa_isr_unhandled,	/* --------- */
	bfa_isr_unhandled,	/* --------- */
};
/*
 * Message handlers for mailbox command classes
 */
static bfa_ioc_mbox_mcfunc_t  bfa_mbox_isrs[BFI_MC_MAX] = {
	NULL,
	NULL,		/* BFI_MC_IOC   */
	NULL,		/* BFI_MC_DIAG  */
	NULL,		/* BFI_MC_FLASH */
	NULL,		/* BFI_MC_CEE   */
	NULL,		/* BFI_MC_PORT  */
	bfa_iocfc_isr,	/* BFI_MC_IOCFC */
	NULL,
};



static void
bfa_com_port_attach(struct bfa_s *bfa)
{
	struct bfa_port_s	*port = &bfa->modules.port;
	struct bfa_mem_dma_s	*port_dma = BFA_MEM_PORT_DMA(bfa);

	bfa_port_attach(port, &bfa->ioc, bfa, bfa->trcmod);
	bfa_port_mem_claim(port, port_dma->kva_curp, port_dma->dma_curp);
}

/*
 * ablk module attach
 */
static void
bfa_com_ablk_attach(struct bfa_s *bfa)
{
	struct bfa_ablk_s	*ablk = &bfa->modules.ablk;
	struct bfa_mem_dma_s	*ablk_dma = BFA_MEM_ABLK_DMA(bfa);

	bfa_ablk_attach(ablk, &bfa->ioc);
	bfa_ablk_memclaim(ablk, ablk_dma->kva_curp, ablk_dma->dma_curp);
}

static void
bfa_com_cee_attach(struct bfa_s *bfa)
{
	struct bfa_cee_s	*cee = &bfa->modules.cee;
	struct bfa_mem_dma_s	*cee_dma = BFA_MEM_CEE_DMA(bfa);

	cee->trcmod = bfa->trcmod;
	bfa_cee_attach(cee, &bfa->ioc, bfa);
	bfa_cee_mem_claim(cee, cee_dma->kva_curp, cee_dma->dma_curp);
}

static void
bfa_com_sfp_attach(struct bfa_s *bfa)
{
	struct bfa_sfp_s	*sfp = BFA_SFP_MOD(bfa);
	struct bfa_mem_dma_s	*sfp_dma = BFA_MEM_SFP_DMA(bfa);

	bfa_sfp_attach(sfp, &bfa->ioc, bfa, bfa->trcmod);
	bfa_sfp_memclaim(sfp, sfp_dma->kva_curp, sfp_dma->dma_curp);
}

static void
bfa_com_flash_attach(struct bfa_s *bfa, bfa_boolean_t mincfg)
{
	struct bfa_flash_s	*flash = BFA_FLASH(bfa);
	struct bfa_mem_dma_s	*flash_dma = BFA_MEM_FLASH_DMA(bfa);

	bfa_flash_attach(flash, &bfa->ioc, bfa, bfa->trcmod, mincfg);
	bfa_flash_memclaim(flash, flash_dma->kva_curp,
			   flash_dma->dma_curp, mincfg);
}

static void
bfa_com_diag_attach(struct bfa_s *bfa)
{
	struct bfa_diag_s	*diag = BFA_DIAG_MOD(bfa);
	struct bfa_mem_dma_s	*diag_dma = BFA_MEM_DIAG_DMA(bfa);

	bfa_diag_attach(diag, &bfa->ioc, bfa, bfa_fcport_beacon, bfa->trcmod);
	bfa_diag_memclaim(diag, diag_dma->kva_curp, diag_dma->dma_curp);
}

static void
bfa_com_phy_attach(struct bfa_s *bfa, bfa_boolean_t mincfg)
{
	struct bfa_phy_s	*phy = BFA_PHY(bfa);
	struct bfa_mem_dma_s	*phy_dma = BFA_MEM_PHY_DMA(bfa);

	bfa_phy_attach(phy, &bfa->ioc, bfa, bfa->trcmod, mincfg);
	bfa_phy_memclaim(phy, phy_dma->kva_curp, phy_dma->dma_curp, mincfg);
}

static void
bfa_com_fru_attach(struct bfa_s *bfa, bfa_boolean_t mincfg)
{
	struct bfa_fru_s	*fru = BFA_FRU(bfa);
	struct bfa_mem_dma_s	*fru_dma = BFA_MEM_FRU_DMA(bfa);

	bfa_fru_attach(fru, &bfa->ioc, bfa, bfa->trcmod, mincfg);
	bfa_fru_memclaim(fru, fru_dma->kva_curp, fru_dma->dma_curp, mincfg);
}

/*
 * BFA IOC FC related definitions
 */

/*
 * IOC local definitions
 */
#define BFA_IOCFC_TOV		5000	/* msecs */

enum {
	BFA_IOCFC_ACT_NONE	= 0,
	BFA_IOCFC_ACT_INIT	= 1,
	BFA_IOCFC_ACT_STOP	= 2,
	BFA_IOCFC_ACT_DISABLE	= 3,
	BFA_IOCFC_ACT_ENABLE	= 4,
};

#define DEF_CFG_NUM_FABRICS		1
#define DEF_CFG_NUM_LPORTS		256
#define DEF_CFG_NUM_CQS			4
#define DEF_CFG_NUM_IOIM_REQS		(BFA_IOIM_MAX)
#define DEF_CFG_NUM_TSKIM_REQS		128
#define DEF_CFG_NUM_FCXP_REQS		64
#define DEF_CFG_NUM_UF_BUFS		64
#define DEF_CFG_NUM_RPORTS		1024
#define DEF_CFG_NUM_ITNIMS		(DEF_CFG_NUM_RPORTS)
#define DEF_CFG_NUM_TINS		256

#define DEF_CFG_NUM_SGPGS		2048
#define DEF_CFG_NUM_REQQ_ELEMS		256
#define DEF_CFG_NUM_RSPQ_ELEMS		64
#define DEF_CFG_NUM_SBOOT_TGTS		16
#define DEF_CFG_NUM_SBOOT_LUNS		16

/*
 * IOCFC state machine definitions/declarations
 */
bfa_fsm_state_decl(bfa_iocfc, stopped, struct bfa_iocfc_s, enum iocfc_event);
bfa_fsm_state_decl(bfa_iocfc, initing, struct bfa_iocfc_s, enum iocfc_event);
bfa_fsm_state_decl(bfa_iocfc, dconf_read, struct bfa_iocfc_s, enum iocfc_event);
bfa_fsm_state_decl(bfa_iocfc, init_cfg_wait,
		   struct bfa_iocfc_s, enum iocfc_event);
bfa_fsm_state_decl(bfa_iocfc, init_cfg_done,
		   struct bfa_iocfc_s, enum iocfc_event);
bfa_fsm_state_decl(bfa_iocfc, operational,
		   struct bfa_iocfc_s, enum iocfc_event);
bfa_fsm_state_decl(bfa_iocfc, dconf_write,
		   struct bfa_iocfc_s, enum iocfc_event);
bfa_fsm_state_decl(bfa_iocfc, stopping, struct bfa_iocfc_s, enum iocfc_event);
bfa_fsm_state_decl(bfa_iocfc, enabling, struct bfa_iocfc_s, enum iocfc_event);
bfa_fsm_state_decl(bfa_iocfc, cfg_wait, struct bfa_iocfc_s, enum iocfc_event);
bfa_fsm_state_decl(bfa_iocfc, disabling, struct bfa_iocfc_s, enum iocfc_event);
bfa_fsm_state_decl(bfa_iocfc, disabled, struct bfa_iocfc_s, enum iocfc_event);
bfa_fsm_state_decl(bfa_iocfc, failed, struct bfa_iocfc_s, enum iocfc_event);
bfa_fsm_state_decl(bfa_iocfc, init_failed,
		   struct bfa_iocfc_s, enum iocfc_event);

/*
 * forward declaration for IOC FC functions
 */
static void bfa_iocfc_start_submod(struct bfa_s *bfa);
static void bfa_iocfc_disable_submod(struct bfa_s *bfa);
static void bfa_iocfc_send_cfg(void *bfa_arg);
static void bfa_iocfc_enable_cbfn(void *bfa_arg, enum bfa_status status);
static void bfa_iocfc_disable_cbfn(void *bfa_arg);
static void bfa_iocfc_hbfail_cbfn(void *bfa_arg);
static void bfa_iocfc_reset_cbfn(void *bfa_arg);
static struct bfa_ioc_cbfn_s bfa_iocfc_cbfn;
static void bfa_iocfc_init_cb(void *bfa_arg, bfa_boolean_t complete);
static void bfa_iocfc_stop_cb(void *bfa_arg, bfa_boolean_t compl);
static void bfa_iocfc_enable_cb(void *bfa_arg, bfa_boolean_t compl);
static void bfa_iocfc_disable_cb(void *bfa_arg, bfa_boolean_t compl);

static void
bfa_iocfc_sm_stopped_entry(struct bfa_iocfc_s *iocfc)
{
}

static void
bfa_iocfc_sm_stopped(struct bfa_iocfc_s *iocfc, enum iocfc_event event)
{
	bfa_trc(iocfc->bfa, event);

	switch (event) {
	case IOCFC_E_INIT:
	case IOCFC_E_ENABLE:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_initing);
		break;
	default:
		bfa_sm_fault(iocfc->bfa, event);
		break;
	}
}

static void
bfa_iocfc_sm_initing_entry(struct bfa_iocfc_s *iocfc)
{
	bfa_ioc_enable(&iocfc->bfa->ioc);
}

static void
bfa_iocfc_sm_initing(struct bfa_iocfc_s *iocfc, enum iocfc_event event)
{
	bfa_trc(iocfc->bfa, event);

	switch (event) {
	case IOCFC_E_IOC_ENABLED:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_dconf_read);
		break;

	case IOCFC_E_DISABLE:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_disabling);
		break;

	case IOCFC_E_STOP:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_stopping);
		break;

	case IOCFC_E_IOC_FAILED:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_init_failed);
		break;
	default:
		bfa_sm_fault(iocfc->bfa, event);
		break;
	}
}

static void
bfa_iocfc_sm_dconf_read_entry(struct bfa_iocfc_s *iocfc)
{
	bfa_dconf_modinit(iocfc->bfa);
}

static void
bfa_iocfc_sm_dconf_read(struct bfa_iocfc_s *iocfc, enum iocfc_event event)
{
	bfa_trc(iocfc->bfa, event);

	switch (event) {
	case IOCFC_E_DCONF_DONE:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_init_cfg_wait);
		break;

	case IOCFC_E_DISABLE:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_disabling);
		break;

	case IOCFC_E_STOP:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_stopping);
		break;

	case IOCFC_E_IOC_FAILED:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_init_failed);
		break;
	default:
		bfa_sm_fault(iocfc->bfa, event);
		break;
	}
}

static void
bfa_iocfc_sm_init_cfg_wait_entry(struct bfa_iocfc_s *iocfc)
{
	bfa_iocfc_send_cfg(iocfc->bfa);
}

static void
bfa_iocfc_sm_init_cfg_wait(struct bfa_iocfc_s *iocfc, enum iocfc_event event)
{
	bfa_trc(iocfc->bfa, event);

	switch (event) {
	case IOCFC_E_CFG_DONE:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_init_cfg_done);
		break;

	case IOCFC_E_DISABLE:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_disabling);
		break;

	case IOCFC_E_STOP:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_stopping);
		break;

	case IOCFC_E_IOC_FAILED:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_init_failed);
		break;
	default:
		bfa_sm_fault(iocfc->bfa, event);
		break;
	}
}

static void
bfa_iocfc_sm_init_cfg_done_entry(struct bfa_iocfc_s *iocfc)
{
	iocfc->bfa->iocfc.op_status = BFA_STATUS_OK;
	bfa_cb_queue(iocfc->bfa, &iocfc->bfa->iocfc.init_hcb_qe,
		     bfa_iocfc_init_cb, iocfc->bfa);
}

static void
bfa_iocfc_sm_init_cfg_done(struct bfa_iocfc_s *iocfc, enum iocfc_event event)
{
	bfa_trc(iocfc->bfa, event);

	switch (event) {
	case IOCFC_E_START:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_operational);
		break;
	case IOCFC_E_STOP:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_stopping);
		break;
	case IOCFC_E_DISABLE:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_disabling);
		break;
	case IOCFC_E_IOC_FAILED:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_failed);
		break;
	default:
		bfa_sm_fault(iocfc->bfa, event);
		break;
	}
}

static void
bfa_iocfc_sm_operational_entry(struct bfa_iocfc_s *iocfc)
{
	bfa_fcport_init(iocfc->bfa);
	bfa_iocfc_start_submod(iocfc->bfa);
}

static void
bfa_iocfc_sm_operational(struct bfa_iocfc_s *iocfc, enum iocfc_event event)
{
	bfa_trc(iocfc->bfa, event);

	switch (event) {
	case IOCFC_E_STOP:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_dconf_write);
		break;
	case IOCFC_E_DISABLE:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_disabling);
		break;
	case IOCFC_E_IOC_FAILED:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_failed);
		break;
	default:
		bfa_sm_fault(iocfc->bfa, event);
		break;
	}
}

static void
bfa_iocfc_sm_dconf_write_entry(struct bfa_iocfc_s *iocfc)
{
	bfa_dconf_modexit(iocfc->bfa);
}

static void
bfa_iocfc_sm_dconf_write(struct bfa_iocfc_s *iocfc, enum iocfc_event event)
{
	bfa_trc(iocfc->bfa, event);

	switch (event) {
	case IOCFC_E_DCONF_DONE:
	case IOCFC_E_IOC_FAILED:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_stopping);
		break;
	default:
		bfa_sm_fault(iocfc->bfa, event);
		break;
	}
}

static void
bfa_iocfc_sm_stopping_entry(struct bfa_iocfc_s *iocfc)
{
	bfa_ioc_disable(&iocfc->bfa->ioc);
}

static void
bfa_iocfc_sm_stopping(struct bfa_iocfc_s *iocfc, enum iocfc_event event)
{
	bfa_trc(iocfc->bfa, event);

	switch (event) {
	case IOCFC_E_IOC_DISABLED:
		bfa_isr_disable(iocfc->bfa);
		bfa_iocfc_disable_submod(iocfc->bfa);
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_stopped);
		iocfc->bfa->iocfc.op_status = BFA_STATUS_OK;
		bfa_cb_queue(iocfc->bfa, &iocfc->bfa->iocfc.stop_hcb_qe,
			     bfa_iocfc_stop_cb, iocfc->bfa);
		break;

	case IOCFC_E_IOC_ENABLED:
	case IOCFC_E_DCONF_DONE:
	case IOCFC_E_CFG_DONE:
		break;

	default:
		bfa_sm_fault(iocfc->bfa, event);
		break;
	}
}

static void
bfa_iocfc_sm_enabling_entry(struct bfa_iocfc_s *iocfc)
{
	bfa_ioc_enable(&iocfc->bfa->ioc);
}

static void
bfa_iocfc_sm_enabling(struct bfa_iocfc_s *iocfc, enum iocfc_event event)
{
	bfa_trc(iocfc->bfa, event);

	switch (event) {
	case IOCFC_E_IOC_ENABLED:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_cfg_wait);
		break;

	case IOCFC_E_DISABLE:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_disabling);
		break;

	case IOCFC_E_STOP:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_dconf_write);
		break;

	case IOCFC_E_IOC_FAILED:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_failed);

		if (iocfc->bfa->iocfc.cb_reqd == BFA_FALSE)
			break;

		iocfc->bfa->iocfc.op_status = BFA_STATUS_FAILED;
		bfa_cb_queue(iocfc->bfa, &iocfc->bfa->iocfc.en_hcb_qe,
			     bfa_iocfc_enable_cb, iocfc->bfa);
		iocfc->bfa->iocfc.cb_reqd = BFA_FALSE;
		break;
	default:
		bfa_sm_fault(iocfc->bfa, event);
		break;
	}
}

static void
bfa_iocfc_sm_cfg_wait_entry(struct bfa_iocfc_s *iocfc)
{
	bfa_iocfc_send_cfg(iocfc->bfa);
}

static void
bfa_iocfc_sm_cfg_wait(struct bfa_iocfc_s *iocfc, enum iocfc_event event)
{
	bfa_trc(iocfc->bfa, event);

	switch (event) {
	case IOCFC_E_CFG_DONE:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_operational);
		if (iocfc->bfa->iocfc.cb_reqd == BFA_FALSE)
			break;

		iocfc->bfa->iocfc.op_status = BFA_STATUS_OK;
		bfa_cb_queue(iocfc->bfa, &iocfc->bfa->iocfc.en_hcb_qe,
			     bfa_iocfc_enable_cb, iocfc->bfa);
		iocfc->bfa->iocfc.cb_reqd = BFA_FALSE;
		break;
	case IOCFC_E_DISABLE:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_disabling);
		break;

	case IOCFC_E_STOP:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_dconf_write);
		break;
	case IOCFC_E_IOC_FAILED:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_failed);
		if (iocfc->bfa->iocfc.cb_reqd == BFA_FALSE)
			break;

		iocfc->bfa->iocfc.op_status = BFA_STATUS_FAILED;
		bfa_cb_queue(iocfc->bfa, &iocfc->bfa->iocfc.en_hcb_qe,
			     bfa_iocfc_enable_cb, iocfc->bfa);
		iocfc->bfa->iocfc.cb_reqd = BFA_FALSE;
		break;
	default:
		bfa_sm_fault(iocfc->bfa, event);
		break;
	}
}

static void
bfa_iocfc_sm_disabling_entry(struct bfa_iocfc_s *iocfc)
{
	bfa_ioc_disable(&iocfc->bfa->ioc);
}

static void
bfa_iocfc_sm_disabling(struct bfa_iocfc_s *iocfc, enum iocfc_event event)
{
	bfa_trc(iocfc->bfa, event);

	switch (event) {
	case IOCFC_E_IOC_DISABLED:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_disabled);
		break;
	case IOCFC_E_IOC_ENABLED:
	case IOCFC_E_DCONF_DONE:
	case IOCFC_E_CFG_DONE:
		break;
	default:
		bfa_sm_fault(iocfc->bfa, event);
		break;
	}
}

static void
bfa_iocfc_sm_disabled_entry(struct bfa_iocfc_s *iocfc)
{
	bfa_isr_disable(iocfc->bfa);
	bfa_iocfc_disable_submod(iocfc->bfa);
	iocfc->bfa->iocfc.op_status = BFA_STATUS_OK;
	bfa_cb_queue(iocfc->bfa, &iocfc->bfa->iocfc.dis_hcb_qe,
		     bfa_iocfc_disable_cb, iocfc->bfa);
}

static void
bfa_iocfc_sm_disabled(struct bfa_iocfc_s *iocfc, enum iocfc_event event)
{
	bfa_trc(iocfc->bfa, event);

	switch (event) {
	case IOCFC_E_STOP:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_dconf_write);
		break;
	case IOCFC_E_ENABLE:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_enabling);
		break;
	default:
		bfa_sm_fault(iocfc->bfa, event);
		break;
	}
}

static void
bfa_iocfc_sm_failed_entry(struct bfa_iocfc_s *iocfc)
{
	bfa_isr_disable(iocfc->bfa);
	bfa_iocfc_disable_submod(iocfc->bfa);
}

static void
bfa_iocfc_sm_failed(struct bfa_iocfc_s *iocfc, enum iocfc_event event)
{
	bfa_trc(iocfc->bfa, event);

	switch (event) {
	case IOCFC_E_STOP:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_dconf_write);
		break;
	case IOCFC_E_DISABLE:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_disabling);
		break;
	case IOCFC_E_IOC_ENABLED:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_cfg_wait);
		break;
	case IOCFC_E_IOC_FAILED:
		break;
	default:
		bfa_sm_fault(iocfc->bfa, event);
		break;
	}
}

static void
bfa_iocfc_sm_init_failed_entry(struct bfa_iocfc_s *iocfc)
{
	bfa_isr_disable(iocfc->bfa);
	iocfc->bfa->iocfc.op_status = BFA_STATUS_FAILED;
	bfa_cb_queue(iocfc->bfa, &iocfc->bfa->iocfc.init_hcb_qe,
		     bfa_iocfc_init_cb, iocfc->bfa);
}

static void
bfa_iocfc_sm_init_failed(struct bfa_iocfc_s *iocfc, enum iocfc_event event)
{
	bfa_trc(iocfc->bfa, event);

	switch (event) {
	case IOCFC_E_STOP:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_stopping);
		break;
	case IOCFC_E_DISABLE:
		bfa_ioc_disable(&iocfc->bfa->ioc);
		break;
	case IOCFC_E_IOC_ENABLED:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_dconf_read);
		break;
	case IOCFC_E_IOC_DISABLED:
		bfa_fsm_set_state(iocfc, bfa_iocfc_sm_stopped);
		iocfc->bfa->iocfc.op_status = BFA_STATUS_OK;
		bfa_cb_queue(iocfc->bfa, &iocfc->bfa->iocfc.dis_hcb_qe,
			     bfa_iocfc_disable_cb, iocfc->bfa);
		break;
	case IOCFC_E_IOC_FAILED:
		break;
	default:
		bfa_sm_fault(iocfc->bfa, event);
		break;
	}
}

/*
 * BFA Interrupt handling functions
 */
static void
bfa_reqq_resume(struct bfa_s *bfa, int qid)
{
	struct list_head *waitq, *qe, *qen;
	struct bfa_reqq_wait_s *wqe;

	waitq = bfa_reqq(bfa, qid);
	list_for_each_safe(qe, qen, waitq) {
		/*
		 * Callback only as long as there is room in request queue
		 */
		if (bfa_reqq_full(bfa, qid))
			break;

		list_del(qe);
		wqe = (struct bfa_reqq_wait_s *) qe;
		wqe->qresume(wqe->cbarg);
	}
}

bfa_boolean_t
bfa_isr_rspq(struct bfa_s *bfa, int qid)
{
	struct bfi_msg_s *m;
	u32	pi, ci;
	struct list_head *waitq;
	bfa_boolean_t ret;

	ci = bfa_rspq_ci(bfa, qid);
	pi = bfa_rspq_pi(bfa, qid);

	ret = (ci != pi);

	while (ci != pi) {
		m = bfa_rspq_elem(bfa, qid, ci);
		WARN_ON(m->mhdr.msg_class >= BFI_MC_MAX);

		bfa_isrs[m->mhdr.msg_class] (bfa, m);
		CQ_INCR(ci, bfa->iocfc.cfg.drvcfg.num_rspq_elems);
	}

	/*
	 * acknowledge RME completions and update CI
	 */
	bfa_isr_rspq_ack(bfa, qid, ci);

	/*
	 * Resume any pending requests in the corresponding reqq.
	 */
	waitq = bfa_reqq(bfa, qid);
	if (!list_empty(waitq))
		bfa_reqq_resume(bfa, qid);

	return ret;
}

static inline void
bfa_isr_reqq(struct bfa_s *bfa, int qid)
{
	struct list_head *waitq;

	bfa_isr_reqq_ack(bfa, qid);

	/*
	 * Resume any pending requests in the corresponding reqq.
	 */
	waitq = bfa_reqq(bfa, qid);
	if (!list_empty(waitq))
		bfa_reqq_resume(bfa, qid);
}

void
bfa_msix_all(struct bfa_s *bfa, int vec)
{
	u32	intr, qintr;
	int	queue;

	intr = readl(bfa->iocfc.bfa_regs.intr_status);
	if (!intr)
		return;

	/*
	 * RME completion queue interrupt
	 */
	qintr = intr & __HFN_INT_RME_MASK;
	if (qintr && bfa->queue_process) {
		for (queue = 0; queue < BFI_IOC_MAX_CQS; queue++)
			bfa_isr_rspq(bfa, queue);
	}

	intr &= ~qintr;
	if (!intr)
		return;

	/*
	 * CPE completion queue interrupt
	 */
	qintr = intr & __HFN_INT_CPE_MASK;
	if (qintr && bfa->queue_process) {
		for (queue = 0; queue < BFI_IOC_MAX_CQS; queue++)
			bfa_isr_reqq(bfa, queue);
	}
	intr &= ~qintr;
	if (!intr)
		return;

	bfa_msix_lpu_err(bfa, intr);
}

bfa_boolean_t
bfa_intx(struct bfa_s *bfa)
{
	u32 intr, qintr;
	int queue;
	bfa_boolean_t rspq_comp = BFA_FALSE;

	intr = readl(bfa->iocfc.bfa_regs.intr_status);

	qintr = intr & (__HFN_INT_RME_MASK | __HFN_INT_CPE_MASK);
	if (qintr)
		writel(qintr, bfa->iocfc.bfa_regs.intr_status);

	/*
	 * Unconditional RME completion queue interrupt
	 */
	if (bfa->queue_process) {
		for (queue = 0; queue < BFI_IOC_MAX_CQS; queue++)
			if (bfa_isr_rspq(bfa, queue))
				rspq_comp = BFA_TRUE;
	}

	if (!intr)
		return (qintr | rspq_comp) ? BFA_TRUE : BFA_FALSE;

	/*
	 * CPE completion queue interrupt
	 */
	qintr = intr & __HFN_INT_CPE_MASK;
	if (qintr && bfa->queue_process) {
		for (queue = 0; queue < BFI_IOC_MAX_CQS; queue++)
			bfa_isr_reqq(bfa, queue);
	}
	intr &= ~qintr;
	if (!intr)
		return BFA_TRUE;

	if (bfa->intr_enabled)
		bfa_msix_lpu_err(bfa, intr);

	return BFA_TRUE;
}

void
bfa_isr_enable(struct bfa_s *bfa)
{
	u32 umsk;
	int port_id = bfa_ioc_portid(&bfa->ioc);

	bfa_trc(bfa, bfa_ioc_pcifn(&bfa->ioc));
	bfa_trc(bfa, port_id);

	bfa_msix_ctrl_install(bfa);

	if (bfa_asic_id_ct2(bfa->ioc.pcidev.device_id)) {
		umsk = __HFN_INT_ERR_MASK_CT2;
		umsk |= port_id == 0 ?
			__HFN_INT_FN0_MASK_CT2 : __HFN_INT_FN1_MASK_CT2;
	} else {
		umsk = __HFN_INT_ERR_MASK;
		umsk |= port_id == 0 ? __HFN_INT_FN0_MASK : __HFN_INT_FN1_MASK;
	}

	writel(umsk, bfa->iocfc.bfa_regs.intr_status);
	writel(~umsk, bfa->iocfc.bfa_regs.intr_mask);
	bfa->iocfc.intr_mask = ~umsk;
	bfa_isr_mode_set(bfa, bfa->msix.nvecs != 0);

	/*
	 * Set the flag indicating successful enabling of interrupts
	 */
	bfa->intr_enabled = BFA_TRUE;
}

void
bfa_isr_disable(struct bfa_s *bfa)
{
	bfa->intr_enabled = BFA_FALSE;
	bfa_isr_mode_set(bfa, BFA_FALSE);
	writel(-1L, bfa->iocfc.bfa_regs.intr_mask);
	bfa_msix_uninstall(bfa);
}

void
bfa_msix_reqq(struct bfa_s *bfa, int vec)
{
	bfa_isr_reqq(bfa, vec - bfa->iocfc.hwif.cpe_vec_q0);
}

void
bfa_isr_unhandled(struct bfa_s *bfa, struct bfi_msg_s *m)
{
	bfa_trc(bfa, m->mhdr.msg_class);
	bfa_trc(bfa, m->mhdr.msg_id);
	bfa_trc(bfa, m->mhdr.mtag.i2htok);
	WARN_ON(1);
	bfa_trc_stop(bfa->trcmod);
}

void
bfa_msix_rspq(struct bfa_s *bfa, int vec)
{
	bfa_isr_rspq(bfa, vec - bfa->iocfc.hwif.rme_vec_q0);
}

void
bfa_msix_lpu_err(struct bfa_s *bfa, int vec)
{
	u32 intr, curr_value;
	bfa_boolean_t lpu_isr, halt_isr, pss_isr;

	intr = readl(bfa->iocfc.bfa_regs.intr_status);

	if (bfa_asic_id_ct2(bfa->ioc.pcidev.device_id)) {
		halt_isr = intr & __HFN_INT_CPQ_HALT_CT2;
		pss_isr  = intr & __HFN_INT_ERR_PSS_CT2;
		lpu_isr  = intr & (__HFN_INT_MBOX_LPU0_CT2 |
				   __HFN_INT_MBOX_LPU1_CT2);
		intr    &= __HFN_INT_ERR_MASK_CT2;
	} else {
		halt_isr = bfa_asic_id_ct(bfa->ioc.pcidev.device_id) ?
					  (intr & __HFN_INT_LL_HALT) : 0;
		pss_isr  = intr & __HFN_INT_ERR_PSS;
		lpu_isr  = intr & (__HFN_INT_MBOX_LPU0 | __HFN_INT_MBOX_LPU1);
		intr    &= __HFN_INT_ERR_MASK;
	}

	if (lpu_isr)
		bfa_ioc_mbox_isr(&bfa->ioc);

	if (intr) {
		if (halt_isr) {
			/*
			 * If LL_HALT bit is set then FW Init Halt LL Port
			 * Register needs to be cleared as well so Interrupt
			 * Status Register will be cleared.
			 */
			curr_value = readl(bfa->ioc.ioc_regs.ll_halt);
			curr_value &= ~__FW_INIT_HALT_P;
			writel(curr_value, bfa->ioc.ioc_regs.ll_halt);
		}

		if (pss_isr) {
			/*
			 * ERR_PSS bit needs to be cleared as well in case
			 * interrups are shared so driver's interrupt handler is
			 * still called even though it is already masked out.
			 */
			curr_value = readl(
					bfa->ioc.ioc_regs.pss_err_status_reg);
			writel(curr_value,
				bfa->ioc.ioc_regs.pss_err_status_reg);
		}

		writel(intr, bfa->iocfc.bfa_regs.intr_status);
		bfa_ioc_error_isr(&bfa->ioc);
	}
}

/*
 * BFA IOC FC related functions
 */

/*
 *  BFA IOC private functions
 */

/*
 * Use the Mailbox interface to send BFI_IOCFC_H2I_CFG_REQ
 */
static void
bfa_iocfc_send_cfg(void *bfa_arg)
{
	struct bfa_s *bfa = bfa_arg;
	struct bfa_iocfc_s *iocfc = &bfa->iocfc;
	struct bfi_iocfc_cfg_req_s cfg_req;
	struct bfi_iocfc_cfg_s *cfg_info = iocfc->cfginfo;
	struct bfa_iocfc_cfg_s	*cfg = &iocfc->cfg;
	int		i;

	WARN_ON(cfg->fwcfg.num_cqs > BFI_IOC_MAX_CQS);
	bfa_trc(bfa, cfg->fwcfg.num_cqs);

	bfa_iocfc_reset_queues(bfa);

	/*
	 * initialize IOC configuration info
	 */
	cfg_info->single_msix_vec = 0;
	if (bfa->msix.nvecs == 1)
		cfg_info->single_msix_vec = 1;
	cfg_info->endian_sig = BFI_IOC_ENDIAN_SIG;
	cfg_info->num_cqs = cfg->fwcfg.num_cqs;
	cfg_info->num_ioim_reqs = cpu_to_be16(bfa_fcpim_get_throttle_cfg(bfa,
					       cfg->fwcfg.num_ioim_reqs));
	cfg_info->num_fwtio_reqs = cpu_to_be16(cfg->fwcfg.num_fwtio_reqs);

	bfa_dma_be_addr_set(cfg_info->cfgrsp_addr, iocfc->cfgrsp_dma.pa);
	/*
	 * dma map REQ and RSP circular queues and shadow pointers
	 */
	for (i = 0; i < cfg->fwcfg.num_cqs; i++) {
		bfa_dma_be_addr_set(cfg_info->req_cq_ba[i],
				    iocfc->req_cq_ba[i].pa);
		bfa_dma_be_addr_set(cfg_info->req_shadow_ci[i],
				    iocfc->req_cq_shadow_ci[i].pa);
		cfg_info->req_cq_elems[i] =
			cpu_to_be16(cfg->drvcfg.num_reqq_elems);

		bfa_dma_be_addr_set(cfg_info->rsp_cq_ba[i],
				    iocfc->rsp_cq_ba[i].pa);
		bfa_dma_be_addr_set(cfg_info->rsp_shadow_pi[i],
				    iocfc->rsp_cq_shadow_pi[i].pa);
		cfg_info->rsp_cq_elems[i] =
			cpu_to_be16(cfg->drvcfg.num_rspq_elems);
	}

	/*
	 * Enable interrupt coalescing if it is driver init path
	 * and not ioc disable/enable path.
	 */
	if (bfa_fsm_cmp_state(iocfc, bfa_iocfc_sm_init_cfg_wait))
		cfg_info->intr_attr.coalesce = BFA_TRUE;

	/*
	 * dma map IOC configuration itself
	 */
	bfi_h2i_set(cfg_req.mh, BFI_MC_IOCFC, BFI_IOCFC_H2I_CFG_REQ,
		    bfa_fn_lpu(bfa));
	bfa_dma_be_addr_set(cfg_req.ioc_cfg_dma_addr, iocfc->cfg_info.pa);

	bfa_ioc_mbox_send(&bfa->ioc, &cfg_req,
			  sizeof(struct bfi_iocfc_cfg_req_s));
}

static void
bfa_iocfc_init_mem(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		   struct bfa_pcidev_s *pcidev)
{
	struct bfa_iocfc_s	*iocfc = &bfa->iocfc;

	bfa->bfad = bfad;
	iocfc->bfa = bfa;
	iocfc->cfg = *cfg;

	/*
	 * Initialize chip specific handlers.
	 */
	if (bfa_asic_id_ctc(bfa_ioc_devid(&bfa->ioc))) {
		iocfc->hwif.hw_reginit = bfa_hwct_reginit;
		iocfc->hwif.hw_reqq_ack = bfa_hwct_reqq_ack;
		iocfc->hwif.hw_rspq_ack = bfa_hwct_rspq_ack;
		iocfc->hwif.hw_msix_init = bfa_hwct_msix_init;
		iocfc->hwif.hw_msix_ctrl_install = bfa_hwct_msix_ctrl_install;
		iocfc->hwif.hw_msix_queue_install = bfa_hwct_msix_queue_install;
		iocfc->hwif.hw_msix_uninstall = bfa_hwct_msix_uninstall;
		iocfc->hwif.hw_isr_mode_set = bfa_hwct_isr_mode_set;
		iocfc->hwif.hw_msix_getvecs = bfa_hwct_msix_getvecs;
		iocfc->hwif.hw_msix_get_rme_range = bfa_hwct_msix_get_rme_range;
		iocfc->hwif.rme_vec_q0 = BFI_MSIX_RME_QMIN_CT;
		iocfc->hwif.cpe_vec_q0 = BFI_MSIX_CPE_QMIN_CT;
	} else {
		iocfc->hwif.hw_reginit = bfa_hwcb_reginit;
		iocfc->hwif.hw_reqq_ack = NULL;
		iocfc->hwif.hw_rspq_ack = bfa_hwcb_rspq_ack;
		iocfc->hwif.hw_msix_init = bfa_hwcb_msix_init;
		iocfc->hwif.hw_msix_ctrl_install = bfa_hwcb_msix_ctrl_install;
		iocfc->hwif.hw_msix_queue_install = bfa_hwcb_msix_queue_install;
		iocfc->hwif.hw_msix_uninstall = bfa_hwcb_msix_uninstall;
		iocfc->hwif.hw_isr_mode_set = bfa_hwcb_isr_mode_set;
		iocfc->hwif.hw_msix_getvecs = bfa_hwcb_msix_getvecs;
		iocfc->hwif.hw_msix_get_rme_range = bfa_hwcb_msix_get_rme_range;
		iocfc->hwif.rme_vec_q0 = BFI_MSIX_RME_QMIN_CB +
			bfa_ioc_pcifn(&bfa->ioc) * BFI_IOC_MAX_CQS;
		iocfc->hwif.cpe_vec_q0 = BFI_MSIX_CPE_QMIN_CB +
			bfa_ioc_pcifn(&bfa->ioc) * BFI_IOC_MAX_CQS;
	}

	if (bfa_asic_id_ct2(bfa_ioc_devid(&bfa->ioc))) {
		iocfc->hwif.hw_reginit = bfa_hwct2_reginit;
		iocfc->hwif.hw_isr_mode_set = NULL;
		iocfc->hwif.hw_rspq_ack = bfa_hwct2_rspq_ack;
	}

	iocfc->hwif.hw_reginit(bfa);
	bfa->msix.nvecs = 0;
}

static void
bfa_iocfc_mem_claim(struct bfa_s *bfa, struct bfa_iocfc_cfg_s *cfg)
{
	u8	*dm_kva = NULL;
	u64	dm_pa = 0;
	int	i, per_reqq_sz, per_rspq_sz;
	struct bfa_iocfc_s  *iocfc = &bfa->iocfc;
	struct bfa_mem_dma_s *ioc_dma = BFA_MEM_IOC_DMA(bfa);
	struct bfa_mem_dma_s *iocfc_dma = BFA_MEM_IOCFC_DMA(bfa);
	struct bfa_mem_dma_s *reqq_dma, *rspq_dma;

	/* First allocate dma memory for IOC */
	bfa_ioc_mem_claim(&bfa->ioc, bfa_mem_dma_virt(ioc_dma),
			bfa_mem_dma_phys(ioc_dma));

	/* Claim DMA-able memory for the request/response queues */
	per_reqq_sz = BFA_ROUNDUP((cfg->drvcfg.num_reqq_elems * BFI_LMSG_SZ),
				BFA_DMA_ALIGN_SZ);
	per_rspq_sz = BFA_ROUNDUP((cfg->drvcfg.num_rspq_elems * BFI_LMSG_SZ),
				BFA_DMA_ALIGN_SZ);

	for (i = 0; i < cfg->fwcfg.num_cqs; i++) {
		reqq_dma = BFA_MEM_REQQ_DMA(bfa, i);
		iocfc->req_cq_ba[i].kva = bfa_mem_dma_virt(reqq_dma);
		iocfc->req_cq_ba[i].pa = bfa_mem_dma_phys(reqq_dma);
		memset(iocfc->req_cq_ba[i].kva, 0, per_reqq_sz);

		rspq_dma = BFA_MEM_RSPQ_DMA(bfa, i);
		iocfc->rsp_cq_ba[i].kva = bfa_mem_dma_virt(rspq_dma);
		iocfc->rsp_cq_ba[i].pa = bfa_mem_dma_phys(rspq_dma);
		memset(iocfc->rsp_cq_ba[i].kva, 0, per_rspq_sz);
	}

	/* Claim IOCFC dma memory - for shadow CI/PI */
	dm_kva = bfa_mem_dma_virt(iocfc_dma);
	dm_pa  = bfa_mem_dma_phys(iocfc_dma);

	for (i = 0; i < cfg->fwcfg.num_cqs; i++) {
		iocfc->req_cq_shadow_ci[i].kva = dm_kva;
		iocfc->req_cq_shadow_ci[i].pa = dm_pa;
		dm_kva += BFA_CACHELINE_SZ;
		dm_pa += BFA_CACHELINE_SZ;

		iocfc->rsp_cq_shadow_pi[i].kva = dm_kva;
		iocfc->rsp_cq_shadow_pi[i].pa = dm_pa;
		dm_kva += BFA_CACHELINE_SZ;
		dm_pa += BFA_CACHELINE_SZ;
	}

	/* Claim IOCFC dma memory - for the config info page */
	bfa->iocfc.cfg_info.kva = dm_kva;
	bfa->iocfc.cfg_info.pa = dm_pa;
	bfa->iocfc.cfginfo = (struct bfi_iocfc_cfg_s *) dm_kva;
	dm_kva += BFA_ROUNDUP(sizeof(struct bfi_iocfc_cfg_s), BFA_CACHELINE_SZ);
	dm_pa += BFA_ROUNDUP(sizeof(struct bfi_iocfc_cfg_s), BFA_CACHELINE_SZ);

	/* Claim IOCFC dma memory - for the config response */
	bfa->iocfc.cfgrsp_dma.kva = dm_kva;
	bfa->iocfc.cfgrsp_dma.pa = dm_pa;
	bfa->iocfc.cfgrsp = (struct bfi_iocfc_cfgrsp_s *) dm_kva;
	dm_kva += BFA_ROUNDUP(sizeof(struct bfi_iocfc_cfgrsp_s),
			BFA_CACHELINE_SZ);
	dm_pa += BFA_ROUNDUP(sizeof(struct bfi_iocfc_cfgrsp_s),
			BFA_CACHELINE_SZ);

	/* Claim IOCFC kva memory */
	bfa_ioc_debug_memclaim(&bfa->ioc, bfa_mem_kva_curp(iocfc));
	bfa_mem_kva_curp(iocfc) += BFA_DBG_FWTRC_LEN;
}

/*
 * Start BFA submodules.
 */
static void
bfa_iocfc_start_submod(struct bfa_s *bfa)
{
	int		i;

	bfa->queue_process = BFA_TRUE;
	for (i = 0; i < BFI_IOC_MAX_CQS; i++)
		bfa_isr_rspq_ack(bfa, i, bfa_rspq_ci(bfa, i));

	for (i = 0; hal_mods[i]; i++)
		hal_mods[i]->start(bfa);

	bfa->iocfc.submod_enabled = BFA_TRUE;
}

/*
 * Disable BFA submodules.
 */
static void
bfa_iocfc_disable_submod(struct bfa_s *bfa)
{
	int		i;

	if (bfa->iocfc.submod_enabled == BFA_FALSE)
		return;

	for (i = 0; hal_mods[i]; i++)
		hal_mods[i]->iocdisable(bfa);

	bfa->iocfc.submod_enabled = BFA_FALSE;
}

static void
bfa_iocfc_init_cb(void *bfa_arg, bfa_boolean_t complete)
{
	struct bfa_s	*bfa = bfa_arg;

	if (complete)
		bfa_cb_init(bfa->bfad, bfa->iocfc.op_status);
}

static void
bfa_iocfc_stop_cb(void *bfa_arg, bfa_boolean_t compl)
{
	struct bfa_s  *bfa = bfa_arg;
	struct bfad_s *bfad = bfa->bfad;

	if (compl)
		complete(&bfad->comp);
}

static void
bfa_iocfc_enable_cb(void *bfa_arg, bfa_boolean_t compl)
{
	struct bfa_s	*bfa = bfa_arg;
	struct bfad_s *bfad = bfa->bfad;

	if (compl)
		complete(&bfad->enable_comp);
}

static void
bfa_iocfc_disable_cb(void *bfa_arg, bfa_boolean_t compl)
{
	struct bfa_s  *bfa = bfa_arg;
	struct bfad_s *bfad = bfa->bfad;

	if (compl)
		complete(&bfad->disable_comp);
}

/**
 * configure queue registers from firmware response
 */
static void
bfa_iocfc_qreg(struct bfa_s *bfa, struct bfi_iocfc_qreg_s *qreg)
{
	int     i;
	struct bfa_iocfc_regs_s *r = &bfa->iocfc.bfa_regs;
	void __iomem *kva = bfa_ioc_bar0(&bfa->ioc);

	for (i = 0; i < BFI_IOC_MAX_CQS; i++) {
		bfa->iocfc.hw_qid[i] = qreg->hw_qid[i];
		r->cpe_q_ci[i] = kva + be32_to_cpu(qreg->cpe_q_ci_off[i]);
		r->cpe_q_pi[i] = kva + be32_to_cpu(qreg->cpe_q_pi_off[i]);
		r->cpe_q_ctrl[i] = kva + be32_to_cpu(qreg->cpe_qctl_off[i]);
		r->rme_q_ci[i] = kva + be32_to_cpu(qreg->rme_q_ci_off[i]);
		r->rme_q_pi[i] = kva + be32_to_cpu(qreg->rme_q_pi_off[i]);
		r->rme_q_ctrl[i] = kva + be32_to_cpu(qreg->rme_qctl_off[i]);
	}
}

static void
bfa_iocfc_res_recfg(struct bfa_s *bfa, struct bfa_iocfc_fwcfg_s *fwcfg)
{
	struct bfa_iocfc_s	*iocfc   = &bfa->iocfc;
	struct bfi_iocfc_cfg_s	*cfg_info = iocfc->cfginfo;

	bfa_fcxp_res_recfg(bfa, fwcfg->num_fcxp_reqs);
	bfa_uf_res_recfg(bfa, fwcfg->num_uf_bufs);
	bfa_rport_res_recfg(bfa, fwcfg->num_rports);
	bfa_fcp_res_recfg(bfa, cpu_to_be16(cfg_info->num_ioim_reqs),
			  fwcfg->num_ioim_reqs);
	bfa_tskim_res_recfg(bfa, fwcfg->num_tskim_reqs);
}

/*
 * Update BFA configuration from firmware configuration.
 */
static void
bfa_iocfc_cfgrsp(struct bfa_s *bfa)
{
	struct bfa_iocfc_s		*iocfc	 = &bfa->iocfc;
	struct bfi_iocfc_cfgrsp_s	*cfgrsp	 = iocfc->cfgrsp;
	struct bfa_iocfc_fwcfg_s	*fwcfg	 = &cfgrsp->fwcfg;

	fwcfg->num_cqs	      = fwcfg->num_cqs;
	fwcfg->num_ioim_reqs  = be16_to_cpu(fwcfg->num_ioim_reqs);
	fwcfg->num_fwtio_reqs = be16_to_cpu(fwcfg->num_fwtio_reqs);
	fwcfg->num_tskim_reqs = be16_to_cpu(fwcfg->num_tskim_reqs);
	fwcfg->num_fcxp_reqs  = be16_to_cpu(fwcfg->num_fcxp_reqs);
	fwcfg->num_uf_bufs    = be16_to_cpu(fwcfg->num_uf_bufs);
	fwcfg->num_rports     = be16_to_cpu(fwcfg->num_rports);

	/*
	 * configure queue register offsets as learnt from firmware
	 */
	bfa_iocfc_qreg(bfa, &cfgrsp->qreg);

	/*
	 * Re-configure resources as learnt from Firmware
	 */
	bfa_iocfc_res_recfg(bfa, fwcfg);

	/*
	 * Install MSIX queue handlers
	 */
	bfa_msix_queue_install(bfa);

	if (bfa->iocfc.cfgrsp->pbc_cfg.pbc_pwwn != 0) {
		bfa->ioc.attr->pwwn = bfa->iocfc.cfgrsp->pbc_cfg.pbc_pwwn;
		bfa->ioc.attr->nwwn = bfa->iocfc.cfgrsp->pbc_cfg.pbc_nwwn;
		bfa_fsm_send_event(iocfc, IOCFC_E_CFG_DONE);
	}
}

void
bfa_iocfc_reset_queues(struct bfa_s *bfa)
{
	int		q;

	for (q = 0; q < BFI_IOC_MAX_CQS; q++) {
		bfa_reqq_ci(bfa, q) = 0;
		bfa_reqq_pi(bfa, q) = 0;
		bfa_rspq_ci(bfa, q) = 0;
		bfa_rspq_pi(bfa, q) = 0;
	}
}

/*
 *	Process FAA pwwn msg from fw.
 */
static void
bfa_iocfc_process_faa_addr(struct bfa_s *bfa, struct bfi_faa_addr_msg_s *msg)
{
	struct bfa_iocfc_s		*iocfc   = &bfa->iocfc;
	struct bfi_iocfc_cfgrsp_s	*cfgrsp  = iocfc->cfgrsp;

	cfgrsp->pbc_cfg.pbc_pwwn = msg->pwwn;
	cfgrsp->pbc_cfg.pbc_nwwn = msg->nwwn;

	bfa->ioc.attr->pwwn = msg->pwwn;
	bfa->ioc.attr->nwwn = msg->nwwn;
	bfa_fsm_send_event(iocfc, IOCFC_E_CFG_DONE);
}

/* Fabric Assigned Address specific functions */

/*
 *	Check whether IOC is ready before sending command down
 */
static bfa_status_t
bfa_faa_validate_request(struct bfa_s *bfa)
{
	enum bfa_ioc_type_e	ioc_type = bfa_get_type(bfa);
	u32	card_type = bfa->ioc.attr->card_type;

	if (bfa_ioc_is_operational(&bfa->ioc)) {
		if ((ioc_type != BFA_IOC_TYPE_FC) || bfa_mfg_is_mezz(card_type))
			return BFA_STATUS_FEATURE_NOT_SUPPORTED;
	} else {
		return BFA_STATUS_IOC_NON_OP;
	}

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_faa_query(struct bfa_s *bfa, struct bfa_faa_attr_s *attr,
		bfa_cb_iocfc_t cbfn, void *cbarg)
{
	struct bfi_faa_query_s  faa_attr_req;
	struct bfa_iocfc_s      *iocfc = &bfa->iocfc;
	bfa_status_t            status;

	status = bfa_faa_validate_request(bfa);
	if (status != BFA_STATUS_OK)
		return status;

	if (iocfc->faa_args.busy == BFA_TRUE)
		return BFA_STATUS_DEVBUSY;

	iocfc->faa_args.faa_attr = attr;
	iocfc->faa_args.faa_cb.faa_cbfn = cbfn;
	iocfc->faa_args.faa_cb.faa_cbarg = cbarg;

	iocfc->faa_args.busy = BFA_TRUE;
	memset(&faa_attr_req, 0, sizeof(struct bfi_faa_query_s));
	bfi_h2i_set(faa_attr_req.mh, BFI_MC_IOCFC,
		BFI_IOCFC_H2I_FAA_QUERY_REQ, bfa_fn_lpu(bfa));

	bfa_ioc_mbox_send(&bfa->ioc, &faa_attr_req,
		sizeof(struct bfi_faa_query_s));

	return BFA_STATUS_OK;
}

/*
 *	FAA query response
 */
static void
bfa_faa_query_reply(struct bfa_iocfc_s *iocfc,
		bfi_faa_query_rsp_t *rsp)
{
	void	*cbarg = iocfc->faa_args.faa_cb.faa_cbarg;

	if (iocfc->faa_args.faa_attr) {
		iocfc->faa_args.faa_attr->faa = rsp->faa;
		iocfc->faa_args.faa_attr->faa_state = rsp->faa_status;
		iocfc->faa_args.faa_attr->pwwn_source = rsp->addr_source;
	}

	WARN_ON(!iocfc->faa_args.faa_cb.faa_cbfn);

	iocfc->faa_args.faa_cb.faa_cbfn(cbarg, BFA_STATUS_OK);
	iocfc->faa_args.busy = BFA_FALSE;
}

/*
 * IOC enable request is complete
 */
static void
bfa_iocfc_enable_cbfn(void *bfa_arg, enum bfa_status status)
{
	struct bfa_s	*bfa = bfa_arg;

	if (status == BFA_STATUS_OK)
		bfa_fsm_send_event(&bfa->iocfc, IOCFC_E_IOC_ENABLED);
	else
		bfa_fsm_send_event(&bfa->iocfc, IOCFC_E_IOC_FAILED);
}

/*
 * IOC disable request is complete
 */
static void
bfa_iocfc_disable_cbfn(void *bfa_arg)
{
	struct bfa_s	*bfa = bfa_arg;

	bfa->queue_process = BFA_FALSE;
	bfa_fsm_send_event(&bfa->iocfc, IOCFC_E_IOC_DISABLED);
}

/*
 * Notify sub-modules of hardware failure.
 */
static void
bfa_iocfc_hbfail_cbfn(void *bfa_arg)
{
	struct bfa_s	*bfa = bfa_arg;

	bfa->queue_process = BFA_FALSE;
	bfa_fsm_send_event(&bfa->iocfc, IOCFC_E_IOC_FAILED);
}

/*
 * Actions on chip-reset completion.
 */
static void
bfa_iocfc_reset_cbfn(void *bfa_arg)
{
	struct bfa_s	*bfa = bfa_arg;

	bfa_iocfc_reset_queues(bfa);
	bfa_isr_enable(bfa);
}

/*
 * Query IOC memory requirement information.
 */
void
bfa_iocfc_meminfo(struct bfa_iocfc_cfg_s *cfg, struct bfa_meminfo_s *meminfo,
		  struct bfa_s *bfa)
{
	int q, per_reqq_sz, per_rspq_sz;
	struct bfa_mem_dma_s *ioc_dma = BFA_MEM_IOC_DMA(bfa);
	struct bfa_mem_dma_s *iocfc_dma = BFA_MEM_IOCFC_DMA(bfa);
	struct bfa_mem_kva_s *iocfc_kva = BFA_MEM_IOCFC_KVA(bfa);
	u32	dm_len = 0;

	/* dma memory setup for IOC */
	bfa_mem_dma_setup(meminfo, ioc_dma,
		BFA_ROUNDUP(sizeof(struct bfi_ioc_attr_s), BFA_DMA_ALIGN_SZ));

	/* dma memory setup for REQ/RSP queues */
	per_reqq_sz = BFA_ROUNDUP((cfg->drvcfg.num_reqq_elems * BFI_LMSG_SZ),
				BFA_DMA_ALIGN_SZ);
	per_rspq_sz = BFA_ROUNDUP((cfg->drvcfg.num_rspq_elems * BFI_LMSG_SZ),
				BFA_DMA_ALIGN_SZ);

	for (q = 0; q < cfg->fwcfg.num_cqs; q++) {
		bfa_mem_dma_setup(meminfo, BFA_MEM_REQQ_DMA(bfa, q),
				per_reqq_sz);
		bfa_mem_dma_setup(meminfo, BFA_MEM_RSPQ_DMA(bfa, q),
				per_rspq_sz);
	}

	/* IOCFC dma memory - calculate Shadow CI/PI size */
	for (q = 0; q < cfg->fwcfg.num_cqs; q++)
		dm_len += (2 * BFA_CACHELINE_SZ);

	/* IOCFC dma memory - calculate config info / rsp size */
	dm_len += BFA_ROUNDUP(sizeof(struct bfi_iocfc_cfg_s), BFA_CACHELINE_SZ);
	dm_len += BFA_ROUNDUP(sizeof(struct bfi_iocfc_cfgrsp_s),
			BFA_CACHELINE_SZ);

	/* dma memory setup for IOCFC */
	bfa_mem_dma_setup(meminfo, iocfc_dma, dm_len);

	/* kva memory setup for IOCFC */
	bfa_mem_kva_setup(meminfo, iocfc_kva, BFA_DBG_FWTRC_LEN);
}

/*
 * Query IOC memory requirement information.
 */
void
bfa_iocfc_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		 struct bfa_pcidev_s *pcidev)
{
	int		i;
	struct bfa_ioc_s *ioc = &bfa->ioc;

	bfa_iocfc_cbfn.enable_cbfn = bfa_iocfc_enable_cbfn;
	bfa_iocfc_cbfn.disable_cbfn = bfa_iocfc_disable_cbfn;
	bfa_iocfc_cbfn.hbfail_cbfn = bfa_iocfc_hbfail_cbfn;
	bfa_iocfc_cbfn.reset_cbfn = bfa_iocfc_reset_cbfn;

	ioc->trcmod = bfa->trcmod;
	bfa_ioc_attach(&bfa->ioc, bfa, &bfa_iocfc_cbfn, &bfa->timer_mod);

	bfa_ioc_pci_init(&bfa->ioc, pcidev, BFI_PCIFN_CLASS_FC);
	bfa_ioc_mbox_register(&bfa->ioc, bfa_mbox_isrs);

	bfa_iocfc_init_mem(bfa, bfad, cfg, pcidev);
	bfa_iocfc_mem_claim(bfa, cfg);
	INIT_LIST_HEAD(&bfa->timer_mod.timer_q);

	INIT_LIST_HEAD(&bfa->comp_q);
	for (i = 0; i < BFI_IOC_MAX_CQS; i++)
		INIT_LIST_HEAD(&bfa->reqq_waitq[i]);

	bfa->iocfc.cb_reqd = BFA_FALSE;
	bfa->iocfc.op_status = BFA_STATUS_OK;
	bfa->iocfc.submod_enabled = BFA_FALSE;

	bfa_fsm_set_state(&bfa->iocfc, bfa_iocfc_sm_stopped);
}

/*
 * Query IOC memory requirement information.
 */
void
bfa_iocfc_init(struct bfa_s *bfa)
{
	bfa_fsm_send_event(&bfa->iocfc, IOCFC_E_INIT);
}

/*
 * IOC start called from bfa_start(). Called to start IOC operations
 * at driver instantiation for this instance.
 */
void
bfa_iocfc_start(struct bfa_s *bfa)
{
	bfa_fsm_send_event(&bfa->iocfc, IOCFC_E_START);
}

/*
 * IOC stop called from bfa_stop(). Called only when driver is unloaded
 * for this instance.
 */
void
bfa_iocfc_stop(struct bfa_s *bfa)
{
	bfa_fsm_send_event(&bfa->iocfc, IOCFC_E_STOP);
}

void
bfa_iocfc_isr(void *bfaarg, struct bfi_mbmsg_s *m)
{
	struct bfa_s		*bfa = bfaarg;
	struct bfa_iocfc_s	*iocfc = &bfa->iocfc;
	union bfi_iocfc_i2h_msg_u	*msg;

	msg = (union bfi_iocfc_i2h_msg_u *) m;
	bfa_trc(bfa, msg->mh.msg_id);

	switch (msg->mh.msg_id) {
	case BFI_IOCFC_I2H_CFG_REPLY:
		bfa_iocfc_cfgrsp(bfa);
		break;
	case BFI_IOCFC_I2H_UPDATEQ_RSP:
		iocfc->updateq_cbfn(iocfc->updateq_cbarg, BFA_STATUS_OK);
		break;
	case BFI_IOCFC_I2H_ADDR_MSG:
		bfa_iocfc_process_faa_addr(bfa,
				(struct bfi_faa_addr_msg_s *)msg);
		break;
	case BFI_IOCFC_I2H_FAA_QUERY_RSP:
		bfa_faa_query_reply(iocfc, (bfi_faa_query_rsp_t *)msg);
		break;
	default:
		WARN_ON(1);
	}
}

void
bfa_iocfc_get_attr(struct bfa_s *bfa, struct bfa_iocfc_attr_s *attr)
{
	struct bfa_iocfc_s	*iocfc = &bfa->iocfc;

	attr->intr_attr.coalesce = iocfc->cfginfo->intr_attr.coalesce;

	attr->intr_attr.delay = iocfc->cfginfo->intr_attr.delay ?
				be16_to_cpu(iocfc->cfginfo->intr_attr.delay) :
				be16_to_cpu(iocfc->cfgrsp->intr_attr.delay);

	attr->intr_attr.latency = iocfc->cfginfo->intr_attr.latency ?
			be16_to_cpu(iocfc->cfginfo->intr_attr.latency) :
			be16_to_cpu(iocfc->cfgrsp->intr_attr.latency);

	attr->config	= iocfc->cfg;
}

bfa_status_t
bfa_iocfc_israttr_set(struct bfa_s *bfa, struct bfa_iocfc_intr_attr_s *attr)
{
	struct bfa_iocfc_s		*iocfc = &bfa->iocfc;
	struct bfi_iocfc_set_intr_req_s *m;

	iocfc->cfginfo->intr_attr.coalesce = attr->coalesce;
	iocfc->cfginfo->intr_attr.delay = cpu_to_be16(attr->delay);
	iocfc->cfginfo->intr_attr.latency = cpu_to_be16(attr->latency);

	if (!bfa_iocfc_is_operational(bfa))
		return BFA_STATUS_OK;

	m = bfa_reqq_next(bfa, BFA_REQQ_IOC);
	if (!m)
		return BFA_STATUS_DEVBUSY;

	bfi_h2i_set(m->mh, BFI_MC_IOCFC, BFI_IOCFC_H2I_SET_INTR_REQ,
		    bfa_fn_lpu(bfa));
	m->coalesce = iocfc->cfginfo->intr_attr.coalesce;
	m->delay    = iocfc->cfginfo->intr_attr.delay;
	m->latency  = iocfc->cfginfo->intr_attr.latency;

	bfa_trc(bfa, attr->delay);
	bfa_trc(bfa, attr->latency);

	bfa_reqq_produce(bfa, BFA_REQQ_IOC, m->mh);
	return BFA_STATUS_OK;
}

void
bfa_iocfc_set_snsbase(struct bfa_s *bfa, int seg_no, u64 snsbase_pa)
{
	struct bfa_iocfc_s	*iocfc = &bfa->iocfc;

	iocfc->cfginfo->sense_buf_len = (BFI_IOIM_SNSLEN - 1);
	bfa_dma_be_addr_set(iocfc->cfginfo->ioim_snsbase[seg_no], snsbase_pa);
}
/*
 * Enable IOC after it is disabled.
 */
void
bfa_iocfc_enable(struct bfa_s *bfa)
{
	bfa_plog_str(bfa->plog, BFA_PL_MID_HAL, BFA_PL_EID_MISC, 0,
		     "IOC Enable");
	bfa->iocfc.cb_reqd = BFA_TRUE;
	bfa_fsm_send_event(&bfa->iocfc, IOCFC_E_ENABLE);
}

void
bfa_iocfc_disable(struct bfa_s *bfa)
{
	bfa_plog_str(bfa->plog, BFA_PL_MID_HAL, BFA_PL_EID_MISC, 0,
		     "IOC Disable");

	bfa_fsm_send_event(&bfa->iocfc, IOCFC_E_DISABLE);
}

bfa_boolean_t
bfa_iocfc_is_operational(struct bfa_s *bfa)
{
	return bfa_ioc_is_operational(&bfa->ioc) &&
		bfa_fsm_cmp_state(&bfa->iocfc, bfa_iocfc_sm_operational);
}

/*
 * Return boot target port wwns -- read from boot information in flash.
 */
void
bfa_iocfc_get_bootwwns(struct bfa_s *bfa, u8 *nwwns, wwn_t *wwns)
{
	struct bfa_iocfc_s *iocfc = &bfa->iocfc;
	struct bfi_iocfc_cfgrsp_s *cfgrsp = iocfc->cfgrsp;
	int i;

	if (cfgrsp->pbc_cfg.boot_enabled && cfgrsp->pbc_cfg.nbluns) {
		bfa_trc(bfa, cfgrsp->pbc_cfg.nbluns);
		*nwwns = cfgrsp->pbc_cfg.nbluns;
		for (i = 0; i < cfgrsp->pbc_cfg.nbluns; i++)
			wwns[i] = cfgrsp->pbc_cfg.blun[i].tgt_pwwn;

		return;
	}

	*nwwns = cfgrsp->bootwwns.nwwns;
	memcpy(wwns, cfgrsp->bootwwns.wwn, sizeof(cfgrsp->bootwwns.wwn));
}

int
bfa_iocfc_get_pbc_vports(struct bfa_s *bfa, struct bfi_pbc_vport_s *pbc_vport)
{
	struct bfa_iocfc_s *iocfc = &bfa->iocfc;
	struct bfi_iocfc_cfgrsp_s *cfgrsp = iocfc->cfgrsp;

	memcpy(pbc_vport, cfgrsp->pbc_cfg.vport, sizeof(cfgrsp->pbc_cfg.vport));
	return cfgrsp->pbc_cfg.nvports;
}


/*
 * Use this function query the memory requirement of the BFA library.
 * This function needs to be called before bfa_attach() to get the
 * memory required of the BFA layer for a given driver configuration.
 *
 * This call will fail, if the cap is out of range compared to pre-defined
 * values within the BFA library
 *
 * @param[in] cfg -	pointer to bfa_ioc_cfg_t. Driver layer should indicate
 *			its configuration in this structure.
 *			The default values for struct bfa_iocfc_cfg_s can be
 *			fetched using bfa_cfg_get_default() API.
 *
 *			If cap's boundary check fails, the library will use
 *			the default bfa_cap_t values (and log a warning msg).
 *
 * @param[out] meminfo - pointer to bfa_meminfo_t. This content
 *			indicates the memory type (see bfa_mem_type_t) and
 *			amount of memory required.
 *
 *			Driver should allocate the memory, populate the
 *			starting address for each block and provide the same
 *			structure as input parameter to bfa_attach() call.
 *
 * @param[in] bfa -	pointer to the bfa structure, used while fetching the
 *			dma, kva memory information of the bfa sub-modules.
 *
 * @return void
 *
 * Special Considerations: @note
 */
void
bfa_cfg_get_meminfo(struct bfa_iocfc_cfg_s *cfg, struct bfa_meminfo_s *meminfo,
		struct bfa_s *bfa)
{
	int		i;
	struct bfa_mem_dma_s *port_dma = BFA_MEM_PORT_DMA(bfa);
	struct bfa_mem_dma_s *ablk_dma = BFA_MEM_ABLK_DMA(bfa);
	struct bfa_mem_dma_s *cee_dma = BFA_MEM_CEE_DMA(bfa);
	struct bfa_mem_dma_s *sfp_dma = BFA_MEM_SFP_DMA(bfa);
	struct bfa_mem_dma_s *flash_dma = BFA_MEM_FLASH_DMA(bfa);
	struct bfa_mem_dma_s *diag_dma = BFA_MEM_DIAG_DMA(bfa);
	struct bfa_mem_dma_s *phy_dma = BFA_MEM_PHY_DMA(bfa);
	struct bfa_mem_dma_s *fru_dma = BFA_MEM_FRU_DMA(bfa);

	WARN_ON((cfg == NULL) || (meminfo == NULL));

	memset((void *)meminfo, 0, sizeof(struct bfa_meminfo_s));

	/* Initialize the DMA & KVA meminfo queues */
	INIT_LIST_HEAD(&meminfo->dma_info.qe);
	INIT_LIST_HEAD(&meminfo->kva_info.qe);

	bfa_iocfc_meminfo(cfg, meminfo, bfa);

	for (i = 0; hal_mods[i]; i++)
		hal_mods[i]->meminfo(cfg, meminfo, bfa);

	/* dma info setup */
	bfa_mem_dma_setup(meminfo, port_dma, bfa_port_meminfo());
	bfa_mem_dma_setup(meminfo, ablk_dma, bfa_ablk_meminfo());
	bfa_mem_dma_setup(meminfo, cee_dma, bfa_cee_meminfo());
	bfa_mem_dma_setup(meminfo, sfp_dma, bfa_sfp_meminfo());
	bfa_mem_dma_setup(meminfo, flash_dma,
			  bfa_flash_meminfo(cfg->drvcfg.min_cfg));
	bfa_mem_dma_setup(meminfo, diag_dma, bfa_diag_meminfo());
	bfa_mem_dma_setup(meminfo, phy_dma,
			  bfa_phy_meminfo(cfg->drvcfg.min_cfg));
	bfa_mem_dma_setup(meminfo, fru_dma,
			  bfa_fru_meminfo(cfg->drvcfg.min_cfg));
}

/*
 * Use this function to do attach the driver instance with the BFA
 * library. This function will not trigger any HW initialization
 * process (which will be done in bfa_init() call)
 *
 * This call will fail, if the cap is out of range compared to
 * pre-defined values within the BFA library
 *
 * @param[out]	bfa	Pointer to bfa_t.
 * @param[in]	bfad	Opaque handle back to the driver's IOC structure
 * @param[in]	cfg	Pointer to bfa_ioc_cfg_t. Should be same structure
 *			that was used in bfa_cfg_get_meminfo().
 * @param[in]	meminfo	Pointer to bfa_meminfo_t. The driver should
 *			use the bfa_cfg_get_meminfo() call to
 *			find the memory blocks required, allocate the
 *			required memory and provide the starting addresses.
 * @param[in]	pcidev	pointer to struct bfa_pcidev_s
 *
 * @return
 * void
 *
 * Special Considerations:
 *
 * @note
 *
 */
void
bfa_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
	       struct bfa_meminfo_s *meminfo, struct bfa_pcidev_s *pcidev)
{
	int	i;
	struct bfa_mem_dma_s *dma_info, *dma_elem;
	struct bfa_mem_kva_s *kva_info, *kva_elem;
	struct list_head *dm_qe, *km_qe;

	bfa->fcs = BFA_FALSE;

	WARN_ON((cfg == NULL) || (meminfo == NULL));

	/* Initialize memory pointers for iterative allocation */
	dma_info = &meminfo->dma_info;
	dma_info->kva_curp = dma_info->kva;
	dma_info->dma_curp = dma_info->dma;

	kva_info = &meminfo->kva_info;
	kva_info->kva_curp = kva_info->kva;

	list_for_each(dm_qe, &dma_info->qe) {
		dma_elem = (struct bfa_mem_dma_s *) dm_qe;
		dma_elem->kva_curp = dma_elem->kva;
		dma_elem->dma_curp = dma_elem->dma;
	}

	list_for_each(km_qe, &kva_info->qe) {
		kva_elem = (struct bfa_mem_kva_s *) km_qe;
		kva_elem->kva_curp = kva_elem->kva;
	}

	bfa_iocfc_attach(bfa, bfad, cfg, pcidev);

	for (i = 0; hal_mods[i]; i++)
		hal_mods[i]->attach(bfa, bfad, cfg, pcidev);

	bfa_com_port_attach(bfa);
	bfa_com_ablk_attach(bfa);
	bfa_com_cee_attach(bfa);
	bfa_com_sfp_attach(bfa);
	bfa_com_flash_attach(bfa, cfg->drvcfg.min_cfg);
	bfa_com_diag_attach(bfa);
	bfa_com_phy_attach(bfa, cfg->drvcfg.min_cfg);
	bfa_com_fru_attach(bfa, cfg->drvcfg.min_cfg);
}

/*
 * Use this function to delete a BFA IOC. IOC should be stopped (by
 * calling bfa_stop()) before this function call.
 *
 * @param[in] bfa - pointer to bfa_t.
 *
 * @return
 * void
 *
 * Special Considerations:
 *
 * @note
 */
void
bfa_detach(struct bfa_s *bfa)
{
	int	i;

	for (i = 0; hal_mods[i]; i++)
		hal_mods[i]->detach(bfa);
	bfa_ioc_detach(&bfa->ioc);
}

void
bfa_comp_deq(struct bfa_s *bfa, struct list_head *comp_q)
{
	INIT_LIST_HEAD(comp_q);
	list_splice_tail_init(&bfa->comp_q, comp_q);
}

void
bfa_comp_process(struct bfa_s *bfa, struct list_head *comp_q)
{
	struct list_head		*qe;
	struct list_head		*qen;
	struct bfa_cb_qe_s	*hcb_qe;
	bfa_cb_cbfn_status_t	cbfn;

	list_for_each_safe(qe, qen, comp_q) {
		hcb_qe = (struct bfa_cb_qe_s *) qe;
		if (hcb_qe->pre_rmv) {
			/* qe is invalid after return, dequeue before cbfn() */
			list_del(qe);
			cbfn = (bfa_cb_cbfn_status_t)(hcb_qe->cbfn);
			cbfn(hcb_qe->cbarg, hcb_qe->fw_status);
		} else
			hcb_qe->cbfn(hcb_qe->cbarg, BFA_TRUE);
	}
}

void
bfa_comp_free(struct bfa_s *bfa, struct list_head *comp_q)
{
	struct list_head		*qe;
	struct bfa_cb_qe_s	*hcb_qe;

	while (!list_empty(comp_q)) {
		bfa_q_deq(comp_q, &qe);
		hcb_qe = (struct bfa_cb_qe_s *) qe;
		WARN_ON(hcb_qe->pre_rmv);
		hcb_qe->cbfn(hcb_qe->cbarg, BFA_FALSE);
	}
}

/*
 * Return the list of PCI vendor/device id lists supported by this
 * BFA instance.
 */
void
bfa_get_pciids(struct bfa_pciid_s **pciids, int *npciids)
{
	static struct bfa_pciid_s __pciids[] = {
		{BFA_PCI_VENDOR_ID_BROCADE, BFA_PCI_DEVICE_ID_FC_8G2P},
		{BFA_PCI_VENDOR_ID_BROCADE, BFA_PCI_DEVICE_ID_FC_8G1P},
		{BFA_PCI_VENDOR_ID_BROCADE, BFA_PCI_DEVICE_ID_CT},
		{BFA_PCI_VENDOR_ID_BROCADE, BFA_PCI_DEVICE_ID_CT_FC},
	};

	*npciids = sizeof(__pciids) / sizeof(__pciids[0]);
	*pciids = __pciids;
}

/*
 * Use this function query the default struct bfa_iocfc_cfg_s value (compiled
 * into BFA layer). The OS driver can then turn back and overwrite entries that
 * have been configured by the user.
 *
 * @param[in] cfg - pointer to bfa_ioc_cfg_t
 *
 * @return
 *	void
 *
 * Special Considerations:
 * note
 */
void
bfa_cfg_get_default(struct bfa_iocfc_cfg_s *cfg)
{
	cfg->fwcfg.num_fabrics = DEF_CFG_NUM_FABRICS;
	cfg->fwcfg.num_lports = DEF_CFG_NUM_LPORTS;
	cfg->fwcfg.num_rports = DEF_CFG_NUM_RPORTS;
	cfg->fwcfg.num_ioim_reqs = DEF_CFG_NUM_IOIM_REQS;
	cfg->fwcfg.num_tskim_reqs = DEF_CFG_NUM_TSKIM_REQS;
	cfg->fwcfg.num_fcxp_reqs = DEF_CFG_NUM_FCXP_REQS;
	cfg->fwcfg.num_uf_bufs = DEF_CFG_NUM_UF_BUFS;
	cfg->fwcfg.num_cqs = DEF_CFG_NUM_CQS;
	cfg->fwcfg.num_fwtio_reqs = 0;

	cfg->drvcfg.num_reqq_elems = DEF_CFG_NUM_REQQ_ELEMS;
	cfg->drvcfg.num_rspq_elems = DEF_CFG_NUM_RSPQ_ELEMS;
	cfg->drvcfg.num_sgpgs = DEF_CFG_NUM_SGPGS;
	cfg->drvcfg.num_sboot_tgts = DEF_CFG_NUM_SBOOT_TGTS;
	cfg->drvcfg.num_sboot_luns = DEF_CFG_NUM_SBOOT_LUNS;
	cfg->drvcfg.path_tov = BFA_FCPIM_PATHTOV_DEF;
	cfg->drvcfg.ioc_recover = BFA_FALSE;
	cfg->drvcfg.delay_comp = BFA_FALSE;

}

void
bfa_cfg_get_min(struct bfa_iocfc_cfg_s *cfg)
{
	bfa_cfg_get_default(cfg);
	cfg->fwcfg.num_ioim_reqs   = BFA_IOIM_MIN;
	cfg->fwcfg.num_tskim_reqs  = BFA_TSKIM_MIN;
	cfg->fwcfg.num_fcxp_reqs   = BFA_FCXP_MIN;
	cfg->fwcfg.num_uf_bufs     = BFA_UF_MIN;
	cfg->fwcfg.num_rports      = BFA_RPORT_MIN;
	cfg->fwcfg.num_fwtio_reqs = 0;

	cfg->drvcfg.num_sgpgs      = BFA_SGPG_MIN;
	cfg->drvcfg.num_reqq_elems = BFA_REQQ_NELEMS_MIN;
	cfg->drvcfg.num_rspq_elems = BFA_RSPQ_NELEMS_MIN;
	cfg->drvcfg.min_cfg	   = BFA_TRUE;
}
