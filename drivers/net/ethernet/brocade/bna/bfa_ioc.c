// SPDX-License-Identifier: GPL-2.0-only
/*
 * Linux network driver for QLogic BR-series Converged Network Adapter.
 */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014-2015 QLogic Corporation
 * All rights reserved
 * www.qlogic.com
 */

#include "bfa_ioc.h"
#include "bfi_reg.h"
#include "bfa_defs.h"

/* IOC local definitions */

/* Asic specific macros : see bfa_hw_cb.c and bfa_hw_ct.c for details. */

#define bfa_ioc_firmware_lock(__ioc)			\
			((__ioc)->ioc_hwif->ioc_firmware_lock(__ioc))
#define bfa_ioc_firmware_unlock(__ioc)			\
			((__ioc)->ioc_hwif->ioc_firmware_unlock(__ioc))
#define bfa_ioc_reg_init(__ioc) ((__ioc)->ioc_hwif->ioc_reg_init(__ioc))
#define bfa_ioc_map_port(__ioc) ((__ioc)->ioc_hwif->ioc_map_port(__ioc))
#define bfa_ioc_notify_fail(__ioc)			\
			((__ioc)->ioc_hwif->ioc_notify_fail(__ioc))
#define bfa_ioc_sync_start(__ioc)               \
			((__ioc)->ioc_hwif->ioc_sync_start(__ioc))
#define bfa_ioc_sync_join(__ioc)			\
			((__ioc)->ioc_hwif->ioc_sync_join(__ioc))
#define bfa_ioc_sync_leave(__ioc)			\
			((__ioc)->ioc_hwif->ioc_sync_leave(__ioc))
#define bfa_ioc_sync_ack(__ioc)				\
			((__ioc)->ioc_hwif->ioc_sync_ack(__ioc))
#define bfa_ioc_sync_complete(__ioc)			\
			((__ioc)->ioc_hwif->ioc_sync_complete(__ioc))
#define bfa_ioc_set_cur_ioc_fwstate(__ioc, __fwstate)		\
			((__ioc)->ioc_hwif->ioc_set_fwstate(__ioc, __fwstate))
#define bfa_ioc_get_cur_ioc_fwstate(__ioc)		\
			((__ioc)->ioc_hwif->ioc_get_fwstate(__ioc))
#define bfa_ioc_set_alt_ioc_fwstate(__ioc, __fwstate)		\
		((__ioc)->ioc_hwif->ioc_set_alt_fwstate(__ioc, __fwstate))

static bool bfa_nw_auto_recover = true;

/*
 * forward declarations
 */
static void bfa_ioc_hw_sem_init(struct bfa_ioc *ioc);
static void bfa_ioc_hw_sem_get(struct bfa_ioc *ioc);
static void bfa_ioc_hw_sem_get_cancel(struct bfa_ioc *ioc);
static void bfa_ioc_hwinit(struct bfa_ioc *ioc, bool force);
static void bfa_ioc_poll_fwinit(struct bfa_ioc *ioc);
static void bfa_ioc_send_enable(struct bfa_ioc *ioc);
static void bfa_ioc_send_disable(struct bfa_ioc *ioc);
static void bfa_ioc_send_getattr(struct bfa_ioc *ioc);
static void bfa_ioc_hb_monitor(struct bfa_ioc *ioc);
static void bfa_ioc_hb_stop(struct bfa_ioc *ioc);
static void bfa_ioc_reset(struct bfa_ioc *ioc, bool force);
static void bfa_ioc_mbox_poll(struct bfa_ioc *ioc);
static void bfa_ioc_mbox_flush(struct bfa_ioc *ioc);
static void bfa_ioc_recover(struct bfa_ioc *ioc);
static void bfa_ioc_event_notify(struct bfa_ioc *, enum bfa_ioc_event);
static void bfa_ioc_disable_comp(struct bfa_ioc *ioc);
static void bfa_ioc_lpu_stop(struct bfa_ioc *ioc);
static void bfa_nw_ioc_debug_save_ftrc(struct bfa_ioc *ioc);
static void bfa_ioc_fail_notify(struct bfa_ioc *ioc);
static void bfa_ioc_pf_enabled(struct bfa_ioc *ioc);
static void bfa_ioc_pf_disabled(struct bfa_ioc *ioc);
static void bfa_ioc_pf_failed(struct bfa_ioc *ioc);
static void bfa_ioc_pf_hwfailed(struct bfa_ioc *ioc);
static void bfa_ioc_pf_fwmismatch(struct bfa_ioc *ioc);
static enum bfa_status bfa_ioc_boot(struct bfa_ioc *ioc,
			enum bfi_fwboot_type boot_type, u32 boot_param);
static u32 bfa_ioc_smem_pgnum(struct bfa_ioc *ioc, u32 fmaddr);
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

/* IOC state machine definitions/declarations */
enum ioc_event {
	IOC_E_RESET		= 1,	/*!< IOC reset request		*/
	IOC_E_ENABLE		= 2,	/*!< IOC enable request		*/
	IOC_E_DISABLE		= 3,	/*!< IOC disable request	*/
	IOC_E_DETACH		= 4,	/*!< driver detach cleanup	*/
	IOC_E_ENABLED		= 5,	/*!< f/w enabled		*/
	IOC_E_FWRSP_GETATTR	= 6,	/*!< IOC get attribute response	*/
	IOC_E_DISABLED		= 7,	/*!< f/w disabled		*/
	IOC_E_PFFAILED		= 8,	/*!< failure notice by iocpf sm	*/
	IOC_E_HBFAIL		= 9,	/*!< heartbeat failure		*/
	IOC_E_HWERROR		= 10,	/*!< hardware error interrupt	*/
	IOC_E_TIMEOUT		= 11,	/*!< timeout			*/
	IOC_E_HWFAILED		= 12,	/*!< PCI mapping failure notice	*/
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
bfa_fsm_state_decl(bfa_ioc, hwfail, struct bfa_ioc, enum ioc_event);

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
	{BFA_SM(bfa_ioc_sm_hwfail), BFA_IOC_HWFAIL},
};

/*
 * Forward declareations for iocpf state machine
 */
static void bfa_iocpf_enable(struct bfa_ioc *ioc);
static void bfa_iocpf_disable(struct bfa_ioc *ioc);
static void bfa_iocpf_fail(struct bfa_ioc *ioc);
static void bfa_iocpf_initfail(struct bfa_ioc *ioc);
static void bfa_iocpf_getattrfail(struct bfa_ioc *ioc);
static void bfa_iocpf_stop(struct bfa_ioc *ioc);

/* IOCPF state machine events */
enum iocpf_event {
	IOCPF_E_ENABLE		= 1,	/*!< IOCPF enable request	*/
	IOCPF_E_DISABLE		= 2,	/*!< IOCPF disable request	*/
	IOCPF_E_STOP		= 3,	/*!< stop on driver detach	*/
	IOCPF_E_FWREADY		= 4,	/*!< f/w initialization done	*/
	IOCPF_E_FWRSP_ENABLE	= 5,	/*!< enable f/w response	*/
	IOCPF_E_FWRSP_DISABLE	= 6,	/*!< disable f/w response	*/
	IOCPF_E_FAIL		= 7,	/*!< failure notice by ioc sm	*/
	IOCPF_E_INITFAIL	= 8,	/*!< init fail notice by ioc sm	*/
	IOCPF_E_GETATTRFAIL	= 9,	/*!< init fail notice by ioc sm	*/
	IOCPF_E_SEMLOCKED	= 10,   /*!< h/w semaphore is locked	*/
	IOCPF_E_TIMEOUT		= 11,   /*!< f/w response timeout	*/
	IOCPF_E_SEM_ERROR	= 12,   /*!< h/w sem mapping error	*/
};

/* IOCPF states */
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

/* IOC State Machine */

/* Beginning state. IOC uninit state. */
static void
bfa_ioc_sm_uninit_entry(struct bfa_ioc *ioc)
{
}

/* IOC is in uninit state. */
static void
bfa_ioc_sm_uninit(struct bfa_ioc *ioc, enum ioc_event event)
{
	switch (event) {
	case IOC_E_RESET:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_reset);
		break;

	default:
		bfa_sm_fault(event);
	}
}

/* Reset entry actions -- initialize state machine */
static void
bfa_ioc_sm_reset_entry(struct bfa_ioc *ioc)
{
	bfa_fsm_set_state(&ioc->iocpf, bfa_iocpf_sm_reset);
}

/* IOC is in reset state. */
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
		bfa_sm_fault(event);
	}
}

static void
bfa_ioc_sm_enabling_entry(struct bfa_ioc *ioc)
{
	bfa_iocpf_enable(ioc);
}

/* Host IOC function is being enabled, awaiting response from firmware.
 * Semaphore is acquired.
 */
static void
bfa_ioc_sm_enabling(struct bfa_ioc *ioc, enum ioc_event event)
{
	switch (event) {
	case IOC_E_ENABLED:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_getattr);
		break;

	case IOC_E_PFFAILED:
		/* !!! fall through !!! */
	case IOC_E_HWERROR:
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_fail);
		if (event != IOC_E_PFFAILED)
			bfa_iocpf_initfail(ioc);
		break;

	case IOC_E_HWFAILED:
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_hwfail);
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
		bfa_sm_fault(event);
	}
}

/* Semaphore should be acquired for version check. */
static void
bfa_ioc_sm_getattr_entry(struct bfa_ioc *ioc)
{
	mod_timer(&ioc->ioc_timer, jiffies +
		msecs_to_jiffies(BFA_IOC_TOV));
	bfa_ioc_send_getattr(ioc);
}

/* IOC configuration in progress. Timer is active. */
static void
bfa_ioc_sm_getattr(struct bfa_ioc *ioc, enum ioc_event event)
{
	switch (event) {
	case IOC_E_FWRSP_GETATTR:
		del_timer(&ioc->ioc_timer);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_op);
		break;

	case IOC_E_PFFAILED:
	case IOC_E_HWERROR:
		del_timer(&ioc->ioc_timer);
		/* fall through */
	case IOC_E_TIMEOUT:
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_fail);
		if (event != IOC_E_PFFAILED)
			bfa_iocpf_getattrfail(ioc);
		break;

	case IOC_E_DISABLE:
		del_timer(&ioc->ioc_timer);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_disabling);
		break;

	case IOC_E_ENABLE:
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bfa_ioc_sm_op_entry(struct bfa_ioc *ioc)
{
	ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_OK);
	bfa_ioc_event_notify(ioc, BFA_IOC_E_ENABLED);
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

	case IOC_E_PFFAILED:
	case IOC_E_HWERROR:
		bfa_ioc_hb_stop(ioc);
		/* !!! fall through !!! */
	case IOC_E_HBFAIL:
		if (ioc->iocpf.auto_recover)
			bfa_fsm_set_state(ioc, bfa_ioc_sm_fail_retry);
		else
			bfa_fsm_set_state(ioc, bfa_ioc_sm_fail);

		bfa_ioc_fail_notify(ioc);

		if (event != IOC_E_PFFAILED)
			bfa_iocpf_fail(ioc);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bfa_ioc_sm_disabling_entry(struct bfa_ioc *ioc)
{
	bfa_iocpf_disable(ioc);
}

