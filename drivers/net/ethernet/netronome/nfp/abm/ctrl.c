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
#include "../nfpcore/nfp_nffw.h"
#include "../nfp_app.h"
#include "../nfp_abi.h"
#include "../nfp_main.h"
#include "../nfp_net.h"
#include "main.h"

#define NFP_QLVL_SYM_NAME	"_abi_nfd_out_q_lvls_%u"
#define NFP_QLVL_STRIDE		16
#define NFP_QLVL_THRS		8

static unsigned long long
nfp_abm_q_lvl_thrs(struct nfp_abm_link *alink, unsigned int queue)
{
	return alink->abm->q_lvls->addr +
		(alink->queue_base + queue) * NFP_QLVL_STRIDE + NFP_QLVL_THRS;
}

static int
nfp_abm_ctrl_set_q_lvl(struct nfp_abm_link *alink, unsigned int i, u32 val)
{
	struct nfp_cpp *cpp = alink->abm->app->cpp;
	u32 muw;
	int err;

	muw = NFP_CPP_ATOMIC_WR(alink->abm->q_lvls->target,
				alink->abm->q_lvls->domain);

	err = nfp_cpp_writel(cpp, muw, nfp_abm_q_lvl_thrs(alink, i), val);
	if (err) {
		nfp_err(cpp, "RED offload setting level failed on vNIC %d queue %d\n",
			alink->id, i);
		return err;
	}

	return 0;
}

int nfp_abm_ctrl_set_all_q_lvls(struct nfp_abm_link *alink, u32 val)
{
	int i, err;

	for (i = 0; i < alink->vnic->max_rx_rings; i++) {
		err = nfp_abm_ctrl_set_q_lvl(alink, i, val);
		if (err)
			return err;
	}

	return 0;
}

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

static const struct nfp_rtsym *
nfp_abm_ctrl_find_rtsym(struct nfp_pf *pf, const char *name, unsigned int size)
{
	const struct nfp_rtsym *sym;

	sym = nfp_rtsym_lookup(pf->rtbl, name);
	if (!sym) {
		nfp_err(pf->cpp, "Symbol '%s' not found\n", name);
		return ERR_PTR(-ENOENT);
	}
	if (sym->size != size) {
		nfp_err(pf->cpp,
			"Symbol '%s' wrong size: expected %u got %llu\n",
			name, size, sym->size);
		return ERR_PTR(-EINVAL);
	}

	return sym;
}

static const struct nfp_rtsym *
nfp_abm_ctrl_find_q_rtsym(struct nfp_pf *pf, const char *name,
			  unsigned int size)
{
	return nfp_abm_ctrl_find_rtsym(pf, name, size * NFP_NET_MAX_RX_RINGS);
}

int nfp_abm_ctrl_find_addrs(struct nfp_abm *abm)
{
	struct nfp_pf *pf = abm->app->pf;
	const struct nfp_rtsym *sym;
	unsigned int pf_id;
	char pf_symbol[64];

	pf_id =	nfp_cppcore_pcie_unit(pf->cpp);
	abm->pf_id = pf_id;

	snprintf(pf_symbol, sizeof(pf_symbol), NFP_QLVL_SYM_NAME, pf_id);
	sym = nfp_abm_ctrl_find_q_rtsym(pf, pf_symbol, NFP_QLVL_STRIDE);
	if (IS_ERR(sym))
		return PTR_ERR(sym);
	abm->q_lvls = sym;

	return 0;
}
