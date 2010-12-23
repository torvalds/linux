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

/**
 * IOC local definitions
 */

/**
 * Asic specific macros : see bfa_hw_cb.c and bfa_hw_ct.c for details.
 */

#define bfa_ioc_firmware_lock(__ioc)			\
			((__ioc)->ioc_hwif->ioc_firmware_lock(__ioc))
#define bfa_ioc_firmware_unlock(__ioc)			\
			((__ioc)->ioc_hwif->ioc_firmware_unlock(__ioc))
#define bfa_ioc_reg_init(__ioc) ((__ioc)->ioc_hwif->ioc_reg_init(__ioc))
#define bfa_ioc_map_port(__ioc) ((__ioc)->ioc_hwif->ioc_map_port(__ioc))
#define bfa_ioc_notify_fail(__ioc)			\
			((__ioc)->ioc_hwif->ioc_notify_fail(__ioc))
#define bfa_ioc_sync_join(__ioc)			\
			((__ioc)->ioc_hwif->ioc_sync_join(__ioc))
#define bfa_ioc_sync_leave(__ioc)			\
			((__ioc)->ioc_hwif->ioc_sync_leave(__ioc))
#define bfa_ioc_sync_ack(__ioc)				\
			((__ioc)->ioc_hwif->ioc_sync_ack(__ioc))
#define bfa_ioc_sync_complete(__ioc)			\
			((__ioc)->ioc_hwif->ioc_sync_complete(__ioc))

#define bfa_ioc_mbox_cmd_pending(__ioc)		\
			(!list_empty(&((__ioc)->mbox_mod.cmd_q)) || \
			readl((__ioc)->ioc_regs.hfn_mbox_cmd))

static bool bfa_nw_auto_recover = true;

/*
 * forward declarations
 */
static void bfa_ioc_hw_sem_get(struct bfa_ioc *ioc);
static void bfa_ioc_hw_sem_get_cancel(struct bfa_ioc *ioc);
static void bfa_ioc_hwinit(struct bfa_ioc *ioc, bool force);
static void bfa_ioc_send_enable(struct bfa_ioc *ioc);
static void bfa_ioc_send_disable(struct bfa_ioc *ioc);
static void bfa_ioc_send_getattr(struct bfa_ioc *ioc);
static void bfa_ioc_hb_monitor(struct bfa_ioc *ioc);
static void bfa_ioc_hb_stop(struct bfa_ioc *ioc);
static void bfa_ioc_reset(struct bfa_ioc *ioc, bool force);
static void bfa_ioc_mbox_poll(struct bfa_ioc *ioc);
static void bfa_ioc_mbox_hbfail(struct bfa_ioc *ioc);
static void bfa_ioc_recover(struct bfa_ioc *ioc);
static void bfa_ioc_check_attr_wwns(struct bfa_ioc *ioc);
static void bfa_ioc_disable_comp(struct bfa_ioc *ioc);
static void bfa_ioc_lpu_stop(struct bfa_ioc *ioc);
static void bfa_ioc_fail_notify(struct bfa_ioc *ioc);
static void bfa_ioc_pf_enabled(struct bfa_ioc *ioc);
static void bfa_ioc_pf_disabled(struct bfa_ioc *ioc);
static void bfa_ioc_pf_initfailed(struct bfa_ioc *ioc);
static void bfa_ioc_pf_failed(struct bfa_ioc *ioc);
static void bfa_ioc_pf_fwmismatch(struct bfa_ioc *ioc);
static void bfa_ioc_boot(struct bfa_ioc *ioc, u32 boot_type,
			 u32 boot_param);
static u32 bfa_ioc_smem_pgnum(struct bfa_ioc *ioc, u32 fmaddr);
static u32 bfa_ioc_smem_pgoff(struct bfa_ioc *ioc, u32 fmaddr);
static void bfa_ioc_get_adapter_serial_num(struct bfa_ioc *ioc,
						char *serial_num);
static void bfa_ioc_get_adapter_fw_ver(struct bfa_ioc *ioc,
						char *fw_ver);
static void bfa_ioc_get_pci_chip_rev(struct bfa_ioc *ioc,
						char *chip_rev);
static void bfa_ioc_get_adapter_optrom_ver(struct bfa_ioc *ioc,
						char *optrom_ver);
static void bfa_ioc_get_adapter_manufacturer(struct bfa_ioc *ioc,
						char *manufacturer);
static void bfa_ioc_get_adapter_model(struct bfa_ioc *ioc, char *model);
static u64 bfa_ioc_get_pwwn(struct bfa_ioc *ioc);

/**
 * IOC state machine definitions/declarations
 */
enum ioc_event {
	IOC_E_RESET		= 1,	/*!< IOC reset request		*/
	IOC_E_ENABLE		= 2,	/*!< IOC enable request		*/
	IOC_E_DISABLE		= 3,	/*!< IOC disable request	*/
	IOC_E_DETACH		= 4,	/*!< driver detach cleanup	*/
	IOC_E_ENABLED		= 5,	/*!< f/w enabled		*/
	IOC_E_FWRSP_GETATTR	= 6,	/*!< IOC get attribute response	*/
	IOC_E_DISABLED		= 7,	/*!< f/w disabled		*/
	IOC_E_INITFAILED	= 8,	/*!< failure notice by iocpf sm	*/
	IOC_E_PFAILED		= 9,	/*!< failure notice by iocpf sm	*/
	IOC_E_HBFAIL		= 10,	/*!< heartbeat failure		*/
	IOC_E_HWERROR		= 11,	/*!< hardware error interrupt	*/
	IOC_E_TIMEOUT		= 12,	/*!< timeout			*/
};

bfa_fsm_state_decl(bfa_ioc, uninit, struct bfa_ioc, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, reset, struct bfa_ioc, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, enabling, struct bfa_ioc, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, getattr, struct bfa_ioc, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, op, struct bfa_ioc, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, fail_retry, struct bfa_ioc, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, fail, struct bfa_ioc, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, disabling, struct bfa_ioc, enum ioc_event);
bfa_fsm_state_decl(bfa_ioc, disabled, struct bfa_ioc, enum ioc_event);

static struct bfa_sm_table ioc_sm_table[] = {
	{BFA_SM(bfa_ioc_sm_uninit), BFA_IOC_UNINIT},
	{BFA_SM(bfa_ioc_sm_reset), BFA_IOC_RESET},
	{BFA_SM(bfa_ioc_sm_enabling), BFA_IOC_ENABLING},
	{BFA_SM(bfa_ioc_sm_getattr), BFA_IOC_GETATTR},
	{BFA_SM(bfa_ioc_sm_op), BFA_IOC_OPERATIONAL},
	{BFA_SM(bfa_ioc_sm_fail_retry), BFA_IOC_INITFAIL},
	{BFA_SM(bfa_ioc_sm_fail), BFA_IOC_FAIL},
	{BFA_SM(bfa_ioc_sm_disabling), BFA_IOC_DISABLING},
	{BFA_SM(bfa_ioc_sm_disabled), BFA_IOC_DISABLED},
};

/**
 * IOCPF state machine definitions/declarations
 */

/*
 * Forward declareations for iocpf state machine
 */
static void bfa_iocpf_enable(struct bfa_ioc *ioc);
static void bfa_iocpf_disable(struct bfa_ioc *ioc);
static void bfa_iocpf_fail(struct bfa_ioc *ioc);
static void bfa_iocpf_initfail(struct bfa_ioc *ioc);
static void bfa_iocpf_getattrfail(struct bfa_ioc *ioc);
static void bfa_iocpf_stop(struct bfa_ioc *ioc);

/**
 * IOCPF state machine events
 */
enum iocpf_event {
	IOCPF_E_ENABLE		= 1,	/*!< IOCPF enable request	*/
	IOCPF_E_DISABLE		= 2,	/*!< IOCPF disable request	*/
	IOCPF_E_STOP		= 3,	/*!< stop on driver detach	*/
	IOCPF_E_FWREADY	 	= 4,	/*!< f/w initialization done	*/
	IOCPF_E_FWRSP_ENABLE	= 5,	/*!< enable f/w response	*/
	IOCPF_E_FWRSP_DISABLE	= 6,	/*!< disable f/w response	*/
	IOCPF_E_FAIL		= 7,	/*!< failure notice by ioc sm	*/
	IOCPF_E_INITFAIL	= 8,	/*!< init fail notice by ioc sm	*/
	IOCPF_E_GETATTRFAIL	= 9,	/*!< init fail notice by ioc sm	*/
	IOCPF_E_SEMLOCKED	= 10,   /*!< h/w semaphore is locked	*/
	IOCPF_E_TIMEOUT		= 11,   /*!< f/w response timeout	*/
};