/* IOC is being disabled */
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

	case IOC_E_HWFAILED:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_hwfail);
		bfa_ioc_disable_comp(ioc);
		break;

	default:
		bfa_sm_fault(event);
	}
}

/* IOC disable completion entry. */
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
		bfa_sm_fault(event);
	}
}

static void
bfa_ioc_sm_fail_retry_entry(struct bfa_ioc *ioc)
{
}

/* Hardware initialization retry. */
static void
bfa_ioc_sm_fail_retry(struct bfa_ioc *ioc, enum ioc_event event)
{
	switch (event) {
	case IOC_E_ENABLED:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_getattr);
		break;

	case IOC_E_PFFAILED:
	case IOC_E_HWERROR:
		/**
		 * Initialization retry failed.
		 */
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_fail);
		if (event != IOC_E_PFFAILED)
			bfa_iocpf_initfail(ioc);
		break;

	case IOC_E_HWFAILED:
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		bfa_fsm_set_state(ioc, bfa_ioc_sm_hwfail);
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
		bfa_sm_fault(event);
	}
}

static void
bfa_ioc_sm_fail_entry(struct bfa_ioc *ioc)
{
}

/* IOC failure. */
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
		bfa_sm_fault(event);
	}
}

static void
bfa_ioc_sm_hwfail_entry(struct bfa_ioc *ioc)
{
}

/* IOC failure. */
static void
bfa_ioc_sm_hwfail(struct bfa_ioc *ioc, enum ioc_event event)
{
	switch (event) {

	case IOC_E_ENABLE:
		ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
		break;

	case IOC_E_DISABLE:
		ioc->cbfn->disable_cbfn(ioc->bfa);
		break;

	case IOC_E_DETACH:
		bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
		break;

	default:
		bfa_sm_fault(event);
	}
}

/* IOCPF State Machine */

/* Reset entry actions -- initialize state machine */
static void
bfa_iocpf_sm_reset_entry(struct bfa_iocpf *iocpf)
{
	iocpf->fw_mismatch_notified = false;
	iocpf->auto_recover = bfa_nw_auto_recover;
}

/* Beginning state. IOC is in reset state. */
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
		bfa_sm_fault(event);
	}
}

/* Semaphore should be acquired for version check. */
static void
bfa_iocpf_sm_fwcheck_entry(struct bfa_iocpf *iocpf)
{
	bfa_ioc_hw_sem_init(iocpf->ioc);
	bfa_ioc_hw_sem_get(iocpf->ioc);
}

/* Awaiting h/w semaphore to continue with version check. */
static void
bfa_iocpf_sm_fwcheck(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_SEMLOCKED:
		if (bfa_ioc_firmware_lock(ioc)) {
			if (bfa_ioc_sync_start(ioc)) {
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

	case IOCPF_E_SEM_ERROR:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail);
		bfa_ioc_pf_hwfailed(ioc);
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
		bfa_sm_fault(event);
	}
}

/* Notify enable completion callback */
static void
bfa_iocpf_sm_mismatch_entry(struct bfa_iocpf *iocpf)
{
	/* Call only the first time sm enters fwmismatch state. */
	if (!iocpf->fw_mismatch_notified)
		bfa_ioc_pf_fwmismatch(iocpf->ioc);

	iocpf->fw_mismatch_notified = true;
	mod_timer(&(iocpf->ioc)->iocpf_timer, jiffies +
		msecs_to_jiffies(BFA_IOC_TOV));
}

/* Awaiting firmware version match. */
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
		bfa_sm_fault(event);
	}
}

/* Request for semaphore. */
static void
bfa_iocpf_sm_semwait_entry(struct bfa_iocpf *iocpf)
{
	bfa_ioc_hw_sem_get(iocpf->ioc);
}

/* Awaiting semaphore for h/w initialzation. */
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

	case IOCPF_E_SEM_ERROR:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail);
		bfa_ioc_pf_hwfailed(ioc);
		break;

	case IOCPF_E_DISABLE:
		bfa_ioc_hw_sem_get_cancel(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling_sync);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bfa_iocpf_sm_hwinit_entry(struct bfa_iocpf *iocpf)
{
	iocpf->poll_time = 0;
	bfa_ioc_reset(iocpf->ioc, false);
}

/* Hardware is being initialized. Interrupts are enabled.
 * Holding hardware semaphore lock.
 */
static void
bfa_iocpf_sm_hwinit(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_FWREADY:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_enabling);
		break;

	case IOCPF_E_TIMEOUT:
		bfa_nw_ioc_hw_sem_release(ioc);
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
		bfa_sm_fault(event);
	}
}

static void
bfa_iocpf_sm_enabling_entry(struct bfa_iocpf *iocpf)
{
	mod_timer(&(iocpf->ioc)->iocpf_timer, jiffies +
		msecs_to_jiffies(BFA_IOC_TOV));
	/**
	 * Enable Interrupts before sending fw IOC ENABLE cmd.
	 */
	iocpf->ioc->cbfn->reset_cbfn(iocpf->ioc->bfa);
	bfa_ioc_send_enable(iocpf->ioc);
}

/* Host IOC function is being enabled, awaiting response from firmware.
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
		/* fall through */

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

	default:
		bfa_sm_fault(event);
	}
}

static void
bfa_iocpf_sm_ready_entry(struct bfa_iocpf *iocpf)
{
	bfa_ioc_pf_enabled(iocpf->ioc);
}

static void
bfa_iocpf_sm_ready(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
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

	default:
		bfa_sm_fault(event);
	}
}

static void
bfa_iocpf_sm_disabling_entry(struct bfa_iocpf *iocpf)
{
	mod_timer(&(iocpf->ioc)->iocpf_timer, jiffies +
		msecs_to_jiffies(BFA_IOC_TOV));
	bfa_ioc_send_disable(iocpf->ioc);
}

/* IOC is being disabled */
static void
bfa_iocpf_sm_disabling(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_FWRSP_DISABLE:
		del_timer(&ioc->iocpf_timer);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling_sync);
		break;

	case IOCPF_E_FAIL:
		del_timer(&ioc->iocpf_timer);
		/* fall through*/

	case IOCPF_E_TIMEOUT:
		bfa_ioc_set_cur_ioc_fwstate(ioc, BFI_IOC_FAIL);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling_sync);
		break;

	case IOCPF_E_FWRSP_ENABLE:
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bfa_iocpf_sm_disabling_sync_entry(struct bfa_iocpf *iocpf)
{
	bfa_ioc_hw_sem_get(iocpf->ioc);
}

/* IOC hb ack request is being removed. */
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

	case IOCPF_E_SEM_ERROR:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail);
		bfa_ioc_pf_hwfailed(ioc);
		break;

	case IOCPF_E_FAIL:
		break;

	default:
		bfa_sm_fault(event);
	}
}

/* IOC disable completion entry. */
static void
bfa_iocpf_sm_disabled_entry(struct bfa_iocpf *iocpf)
{
	bfa_ioc_mbox_flush(iocpf->ioc);
	bfa_ioc_pf_disabled(iocpf->ioc);
}

static void
bfa_iocpf_sm_disabled(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_ENABLE:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_semwait);
		break;

	case IOCPF_E_STOP:
		bfa_ioc_firmware_unlock(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_reset);
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bfa_iocpf_sm_initfail_sync_entry(struct bfa_iocpf *iocpf)
{
	bfa_nw_ioc_debug_save_ftrc(iocpf->ioc);
	bfa_ioc_hw_sem_get(iocpf->ioc);
}

/* Hardware initialization failed. */
static void
bfa_iocpf_sm_initfail_sync(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_SEMLOCKED:
		bfa_ioc_notify_fail(ioc);
		bfa_ioc_sync_leave(ioc);
		bfa_ioc_set_cur_ioc_fwstate(ioc, BFI_IOC_FAIL);
		bfa_nw_ioc_hw_sem_release(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_initfail);
		break;

	case IOCPF_E_SEM_ERROR:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail);
		bfa_ioc_pf_hwfailed(ioc);
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
		bfa_sm_fault(event);
	}
}

static void
bfa_iocpf_sm_initfail_entry(struct bfa_iocpf *iocpf)
{
}

/* Hardware initialization failed. */
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
		bfa_sm_fault(event);
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
	bfa_ioc_mbox_flush(iocpf->ioc);
	bfa_ioc_hw_sem_get(iocpf->ioc);
}

/* IOC is in failed state. */
static void
bfa_iocpf_sm_fail_sync(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	struct bfa_ioc *ioc = iocpf->ioc;

	switch (event) {
	case IOCPF_E_SEMLOCKED:
		bfa_ioc_sync_ack(ioc);
		bfa_ioc_notify_fail(ioc);
		if (!iocpf->auto_recover) {
			bfa_ioc_sync_leave(ioc);
			bfa_ioc_set_cur_ioc_fwstate(ioc, BFI_IOC_FAIL);
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

	case IOCPF_E_SEM_ERROR:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_fail);
		bfa_ioc_pf_hwfailed(ioc);
		break;

	case IOCPF_E_DISABLE:
		bfa_ioc_hw_sem_get_cancel(ioc);
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabling_sync);
		break;

	case IOCPF_E_FAIL:
		break;

	default:
		bfa_sm_fault(event);
	}
}

static void
bfa_iocpf_sm_fail_entry(struct bfa_iocpf *iocpf)
{
}

/* IOC is in failed state. */
static void
bfa_iocpf_sm_fail(struct bfa_iocpf *iocpf, enum iocpf_event event)
{
	switch (event) {
	case IOCPF_E_DISABLE:
		bfa_fsm_set_state(iocpf, bfa_iocpf_sm_disabled);
		break;

	default:
		bfa_sm_fault(event);
	}
}

/* BFA IOC private functions */

