// SPDX-License-Identifier: GPL-2.0
/*  Marvell OcteonTx2 RPM driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include "cgx.h"
#include "lmac_common.h"

static struct mac_ops	rpm_mac_ops   = {
	.name		=       "rpm",
	.csr_offset     =       0x4e00,
	.lmac_offset    =       20,
	.int_register	=       RPMX_CMRX_SW_INT,
	.int_set_reg    =       RPMX_CMRX_SW_INT_ENA_W1S,
	.irq_offset     =       1,
	.int_ena_bit    =       BIT_ULL(0),
	.lmac_fwi	=	RPM_LMAC_FWI,
	.non_contiguous_serdes_lane = true,
	.get_nr_lmacs	=	rpm_get_nr_lmacs,
};

struct mac_ops *rpm_get_mac_ops(void)
{
	return &rpm_mac_ops;
}

static u64 rpm_read(rpm_t *rpm, u64 lmac, u64 offset)
{
	return	cgx_read(rpm, lmac, offset);
}

int rpm_get_nr_lmacs(void *rpmd)
{
	rpm_t *rpm = rpmd;

	return hweight8(rpm_read(rpm, 0, CGXX_CMRX_RX_LMACS) & 0xFULL);
}
