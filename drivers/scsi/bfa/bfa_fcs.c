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

/**
 *  bfa_fcs.c BFA FCS main
 */

#include <fcs/bfa_fcs.h>
#include "fcs_port.h"
#include "fcs_uf.h"
#include "fcs_vport.h"
#include "fcs_rport.h"
#include "fcs_fabric.h"
#include "fcs_fcpim.h"
#include "fcs_fcptm.h"
#include "fcbuild.h"
#include "fcs.h"
#include "bfad_drv.h"
#include <fcb/bfa_fcb.h>

/**
 * FCS sub-modules
 */
struct bfa_fcs_mod_s {
	void            (*modinit) (struct bfa_fcs_s *fcs);
	void            (*modexit) (struct bfa_fcs_s *fcs);
};

#define BFA_FCS_MODULE(_mod) { _mod ## _modinit, _mod ## _modexit }

static struct bfa_fcs_mod_s fcs_modules[] = {
	BFA_FCS_MODULE(bfa_fcs_pport),
	BFA_FCS_MODULE(bfa_fcs_uf),
	BFA_FCS_MODULE(bfa_fcs_fabric),
	BFA_FCS_MODULE(bfa_fcs_vport),
	BFA_FCS_MODULE(bfa_fcs_rport),
	BFA_FCS_MODULE(bfa_fcs_fcpim),
};

/**
 *  fcs_api BFA FCS API
 */

static void
bfa_fcs_exit_comp(void *fcs_cbarg)
{
	struct bfa_fcs_s *fcs = fcs_cbarg;
	struct bfad_s *bfad = fcs->bfad;

	complete(&bfad->comp);
}



/**
 *  fcs_api BFA FCS API
 */

/**
 * 		FCS instance initialization.
 *
 * 	param[in]		fcs		FCS instance
 * 	param[in]		bfa		BFA instance
 * 	param[in]		bfad		BFA driver instance
 *
 * 	return None
 */
void
bfa_fcs_init(struct bfa_fcs_s *fcs, struct bfa_s *bfa, struct bfad_s *bfad,
			bfa_boolean_t min_cfg)
{
	int             i;
	struct bfa_fcs_mod_s  *mod;

	fcs->bfa = bfa;
	fcs->bfad = bfad;
	fcs->min_cfg = min_cfg;

	bfa_attach_fcs(bfa);
	fcbuild_init();

	for (i = 0; i < sizeof(fcs_modules) / sizeof(fcs_modules[0]); i++) {
		mod = &fcs_modules[i];
		mod->modinit(fcs);
	}
}

/**
 * Start FCS operations.
 */
void
bfa_fcs_start(struct bfa_fcs_s *fcs)
{
	bfa_fcs_fabric_modstart(fcs);
}

/**
 * 		FCS driver details initialization.
 *
 * 	param[in]		fcs		FCS instance
 * 	param[in]		driver_info	Driver Details
 *
 * 	return None
 */
void
bfa_fcs_driver_info_init(struct bfa_fcs_s *fcs,
			struct bfa_fcs_driver_info_s *driver_info)
{

	fcs->driver_info = *driver_info;

	bfa_fcs_fabric_psymb_init(&fcs->fabric);
}

/**
 * 		FCS instance cleanup and exit.
 *
 * 	param[in]		fcs			FCS instance
 * 	return None
 */
void
bfa_fcs_exit(struct bfa_fcs_s *fcs)
{
	struct bfa_fcs_mod_s  *mod;
	int             nmods, i;

	bfa_wc_init(&fcs->wc, bfa_fcs_exit_comp, fcs);

	nmods = sizeof(fcs_modules) / sizeof(fcs_modules[0]);

	for (i = 0; i < nmods; i++) {
		bfa_wc_up(&fcs->wc);

		mod = &fcs_modules[i];
		mod->modexit(fcs);
	}

	bfa_wc_wait(&fcs->wc);
}


void
bfa_fcs_trc_init(struct bfa_fcs_s *fcs, struct bfa_trc_mod_s *trcmod)
{
	fcs->trcmod = trcmod;
}


void
bfa_fcs_log_init(struct bfa_fcs_s *fcs, struct bfa_log_mod_s *logmod)
{
	fcs->logm = logmod;
}


void
bfa_fcs_aen_init(struct bfa_fcs_s *fcs, struct bfa_aen_s *aen)
{
	fcs->aen = aen;
}

void
bfa_fcs_modexit_comp(struct bfa_fcs_s *fcs)
{
	bfa_wc_down(&fcs->wc);
}