/* Notify common modules registered for notification. */
static void
bfa_ioc_event_notify(struct bfa_ioc *ioc, enum bfa_ioc_event event)
{
	struct bfa_ioc_notify *notify;

	list_for_each_entry(notify, &ioc->notify_q, qe)
		notify->cbfn(notify->cbarg, event);
}

static void
bfa_ioc_disable_comp(struct bfa_ioc *ioc)
{
	ioc->cbfn->disable_cbfn(ioc->bfa);
	bfa_ioc_event_notify(ioc, BFA_IOC_E_DISABLED);
}

bool
bfa_nw_ioc_sem_get(void __iomem *sem_reg)
{
	u32 r32;
	int cnt = 0;
#define BFA_SEM_SPINCNT	3000

	r32 = readl(sem_reg);

	while ((r32 & 1) && (cnt < BFA_SEM_SPINCNT)) {
		cnt++;
		udelay(2);
		r32 = readl(sem_reg);
	}

	if (!(r32 & 1))
		return true;

	return false;
}

void
bfa_nw_ioc_sem_release(void __iomem *sem_reg)
{
	readl(sem_reg);
	writel(1, sem_reg);
}

/* Clear fwver hdr */
static void
bfa_ioc_fwver_clear(struct bfa_ioc *ioc)
{
	u32 pgnum, pgoff, loff = 0;
	int i;

	pgnum = PSS_SMEM_PGNUM(ioc->ioc_regs.smem_pg0, loff);
	pgoff = PSS_SMEM_PGOFF(loff);
	writel(pgnum, ioc->ioc_regs.host_page_num_fn);

	for (i = 0; i < (sizeof(struct bfi_ioc_image_hdr) / sizeof(u32)); i++) {
		writel(0, ioc->ioc_regs.smem_page_start + loff);
		loff += sizeof(u32);
	}
}


