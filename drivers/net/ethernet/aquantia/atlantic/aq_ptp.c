// SPDX-License-Identifier: GPL-2.0-only
/* Aquantia Corporation Network Driver
 * Copyright (C) 2014-2019 Aquantia Corporation. All rights reserved
 */

/* File aq_ptp.c:
 * Definition of functions for Linux PTP support.
 */

#include <linux/ptp_clock_kernel.h>
#include <linux/clocksource.h>

#include "aq_nic.h"
#include "aq_ptp.h"

struct aq_ptp_s {
	struct aq_nic_s *aq_nic;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_info;
};

static struct ptp_clock_info aq_ptp_clock = {
	.owner		= THIS_MODULE,
	.name		= "atlantic ptp",
	.n_ext_ts	= 0,
	.pps		= 0,
	.n_per_out	= 0,
	.n_pins		= 0,
	.pin_config	= NULL,
};

int aq_ptp_init(struct aq_nic_s *aq_nic, unsigned int idx_vec)
{
	struct hw_atl_utils_mbox mbox;
	struct aq_ptp_s *aq_ptp;
	int err = 0;

	hw_atl_utils_mpi_read_stats(aq_nic->aq_hw, &mbox);

	if (!(mbox.info.caps_ex & BIT(CAPS_EX_PHY_PTP_EN))) {
		aq_nic->aq_ptp = NULL;
		return 0;
	}

	aq_ptp = kzalloc(sizeof(*aq_ptp), GFP_KERNEL);
	if (!aq_ptp) {
		err = -ENOMEM;
		goto err_exit;
	}

	aq_ptp->aq_nic = aq_nic;

	aq_ptp->ptp_info = aq_ptp_clock;

	aq_nic->aq_ptp = aq_ptp;

	return 0;

err_exit:
	kfree(aq_ptp);
	aq_nic->aq_ptp = NULL;
	return err;
}

void aq_ptp_unregister(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;

	if (!aq_ptp)
		return;

	ptp_clock_unregister(aq_ptp->ptp_clock);
}

void aq_ptp_free(struct aq_nic_s *aq_nic)
{
	struct aq_ptp_s *aq_ptp = aq_nic->aq_ptp;

	if (!aq_ptp)
		return;

	kfree(aq_ptp);
	aq_nic->aq_ptp = NULL;
}
