/*
 * io.c
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * IO manager interface: Manages IO between CHNL and msg_ctrl.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
#include <dspbridge/std.h>
#include <dspbridge/dbdefs.h>

/*  ----------------------------------- Trace & Debug */
#include <dspbridge/dbc.h>

/*  ----------------------------------- OS Adaptation Layer */
#include <dspbridge/cfg.h>

/*  ----------------------------------- Platform Manager */
#include <dspbridge/dev.h>

/*  ----------------------------------- This */
#include <ioobj.h>
#include <dspbridge/iodefs.h>
#include <dspbridge/io.h>

/*  ----------------------------------- Globals */
static u32 refs;

/*
 *  ======== io_create ========
 *  Purpose:
 *      Create an IO manager object, responsible for managing IO between
 *      CHNL and msg_ctrl
 */
int io_create(OUT struct io_mgr **phIOMgr, struct dev_object *hdev_obj,
		     IN CONST struct io_attrs *pMgrAttrs)
{
	struct bridge_drv_interface *intf_fxns;
	struct io_mgr *hio_mgr = NULL;
	struct io_mgr_ *pio_mgr = NULL;
	int status = 0;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(phIOMgr != NULL);
	DBC_REQUIRE(pMgrAttrs != NULL);

	*phIOMgr = NULL;

	/* A memory base of 0 implies no memory base: */
	if ((pMgrAttrs->shm_base != 0) && (pMgrAttrs->usm_length == 0))
		status = -EINVAL;

	if (pMgrAttrs->word_size == 0)
		status = -EINVAL;

	if (DSP_SUCCEEDED(status)) {
		dev_get_intf_fxns(hdev_obj, &intf_fxns);

		/* Let Bridge channel module finish the create: */
		status = (*intf_fxns->pfn_io_create) (&hio_mgr, hdev_obj,
						      pMgrAttrs);

		if (DSP_SUCCEEDED(status)) {
			pio_mgr = (struct io_mgr_ *)hio_mgr;
			pio_mgr->intf_fxns = intf_fxns;
			pio_mgr->hdev_obj = hdev_obj;

			/* Return the new channel manager handle: */
			*phIOMgr = hio_mgr;
		}
	}

	return status;
}

/*
 *  ======== io_destroy ========
 *  Purpose:
 *      Delete IO manager.
 */
int io_destroy(struct io_mgr *hio_mgr)
{
	struct bridge_drv_interface *intf_fxns;
	struct io_mgr_ *pio_mgr = (struct io_mgr_ *)hio_mgr;
	int status;

	DBC_REQUIRE(refs > 0);

	intf_fxns = pio_mgr->intf_fxns;

	/* Let Bridge channel module destroy the io_mgr: */
	status = (*intf_fxns->pfn_io_destroy) (hio_mgr);

	return status;
}

/*
 *  ======== io_exit ========
 *  Purpose:
 *      Discontinue usage of the IO module.
 */
void io_exit(void)
{
	DBC_REQUIRE(refs > 0);

	refs--;

	DBC_ENSURE(refs >= 0);
}

/*
 *  ======== io_init ========
 *  Purpose:
 *      Initialize the IO module's private state.
 */
bool io_init(void)
{
	bool ret = true;

	DBC_REQUIRE(refs >= 0);

	if (ret)
		refs++;

	DBC_ENSURE((ret && (refs > 0)) || (!ret && (refs >= 0)));

	return ret;
}