static void
bfa_ioc_hw_sem_init(struct bfa_ioc *ioc)
{
	struct bfi_ioc_image_hdr fwhdr;
	u32 fwstate, r32;

	/* Spin on init semaphore to serialize. */
	r32 = readl(ioc->ioc_regs.ioc_init_sem_reg);
	while (r32 & 0x1) {
		udelay(20);
		r32 = readl(ioc->ioc_regs.ioc_init_sem_reg);
	}

	fwstate = bfa_ioc_get_cur_ioc_fwstate(ioc);
	if (fwstate == BFI_IOC_UNINIT) {
		writel(1, ioc->ioc_regs.ioc_init_sem_reg);
		return;
	}

	bfa_nw_ioc_fwver_get(ioc, &fwhdr);

	if (swab32(fwhdr.exec) == BFI_FWBOOT_TYPE_NORMAL) {
		writel(1, ioc->ioc_regs.ioc_init_sem_reg);
		return;
	}

	bfa_ioc_fwver_clear(ioc);
	bfa_ioc_set_cur_ioc_fwstate(ioc, BFI_IOC_UNINIT);
	bfa_ioc_set_alt_ioc_fwstate(ioc, BFI_IOC_UNINIT);

	/*
	 * Try to lock and then unlock the semaphore.
	 */
	readl(ioc->ioc_regs.ioc_sem_reg);
	writel(1, ioc->ioc_regs.ioc_sem_reg);

	/* Unlock init semaphore */
	writel(1, ioc->ioc_regs.ioc_init_sem_reg);
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
	if (r32 == ~0) {
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_SEM_ERROR);
		return;
	}
	if (!(r32 & 1)) {
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

/* Initialize LPU local memory (aka secondary memory / SRAM) */
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

/* Get driver and firmware versions. */
void
bfa_nw_ioc_fwver_get(struct bfa_ioc *ioc, struct bfi_ioc_image_hdr *fwhdr)
{
	u32	pgnum;
	u32	loff = 0;
	int		i;
	u32	*fwsig = (u32 *) fwhdr;

	pgnum = bfa_ioc_smem_pgnum(ioc, loff);
	writel(pgnum, ioc->ioc_regs.host_page_num_fn);

	for (i = 0; i < (sizeof(struct bfi_ioc_image_hdr) / sizeof(u32));
	     i++) {
		fwsig[i] =
			swab32(readl(loff + ioc->ioc_regs.smem_page_start));
		loff += sizeof(u32);
	}
}

static bool
bfa_ioc_fwver_md5_check(struct bfi_ioc_image_hdr *fwhdr_1,
			struct bfi_ioc_image_hdr *fwhdr_2)
{
	int i;

	for (i = 0; i < BFI_IOC_MD5SUM_SZ; i++) {
		if (fwhdr_1->md5sum[i] != fwhdr_2->md5sum[i])
			return false;
	}

	return true;
}

/* Returns TRUE if major minor and maintenance are same.
 * If patch version are same, check for MD5 Checksum to be same.
 */
static bool
bfa_ioc_fw_ver_compatible(struct bfi_ioc_image_hdr *drv_fwhdr,
			  struct bfi_ioc_image_hdr *fwhdr_to_cmp)
{
	if (drv_fwhdr->signature != fwhdr_to_cmp->signature)
		return false;
	if (drv_fwhdr->fwver.major != fwhdr_to_cmp->fwver.major)
		return false;
	if (drv_fwhdr->fwver.minor != fwhdr_to_cmp->fwver.minor)
		return false;
	if (drv_fwhdr->fwver.maint != fwhdr_to_cmp->fwver.maint)
		return false;
	if (drv_fwhdr->fwver.patch == fwhdr_to_cmp->fwver.patch &&
	    drv_fwhdr->fwver.phase == fwhdr_to_cmp->fwver.phase &&
	    drv_fwhdr->fwver.build == fwhdr_to_cmp->fwver.build)
		return bfa_ioc_fwver_md5_check(drv_fwhdr, fwhdr_to_cmp);

	return true;
}

static bool
bfa_ioc_flash_fwver_valid(struct bfi_ioc_image_hdr *flash_fwhdr)
{
	if (flash_fwhdr->fwver.major == 0 || flash_fwhdr->fwver.major == 0xFF)
		return false;

	return true;
}

static bool
fwhdr_is_ga(struct bfi_ioc_image_hdr *fwhdr)
{
	if (fwhdr->fwver.phase == 0 &&
	    fwhdr->fwver.build == 0)
		return false;

	return true;
}

/* Returns TRUE if both are compatible and patch of fwhdr_to_cmp is better. */
static enum bfi_ioc_img_ver_cmp
bfa_ioc_fw_ver_patch_cmp(struct bfi_ioc_image_hdr *base_fwhdr,
			 struct bfi_ioc_image_hdr *fwhdr_to_cmp)
{
	if (!bfa_ioc_fw_ver_compatible(base_fwhdr, fwhdr_to_cmp))
		return BFI_IOC_IMG_VER_INCOMP;

	if (fwhdr_to_cmp->fwver.patch > base_fwhdr->fwver.patch)
		return BFI_IOC_IMG_VER_BETTER;
	else if (fwhdr_to_cmp->fwver.patch < base_fwhdr->fwver.patch)
		return BFI_IOC_IMG_VER_OLD;

	/* GA takes priority over internal builds of the same patch stream.
	 * At this point major minor maint and patch numbers are same.
	 */
	if (fwhdr_is_ga(base_fwhdr))
		if (fwhdr_is_ga(fwhdr_to_cmp))
			return BFI_IOC_IMG_VER_SAME;
		else
			return BFI_IOC_IMG_VER_OLD;
	else
		if (fwhdr_is_ga(fwhdr_to_cmp))
			return BFI_IOC_IMG_VER_BETTER;

	if (fwhdr_to_cmp->fwver.phase > base_fwhdr->fwver.phase)
		return BFI_IOC_IMG_VER_BETTER;
	else if (fwhdr_to_cmp->fwver.phase < base_fwhdr->fwver.phase)
		return BFI_IOC_IMG_VER_OLD;

	if (fwhdr_to_cmp->fwver.build > base_fwhdr->fwver.build)
		return BFI_IOC_IMG_VER_BETTER;
	else if (fwhdr_to_cmp->fwver.build < base_fwhdr->fwver.build)
		return BFI_IOC_IMG_VER_OLD;

	/* All Version Numbers are equal.
	 * Md5 check to be done as a part of compatibility check.
	 */
	return BFI_IOC_IMG_VER_SAME;
}

/* register definitions */
#define FLI_CMD_REG			0x0001d000
#define FLI_WRDATA_REG			0x0001d00c
#define FLI_RDDATA_REG			0x0001d010
#define FLI_ADDR_REG			0x0001d004
#define FLI_DEV_STATUS_REG		0x0001d014

#define BFA_FLASH_FIFO_SIZE		128	/* fifo size */
#define BFA_FLASH_CHECK_MAX		10000	/* max # of status check */
#define BFA_FLASH_BLOCKING_OP_MAX	1000000	/* max # of blocking op check */
#define BFA_FLASH_WIP_MASK		0x01	/* write in progress bit mask */

#define NFC_STATE_RUNNING		0x20000001
#define NFC_STATE_PAUSED		0x00004560
#define NFC_VER_VALID			0x147

enum bfa_flash_cmd {
	BFA_FLASH_FAST_READ	= 0x0b,	/* fast read */
	BFA_FLASH_WRITE_ENABLE	= 0x06,	/* write enable */
	BFA_FLASH_SECTOR_ERASE	= 0xd8,	/* sector erase */
	BFA_FLASH_WRITE		= 0x02,	/* write */
	BFA_FLASH_READ_STATUS	= 0x05,	/* read status */
};

/* hardware error definition */
enum bfa_flash_err {
	BFA_FLASH_NOT_PRESENT	= -1,	/*!< flash not present */
	BFA_FLASH_UNINIT	= -2,	/*!< flash not initialized */
	BFA_FLASH_BAD		= -3,	/*!< flash bad */
	BFA_FLASH_BUSY		= -4,	/*!< flash busy */
	BFA_FLASH_ERR_CMD_ACT	= -5,	/*!< command active never cleared */
	BFA_FLASH_ERR_FIFO_CNT	= -6,	/*!< fifo count never cleared */
	BFA_FLASH_ERR_WIP	= -7,	/*!< write-in-progress never cleared */
	BFA_FLASH_ERR_TIMEOUT	= -8,	/*!< fli timeout */
	BFA_FLASH_ERR_LEN	= -9,	/*!< invalid length */
};

/* flash command register data structure */
union bfa_flash_cmd_reg {
	struct {
#ifdef __BIG_ENDIAN
		u32	act:1;
		u32	rsv:1;
		u32	write_cnt:9;
		u32	read_cnt:9;
		u32	addr_cnt:4;
		u32	cmd:8;
#else
		u32	cmd:8;
		u32	addr_cnt:4;
		u32	read_cnt:9;
		u32	write_cnt:9;
		u32	rsv:1;
		u32	act:1;
#endif
	} r;
	u32	i;
};

/* flash device status register data structure */
union bfa_flash_dev_status_reg {
	struct {
#ifdef __BIG_ENDIAN
		u32	rsv:21;
		u32	fifo_cnt:6;
		u32	busy:1;
		u32	init_status:1;
		u32	present:1;
		u32	bad:1;
		u32	good:1;
#else
		u32	good:1;
		u32	bad:1;
		u32	present:1;
		u32	init_status:1;
		u32	busy:1;
		u32	fifo_cnt:6;
		u32	rsv:21;
#endif
	} r;
	u32	i;
};

/* flash address register data structure */
union bfa_flash_addr_reg {
	struct {
#ifdef __BIG_ENDIAN
		u32	addr:24;
		u32	dummy:8;
#else
		u32	dummy:8;
		u32	addr:24;
#endif
	} r;
	u32	i;
};

/* Flash raw private functions */
static void
bfa_flash_set_cmd(void __iomem *pci_bar, u8 wr_cnt,
		  u8 rd_cnt, u8 ad_cnt, u8 op)
{
	union bfa_flash_cmd_reg cmd;

	cmd.i = 0;
	cmd.r.act = 1;
	cmd.r.write_cnt = wr_cnt;
	cmd.r.read_cnt = rd_cnt;
	cmd.r.addr_cnt = ad_cnt;
	cmd.r.cmd = op;
	writel(cmd.i, (pci_bar + FLI_CMD_REG));
}

static void
bfa_flash_set_addr(void __iomem *pci_bar, u32 address)
{
	union bfa_flash_addr_reg addr;

	addr.r.addr = address & 0x00ffffff;
	addr.r.dummy = 0;
	writel(addr.i, (pci_bar + FLI_ADDR_REG));
}

static int
bfa_flash_cmd_act_check(void __iomem *pci_bar)
{
	union bfa_flash_cmd_reg cmd;

	cmd.i = readl(pci_bar + FLI_CMD_REG);

	if (cmd.r.act)
		return BFA_FLASH_ERR_CMD_ACT;

	return 0;
}

/* Flush FLI data fifo. */
static int
bfa_flash_fifo_flush(void __iomem *pci_bar)
{
	u32 i;
	u32 t;
	union bfa_flash_dev_status_reg dev_status;

	dev_status.i = readl(pci_bar + FLI_DEV_STATUS_REG);

	if (!dev_status.r.fifo_cnt)
		return 0;

	/* fifo counter in terms of words */
	for (i = 0; i < dev_status.r.fifo_cnt; i++)
		t = readl(pci_bar + FLI_RDDATA_REG);

	/* Check the device status. It may take some time. */
	for (i = 0; i < BFA_FLASH_CHECK_MAX; i++) {
		dev_status.i = readl(pci_bar + FLI_DEV_STATUS_REG);
		if (!dev_status.r.fifo_cnt)
			break;
	}

	if (dev_status.r.fifo_cnt)
		return BFA_FLASH_ERR_FIFO_CNT;

	return 0;
}

/* Read flash status. */
static int
bfa_flash_status_read(void __iomem *pci_bar)
{
	union bfa_flash_dev_status_reg	dev_status;
	int				status;
	u32			ret_status;
	int				i;

	status = bfa_flash_fifo_flush(pci_bar);
	if (status < 0)
		return status;

	bfa_flash_set_cmd(pci_bar, 0, 4, 0, BFA_FLASH_READ_STATUS);

	for (i = 0; i < BFA_FLASH_CHECK_MAX; i++) {
		status = bfa_flash_cmd_act_check(pci_bar);
		if (!status)
			break;
	}

	if (status)
		return status;

	dev_status.i = readl(pci_bar + FLI_DEV_STATUS_REG);
	if (!dev_status.r.fifo_cnt)
		return BFA_FLASH_BUSY;

	ret_status = readl(pci_bar + FLI_RDDATA_REG);
	ret_status >>= 24;

	status = bfa_flash_fifo_flush(pci_bar);
	if (status < 0)
		return status;

	return ret_status;
}

/* Start flash read operation. */
static int
bfa_flash_read_start(void __iomem *pci_bar, u32 offset, u32 len,
		     char *buf)
{
	int status;

	/* len must be mutiple of 4 and not exceeding fifo size */
	if (len == 0 || len > BFA_FLASH_FIFO_SIZE || (len & 0x03) != 0)
		return BFA_FLASH_ERR_LEN;

	/* check status */
	status = bfa_flash_status_read(pci_bar);
	if (status == BFA_FLASH_BUSY)
		status = bfa_flash_status_read(pci_bar);

	if (status < 0)
		return status;

	/* check if write-in-progress bit is cleared */
	if (status & BFA_FLASH_WIP_MASK)
		return BFA_FLASH_ERR_WIP;

	bfa_flash_set_addr(pci_bar, offset);

	bfa_flash_set_cmd(pci_bar, 0, (u8)len, 4, BFA_FLASH_FAST_READ);

	return 0;
}

/* Check flash read operation. */
static u32
bfa_flash_read_check(void __iomem *pci_bar)
{
	if (bfa_flash_cmd_act_check(pci_bar))
		return 1;

	return 0;
}

/* End flash read operation. */
static void
bfa_flash_read_end(void __iomem *pci_bar, u32 len, char *buf)
{
	u32 i;

	/* read data fifo up to 32 words */
	for (i = 0; i < len; i += 4) {
		u32 w = readl(pci_bar + FLI_RDDATA_REG);
		*((u32 *)(buf + i)) = swab32(w);
	}

	bfa_flash_fifo_flush(pci_bar);
}

/* Perform flash raw read. */

#define FLASH_BLOCKING_OP_MAX   500
#define FLASH_SEM_LOCK_REG	0x18820

static int
bfa_raw_sem_get(void __iomem *bar)
{
	int	locked;

	locked = readl(bar + FLASH_SEM_LOCK_REG);

	return !locked;
}

static enum bfa_status
bfa_flash_sem_get(void __iomem *bar)
{
	u32 n = FLASH_BLOCKING_OP_MAX;

	while (!bfa_raw_sem_get(bar)) {
		if (--n <= 0)
			return BFA_STATUS_BADFLASH;
		mdelay(10);
	}
	return BFA_STATUS_OK;
}

static void
bfa_flash_sem_put(void __iomem *bar)
{
	writel(0, (bar + FLASH_SEM_LOCK_REG));
}

static enum bfa_status
bfa_flash_raw_read(void __iomem *pci_bar, u32 offset, char *buf,
		   u32 len)
{
	u32 n;
	int status;
	u32 off, l, s, residue, fifo_sz;

	residue = len;
	off = 0;
	fifo_sz = BFA_FLASH_FIFO_SIZE;
	status = bfa_flash_sem_get(pci_bar);
	if (status != BFA_STATUS_OK)
		return status;

	while (residue) {
		s = offset + off;
		n = s / fifo_sz;
		l = (n + 1) * fifo_sz - s;
		if (l > residue)
			l = residue;

		status = bfa_flash_read_start(pci_bar, offset + off, l,
								&buf[off]);
		if (status < 0) {
			bfa_flash_sem_put(pci_bar);
			return BFA_STATUS_FAILED;
		}

		n = BFA_FLASH_BLOCKING_OP_MAX;
		while (bfa_flash_read_check(pci_bar)) {
			if (--n <= 0) {
				bfa_flash_sem_put(pci_bar);
				return BFA_STATUS_FAILED;
			}
		}

		bfa_flash_read_end(pci_bar, l, &buf[off]);

		residue -= l;
		off += l;
	}
	bfa_flash_sem_put(pci_bar);

	return BFA_STATUS_OK;
}

#define BFA_FLASH_PART_FWIMG_ADDR	0x100000 /* fw image address */

static enum bfa_status
bfa_nw_ioc_flash_img_get_chnk(struct bfa_ioc *ioc, u32 off,
			      u32 *fwimg)
{
	return bfa_flash_raw_read(ioc->pcidev.pci_bar_kva,
			BFA_FLASH_PART_FWIMG_ADDR + (off * sizeof(u32)),
			(char *)fwimg, BFI_FLASH_CHUNK_SZ);
}

static enum bfi_ioc_img_ver_cmp
bfa_ioc_flash_fwver_cmp(struct bfa_ioc *ioc,
			struct bfi_ioc_image_hdr *base_fwhdr)
{
	struct bfi_ioc_image_hdr *flash_fwhdr;
	enum bfa_status status;
	u32 fwimg[BFI_FLASH_CHUNK_SZ_WORDS];

	status = bfa_nw_ioc_flash_img_get_chnk(ioc, 0, fwimg);
	if (status != BFA_STATUS_OK)
		return BFI_IOC_IMG_VER_INCOMP;

	flash_fwhdr = (struct bfi_ioc_image_hdr *)fwimg;
	if (bfa_ioc_flash_fwver_valid(flash_fwhdr))
		return bfa_ioc_fw_ver_patch_cmp(base_fwhdr, flash_fwhdr);
	else
		return BFI_IOC_IMG_VER_INCOMP;
}

/**
 * Returns TRUE if driver is willing to work with current smem f/w version.
 */
bool
bfa_nw_ioc_fwver_cmp(struct bfa_ioc *ioc, struct bfi_ioc_image_hdr *fwhdr)
{
	struct bfi_ioc_image_hdr *drv_fwhdr;
	enum bfi_ioc_img_ver_cmp smem_flash_cmp, drv_smem_cmp;

	drv_fwhdr = (struct bfi_ioc_image_hdr *)
		bfa_cb_image_get_chunk(bfa_ioc_asic_gen(ioc), 0);

	/* If smem is incompatible or old, driver should not work with it. */
	drv_smem_cmp = bfa_ioc_fw_ver_patch_cmp(drv_fwhdr, fwhdr);
	if (drv_smem_cmp == BFI_IOC_IMG_VER_INCOMP ||
	    drv_smem_cmp == BFI_IOC_IMG_VER_OLD) {
		return false;
	}

	/* IF Flash has a better F/W than smem do not work with smem.
	 * If smem f/w == flash f/w, as smem f/w not old | incmp, work with it.
	 * If Flash is old or incomp work with smem iff smem f/w == drv f/w.
	 */
	smem_flash_cmp = bfa_ioc_flash_fwver_cmp(ioc, fwhdr);

	if (smem_flash_cmp == BFI_IOC_IMG_VER_BETTER)
		return false;
	else if (smem_flash_cmp == BFI_IOC_IMG_VER_SAME)
		return true;
	else
		return (drv_smem_cmp == BFI_IOC_IMG_VER_SAME) ?
			true : false;
}

/* Return true if current running version is valid. Firmware signature and
 * execution context (driver/bios) must match.
 */
static bool
bfa_ioc_fwver_valid(struct bfa_ioc *ioc, u32 boot_env)
{
	struct bfi_ioc_image_hdr fwhdr;

	bfa_nw_ioc_fwver_get(ioc, &fwhdr);
	if (swab32(fwhdr.bootenv) != boot_env)
		return false;

	return bfa_nw_ioc_fwver_cmp(ioc, &fwhdr);
}

/* Conditionally flush any pending message from firmware at start. */
static void
bfa_ioc_msgflush(struct bfa_ioc *ioc)
{
	u32	r32;

	r32 = readl(ioc->ioc_regs.lpu_mbox_cmd);
	if (r32)
		writel(1, ioc->ioc_regs.lpu_mbox_cmd);
}

static void
bfa_ioc_hwinit(struct bfa_ioc *ioc, bool force)
{
	enum bfi_ioc_state ioc_fwstate;
	bool fwvalid;
	u32 boot_env;

	ioc_fwstate = bfa_ioc_get_cur_ioc_fwstate(ioc);

	if (force)
		ioc_fwstate = BFI_IOC_UNINIT;

	boot_env = BFI_FWBOOT_ENV_OS;

	/**
	 * check if firmware is valid
	 */
	fwvalid = (ioc_fwstate == BFI_IOC_UNINIT) ?
		false : bfa_ioc_fwver_valid(ioc, boot_env);

	if (!fwvalid) {
		if (bfa_ioc_boot(ioc, BFI_FWBOOT_TYPE_NORMAL, boot_env) ==
								BFA_STATUS_OK)
			bfa_ioc_poll_fwinit(ioc);

		return;
	}

	/**
	 * If hardware initialization is in progress (initialized by other IOC),
	 * just wait for an initialization completion interrupt.
	 */
	if (ioc_fwstate == BFI_IOC_INITING) {
		bfa_ioc_poll_fwinit(ioc);
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
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_FWREADY);
		return;
	}

	/**
	 * Initialize the h/w for any other states.
	 */
	if (bfa_ioc_boot(ioc, BFI_FWBOOT_TYPE_NORMAL, boot_env) ==
							BFA_STATUS_OK)
		bfa_ioc_poll_fwinit(ioc);
}

