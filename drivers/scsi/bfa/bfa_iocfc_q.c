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
#include "bfa_intr_priv.h"

BFA_TRC_FILE(HAL, IOCFC_Q);

void
bfa_iocfc_updateq(struct bfa_s *bfa, u32 reqq_ba, u32 rspq_ba,
				u32 reqq_sci, u32 rspq_spi, bfa_cb_iocfc_t cbfn,
				void *cbarg)
{
	struct bfa_iocfc_s *iocfc = &bfa->iocfc;
	struct bfi_iocfc_updateq_req_s updateq_req;

	iocfc->updateq_cbfn = cbfn;
	iocfc->updateq_cbarg = cbarg;

	bfi_h2i_set(updateq_req.mh, BFI_MC_IOCFC, BFI_IOCFC_H2I_UPDATEQ_REQ,
			bfa_lpuid(bfa));

	updateq_req.reqq_ba = bfa_os_htonl(reqq_ba);
	updateq_req.rspq_ba = bfa_os_htonl(rspq_ba);
	updateq_req.reqq_sci = bfa_os_htonl(reqq_sci);
	updateq_req.rspq_spi = bfa_os_htonl(rspq_spi);

	bfa_ioc_mbox_send(&bfa->ioc, &updateq_req,
			sizeof(struct bfi_iocfc_updateq_req_s));
}
