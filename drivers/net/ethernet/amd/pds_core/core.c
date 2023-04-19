// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include "core.h"

int pdsc_setup(struct pdsc *pdsc, bool init)
{
	int err = 0;

	if (init)
		err = pdsc_dev_init(pdsc);
	else
		err = pdsc_dev_reinit(pdsc);
	if (err)
		return err;

	clear_bit(PDSC_S_FW_DEAD, &pdsc->state);
	return 0;
}

void pdsc_teardown(struct pdsc *pdsc, bool removing)
{
	pdsc_devcmd_reset(pdsc);

	if (removing) {
		kfree(pdsc->intr_info);
		pdsc->intr_info = NULL;
	}

	if (pdsc->kern_dbpage) {
		iounmap(pdsc->kern_dbpage);
		pdsc->kern_dbpage = NULL;
	}

	set_bit(PDSC_S_FW_DEAD, &pdsc->state);
}