void
bfa_nw_ioc_timeout(struct bfa_ioc *ioc)
{
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

	bfi_h2i_set(enable_req.mh, BFI_MC_IOC, BFI_IOC_H2I_ENABLE_REQ,
		    bfa_ioc_portid(ioc));
	enable_req.clscode = htons(ioc->clscode);
	enable_req.rsvd = htons(0);
	/* overflow in 2106 */
	enable_req.tv_sec = ntohl(ktime_get_real_seconds());
	bfa_ioc_mbox_send(ioc, &enable_req, sizeof(struct bfi_ioc_ctrl_req));
}

static void
bfa_ioc_send_disable(struct bfa_ioc *ioc)
{
	struct bfi_ioc_ctrl_req disable_req;

	bfi_h2i_set(disable_req.mh, BFI_MC_IOC, BFI_IOC_H2I_DISABLE_REQ,
		    bfa_ioc_portid(ioc));
	disable_req.clscode = htons(ioc->clscode);
	disable_req.rsvd = htons(0);
	/* overflow in 2106 */
	disable_req.tv_sec = ntohl(ktime_get_real_seconds());
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
bfa_nw_ioc_hb_check(struct bfa_ioc *ioc)
{
	u32 hb_count;

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

/* Initiate a full firmware download. */
static enum bfa_status
bfa_ioc_download_fw(struct bfa_ioc *ioc, u32 boot_type,
		    u32 boot_env)
{
	u32 *fwimg;
	u32 pgnum;
	u32 loff = 0;
	u32 chunkno = 0;
	u32 i;
	u32 asicmode;
	u32 fwimg_size;
	u32 fwimg_buf[BFI_FLASH_CHUNK_SZ_WORDS];
	enum bfa_status status;

	if (boot_env == BFI_FWBOOT_ENV_OS &&
	    boot_type == BFI_FWBOOT_TYPE_FLASH) {
		fwimg_size = BFI_FLASH_IMAGE_SZ/sizeof(u32);

		status = bfa_nw_ioc_flash_img_get_chnk(ioc,
			BFA_IOC_FLASH_CHUNK_ADDR(chunkno), fwimg_buf);
		if (status != BFA_STATUS_OK)
			return status;

		fwimg = fwimg_buf;
	} else {
		fwimg_size = bfa_cb_image_get_size(bfa_ioc_asic_gen(ioc));
		fwimg = bfa_cb_image_get_chunk(bfa_ioc_asic_gen(ioc),
					BFA_IOC_FLASH_CHUNK_ADDR(chunkno));
	}

	pgnum = bfa_ioc_smem_pgnum(ioc, loff);

	writel(pgnum, ioc->ioc_regs.host_page_num_fn);

	for (i = 0; i < fwimg_size; i++) {
		if (BFA_IOC_FLASH_CHUNK_NO(i) != chunkno) {
			chunkno = BFA_IOC_FLASH_CHUNK_NO(i);
			if (boot_env == BFI_FWBOOT_ENV_OS &&
			    boot_type == BFI_FWBOOT_TYPE_FLASH) {
				status = bfa_nw_ioc_flash_img_get_chnk(ioc,
					BFA_IOC_FLASH_CHUNK_ADDR(chunkno),
					fwimg_buf);
				if (status != BFA_STATUS_OK)
					return status;

				fwimg = fwimg_buf;
			} else {
				fwimg = bfa_cb_image_get_chunk(
					bfa_ioc_asic_gen(ioc),
					BFA_IOC_FLASH_CHUNK_ADDR(chunkno));
			}
		}

		/**
		 * write smem
		 */
		writel(swab32(fwimg[BFA_IOC_FLASH_OFFSET_IN_CHUNK(i)]),
		       ioc->ioc_regs.smem_page_start + loff);

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
	 * Set boot type, env and device mode at the end.
	*/
	if (boot_env == BFI_FWBOOT_ENV_OS &&
	    boot_type == BFI_FWBOOT_TYPE_FLASH) {
		boot_type = BFI_FWBOOT_TYPE_NORMAL;
	}
	asicmode = BFI_FWBOOT_DEVMODE(ioc->asic_gen, ioc->asic_mode,
					ioc->port0_mode, ioc->port1_mode);
	writel(asicmode, ((ioc->ioc_regs.smem_page_start)
			+ BFI_FWBOOT_DEVMODE_OFF));
	writel(boot_type, ((ioc->ioc_regs.smem_page_start)
			+ (BFI_FWBOOT_TYPE_OFF)));
	writel(boot_env, ((ioc->ioc_regs.smem_page_start)
			+ (BFI_FWBOOT_ENV_OFF)));
	return BFA_STATUS_OK;
}

static void
bfa_ioc_reset(struct bfa_ioc *ioc, bool force)
{
	bfa_ioc_hwinit(ioc, force);
}

/* BFA ioc enable reply by firmware */
static void
bfa_ioc_enable_reply(struct bfa_ioc *ioc, enum bfa_mode port_mode,
			u8 cap_bm)
{
	struct bfa_iocpf *iocpf = &ioc->iocpf;

	ioc->port_mode = ioc->port_mode_cfg = port_mode;
	ioc->ad_cap_bm = cap_bm;
	bfa_fsm_send_event(iocpf, IOCPF_E_FWRSP_ENABLE);
}

/* Update BFA configuration from firmware configuration. */
static void
bfa_ioc_getattr_reply(struct bfa_ioc *ioc)
{
	struct bfi_ioc_attr *attr = ioc->attr;

	attr->adapter_prop  = ntohl(attr->adapter_prop);
	attr->card_type     = ntohl(attr->card_type);
	attr->maxfrsize	    = ntohs(attr->maxfrsize);

	bfa_fsm_send_event(ioc, IOC_E_FWRSP_GETATTR);
}

/* Attach time initialization of mbox logic. */
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

/* Mbox poll timer -- restarts any pending mailbox requests. */
static void
bfa_ioc_mbox_poll(struct bfa_ioc *ioc)
{
	struct bfa_ioc_mbox_mod *mod = &ioc->mbox_mod;
	struct bfa_mbox_cmd *cmd;
	bfa_mbox_cmd_cbfn_t cbfn;
	void *cbarg;
	u32 stat;

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
	cmd = list_first_entry(&mod->cmd_q, struct bfa_mbox_cmd, qe);
	list_del(&cmd->qe);
	bfa_ioc_mbox_send(ioc, cmd->msg, sizeof(cmd->msg));

	/**
	 * Give a callback to the client, indicating that the command is sent
	 */
	if (cmd->cbfn) {
		cbfn = cmd->cbfn;
		cbarg = cmd->cbarg;
		cmd->cbfn = NULL;
		cbfn(cbarg);
	}
}

/* Cleanup any pending requests. */
static void
bfa_ioc_mbox_flush(struct bfa_ioc *ioc)
{
	struct bfa_ioc_mbox_mod *mod = &ioc->mbox_mod;
	struct bfa_mbox_cmd *cmd;

	while (!list_empty(&mod->cmd_q)) {
		cmd = list_first_entry(&mod->cmd_q, struct bfa_mbox_cmd, qe);
		list_del(&cmd->qe);
	}
}

/**
 * bfa_nw_ioc_smem_read - Read data from SMEM to host through PCI memmap
 *
 * @ioc:     memory for IOC
 * @tbuf:    app memory to store data from smem
 * @soff:    smem offset
 * @sz:      size of smem in bytes
 */
static int
bfa_nw_ioc_smem_read(struct bfa_ioc *ioc, void *tbuf, u32 soff, u32 sz)
{
	u32 pgnum, loff, r32;
	int i, len;
	u32 *buf = tbuf;

	pgnum = PSS_SMEM_PGNUM(ioc->ioc_regs.smem_pg0, soff);
	loff = PSS_SMEM_PGOFF(soff);

	/*
	 *  Hold semaphore to serialize pll init and fwtrc.
	*/
	if (!bfa_nw_ioc_sem_get(ioc->ioc_regs.ioc_init_sem_reg))
		return 1;

	writel(pgnum, ioc->ioc_regs.host_page_num_fn);

	len = sz/sizeof(u32);
	for (i = 0; i < len; i++) {
		r32 = swab32(readl(loff + ioc->ioc_regs.smem_page_start));
		buf[i] = be32_to_cpu(r32);
		loff += sizeof(u32);

		/**
		 * handle page offset wrap around
		 */
		loff = PSS_SMEM_PGOFF(loff);
		if (loff == 0) {
			pgnum++;
			writel(pgnum, ioc->ioc_regs.host_page_num_fn);
		}
	}

	writel(PSS_SMEM_PGNUM(ioc->ioc_regs.smem_pg0, 0),
	       ioc->ioc_regs.host_page_num_fn);

	/*
	 * release semaphore
	 */
	readl(ioc->ioc_regs.ioc_init_sem_reg);
	writel(1, ioc->ioc_regs.ioc_init_sem_reg);
	return 0;
}

/* Retrieve saved firmware trace from a prior IOC failure. */
int
bfa_nw_ioc_debug_fwtrc(struct bfa_ioc *ioc, void *trcdata, int *trclen)
{
	u32 loff = BFI_IOC_TRC_OFF + BNA_DBG_FWTRC_LEN * ioc->port_id;
	int tlen, status = 0;

	tlen = *trclen;
	if (tlen > BNA_DBG_FWTRC_LEN)
		tlen = BNA_DBG_FWTRC_LEN;

	status = bfa_nw_ioc_smem_read(ioc, trcdata, loff, tlen);
	*trclen = tlen;
	return status;
}

/* Save firmware trace if configured. */
static void
bfa_nw_ioc_debug_save_ftrc(struct bfa_ioc *ioc)
{
	int tlen;

	if (ioc->dbg_fwsave_once) {
		ioc->dbg_fwsave_once = false;
		if (ioc->dbg_fwsave_len) {
			tlen = ioc->dbg_fwsave_len;
			bfa_nw_ioc_debug_fwtrc(ioc, ioc->dbg_fwsave, &tlen);
		}
	}
}

/* Retrieve saved firmware trace from a prior IOC failure. */
int
bfa_nw_ioc_debug_fwsave(struct bfa_ioc *ioc, void *trcdata, int *trclen)
{
	int tlen;

	if (ioc->dbg_fwsave_len == 0)
		return BFA_STATUS_ENOFSAVE;

	tlen = *trclen;
	if (tlen > ioc->dbg_fwsave_len)
		tlen = ioc->dbg_fwsave_len;

	memcpy(trcdata, ioc->dbg_fwsave, tlen);
	*trclen = tlen;
	return BFA_STATUS_OK;
}

static void
bfa_ioc_fail_notify(struct bfa_ioc *ioc)
{
	/**
	 * Notify driver and common modules registered for notification.
	 */
	ioc->cbfn->hbfail_cbfn(ioc->bfa);
	bfa_ioc_event_notify(ioc, BFA_IOC_E_FAILED);
	bfa_nw_ioc_debug_save_ftrc(ioc);
}

/* IOCPF to IOC interface */
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
bfa_ioc_pf_failed(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(ioc, IOC_E_PFFAILED);
}

static void
bfa_ioc_pf_hwfailed(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(ioc, IOC_E_HWFAILED);
}

static void
bfa_ioc_pf_fwmismatch(struct bfa_ioc *ioc)
{
	/**
	 * Provide enable completion callback and AEN notification.
	 */
	ioc->cbfn->enable_cbfn(ioc->bfa, BFA_STATUS_IOC_FAILURE);
}

/* IOC public */
static enum bfa_status
bfa_ioc_pll_init(struct bfa_ioc *ioc)
{
	/*
	 *  Hold semaphore so that nobody can access the chip during init.
	 */
	bfa_nw_ioc_sem_get(ioc->ioc_regs.ioc_init_sem_reg);

	bfa_ioc_pll_init_asic(ioc);

	ioc->pllinit = true;

	/* Initialize LMEM */
	bfa_ioc_lmem_init(ioc);

	/*
	 *  release semaphore.
	 */
	bfa_nw_ioc_sem_release(ioc->ioc_regs.ioc_init_sem_reg);

	return BFA_STATUS_OK;
}

/* Interface used by diag module to do firmware boot with memory test
 * as the entry vector.
 */
static enum bfa_status
bfa_ioc_boot(struct bfa_ioc *ioc, enum bfi_fwboot_type boot_type,
		u32 boot_env)
{
	struct bfi_ioc_image_hdr *drv_fwhdr;
	enum bfa_status status;
	bfa_ioc_stats(ioc, ioc_boots);

	if (bfa_ioc_pll_init(ioc) != BFA_STATUS_OK)
		return BFA_STATUS_FAILED;
	if (boot_env == BFI_FWBOOT_ENV_OS &&
	    boot_type == BFI_FWBOOT_TYPE_NORMAL) {
		drv_fwhdr = (struct bfi_ioc_image_hdr *)
			bfa_cb_image_get_chunk(bfa_ioc_asic_gen(ioc), 0);
		/* Work with Flash iff flash f/w is better than driver f/w.
		 * Otherwise push drivers firmware.
		 */
		if (bfa_ioc_flash_fwver_cmp(ioc, drv_fwhdr) ==
			BFI_IOC_IMG_VER_BETTER)
			boot_type = BFI_FWBOOT_TYPE_FLASH;
	}

	/**
	 * Initialize IOC state of all functions on a chip reset.
	 */
	if (boot_type == BFI_FWBOOT_TYPE_MEMTEST) {
		bfa_ioc_set_cur_ioc_fwstate(ioc, BFI_IOC_MEMTEST);
		bfa_ioc_set_alt_ioc_fwstate(ioc, BFI_IOC_MEMTEST);
	} else {
		bfa_ioc_set_cur_ioc_fwstate(ioc, BFI_IOC_INITING);
		bfa_ioc_set_alt_ioc_fwstate(ioc, BFI_IOC_INITING);
	}

	bfa_ioc_msgflush(ioc);
	status = bfa_ioc_download_fw(ioc, boot_type, boot_env);
	if (status == BFA_STATUS_OK)
		bfa_ioc_lpu_start(ioc);
	else
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_TIMEOUT);

	return status;
}

/* Enable/disable IOC failure auto recovery. */
void
bfa_nw_ioc_auto_recover(bool auto_recover)
{
	bfa_nw_auto_recover = auto_recover;
}

static bool
bfa_ioc_msgget(struct bfa_ioc *ioc, void *mbmsg)
{
	u32	*msgp = mbmsg;
	u32	r32;
	int		i;

	r32 = readl(ioc->ioc_regs.lpu_mbox_cmd);
	if ((r32 & 1) == 0)
		return false;

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

	return true;
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

	case BFI_IOC_I2H_ENABLE_REPLY:
		bfa_ioc_enable_reply(ioc,
			(enum bfa_mode)msg->fw_event.port_mode,
			msg->fw_event.cap_bm);
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
 * bfa_nw_ioc_attach - IOC attach time initialization and setup.
 *
 * @ioc:	memory for IOC
 * @bfa:	driver instance structure
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
	INIT_LIST_HEAD(&ioc->notify_q);

	bfa_fsm_set_state(ioc, bfa_ioc_sm_uninit);
	bfa_fsm_send_event(ioc, IOC_E_RESET);
}

/* Driver detach time IOC cleanup. */
void
bfa_nw_ioc_detach(struct bfa_ioc *ioc)
{
	bfa_fsm_send_event(ioc, IOC_E_DETACH);

	/* Done with detach, empty the notify_q. */
	INIT_LIST_HEAD(&ioc->notify_q);
}

/**
 * bfa_nw_ioc_pci_init - Setup IOC PCI properties.
 *
 * @pcidev:	PCI device information for this IOC
 */
void
bfa_nw_ioc_pci_init(struct bfa_ioc *ioc, struct bfa_pcidev *pcidev,
		 enum bfi_pcifn_class clscode)
{
	ioc->clscode	= clscode;
	ioc->pcidev	= *pcidev;

	/**
	 * Initialize IOC and device personality
	 */
	ioc->port0_mode = ioc->port1_mode = BFI_PORT_MODE_FC;
	ioc->asic_mode  = BFI_ASIC_MODE_FC;

	switch (pcidev->device_id) {
	case PCI_DEVICE_ID_BROCADE_CT:
		ioc->asic_gen = BFI_ASIC_GEN_CT;
		ioc->port0_mode = ioc->port1_mode = BFI_PORT_MODE_ETH;
		ioc->asic_mode  = BFI_ASIC_MODE_ETH;
		ioc->port_mode = ioc->port_mode_cfg = BFA_MODE_CNA;
		ioc->ad_cap_bm = BFA_CM_CNA;
		break;

	case BFA_PCI_DEVICE_ID_CT2:
		ioc->asic_gen = BFI_ASIC_GEN_CT2;
		if (clscode == BFI_PCIFN_CLASS_FC &&
			pcidev->ssid == BFA_PCI_CT2_SSID_FC) {
			ioc->asic_mode  = BFI_ASIC_MODE_FC16;
			ioc->fcmode = true;
			ioc->port_mode = ioc->port_mode_cfg = BFA_MODE_HBA;
			ioc->ad_cap_bm = BFA_CM_HBA;
		} else {
			ioc->port0_mode = ioc->port1_mode = BFI_PORT_MODE_ETH;
			ioc->asic_mode  = BFI_ASIC_MODE_ETH;
			if (pcidev->ssid == BFA_PCI_CT2_SSID_FCoE) {
				ioc->port_mode =
				ioc->port_mode_cfg = BFA_MODE_CNA;
				ioc->ad_cap_bm = BFA_CM_CNA;
			} else {
				ioc->port_mode =
				ioc->port_mode_cfg = BFA_MODE_NIC;
				ioc->ad_cap_bm = BFA_CM_NIC;
			}
		}
		break;

	default:
		BUG_ON(1);
	}

	/**
	 * Set asic specific interfaces.
	 */
	if (ioc->asic_gen == BFI_ASIC_GEN_CT)
		bfa_nw_ioc_set_ct_hwif(ioc);
	else {
		WARN_ON(ioc->asic_gen != BFI_ASIC_GEN_CT2);
		bfa_nw_ioc_set_ct2_hwif(ioc);
		bfa_nw_ioc_ct2_poweron(ioc);
	}

	bfa_ioc_map_port(ioc);
	bfa_ioc_reg_init(ioc);
}

/**
 * bfa_nw_ioc_mem_claim - Initialize IOC dma memory
 *
 * @dm_kva:	kernel virtual address of IOC dma memory
 * @dm_pa:	physical address of IOC dma memory
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

/* Return size of dma memory required. */
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

/* Initialize memory for saving firmware trace. */
void
bfa_nw_ioc_debug_memclaim(struct bfa_ioc *ioc, void *dbg_fwsave)
{
	ioc->dbg_fwsave = dbg_fwsave;
	ioc->dbg_fwsave_len = ioc->iocpf.auto_recover ? BNA_DBG_FWTRC_LEN : 0;
}

static u32
bfa_ioc_smem_pgnum(struct bfa_ioc *ioc, u32 fmaddr)
{
	return PSS_SMEM_PGNUM(ioc->ioc_regs.smem_pg0, fmaddr);
}

/* Register mailbox message handler function, to be called by common modules */
void
bfa_nw_ioc_mbox_regisr(struct bfa_ioc *ioc, enum bfi_mclass mc,
		    bfa_ioc_mbox_mcfunc_t cbfn, void *cbarg)
{
	struct bfa_ioc_mbox_mod *mod = &ioc->mbox_mod;

	mod->mbhdlr[mc].cbfn	= cbfn;
	mod->mbhdlr[mc].cbarg = cbarg;
}

/**
 * bfa_nw_ioc_mbox_queue - Queue a mailbox command request to firmware.
 *
 * @ioc:	IOC instance
 * @cmd:	Mailbox command
 *
 * Waits if mailbox is busy. Responsibility of caller to serialize
 */
bool
bfa_nw_ioc_mbox_queue(struct bfa_ioc *ioc, struct bfa_mbox_cmd *cmd,
			bfa_mbox_cmd_cbfn_t cbfn, void *cbarg)
{
	struct bfa_ioc_mbox_mod *mod = &ioc->mbox_mod;
	u32			stat;

	cmd->cbfn = cbfn;
	cmd->cbarg = cbarg;

	/**
	 * If a previous command is pending, queue new command
	 */
	if (!list_empty(&mod->cmd_q)) {
		list_add_tail(&cmd->qe, &mod->cmd_q);
		return true;
	}

	/**
	 * If mailbox is busy, queue command for poll timer
	 */
	stat = readl(ioc->ioc_regs.hfn_mbox_cmd);
	if (stat) {
		list_add_tail(&cmd->qe, &mod->cmd_q);
		return true;
	}

	/**
	 * mailbox is free -- queue command to firmware
	 */
	bfa_ioc_mbox_send(ioc, cmd->msg, sizeof(cmd->msg));

	return false;
}

/* Handle mailbox interrupts */
void
bfa_nw_ioc_mbox_isr(struct bfa_ioc *ioc)
{
	struct bfa_ioc_mbox_mod *mod = &ioc->mbox_mod;
	struct bfi_mbmsg m;
	int				mc;

	if (bfa_ioc_msgget(ioc, &m)) {
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

	bfa_ioc_lpu_read_stat(ioc);

	/**
	 * Try to send pending mailbox commands
	 */
	bfa_ioc_mbox_poll(ioc);
}

void
bfa_nw_ioc_error_isr(struct bfa_ioc *ioc)
{
	bfa_ioc_stats(ioc, ioc_hbfails);
	bfa_ioc_stats_hb_count(ioc, ioc->hb_count);
	bfa_fsm_send_event(ioc, IOC_E_HWERROR);
}

/* return true if IOC is disabled */
bool
bfa_nw_ioc_is_disabled(struct bfa_ioc *ioc)
{
	return bfa_fsm_cmp_state(ioc, bfa_ioc_sm_disabling) ||
		bfa_fsm_cmp_state(ioc, bfa_ioc_sm_disabled);
}

/* return true if IOC is operational */
bool
bfa_nw_ioc_is_operational(struct bfa_ioc *ioc)
{
	return bfa_fsm_cmp_state(ioc, bfa_ioc_sm_op);
}

/* Add to IOC heartbeat failure notification queue. To be used by common
 * modules such as cee, port, diag.
 */
void
bfa_nw_ioc_notify_register(struct bfa_ioc *ioc,
			struct bfa_ioc_notify *notify)
{
	list_add_tail(&notify->qe, &ioc->notify_q);
}

#define BFA_MFG_NAME "QLogic"
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
	bfa_nw_ioc_get_mac(ioc, ad_attr->mac);

	ad_attr->pcie_gen = ioc_attr->pcie_gen;
	ad_attr->pcie_lanes = ioc_attr->pcie_lanes;
	ad_attr->pcie_lanes_orig = ioc_attr->pcie_lanes_orig;
	ad_attr->asic_rev = ioc_attr->asic_rev;

	bfa_ioc_get_pci_chip_rev(ioc, ad_attr->hw_ver);
}

static enum bfa_ioc_type
bfa_ioc_get_type(struct bfa_ioc *ioc)
{
	if (ioc->clscode == BFI_PCIFN_CLASS_ETH)
		return BFA_IOC_TYPE_LL;

	BUG_ON(!(ioc->clscode == BFI_PCIFN_CLASS_FC));

	return (ioc->attr->port_mode == BFI_PORT_MODE_FC)
		? BFA_IOC_TYPE_FC : BFA_IOC_TYPE_FCoE;
}

static void
bfa_ioc_get_adapter_serial_num(struct bfa_ioc *ioc, char *serial_num)
{
	memcpy(serial_num,
			(void *)ioc->attr->brcd_serialnum,
			BFA_ADAPTER_SERIAL_NUM_LEN);
}

static void
bfa_ioc_get_adapter_fw_ver(struct bfa_ioc *ioc, char *fw_ver)
{
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
	memcpy(optrom_ver, ioc->attr->optrom_version,
		      BFA_VERSION_LEN);
}

static void
bfa_ioc_get_adapter_manufacturer(struct bfa_ioc *ioc, char *manufacturer)
{
	strncpy(manufacturer, BFA_MFG_NAME, BFA_ADAPTER_MFG_NAME_LEN);
}

static void
bfa_ioc_get_adapter_model(struct bfa_ioc *ioc, char *model)
{
	struct bfi_ioc_attr *ioc_attr;

	BUG_ON(!(model));
	memset(model, 0, BFA_ADAPTER_MODEL_NAME_LEN);

	ioc_attr = ioc->attr;

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
	ioc_attr->port_id = bfa_ioc_portid(ioc);
	ioc_attr->port_mode = ioc->port_mode;

	ioc_attr->port_mode_cfg = ioc->port_mode_cfg;
	ioc_attr->cap_bm = ioc->ad_cap_bm;

	ioc_attr->ioc_type = bfa_ioc_get_type(ioc);

	bfa_ioc_get_adapter_attr(ioc, &ioc_attr->adapter_attr);

	ioc_attr->pci_attr.device_id = bfa_ioc_devid(ioc);
	ioc_attr->pci_attr.pcifn = bfa_ioc_pcifn(ioc);
	ioc_attr->def_fn = bfa_ioc_is_default(ioc);
	bfa_ioc_get_pci_chip_rev(ioc, ioc_attr->pci_attr.chip_rev);
}

/* WWN public */
static u64
bfa_ioc_get_pwwn(struct bfa_ioc *ioc)
{
	return ioc->attr->pwwn;
}

void
bfa_nw_ioc_get_mac(struct bfa_ioc *ioc, u8 *mac)
{
	ether_addr_copy(mac, ioc->attr->mac);
}

/* Firmware failure detected. Start recovery actions. */
static void
bfa_ioc_recover(struct bfa_ioc *ioc)
{
	pr_crit("Heart Beat of IOC has failed\n");
	bfa_ioc_stats(ioc, ioc_hbfails);
	bfa_ioc_stats_hb_count(ioc, ioc->hb_count);
	bfa_fsm_send_event(ioc, IOC_E_HBFAIL);
}

/* BFA IOC PF private functions */

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
bfa_nw_iocpf_timeout(struct bfa_ioc *ioc)
{
	enum bfa_iocpf_state iocpf_st;

	iocpf_st = bfa_sm_to_state(iocpf_sm_table, ioc->iocpf.fsm);

	if (iocpf_st == BFA_IOCPF_HWINIT)
		bfa_ioc_poll_fwinit(ioc);
	else
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_TIMEOUT);
}

void
bfa_nw_iocpf_sem_timeout(struct bfa_ioc *ioc)
{
	bfa_ioc_hw_sem_get(ioc);
}

static void
bfa_ioc_poll_fwinit(struct bfa_ioc *ioc)
{
	u32 fwstate = bfa_ioc_get_cur_ioc_fwstate(ioc);

	if (fwstate == BFI_IOC_DISABLED) {
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_FWREADY);
		return;
	}

	if (ioc->iocpf.poll_time >= BFA_IOC_TOV) {
		bfa_fsm_send_event(&ioc->iocpf, IOCPF_E_TIMEOUT);
	} else {
		ioc->iocpf.poll_time += BFA_IOC_POLL_TOV;
		mod_timer(&ioc->iocpf_timer, jiffies +
			msecs_to_jiffies(BFA_IOC_POLL_TOV));
	}
}

