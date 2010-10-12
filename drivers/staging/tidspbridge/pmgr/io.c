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
#include <linux/types.h>

/*  ----------------------------------- Host OS */
#include <dspbridge/host_os.h>

/*  ----------------------------------- DSP/BIOS Bridge */
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
int io_create(struct io_mgr **io_man, struct dev_object *hdev_obj,
		     const struct io_attrs *mgr_attrts)
{
	struct bridge_drv_interface *intf_fxns;
	struct io_mgr *hio_mgr = NULL;
	struct io_mgr_ *pio_mgr = NULL;
	int status = 0;

	DBC_REQUIRE(refs > 0);
	DBC_REQUIRE(io_man != NULL);
	DBC_REQUIRE(mgr_attrts != NULL);

	*io_man = NULL;

	/* A memory base of 0 implies no memory base: */
	if ((mgr_attrts->shm_base != 0) && (mgr_attrts->usm_length == 0))
		status = -EINVAL;

	if (mgr_attrts->word_size == 0)
		status = -EINVAL;

	if (!status) {
		dev_get_intf_fxns(hdev_obj, &intf_fxns);

		/* Let Bridge channel module finish the create: */
		status = (*intf_fxns->pfn_io_create) (&hio_mgr, hdev_obj,
						      mgr_attrts);

		if (!status) {
			pio_mgr = (struct io_mgr_ *)hio_mgr;
			pio_mgr->intf_fxns = intf_fxns;
			pio_mgr->hdev_obj = hdev_obj;

			/* Return the new channel manager handle: */
			*io_man = hio_mgr;
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