/**
 * IOCPF states
 */
enum bfa_iocpf_state {
	BFA_IOCPF_RESET		= 1,	/*!< IOC is in reset state */
	BFA_IOCPF_SEMWAIT	= 2,	/*!< Waiting for IOC h/w semaphore */
	BFA_IOCPF_HWINIT	= 3,	/*!< IOC h/w is being initialized */
	BFA_IOCPF_READY		= 4,	/*!< IOCPF is initialized */
	BFA_IOCPF_INITFAIL	= 5,	/*!< IOCPF failed */
	BFA_IOCPF_FAIL		= 6,	/*!< IOCPF failed */
	BFA_IOCPF_DISABLING	= 7,	/*!< IOCPF is being disabled */
	BFA_IOCPF_DISABLED	= 8,	/*!< IOCPF is disabled */
	BFA_IOCPF_FWMISMATCH	= 9,	/*!< IOC f/w different from drivers */
};

bfa_fsm_state_decl(bfa_iocpf, reset, struct bfa_iocpf, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, fwcheck, struct bfa_iocpf, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, mismatch, struct bfa_iocpf, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, semwait, struct bfa_iocpf, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, hwinit, struct bfa_iocpf, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, enabling, struct bfa_iocpf, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, ready, struct bfa_iocpf, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, initfail_sync, struct bfa_iocpf,
						enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, initfail, struct bfa_iocpf, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, fail_sync, struct bfa_iocpf, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, fail, struct bfa_iocpf, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, disabling, struct bfa_iocpf, enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, disabling_sync, struct bfa_iocpf,
						enum iocpf_event);
bfa_fsm_state_decl(bfa_iocpf, disabled, struct bfa_iocpf, enum iocpf_event);

static struct bfa_sm_table iocpf_sm_table[] = {
	{BFA_SM(bfa_iocpf_sm_reset), BFA_IOCPF_RESET},
	{BFA_SM(bfa_iocpf_sm_fwcheck), BFA_IOCPF_FWMISMATCH},
	{BFA_SM(bfa_iocpf_sm_mismatch), BFA_IOCPF_FWMISMATCH},
	{BFA_SM(bfa_iocpf_sm_semwait), BFA_IOCPF_SEMWAIT},
	{BFA_SM(bfa_iocpf_sm_hwinit), BFA_IOCPF_HWINIT},
	{BFA_SM(bfa_iocpf_sm_enabling), BFA_IOCPF_HWINIT},
	{BFA_SM(bfa_iocpf_sm_ready), BFA_IOCPF_READY},
	{BFA_SM(bfa_iocpf_sm_initfail_sync), BFA_IOCPF_INITFAIL},
	{BFA_SM(bfa_iocpf_sm_initfail), BFA_IOCPF_INITFAIL},
	{BFA_SM(bfa_iocpf_sm_fail_sync), BFA_IOCPF_FAIL},
	{BFA_SM(bfa_iocpf_sm_fail), BFA_IOCPF_FAIL},
	{BFA_SM(bfa_iocpf_sm_disabling), BFA_IOCPF_DISABLING},
	{BFA_SM(bfa_iocpf_sm_disabling_sync), BFA_IOCPF_DISABLING},
	{BFA_SM(bfa_iocpf_sm_disabled), BFA_IOCPF_DISABLED},
};

/**
 * IOC State Machine
 */

/**
 * Beginning state. IOC uninit state.
 */
static void
bfa_ioc_sm_uninit_entry(struct bfa_ioc *ioc)
{
}

/**
 * IOC is in uninit state.
 */
static void
bfa_ioc_sm_uninit(struct bfa_ioc *ioc, enum ioc_event event)
{
	switch (event) {
	case IOC_E_RESET:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_reset);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

/**
 * Reset entry actions -- initialize state machine
 */
static void
bfa_ioc_sm_reset_entry(struct bfa_ioc *ioc)
{
	bfa_fsm_set_state(&ioc->iocpf, bfa_iocpf_sm_reset);
}

/**
 * IOC is in reset state.
 */
static void
bfa_ioc_sm_reset(struct bfa_ioc *ioc, enum ioc_event event)
{
	switch (event) {
	case IOC_E_ENABLE:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_enabling);
		break;

	case IOC_E_DISABLE:
		bfa_ioc_disable_comp(ioc);
		break;

	case IOC_E_DETACH:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_ioc_sm_enabling_entry(struct bfa_ioc *ioc)
{
	bfa_iocpf_enable(ioc);
}

/**
 * Host IOC function is being enabled, awaiting response from firmware.
 * Semaphore is acquired.
 */
static void
bfa_ioc_sm_enabling(struct bfa_ioc *ioc, enum ioc_event event)
{
	switch (event) {
	case IOC_E_ENABLED:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_getattr);
		break;

	case IOC_E_PFAILED:
		/* !!! fall through !!! */
	case IOC_E_HWERROR:
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_fail_retry);
		if (event != IOC_E_PFAILED)
			bfa_iocpf_initfail(ioc);
		break;

	case IOC_E_DISABLE:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_disabling);
		break;

	case IOC_E_DETACH:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
		bfa_iocpf_stop(ioc);
		break;

	case IOC_E_ENABLE:
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

/**
 * Semaphore should be acquired for version check.
 */
static void
bfa_ioc_sm_getattr_entry(struct bfa_ioc *ioc)
{
	mod_timer(&ioc->ioc_timer, jiffies +
		msecs_to_jiffies(BFA_IOC_TOV));
	bfa_ioc_send_getattr(ioc);
}

/**
 * IOC configuration in progress. Timer is active.
 */
static void
bfa_ioc_sm_getattr(struct bfa_ioc *ioc, enum ioc_event event)
{
	switch (event) {
	case IOC_E_FWRSP_GETATTR:
		del_timer(&ioc->ioc_timer);
		bfa_ioc_check_attr_wwns(ioc);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_op);
		break;

	case IOC_E_PFAILED:
	case IOC_E_HWERROR:
		del_timer(&ioc->ioc_timer);
		/* fall through */
	case IOC_E_TIMEOUT:
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_fail_retry);
		if (event != IOC_E_PFAILED)
			bfa_iocpf_getattrfail(ioc);
		break;

	case IOC_E_DISABLE:
		del_timer(&ioc->ioc_timer);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_disabling);
		break;

	case IOC_E_ENABLE:
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_ioc_sm_op_entry(struct bfa_ioc *ioc)
{
	ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_OK);
	bfa_ioc_hb_monitor(ioc);
}