/*
 *	Flash module specific
 */

/*
 * FLASH DMA buffer should be big enough to hold both MFG block and
 * asic block(64k) at the same time and also should be 2k aligned to
 * avoid write segement to cross sector boundary.
 */
#define BFA_FLASH_SEG_SZ	2048
#define BFA_FLASH_DMA_BUF_SZ	\
	roundup(0x010000 + sizeof(struct bfa_mfg_block), BFA_FLASH_SEG_SZ)

static void
bfa_flash_cb(struct bfa_flash *flash)
{
	flash->op_busy = 0;
	if (flash->cbfn)
		flash->cbfn(flash->cbarg, flash->status);
}

static void
bfa_flash_notify(void *cbarg, enum bfa_ioc_event event)
{
	struct bfa_flash *flash = cbarg;

	switch (event) {
	case BFA_IOC_E_DISABLED:
	case BFA_IOC_E_FAILED:
		if (flash->op_busy) {
			flash->status = BFA_STATUS_IOC_FAILURE;
			flash->cbfn(flash->cbarg, flash->status);
			flash->op_busy = 0;
		}
		break;
	default:
		break;
	}
}

/*
 * Send flash write request.
 */
static void
bfa_flash_write_send(struct bfa_flash *flash)
{
	struct bfi_flash_write_req *msg =
			(struct bfi_flash_write_req *) flash->mb.msg;
	u32	len;

	msg->type = be32_to_cpu(flash->type);
	msg->instance = flash->instance;
	msg->offset = be32_to_cpu(flash->addr_off + flash->offset);
	len = (flash->residue < BFA_FLASH_DMA_BUF_SZ) ?
	       flash->residue : BFA_FLASH_DMA_BUF_SZ;
	msg->length = be32_to_cpu(len);

	/* indicate if it's the last msg of the whole write operation */
	msg->last = (len == flash->residue) ? 1 : 0;

	bfi_h2i_set(msg->mh, BFI_MC_FLASH, BFI_FLASH_H2I_WRITE_REQ,
		    bfa_ioc_portid(flash->ioc));
	bfa_alen_set(&msg->alen, len, flash->dbuf_pa);
	memcpy(flash->dbuf_kva, flash->ubuf + flash->offset, len);
	bfa_nw_ioc_mbox_queue(flash->ioc, &flash->mb, NULL, NULL);

	flash->residue -= len;
	flash->offset += len;
}

