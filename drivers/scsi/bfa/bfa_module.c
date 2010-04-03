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
#include <defs/bfa_defs_pci.h>
#include <cs/bfa_debug.h>
#include <bfa_iocfc.h>

/**
 * BFA module list terminated by NULL
 */
struct bfa_module_s *hal_mods[] = {
	&hal_mod_sgpg,
	&hal_mod_fcport,
	&hal_mod_fcxp,
	&hal_mod_lps,
	&hal_mod_uf,
	&hal_mod_rport,
	&hal_mod_fcpim,
#ifdef BFA_CFG_PBIND
	&hal_mod_pbind,
#endif
	NULL
};

/**
 * Message handlers for various modules.
 */
bfa_isr_func_t  bfa_isrs[BFI_MC_MAX] = {
	bfa_isr_unhandled,	/* NONE */
	bfa_isr_unhandled,	/* BFI_MC_IOC */
	bfa_isr_unhandled,	/* BFI_MC_DIAG */
	bfa_isr_unhandled,	/* BFI_MC_FLASH */
	bfa_isr_unhandled,	/* BFI_MC_CEE */
	bfa_fcport_isr,		/* BFI_MC_FCPORT */
	bfa_isr_unhandled,	/* BFI_MC_IOCFC */
	bfa_isr_unhandled,	/* BFI_MC_LL */
	bfa_uf_isr,		/* BFI_MC_UF */
	bfa_fcxp_isr,		/* BFI_MC_FCXP */
	bfa_lps_isr,		/* BFI_MC_LPS */
	bfa_rport_isr,		/* BFI_MC_RPORT */
	bfa_itnim_isr,		/* BFI_MC_ITNIM */
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

/**
 * Message handlers for mailbox command classes
 */
bfa_ioc_mbox_mcfunc_t  bfa_mbox_isrs[BFI_MC_MAX] = {
	NULL,
	NULL,			/* BFI_MC_IOC	*/
	NULL,			/* BFI_MC_DIAG	*/
	NULL,		/* BFI_MC_FLASH */
	NULL,			/* BFI_MC_CEE	*/
	NULL,			/* BFI_MC_PORT	*/
	bfa_iocfc_isr,		/* BFI_MC_IOCFC */
	NULL,
};