static void
bfa_ioc_sm_op(struct bfa_ioc *ioc, enum ioc_event event)
{
	switch (event) {
	case IOC_E_ENABLE:
		break;

	case IOC_E_DISABLE:
		bfa_ioc_hb_stop(ioc);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_disabling);
		break;

	case IOC_E_PFAILED:
	case IOC_E_HWERROR:
		bfa_ioc_hb_stop(ioc);
		/* !!! fall through !!! */
	case IOC_E_HBFAIL:
		bfa_ioc_fail_notify(ioc);
		if (ioc->iocpf.auto_recover)
			bfa_fsm_set_state(ioc, bfa_ioc_sm_fail_retry);
		else
			bfa_fsm_set_state(ioc, bfa_ioc_sm_fail);

		if (event != IOC_E_PFAILED)
			bfa_iocpf_fail(ioc);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_ioc_sm_disabling_entry(struct bfa_ioc *ioc)
{
	bfa_iocpf_disable(ioc);
}

/**
 * IOC is being desabled
 */
static void
bfa_ioc_sm_disabling(struct bfa_ioc *ioc, enum ioc_event event)
{
	switch (event) {
	case IOC_E_DISABLED:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_disabled);
		break;

	case IOC_E_HWERROR:
		/*
		 * No state change.  Will move to disabled state
		 * after iocpf sm completes failure processing and
		 * moves to disabled state.
		 */
		bfa_iocpf_fail(ioc);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

/**
 * IOC desable completion entry.
 */
static void
bfa_ioc_sm_disabled_entry(struct bfa_ioc *ioc)
{
	bfa_ioc_disable_comp(ioc);
}

static void
bfa_ioc_sm_disabled(struct bfa_ioc *ioc, enum ioc_event event)
{
	switch (event) {
	case IOC_E_ENABLE:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_enabling);
		break;

	case IOC_E_DISABLE:
		ioc->cbfn->disable_cbfn(ioc->bfa);
		break;

	case IOC_E_DETACH:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
		bfa_iocpf_stop(ioc);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_ioc_sm_fail_retry_entry(struct bfa_ioc *ioc)
{
}

/**
 * Hardware initialization retry.
 */
static void
bfa_ioc_sm_fail_retry(struct bfa_ioc *ioc, enum ioc_event event)
{
	switch (event) {
	case IOC_E_ENABLED:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_getattr);
		break;

	case IOC_E_PFAILED:
	case IOC_E_HWERROR:
		/**
		 * Initialization retry failed.
		 */
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		if (event != IOC_E_PFAILED)
			bfa_iocpf_initfail(ioc);
		break;

	case IOC_E_INITFAILED:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_fail);
		break;

	case IOC_E_ENABLE:
		break;

	case IOC_E_DISABLE:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_disabling);
		break;

	case IOC_E_DETACH:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
		bfa_iocpf_stop(ioc);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_ioc_sm_fail_entry(struct bfa_ioc *ioc)
{
}

/**
 * IOC failure.
 */
static void
bfa_ioc_sm_fail(struct bfa_ioc *ioc, enum ioc_event event)
{
	switch (event) {
	case IOC_E_ENABLE:
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		break;

	case IOC_E_DISABLE:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_disabling);
		break;

	case IOC_E_DETACH:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
		bfa_iocpf_stop(ioc);
		break;

	case IOC_E_HWERROR:
		/* HB failure notification, ignore. */
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

/**
 * IOCPF State Machine
 */

/**
 * Reset entry actions -- initialize state machine
 */
static void
bfa_iocpf_sm_reset_entry(struct bfa_iocpf *iocpf)
{
	iocpf->retry_count = 0;
	iocpf->auto_recover = bfa_nw_auto_recover;
}

/**
 * Beginning state. IOC is in reset state.
 */
static void
bfa_iocpf_sm_reset(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	switch (event) {
	case IOCPF_E_ENABLE:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fwcheck);
		break;

	case IOCPF_E_STOP:
		break;

	default:
		bfa_sm_fault(iocpf->ioc, event);
	}
}

/**
 * Semaphore should be acquired for version check.
 */
static void
bfa_iocpf_sm_fwcheck_entry(struct bfa_iocpf *iocpf)
{
	bfa_ioc_hw_sem_get(iocpf->ioc);
}

/**
 * Awaiting h/w semaphore to continue with version check.
 */
static void
bfa_iocpf_sm_fwcheck(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_SEMLOCKED:
		if (bfa_ioc_firmware_lock(ioc)) {
			if (bfa_ioc_sync_complete(ioc)) {
				iocpf->retry_count = 0;
				bfa_ioc_sync_join(ioc);
				bfa_fsm_set_state(iocpf, bfa_iocpf_sm_hwinit);
			} else {
				bfa_ioc_firmware_unlock(ioc);
				bfa_nw_ioc_hw_sem_release(ioc);
				mod_timer(&ioc->sem_timer, jiffies +
					msecs_to_jiffies(BFA_IOC_HWSEM_TOV));
			}
		} else {
			bfa_nw_ioc_hw_sem_release(ioc);
			bfa_fsm_set_state(iocpf, bfa_iocpf_sm_mismatch);
		}
		break;

	case IOCPF_E_DISABLE:
		bfa_ioc_hw_sem_get_cancel(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		bfa_ioc_pf_disabled(ioc);
		break;

	case IOCPF_E_STOP:
		bfa_ioc_hw_sem_get_cancel(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

/**
 * Notify enable completion callback
 */
static void
bfa_iocpf_sm_mismatch_entry(struct bfa_iocpf *iocpf)
{
	/* Call only the first time sm enters fwmismatch state. */
	if (iocpf->retry_count == 0)
		bfa_ioc_pf_fwmismatch(iocpf->ioc);

	iocpf->retry_count++;
	mod_timer(&(iocpf->ioc)->iocpf_timer, jiffies +
		msecs_to_jiffies(BFA_IOC_TOV));
}

/**
 * Awaiting firmware version match.
 */
static void
bfa_iocpf_sm_mismatch(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_TIMEOUT:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fwcheck);
		break;

	case IOCPF_E_DISABLE:
		del_timer(&ioc->iocpf_timer);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		bfa_ioc_pf_disabled(ioc);
		break;

	case IOCPF_E_STOP:
		del_timer(&ioc->iocpf_timer);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

/**
 * Request for semaphore.
 */
static void
bfa_iocpf_sm_semwait_entry(struct bfa_iocpf *iocpf)
{
	bfa_ioc_hw_sem_get(iocpf->ioc);
}

/**
 * Awaiting semaphore for h/w initialzation.
 */
static void
bfa_iocpf_sm_semwait(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_SEMLOCKED:
		if (bfa_ioc_sync_complete(ioc)) {
			bfa_ioc_sync_join(ioc);
			bfa_fsm_set_state(iocpf, bfa_iocpf_sm_hwinit);
		} else {
			bfa_nw_ioc_hw_sem_release(ioc);
			mod_timer(&ioc->sem_timer, jiffies +
				msecs_to_jiffies(BFA_IOC_HWSEM_TOV));
		}
		break;

	case IOCPF_E_DISABLE:
		bfa_ioc_hw_sem_get_cancel(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling_sync);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_hwinit_entry(struct bfa_iocpf *iocpf)
{
	mod_timer(&(iocpf->ioc)->iocpf_timer, jiffies +
		msecs_to_jiffies(BFA_IOC_TOV));
	bfa_ioc_reset(iocpf->ioc, 0);
}

/**
 * Hardware is being initialized. Interrupts are enabled.
 * Holding hardware semaphore lock.
 */
static void
bfa_iocpf_sm_hwinit(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_FWREADY:
		del_timer(&ioc->iocpf_timer);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_enabling);
		break;

	case IOCPF_E_INITFAIL:
		del_timer(&ioc->iocpf_timer);
		/*
		 * !!! fall through !!!
		 */

	case IOCPF_E_TIMEOUT:
		bfa_nw_ioc_hw_sem_release(ioc);
		if (event == IOCPF_E_TIMEOUT)
			bfa_ioc_pf_failed(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_initfail_sync);
		break;

	case IOCPF_E_DISABLE:
		del_timer(&ioc->iocpf_timer);
		bfa_ioc_sync_leave(ioc);
		bfa_nw_ioc_hw_sem_release(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabled);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_enabling_entry(struct bfa_iocpf *iocpf)
{
	mod_timer(&(iocpf->ioc)->iocpf_timer, jiffies +
		msecs_to_jiffies(BFA_IOC_TOV));
	bfa_ioc_send_enable(iocpf->ioc);
}

/**
 * Host IOC function is being enabled, awaiting response from firmware.
 * Semaphore is acquired.
 */
static void
bfa_iocpf_sm_enabling(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_FWRSP_ENABLE:
		del_timer(&ioc->iocpf_timer);
		bfa_nw_ioc_hw_sem_release(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_ready);
		break;

	case IOCPF_E_INITFAIL:
		del_timer(&ioc->iocpf_timer);
		/*
		 * !!! fall through !!!
		 */
	case IOCPF_E_TIMEOUT:
		bfa_nw_ioc_hw_sem_release(ioc);
		if (event == IOCPF_E_TIMEOUT)
			bfa_ioc_pf_failed(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_initfail_sync);
		break;

	case IOCPF_E_DISABLE:
		del_timer(&ioc->iocpf_timer);
		bfa_nw_ioc_hw_sem_release(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling);
		break;

	case IOCPF_E_FWREADY:
		bfa_ioc_send_enable(ioc);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static bool
bfa_nw_ioc_is_operational(struct bfa_ioc *ioc)
{
	return bfa_fsm_cmp_state(ioc, bfa_ioc_sm_op);
}

static void
bfa_iocpf_sm_ready_entry(struct bfa_iocpf *iocpf)
{
	bfa_ioc_pf_enabled(iocpf->ioc);
}

static void
bfa_iocpf_sm_ready(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_DISABLE:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling);
		break;

	case IOCPF_E_GETATTRFAIL:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_initfail_sync);
		break;

	case IOCPF_E_FAIL:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail_sync);
		break;

	case IOCPF_E_FWREADY:
		bfa_ioc_pf_failed(ioc);
		if (bfa_nw_ioc_is_operational(ioc))
			bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail_sync);
		else
			bfa_fsm_set_state(iocpf, bfa_iocpf_sm_initfail_sync);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_disabling_entry(struct bfa_iocpf *iocpf)
{
	mod_timer(&(iocpf->ioc)->iocpf_timer, jiffies +
		msecs_to_jiffies(BFA_IOC_TOV));
	bfa_ioc_send_disable(iocpf->ioc);
}

/**
 * IOC is being disabled
 */
static void
bfa_iocpf_sm_disabling(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_FWRSP_DISABLE:
	case IOCPF_E_FWREADY:
		del_timer(&ioc->iocpf_timer);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling_sync);
		break;

	case IOCPF_E_FAIL:
		del_timer(&ioc->iocpf_timer);
		/*
		 * !!! fall through !!!
		 */

	case IOCPF_E_TIMEOUT:
		writel(BFI_IOC_FAIL, ioc->ioc_regs.ioc_fwstate);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling_sync);
		break;

	case IOCPF_E_FWRSP_ENABLE:
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_disabling_sync_entry(struct bfa_iocpf *iocpf)
{
	bfa_ioc_hw_sem_get(iocpf->ioc);
}

/**
 * IOC hb ack request is being removed.
 */
static void
bfa_iocpf_sm_disabling_sync(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_SEMLOCKED:
		bfa_ioc_sync_leave(ioc);
		bfa_nw_ioc_hw_sem_release(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabled);
		break;

	case IOCPF_E_FAIL:
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

/**
 * IOC disable completion entry.
 */
static void
bfa_iocpf_sm_disabled_entry(struct bfa_iocpf *iocpf)
{
	bfa_ioc_pf_disabled(iocpf->ioc);
}

static void
bfa_iocpf_sm_disabled(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_ENABLE:
		iocpf->retry_count = 0;
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_semwait);
		break;

	case IOCPF_E_STOP:
		bfa_ioc_firmware_unlock(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_initfail_sync_entry(struct bfa_iocpf *iocpf)
{
	bfa_ioc_hw_sem_get(iocpf->ioc);
}

/**
 * Hardware initialization failed.
 */
static void
bfa_iocpf_sm_initfail_sync(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_SEMLOCKED:
		bfa_ioc_notify_fail(ioc);
		bfa_ioc_sync_ack(ioc);
		iocpf->retry_count++;
		if (iocpf->retry_count >= BFA_IOC_HWINIT_MAX) {
			bfa_ioc_sync_leave(ioc);
			bfa_nw_ioc_hw_sem_release(ioc);
			bfa_fsm_set_state(iocpf, bfa_iocpf_sm_initfail);
		} else {
			if (bfa_ioc_sync_complete(ioc))
				bfa_fsm_set_state(iocpf, bfa_iocpf_sm_hwinit);
			else {
				bfa_nw_ioc_hw_sem_release(ioc);
				bfa_fsm_set_state(iocpf, bfa_iocpf_sm_semwait);
			}
		}
		break;

	case IOCPF_E_DISABLE:
		bfa_ioc_hw_sem_get_cancel(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling_sync);
		break;

	case IOCPF_E_STOP:
		bfa_ioc_hw_sem_get_cancel(ioc);
		bfa_ioc_firmware_unlock(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		break;

	case IOCPF_E_FAIL:
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_initfail_entry(struct bfa_iocpf *iocpf)
{
	bfa_ioc_pf_initfailed(iocpf->ioc);
}

/**
 * Hardware initialization failed.
 */
static void
bfa_iocpf_sm_initfail(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_DISABLE:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabled);
		break;

	case IOCPF_E_STOP:
		bfa_ioc_firmware_unlock(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_fail_sync_entry(struct bfa_iocpf *iocpf)
{
	/**
	 * Mark IOC as failed in hardware and stop firmware.
	 */
	bfa_ioc_lpu_stop(iocpf->ioc);

	/**
	 * Flush any queued up mailbox requests.
	 */
	bfa_ioc_mbox_hbfail(iocpf->ioc);
	bfa_ioc_hw_sem_get(iocpf->ioc);
}

/**
 * IOC is in failed state.
 */
static void
bfa_iocpf_sm_fail_sync(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_SEMLOCKED:
		iocpf->retry_count = 0;
		bfa_ioc_sync_ack(ioc);
		bfa_ioc_notify_fail(ioc);
		if (!iocpf->auto_recover) {
			bfa_ioc_sync_leave(ioc);
			bfa_nw_ioc_hw_sem_release(ioc);
			bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail);
		} else {
			if (bfa_ioc_sync_complete(ioc))
				bfa_fsm_set_state(iocpf, bfa_iocpf_sm_hwinit);
			else {
				bfa_nw_ioc_hw_sem_release(ioc);
				bfa_fsm_set_state(iocpf, bfa_iocpf_sm_semwait);
			}
		}
		break;

	case IOCPF_E_DISABLE:
		bfa_ioc_hw_sem_get_cancel(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling_sync);
		break;

	case IOCPF_E_FAIL:
		break;

	default:
		bfa_sm_fault(ioc, event);
	}
}

static void
bfa_iocpf_sm_fail_entry(struct bfa_iocpf *iocpf)
{
}

/**
 * @brief
 * IOC is in failed state.
 */
static void
bfa_iocpf_sm_fail(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	switch (event) {
	case IOCPF_E_DISABLE:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabled);
		break;

	default:
		bfa_sm_fault(iocpf->ioc, event);
	}
}

/**
 * BFA IOC private functions
 */

static void
bfa_ioc_disable_comp(struct bfa_ioc *ioc)
{
	struct list_head			*qe;
	struct bfa_ioc_hbfail_notify *notify;

	ioc->cbfn->disable_cbfn(ioc->bfa);

	/**
	 * Notify common modules registered for notification.
	 */
	list_for_each(qe, &ioc->hb_notify_q) {
		notify = (struct bfa_ioc_hbfail_notify *) qe;
		notify->cbfn(notify->cbarg);
	}
}

bool
bfa_nw_ioc_sem_get(void __iomem *sem_reg)
{
	u32 r32;
	int cnt = 0;
#define BFA_SEM_SPINCNT	3000

	r32 = readl(sem_reg);

	while (r32 && (cnt < BFA_SEM_SPINCNT)) {
		cnt++;
		udelay(2);
		r32 = readl(sem_reg);
	}

	if (r32 == 0)
		return true;

	BUG_ON(!(cnt < BFA_SEM_SPINCNT));
	return false;
}

void
bfa_nw_ioc_sem_release(void __iomem *sem_reg)
{
	writel(1, sem_reg);
}

static void
bfa_ioc_hw_sem_get(struct bfa_ioc *ioc)
{
	u32	r32;

	/**
	 * First read to the semaphore register will return 0, subsequent reads
	 * will return 1. Semaphore is released by writing 1 to the register
	 */
	r32 = readl(ioc->ioc_regs.ioc_sem_reg);
	if (r32 == 0) {
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_SEMLOCKED);
		return;
	}

	mod_timer(&ioc->sem_timer, jiffies +
		msecs_to_jiffies(BFA_IOC_HWSEM_TOV));
}

void
bfa_nw_ioc_hw_sem_release(struct bfa_ioc *ioc)
{
	writel(1, ioc->ioc_regs.ioc_sem_reg);
}

static void
bfa_ioc_hw_sem_get_cancel(struct bfa_ioc *ioc)
{
	del_timer(&ioc->sem_timer);
}

/**
 * @brief
 * Initialize LPU local memory (aka secondary memory / SRAM)
 */
static void
bfa_ioc_lmem_init(struct bfa_ioc *ioc)
{
	u32	pss_ctl;
	int		i;
#define PSS_LMEM_INIT_TIME  10000

	pss_ctl = readl(ioc->ioc_regs.pss_ctl_reg);
	pss_ctl &= ~__PSS_LMEM_RESET;
	pss_ctl |= __PSS_LMEM_INIT_EN;

	/*
	 * i2c workaround 12.5khz clock
	 */
	pss_ctl |= __PSS_I2C_CLK_DIV(3UL);
	writel(pss_ctl, ioc->ioc_regs.pss_ctl_reg);

	/**
	 * wait for memory initialization to be complete
	 */
	i = 0;
	do {
		pss_ctl = readl(ioc->ioc_regs.pss_ctl_reg);
		i++;
	} while (!(pss_ctl & __PSS_LMEM_INIT_DONE) && (i < PSS_LMEM_INIT_TIME));

	/**
	 * If memory initialization is not successful, IOC timeout will catch
	 * such failures.
	 */
	BUG_ON(!(pss_ctl & __PSS_LMEM_INIT_DONE));

	pss_ctl &= ~(__PSS_LMEM_INIT_DONE | __PSS_LMEM_INIT_EN);
	writel(pss_ctl, ioc->ioc_regs.pss_ctl_reg);
}

static void
bfa_ioc_lpu_start(struct bfa_ioc *ioc)
{
	u32	pss_ctl;

	/**
	 * Take processor out of reset.
	 */
	pss_ctl = readl(ioc->ioc_regs.pss_ctl_reg);
	pss_ctl &= ~__PSS_LPU0_RESET;

	writel(pss_ctl, ioc->ioc_regs.pss_ctl_reg);
}

static void
bfa_ioc_lpu_stop(struct bfa_ioc *ioc)
{
	u32	pss_ctl;

	/**
	 * Put processors in reset.
	 */
	pss_ctl = readl(ioc->ioc_regs.pss_ctl_reg);
	pss_ctl |= (__PSS_LPU0_RESET | __PSS_LPU1_RESET);

	writel(pss_ctl, ioc->ioc_regs.pss_ctl_reg);
}

/**
 * Get driver and firmware versions.
 */
void
bfa_nw_ioc_fwver_get(struct bfa_ioc *ioc, struct bfi_ioc_image_hdr *fwhdr)
{
	u32	pgnum, pgoff;
	u32	loff = 0;
	int		i;
	u32	*fwsig = (u32 *) fwhdr;

	pgnum = bfa_ioc_smem_pgnum(ioc, loff);
	pgoff = bfa_ioc_smem_pgoff(ioc, loff);
	writel(pgnum, ioc->ioc_regs.host_page_num_fn);

	for (i = 0; i < (sizeof(struct bfi_ioc_image_hdr) / sizeof(u32));
	     i++) {
		fwsig[i] =
			swab32(readl((loff) + (ioc->ioc_regs.smem_page_start)));
		loff += sizeof(u32);
	}
}

/**
 * Returns TRUE if same.
 */
bool
bfa_nw_ioc_fwver_cmp(struct bfa_ioc *ioc, struct bfi_ioc_image_hdr *fwhdr)
{
	struct bfi_ioc_image_hdr *drv_fwhdr;
	int i;

	drv_fwhdr = (struct bfi_ioc_image_hdr *)
		bfa_cb_image_get_chunk(BFA_IOC_FWIMG_TYPE(ioc), 0);

	for (i = 0; i < BFI_IOC_MD5SUM_SZ; i++) {
		if (fwhdr->md5sum[i] != drv_fwhdr->md5sum[i])
			return false;
	}

	return true;
}

/**
 * Return true if current running version is valid. Firmware signature and
 * execution context (driver/bios) must match.
 */
static bool
bfa_ioc_fwver_valid(struct bfa_ioc *ioc)
{
	struct bfi_ioc_image_hdr fwhdr, *drv_fwhdr;

	bfa_nw_ioc_fwver_get(ioc, &fwhdr);
	drv_fwhdr = (struct bfi_ioc_image_hdr *)
		bfa_cb_image_get_chunk(BFA_IOC_FWIMG_TYPE(ioc), 0);

	if (fwhdr.signature != drv_fwhdr->signature)
		return false;

	if (fwhdr.exec != drv_fwhdr->exec)
		return false;

	return bfa_nw_ioc_fwver_cmp(ioc, &fwhdr);
}

/**
 * Conditionally flush any pending message from firmware at start.
 */
static void
bfa_ioc_msgflush(struct bfa_ioc *ioc)
{
	u32	r32;

	r32 = readl(ioc->ioc_regs.lpu_mbox_cmd);
	if (r32)
		writel(1, ioc->ioc_regs.lpu_mbox_cmd);
}

/**
 * @img ioc_init_logic.jpg
 */
static void
bfa_ioc_hwinit(struct bfa_ioc *ioc, bool force)
{
	enum bfi_ioc_state ioc_fwstate;
	bool fwvalid;

	ioc_fwstate = readl(ioc->ioc_regs.ioc_fwstate);

	if (force)
		ioc_fwstate = BFI_IOC_UNINIT;

	/**
	 * check if firmware is valid
	 */
	fwvalid = (ioc_fwstate == BFI_IOC_UNINIT) ?
		false : bfa_ioc_fwver_valid(ioc);

	if (!fwvalid) {
		bfa_ioc_boot(ioc, BFI_BOOT_TYPE_NORMAL, ioc->pcidev.device_id);
		return;
	}

	/**
	 * If hardware initialization is in progress (initialized by other IOC),
	 * just wait for an initialization completion interrupt.
	 */
	if (ioc_fwstate == BFI_IOC_INITING) {
		ioc->cbfn->reset_cbfn(ioc->bfa);
		return;
	}

	/**
	 * If IOC function is disabled and firmware version is same,
	 * just re-enable IOC.
	 */
	if (ioc_fwstate == BFI_IOC_DISABLED || ioc_fwstate == BFI_IOC_OP) {
		/**
		 * When using MSI-X any pending firmware ready event should
		 * be flushed. Otherwise MSI-X interrupts are not delivered.
		 */
		bfa_ioc_msgflush(ioc);
		ioc->cbfn->reset_cbfn(ioc->bfa);
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_FWREADY);
		return;
	}

	/**
	 * Initialize the h/w for any other states.
	 */
	bfa_ioc_boot(ioc, BFI_BOOT_TYPE_NORMAL, ioc->pcidev.device_id);
}

void
bfa_nw_ioc_timeout(void *ioc_arg)
{
	struct bfa_ioc *ioc = (struct bfa_ioc *) ioc_arg;

	bfa_fsm_send_event(ioc, IOC_E_TIMEOUT);
}

static void
bfa_ioc_mbox_send(struct bfa_ioc *ioc, void *ioc_msg, int len)
{
	u32 *msgp = (u32 *) ioc_msg;
	u32 i;

	BUG_ON(!(len <= BFI_IOC_MSGLEN_MAX));

	/*
	 * first write msg to mailbox registers
	 */
	for (i = 0; i < len / sizeof(u32); i++)
		writel(cpu_to_le32(msgp[i]),
			      ioc->ioc_regs.hfn_mbox + i * sizeof(u32));

	for (; i < BFI_IOC_MSGLEN_MAX / sizeof(u32); i++)
		writel(0, ioc->ioc_regs.hfn_mbox + i * sizeof(u32));

	/*
	 * write 1 to mailbox CMD to trigger LPU event
	 */
	writel(1, ioc->ioc_regs.hfn_mbox_cmd);
	(void) readl(ioc->ioc_regs.hfn_mbox_cmd);
}

static void
bfa_ioc_send_enable(struct bfa_ioc *ioc)
{
	struct bfi_ioc_ctrl_req enable_req;
	struct timeval tv;

	bfi_h2i_set(enable_req.mh, BFI_MC_IOC, BFI_IOC_H2I_ENABLE_REQ,
		    bfa_ioc_portid(ioc));
	enable_req.ioc_class = ioc->ioc_mc;
	do_gettimeofday(&tv);
	enable_req.tv_sec = ntohl(tv.tv_sec);
	bfa_ioc_mbox_send(ioc, &enable_req, sizeof(struct bfi_ioc_ctrl_req));
}

static void
bfa_ioc_send_disable(struct bfa_ioc *ioc)
{
	struct bfi_ioc_ctrl_req disable_req;

	bfi_h2i_set(disable_req.mh, BFI_MC_IOC, BFI_IOC_H2I_DISABLE_REQ,
		    bfa_ioc_portid(ioc));
	bfa_ioc_mbox_send(ioc, &disable_req, sizeof(struct bfi_ioc_ctrl_req));
}

static void
bfa_ioc_send_getattr(struct bfa_ioc *ioc)
{
	struct bfi_ioc_getattr_req attr_req;

	bfi_h2i_set(attr_req.mh, BFI_MC_IOC, BFI_IOC_H2I_GETATTR_REQ,
		    bfa_ioc_portid(ioc));
	bfa_dma_be_addr_set(attr_req.attr_addr, ioc->attr_dma.pa);
	bfa_ioc_mbox_send(ioc, &attr_req, sizeof(attr_req));
}

void
bfa_nw_ioc_hb_check(void *cbarg)
{
	struct bfa_ioc *ioc = cbarg;
	u32	hb_count;

	hb_count = readl(ioc->ioc_regs.heartbeat);
	if (ioc->hb_count == hb_count) {
		bfa_ioc_recover(ioc);
		return;
	} else {
		ioc->hb_count = hb_count;
	}

	bfa_ioc_mbox_poll(ioc);
	mod_timer(&ioc->hb_timer, jiffies +
		msecs_to_jiffies(BFA_IOC_HB_TOV));
}

static void
bfa_ioc_hb_monitor(struct bfa_ioc *ioc)
{
	ioc->hb_count = readl(ioc->ioc_regs.heartbeat);
	mod_timer(&ioc->hb_timer, jiffies +
		msecs_to_jiffies(BFA_IOC_HB_TOV));
}

static void
bfa_ioc_hb_stop(struct bfa_ioc *ioc)
{
	del_timer(&ioc->hb_timer);
}

/**
 * @brief
 *	Initiate a full firmware download.
 */
static void
bfa_ioc_download_fw(struct bfa_ioc *ioc, u32 boot_type,
		    u32 boot_param)
{
	u32 *fwimg;
	u32 pgnum, pgoff;
	u32 loff = 0;
	u32 chunkno = 0;
	u32 i;

	/**
	 * Initialize LMEM first before code download
	 */
	bfa_ioc_lmem_init(ioc);

	fwimg = bfa_cb_image_get_chunk(BFA_IOC_FWIMG_TYPE(ioc), chunkno);

	pgnum = bfa_ioc_smem_pgnum(ioc, loff);
	pgoff = bfa_ioc_smem_pgoff(ioc, loff);

	writel(pgnum, ioc->ioc_regs.host_page_num_fn);

	for (i = 0; i < bfa_cb_image_get_size(BFA_IOC_FWIMG_TYPE(ioc)); i++) {
		if (BFA_IOC_FLASH_CHUNK_NO(i) != chunkno) {
			chunkno = BFA_IOC_FLASH_CHUNK_NO(i);
			fwimg = bfa_cb_image_get_chunk(BFA_IOC_FWIMG_TYPE(ioc),
					BFA_IOC_FLASH_CHUNK_ADDR(chunkno));
		}

		/**
		 * write smem
		 */
		writel((swab32(fwimg[BFA_IOC_FLASH_OFFSET_IN_CHUNK(i)])),
			      ((ioc->ioc_regs.smem_page_start) + (loff)));

		loff += sizeof(u32);

		/**
		 * handle page offset wrap around
		 */
		loff = PSS_SMEM_PGOFF(loff);
		if (loff == 0) {
			pgnum++;
			writel(pgnum,
				      ioc->ioc_regs.host_page_num_fn);
		}
	}

	writel(bfa_ioc_smem_pgnum(ioc, 0),
		      ioc->ioc_regs.host_page_num_fn);

	/*
	 * Set boot type and boot param at the end.
	*/
	writel((swab32(swab32(boot_type))), ((ioc->ioc_regs.smem_page_start)
			+ (BFI_BOOT_TYPE_OFF)));
	writel((swab32(swab32(boot_param))), ((ioc->ioc_regs.smem_page_start)
			+ (BFI_BOOT_PARAM_OFF)));
}

static void
bfa_ioc_reset(struct bfa_ioc *ioc, bool force)
{
	bfa_ioc_hwinit(ioc, force);
}

/**
 * @brief
 * Update BFA configuration from firmware configuration.
 */
static void
bfa_ioc_getattr_reply(struct bfa_ioc *ioc)
{
	struct bfi_ioc_attr *attr = ioc->attr;

	attr->adapter_prop  = ntohl(attr->adapter_prop);
	attr->card_type     = ntohl(attr->card_type);
	attr->maxfrsize	    = ntohs(attr->maxfrsize);

	bfa_fsm_send_event(ioc, IOC_E_FWRSP_GETATTR);
}

/**
 * Attach time initialization of mbox logic.
 */
static void
bfa_ioc_mbox_attach(struct bfa_ioc *ioc)
{
	struct bfa_ioc_mbox_mod *mod = &ioc->mbox_mod;
	int	mc;

	INIT_LIST_HEAD(&mod->cmd_q);
	for (mc = 0; mc < BFI_MC_MAX; mc++) {
		mod->mbhdlr[mc].cbfn = NULL;
		mod->mbhdlr[mc].cbarg = ioc->bfa;
	}
}

/**
 * Mbox poll timer -- restarts any pending mailbox requests.
 */
static void
bfa_ioc_mbox_poll(struct bfa_ioc *ioc)
{
	struct bfa_ioc_mbox_mod *mod = &ioc->mbox_mod;
	struct bfa_mbox_cmd *cmd;
	u32			stat;

	/**
	 * If no command pending, do nothing
	 */
	if (list_empty(&mod->cmd_q))
		return;

	/**
	 * If previous command is not yet fetched by firmware, do nothing
	 */
	stat = readl(ioc->ioc_regs.hfn_mbox_cmd);
	if (stat)
		return;

	/**
	 * Enqueue command to firmware.
	 */
	bfa_q_deq(&mod->cmd_q, &cmd);
	bfa_ioc_mbox_send(ioc, cmd->msg, sizeof(cmd->msg));
}

/**
 * Cleanup any pending requests.
 */
static void
bfa_ioc_mbox_hbfail(struct bfa_ioc *ioc)
{
	struct bfa_ioc_mbox_mod *mod = &ioc->mbox_mod;
	struct bfa_mbox_cmd *cmd;

	while (!list_empty(&mod->cmd_q))
		bfa_q_deq(&mod->cmd_q, &cmd);
}

static void
bfa_ioc_fail_notify(struct bfa_ioc *ioc)
{
	struct list_head		*qe;
	struct bfa_ioc_hbfail_notify	*notify;

	/**
	 * Notify driver and common modules registered for notification.
	 */
	ioc->cbfn->hbfail_cbfn(ioc->bfa);
	list_for_each(qe, &ioc->hb_notify_q) {
		notify = (struct bfa_ioc_hbfail_notify *) qe;
		notify->cbfn(notify->cbarg);
	}
}

static void
bfa_ioc_pf_enabled(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(ioc, IOC_E_ENABLED);
}

static void
bfa_ioc_pf_disabled(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(ioc, IOC_E_DISABLED);
}

static void
bfa_ioc_pf_initfailed(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(ioc, IOC_E_INITFAILED);
}

static void
bfa_ioc_pf_failed(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(ioc, IOC_E_PFAILED);
}

static void
bfa_ioc_pf_fwmismatch(struct bfa_ioc *ioc)
{
	/**
	 * Provide enable completion callback and AEN notification.
	 */
	ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
}

/**
 * IOC public
 */
static enum bfa_status
bfa_ioc_pll_init(struct bfa_ioc *ioc)
{
	/*
	 *  Hold semaphore so that nobody can access the chip during init.
	 */
	bfa_nw_ioc_sem_get(ioc->ioc_regs.ioc_init_sem_reg);

	bfa_ioc_pll_init_asic(ioc);

	ioc->pllinit = true;
	/*
	 *  release semaphore.
	 */
	bfa_nw_ioc_sem_release(ioc->ioc_regs.ioc_init_sem_reg);

	return BFA_STATUS_OK;
}

/**
 * Interface used by diag module to do firmware boot with memory test
 * as the entry vector.
 */
static void
bfa_ioc_boot(struct bfa_ioc *ioc, u32 boot_type, u32 boot_param)
{
	void __iomem *rb;

	bfa_ioc_stats(ioc, ioc_boots);

	if (bfa_ioc_pll_init(ioc) != BFA_STATUS_OK)
		return;

	/**
	 * Initialize IOC state of all functions on a chip reset.
	 */
	rb = ioc->pcidev.pci_bar_kva;
	if (boot_param == BFI_BOOT_TYPE_MEMTEST) {
		writel(BFI_IOC_MEMTEST, (rb + BFA_IOC0_STATE_REG));
		writel(BFI_IOC_MEMTEST, (rb + BFA_IOC1_STATE_REG));
	} else {
		writel(BFI_IOC_INITING, (rb + BFA_IOC0_STATE_REG));
		writel(BFI_IOC_INITING, (rb + BFA_IOC1_STATE_REG));
	}

	bfa_ioc_msgflush(ioc);
	bfa_ioc_download_fw(ioc, boot_type, boot_param);

	/**
	 * Enable interrupts just before starting LPU
	 */
	ioc->cbfn->reset_cbfn(ioc->bfa);
	bfa_ioc_lpu_start(ioc);
}

/**
 * Enable/disable IOC failure auto recovery.
 */
void
bfa_nw_ioc_auto_recover(bool auto_recover)
{
	bfa_nw_auto_recover = auto_recover;
}

static void
bfa_ioc_msgget(struct bfa_ioc *ioc, void *mbmsg)
{
	u32	*msgp = mbmsg;
	u32	r32;
	int		i;

	/**
	 * read the MBOX msg
	 */
	for (i = 0; i < (sizeof(union bfi_ioc_i2h_msg_u) / sizeof(u32));
	     i++) {
		r32 = readl(ioc->ioc_regs.lpu_mbox +
				   i * sizeof(u32));
		msgp[i] = htonl(r32);
	}

	/**
	 * turn off mailbox interrupt by clearing mailbox status
	 */
	writel(1, ioc->ioc_regs.lpu_mbox_cmd);
	readl(ioc->ioc_regs.lpu_mbox_cmd);
}

static void
bfa_ioc_isr(struct bfa_ioc *ioc, struct bfi_mbmsg *m)
{
	union bfi_ioc_i2h_msg_u	*msg;
	struct bfa_iocpf *iocpf = &ioc->iocpf;

	msg = (union bfi_ioc_i2h_msg_u *) m;

	bfa_ioc_stats(ioc, ioc_isrs);

	switch (msg->mh.msg_id) {
	case BFI_IOC_I2H_HBEAT:
		break;

	case BFI_IOC_I2H_READY_EVENT:
		bfa_fsm_send_event(iocpf, IOCPF_E_FWREADY);
		break;

	case BFI_IOC_I2H_ENABLE_REPLY:
		bfa_fsm_send_event(iocpf, IOCPF_E_FWRSP_ENABLE);
		break;

	case BFI_IOC_I2H_DISABLE_REPLY:
		bfa_fsm_send_event(iocpf, IOCPF_E_FWRSP_DISABLE);
		break;

	case BFI_IOC_I2H_GETATTR_REPLY:
		bfa_ioc_getattr_reply(ioc);
		break;

	default:
		BUG_ON(1);
	}
}

/**
 * IOC attach time initialization and setup.
 *
 * @param[in]	ioc	memory for IOC
 * @param[in]	bfa	driver instance structure
 */
void
bfa_nw_ioc_attach(struct bfa_ioc *ioc, void *bfa, struct bfa_ioc_cbfn *cbfn)
{
	ioc->bfa	= bfa;
	ioc->cbfn	= cbfn;
	ioc->fcmode	= false;
	ioc->pllinit	= false;
	ioc->dbg_fwsave_once = true;
	ioc->iocpf.ioc  = ioc;

	bfa_ioc_mbox_attach(ioc);
	INIT_LIST_HEAD(&ioc->hb_notify_q);

	bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
	bfa_fsm_send_event(ioc, IOC_E_RESET);
}

/**
 * Driver detach time IOC cleanup.
 */
void
bfa_nw_ioc_detach(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(ioc, IOC_E_DETACH);
}

/**
 * Setup IOC PCI properties.
 *
 * @param[in]	pcidev	PCI device information for this IOC
 */
void
bfa_nw_ioc_pci_init(struct bfa_ioc *ioc, struct bfa_pcidev *pcidev,
		 enum bfi_mclass mc)
{
	ioc->ioc_mc	= mc;
	ioc->pcidev	= *pcidev;
	ioc->ctdev	= bfa_asic_id_ct(ioc->pcidev.device_id);
	ioc->cna	= ioc->ctdev && !ioc->fcmode;

	bfa_nw_ioc_set_ct_hwif(ioc);

	bfa_ioc_map_port(ioc);
	bfa_ioc_reg_init(ioc);
}

/**
 * Initialize IOC dma memory
 *
 * @param[in]	dm_kva	kernel virtual address of IOC dma memory
 * @param[in]	dm_pa	physical address of IOC dma memory
 */
void
bfa_nw_ioc_mem_claim(struct bfa_ioc *ioc,  u8 *dm_kva, u64 dm_pa)
{
	/**
	 * dma memory for firmware attribute
	 */
	ioc->attr_dma.kva = dm_kva;
	ioc->attr_dma.pa = dm_pa;
	ioc->attr = (struct bfi_ioc_attr *) dm_kva;
}

/**
 * Return size of dma memory required.
 */
u32
bfa_nw_ioc_meminfo(void)
{
	return roundup(sizeof(struct bfi_ioc_attr), BFA_DMA_ALIGN_SZ);
}

void
bfa_nw_ioc_enable(struct bfa_ioc *ioc)
{
	bfa_ioc_stats(ioc, ioc_enables);
	ioc->dbg_fwsave_once = true;

	bfa_fsm_send_event(ioc, IOC_E_ENABLE);
}

void
bfa_nw_ioc_disable(struct bfa_ioc *ioc)
{
	bfa_ioc_stats(ioc, ioc_disables);
	bfa_fsm_send_event(ioc, IOC_E_DISABLE);
}

static u32
bfa_ioc_smem_pgnum(struct bfa_ioc *ioc, u32 fmaddr)
{
	return PSS_SMEM_PGNUM(ioc->ioc_regs.smem_pg0, fmaddr);
}

static u32
bfa_ioc_smem_pgoff(struct bfa_ioc *ioc, u32 fmaddr)
{
	return PSS_SMEM_PGOFF(fmaddr);
}

/**
 * Register mailbox message handler function, to be called by common modules
 */
void
bfa_nw_ioc_mbox_regisr(struct bfa_ioc *ioc, enum bfi_mclass mc,
		    bfa_ioc_mbox_mcfunc_t cbfn, void *cbarg)
{
	struct bfa_ioc_mbox_mod *mod = &ioc->mbox_mod;

	mod->mbhdlr[mc].cbfn	= cbfn;
	mod->mbhdlr[mc].cbarg = cbarg;
}

/**
 * Queue a mailbox command request to firmware. Waits if mailbox is busy.
 * Responsibility of caller to serialize
 *
 * @param[in]	ioc	IOC instance
 * @param[i]	cmd	Mailbox command
 */
void
bfa_nw_ioc_mbox_queue(struct bfa_ioc *ioc, struct bfa_mbox_cmd *cmd)
{
	struct bfa_ioc_mbox_mod *mod = &ioc->mbox_mod;
	u32			stat;

	/**
	 * If a previous command is pending, queue new command
	 */
	if (!list_empty(&mod->cmd_q)) {
		list_add_tail(&cmd->qe, &mod->cmd_q);
		return;
	}

	/**
	 * If mailbox is busy, queue command for poll timer
	 */
	stat = readl(ioc->ioc_regs.hfn_mbox_cmd);
	if (stat) {
		list_add_tail(&cmd->qe, &mod->cmd_q);
		return;
	}

	/**
	 * mailbox is free -- queue command to firmware
	 */
	bfa_ioc_mbox_send(ioc, cmd->msg, sizeof(cmd->msg));
}

/**
 * Handle mailbox interrupts
 */
void
bfa_nw_ioc_mbox_isr(struct bfa_ioc *ioc)
{
	struct bfa_ioc_mbox_mod *mod = &ioc->mbox_mod;
	struct bfi_mbmsg m;
	int				mc;

	bfa_ioc_msgget(ioc, &m);

	/**
	 * Treat IOC message class as special.
	 */
	mc = m.mh.msg_class;
	if (mc == BFI_MC_IOC) {
		bfa_ioc_isr(ioc, &m);
		return;
	}

	if ((mc >= BFI_MC_MAX) || (mod->mbhdlr[mc].cbfn == NULL))
		return;

	mod->mbhdlr[mc].cbfn(mod->mbhdlr[mc].cbarg, &m);
}

void
bfa_nw_ioc_error_isr(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(ioc, IOC_E_HWERROR);
}

/**
 * Add to IOC heartbeat failure notification queue. To be used by common
 * modules such as cee, port, diag.
 */
void
bfa_nw_ioc_hbfail_register(struct bfa_ioc *ioc,
			struct bfa_ioc_hbfail_notify *notify)
{
	list_add_tail(&notify->qe, &ioc->hb_notify_q);
}

#define BFA_MFG_NAME "Brocade"
static void
bfa_ioc_get_adapter_attr(struct bfa_ioc *ioc,
			 struct bfa_adapter_attr *ad_attr)
{
	struct bfi_ioc_attr *ioc_attr;

	ioc_attr = ioc->attr;

	bfa_ioc_get_adapter_serial_num(ioc, ad_attr->serial_num);
	bfa_ioc_get_adapter_fw_ver(ioc, ad_attr->fw_ver);
	bfa_ioc_get_adapter_optrom_ver(ioc, ad_attr->optrom_ver);
	bfa_ioc_get_adapter_manufacturer(ioc, ad_attr->manufacturer);
	memcpy(&ad_attr->vpd, &ioc_attr->vpd,
		      sizeof(struct bfa_mfg_vpd));

	ad_attr->nports = bfa_ioc_get_nports(ioc);
	ad_attr->max_speed = bfa_ioc_speed_sup(ioc);

	bfa_ioc_get_adapter_model(ioc, ad_attr->model);
	/* For now, model descr uses same model string */
	bfa_ioc_get_adapter_model(ioc, ad_attr->model_descr);

	ad_attr->card_type = ioc_attr->card_type;
	ad_attr->is_mezz = bfa_mfg_is_mezz(ioc_attr->card_type);

	if (BFI_ADAPTER_IS_SPECIAL(ioc_attr->adapter_prop))
		ad_attr->prototype = 1;
	else
		ad_attr->prototype = 0;

	ad_attr->pwwn = bfa_ioc_get_pwwn(ioc);
	ad_attr->mac  = bfa_nw_ioc_get_mac(ioc);

	ad_attr->pcie_gen = ioc_attr->pcie_gen;
	ad_attr->pcie_lanes = ioc_attr->pcie_lanes;
	ad_attr->pcie_lanes_orig = ioc_attr->pcie_lanes_orig;
	ad_attr->asic_rev = ioc_attr->asic_rev;

	bfa_ioc_get_pci_chip_rev(ioc, ad_attr->hw_ver);

	ad_attr->cna_capable = ioc->cna;
	ad_attr->trunk_capable = (ad_attr->nports > 1) && !ioc->cna;
}

static enum bfa_ioc_type
bfa_ioc_get_type(struct bfa_ioc *ioc)
{
	if (!ioc->ctdev || ioc->fcmode)
		return BFA_IOC_TYPE_FC;
	else if (ioc->ioc_mc == BFI_MC_IOCFC)
		return BFA_IOC_TYPE_FCoE;
	else if (ioc->ioc_mc == BFI_MC_LL)
		return BFA_IOC_TYPE_LL;
	else {
		BUG_ON(!(ioc->ioc_mc == BFI_MC_LL));
		return BFA_IOC_TYPE_LL;
	}
}

static void
bfa_ioc_get_adapter_serial_num(struct bfa_ioc *ioc, char *serial_num)
{
	memset(serial_num, 0, BFA_ADAPTER_SERIAL_NUM_LEN);
	memcpy(serial_num,
			(void *)ioc->attr->brcd_serialnum,
			BFA_ADAPTER_SERIAL_NUM_LEN);
}

static void
bfa_ioc_get_adapter_fw_ver(struct bfa_ioc *ioc, char *fw_ver)
{
	memset(fw_ver, 0, BFA_VERSION_LEN);
	memcpy(fw_ver, ioc->attr->fw_version, BFA_VERSION_LEN);
}

static void
bfa_ioc_get_pci_chip_rev(struct bfa_ioc *ioc, char *chip_rev)
{
	BUG_ON(!(chip_rev));

	memset(chip_rev, 0, BFA_IOC_CHIP_REV_LEN);

	chip_rev[0] = 'R';
	chip_rev[1] = 'e';
	chip_rev[2] = 'v';
	chip_rev[3] = '-';
	chip_rev[4] = ioc->attr->asic_rev;
	chip_rev[5] = '\0';
}

static void
bfa_ioc_get_adapter_optrom_ver(struct bfa_ioc *ioc, char *optrom_ver)
{
	memset(optrom_ver, 0, BFA_VERSION_LEN);
	memcpy(optrom_ver, ioc->attr->optrom_version,
		      BFA_VERSION_LEN);
}

static void
bfa_ioc_get_adapter_manufacturer(struct bfa_ioc *ioc, char *manufacturer)
{
	memset(manufacturer, 0, BFA_ADAPTER_MFG_NAME_LEN);
	memcpy(manufacturer, BFA_MFG_NAME, BFA_ADAPTER_MFG_NAME_LEN);
}

static void
bfa_ioc_get_adapter_model(struct bfa_ioc *ioc, char *model)
{
	struct bfi_ioc_attr *ioc_attr;

	BUG_ON(!(model));
	memset(model, 0, BFA_ADAPTER_MODEL_NAME_LEN);

	ioc_attr = ioc->attr;

	/**
	 * model name
	 */
	snprintf(model, BFA_ADAPTER_MODEL_NAME_LEN, "%s-%u",
		BFA_MFG_NAME, ioc_attr->card_type);
}

static enum bfa_ioc_state
bfa_ioc_get_state(struct bfa_ioc *ioc)
{
	enum bfa_iocpf_state iocpf_st;
	enum bfa_ioc_state ioc_st = bfa_sm_to_state(ioc_sm_table, ioc->fsm);

	if (ioc_st == BFA_IOC_ENABLING ||
		ioc_st == BFA_IOC_FAIL || ioc_st == BFA_IOC_INITFAIL) {

		iocpf_st = bfa_sm_to_state(iocpf_sm_table, ioc->iocpf.fsm);

		switch (iocpf_st) {
		case BFA_IOCPF_SEMWAIT:
			ioc_st = BFA_IOC_SEMWAIT;
			break;

		case BFA_IOCPF_HWINIT:
			ioc_st = BFA_IOC_HWINIT;
			break;

		case BFA_IOCPF_FWMISMATCH:
			ioc_st = BFA_IOC_FWMISMATCH;
			break;

		case BFA_IOCPF_FAIL:
			ioc_st = BFA_IOC_FAIL;
			break;

		case BFA_IOCPF_INITFAIL:
			ioc_st = BFA_IOC_INITFAIL;
			break;

		default:
			break;
		}
	}
	return ioc_st;
}

void
bfa_nw_ioc_get_attr(struct bfa_ioc *ioc, struct bfa_ioc_attr *ioc_attr)
{
	memset((void *)ioc_attr, 0, sizeof(struct bfa_ioc_attr));

	ioc_attr->state = bfa_ioc_get_state(ioc);
	ioc_attr->port_id = ioc->port_id;

	ioc_attr->ioc_type = bfa_ioc_get_type(ioc);

	bfa_ioc_get_adapter_attr(ioc, &ioc_attr->adapter_attr);

	ioc_attr->pci_attr.device_id = ioc->pcidev.device_id;
	ioc_attr->pci_attr.pcifn = ioc->pcidev.pci_func;
	bfa_ioc_get_pci_chip_rev(ioc, ioc_attr->pci_attr.chip_rev);
}

/**
 * WWN public
 */
static u64
bfa_ioc_get_pwwn(struct bfa_ioc *ioc)
{
	return ioc->attr->pwwn;
}

mac_t
bfa_nw_ioc_get_mac(struct bfa_ioc *ioc)
{
	return ioc->attr->mac;
}

/**
 * Firmware failure detected. Start recovery actions.
 */
static void
bfa_ioc_recover(struct bfa_ioc *ioc)
{
	u16 bdf;

	bdf = (ioc->pcidev.pci_slot << 8 | ioc->pcidev.pci_func << 3 |
					ioc->pcidev.device_id);

	pr_crit("Firmware heartbeat failure at %d", bdf);
	BUG_ON(1);
}

static void
bfa_ioc_check_attr_wwns(struct bfa_ioc *ioc)
{
	if (bfa_ioc_get_type(ioc) == BFA_IOC_TYPE_LL)
		return;
}

/**
 * @dg hal_iocpf_pvt BFA IOC PF private functions
 * @{
 */

static void
bfa_iocpf_enable(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_ENABLE);
}

static void
bfa_iocpf_disable(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_DISABLE);
}

static void
bfa_iocpf_fail(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_FAIL);
}

static void
bfa_iocpf_initfail(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_INITFAIL);
}

static void
bfa_iocpf_getattrfail(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_GETATTRFAIL);
}

static void
bfa_iocpf_stop(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_STOP);
}

void
bfa_nw_iocpf_timeout(void *ioc_arg)
{
	struct bfa_ioc  *ioc = (struct bfa_ioc *) ioc_arg;

	bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_TIMEOUT);
}

void
bfa_nw_iocpf_sem_timeout(void *ioc_arg)
{
	struct bfa_ioc  *ioc = (struct bfa_ioc *) ioc_arg;

	bfa_ioc_hw_sem_get(ioc);
}