/**
 * bfa_flash_read_send - Send flash read request.
 *
 * @cbarg: callback argument
 */
static void
bfa_flash_read_send(void *cbarg)
{
	struct bfa_flash *flash = cbarg;
	struct bfi_flash_read_req *msg =
			(struct bfi_flash_read_req *) flash->mb.msg;
	u32	len;

	msg->type = be32_to_cpu(flash->type);
	msg->instance = flash->instance;
	msg->offset = be32_to_cpu(flash->addr_off + flash->offset);
	len = (flash->residue < BFA_FLASH_DMA_BUF_SZ) ?
	       flash->residue : BFA_FLASH_DMA_BUF_SZ;
	msg->length = be32_to_cpu(len);
	bfi_h2i_set(msg->mh, BFI_MC_FLASH, BFI_FLASH_H2I_READ_REQ,
		    bfa_ioc_portid(flash->ioc));
	bfa_alen_set(&msg->alen, len, flash->dbuf_pa);
	bfa_nw_ioc_mbox_queue(flash->ioc, &flash->mb, NULL, NULL);
}

/**
 * bfa_flash_intr - Process flash response messages upon receiving interrupts.
 *
 * @flasharg: flash structure
 * @msg: message structure
 */
static void
bfa_flash_intr(void *flasharg, struct bfi_mbmsg *msg)
{
	struct bfa_flash *flash = flasharg;
	u32	status;

	union {
		struct bfi_flash_query_rsp *query;
		struct bfi_flash_write_rsp *write;
		struct bfi_flash_read_rsp *read;
		struct bfi_mbmsg   *msg;
	} m;

	m.msg = msg;

	/* receiving response after ioc failure */
	if (!flash->op_busy && msg->mh.msg_id != BFI_FLASH_I2H_EVENT)
		return;

	switch (msg->mh.msg_id) {
	case BFI_FLASH_I2H_QUERY_RSP:
		status = be32_to_cpu(m.query->status);
		if (status == BFA_STATUS_OK) {
			u32	i;
			struct bfa_flash_attr *attr, *f;

			attr = (struct bfa_flash_attr *) flash->ubuf;
			f = (struct bfa_flash_attr *) flash->dbuf_kva;
			attr->status = be32_to_cpu(f->status);
			attr->npart = be32_to_cpu(f->npart);
			for (i = 0; i < attr->npart; i++) {
				attr->part[i].part_type =
					be32_to_cpu(f->part[i].part_type);
				attr->part[i].part_instance =
					be32_to_cpu(f->part[i].part_instance);
				attr->part[i].part_off =
					be32_to_cpu(f->part[i].part_off);
				attr->part[i].part_size =
					be32_to_cpu(f->part[i].part_size);
				attr->part[i].part_len =
					be32_to_cpu(f->part[i].part_len);
				attr->part[i].part_status =
					be32_to_cpu(f->part[i].part_status);
			}
		}
		flash->status = status;
		bfa_flash_cb(flash);
		break;
	case BFI_FLASH_I2H_WRITE_RSP:
		status = be32_to_cpu(m.write->status);
		if (status != BFA_STATUS_OK || flash->residue == 0) {
			flash->status = status;
			bfa_flash_cb(flash);
		} else
			bfa_flash_write_send(flash);
		break;
	case BFI_FLASH_I2H_READ_RSP:
		status = be32_to_cpu(m.read->status);
		if (status != BFA_STATUS_OK) {
			flash->status = status;
			bfa_flash_cb(flash);
		} else {
			u32 len = be32_to_cpu(m.read->length);
			memcpy(flash->ubuf + flash->offset,
			       flash->dbuf_kva, len);
			flash->residue -= len;
			flash->offset += len;
			if (flash->residue == 0) {
				flash->status = status;
				bfa_flash_cb(flash);
			} else
				bfa_flash_read_send(flash);
		}
		break;
	case BFI_FLASH_I2H_BOOT_VER_RSP:
	case BFI_FLASH_I2H_EVENT:
		break;
	default:
		WARN_ON(1);
	}
}

