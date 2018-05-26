// SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause)
/*
 * Copyright (C) 2018 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/kernel.h>

#include "../nfpcore/nfp_cpp.h"
#include "../nfp_app.h"
#include "../nfp_abi.h"
#include "../nfp_main.h"
#include "../nfp_net.h"
#include "main.h"

int nfp_abm_ctrl_qm_enable(struct nfp_abm *abm)
{
	return nfp_mbox_cmd(abm->app->pf, NFP_MBOX_PCIE_ABM_ENABLE,
			    NULL, 0, NULL, 0);
}

int nfp_abm_ctrl_qm_disable(struct nfp_abm *abm)
{
	return nfp_mbox_cmd(abm->app->pf, NFP_MBOX_PCIE_ABM_DISABLE,
			    NULL, 0, NULL, 0);
}

void nfp_abm_ctrl_read_params(struct nfp_abm_link *alink)
{
	alink->queue_base = nn_readl(alink->vnic, NFP_NET_CFG_START_RXQ);
	alink->queue_base /= alink->vnic->stride_rx;
}

int nfp_abm_ctrl_find_addrs(struct nfp_abm *abm)
{
	struct nfp_pf *pf = abm->app->pf;
	unsigned int pf_id;

	pf_id =	nfp_cppcore_pcie_unit(pf->cpp);
	abm->pf_id = pf_id;

	return 0;
}