/*
 * Flash memory info API.
 */
u32
bfa_nw_flash_meminfo(void)
{
	return roundup(BFA_FLASH_DMA_BUF_SZ, BFA_DMA_ALIGN_SZ);
}

/**
 * bfa_nw_flash_attach - Flash attach API.
 *
 * @flash: flash structure
 * @ioc: ioc structure
 * @dev: device structure
 */
void
bfa_nw_flash_attach(struct bfa_flash *flash, struct bfa_ioc *ioc, void *dev)
{
	flash->ioc = ioc;
	flash->cbfn = NULL;
	flash->cbarg = NULL;
	flash->op_busy = 0;

	bfa_nw_ioc_mbox_regisr(flash->ioc, BFI_MC_FLASH, bfa_flash_intr, flash);
	bfa_ioc_notify_init(&flash->ioc_notify, bfa_flash_notify, flash);
	list_add_tail(&flash->ioc_notify.qe, &flash->ioc->notify_q);
}

/**
 * bfa_nw_flash_memclaim - Claim memory for flash
 *
 * @flash: flash structure
 * @dm_kva: pointer to virtual memory address
 * @dm_pa: physical memory address
 */
void
bfa_nw_flash_memclaim(struct bfa_flash *flash, u8 *dm_kva, u64 dm_pa)
{
	flash->dbuf_kva = dm_kva;
	flash->dbuf_pa = dm_pa;
	memset(flash->dbuf_kva, 0, BFA_FLASH_DMA_BUF_SZ);
	dm_kva += roundup(BFA_FLASH_DMA_BUF_SZ, BFA_DMA_ALIGN_SZ);
	dm_pa += roundup(BFA_FLASH_DMA_BUF_SZ, BFA_DMA_ALIGN_SZ);
}

/**
 * bfa_nw_flash_get_attr - Get flash attribute.
 *
 * @flash: flash structure
 * @attr: flash attribute structure
 * @cbfn: callback function
 * @cbarg: callback argument
 *
 * Return status.
 */
enum bfa_status
bfa_nw_flash_get_attr(struct bfa_flash *flash, struct bfa_flash_attr *attr,
		      bfa_cb_flash cbfn, void *cbarg)
{
	struct bfi_flash_query_req *msg =
			(struct bfi_flash_query_req *) flash->mb.msg;

	if (!bfa_nw_ioc_is_operational(flash->ioc))
		return BFA_STATUS_IOC_NON_OP;

	if (flash->op_busy)
		return BFA_STATUS_DEVBUSY;

	flash->op_busy = 1;
	flash->cbfn = cbfn;
	flash->cbarg = cbarg;
	flash->ubuf = (u8 *) attr;

	bfi_h2i_set(msg->mh, BFI_MC_FLASH, BFI_FLASH_H2I_QUERY_REQ,
		    bfa_ioc_portid(flash->ioc));
	bfa_alen_set(&msg->alen, sizeof(struct bfa_flash_attr), flash->dbuf_pa);
	bfa_nw_ioc_mbox_queue(flash->ioc, &flash->mb, NULL, NULL);

	return BFA_STATUS_OK;
}

/**
 * bfa_nw_flash_update_part - Update flash partition.
 *
 * @flash: flash structure
 * @type: flash partition type
 * @instance: flash partition instance
 * @buf: update data buffer
 * @len: data buffer length
 * @offset: offset relative to the partition starting address
 * @cbfn: callback function
 * @cbarg: callback argument
 *
 * Return status.
 */
enum bfa_status
bfa_nw_flash_update_part(struct bfa_flash *flash, u32 type, u8 instance,
			 void *buf, u32 len, u32 offset,
			 bfa_cb_flash cbfn, void *cbarg)
{
	if (!bfa_nw_ioc_is_operational(flash->ioc))
		return BFA_STATUS_IOC_NON_OP;

	/*
	 * 'len' must be in word (4-byte) boundary
	 */
	if (!len || (len & 0x03))
		return BFA_STATUS_FLASH_BAD_LEN;

	if (type == BFA_FLASH_PART_MFG)
		return BFA_STATUS_EINVAL;

	if (flash->op_busy)
		return BFA_STATUS_DEVBUSY;

	flash->op_busy = 1;
	flash->cbfn = cbfn;
	flash->cbarg = cbarg;
	flash->type = type;
	flash->instance = instance;
	flash->residue = len;
	flash->offset = 0;
	flash->addr_off = offset;
	flash->ubuf = buf;

	bfa_flash_write_send(flash);

	return BFA_STATUS_OK;
}

/**
 * bfa_nw_flash_read_part - Read flash partition.
 *
 * @flash: flash structure
 * @type: flash partition type
 * @instance: flash partition instance
 * @buf: read data buffer
 * @len: data buffer length
 * @offset: offset relative to the partition starting address
 * @cbfn: callback function
 * @cbarg: callback argument
 *
 * Return status.
 */
enum bfa_status
bfa_nw_flash_read_part(struct bfa_flash *flash, u32 type, u8 instance,
		       void *buf, u32 len, u32 offset,
		       bfa_cb_flash cbfn, void *cbarg)
{
	if (!bfa_nw_ioc_is_operational(flash->ioc))
		return BFA_STATUS_IOC_NON_OP;

	/*
	 * 'len' must be in word (4-byte) boundary
	 */
	if (!len || (len & 0x03))
		return BFA_STATUS_FLASH_BAD_LEN;

	if (flash->op_busy)
		return BFA_STATUS_DEVBUSY;

	flash->op_busy = 1;
	flash->cbfn = cbfn;
	flash->cbarg = cbarg;
	flash->type = type;
	flash->instance = instance;
	flash->residue = len;
	flash->offset = 0;
	flash->addr_off = offset;
	flash->ubuf = buf;

	bfa_flash_read_send(flash);

	return BFA_STATUS_OK;
}
