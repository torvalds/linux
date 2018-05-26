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
#define NFP_QLVL_BLOG_BYTES	0
#define NFP_QLVL_BLOG_PKTS	4
#define NFP_QLVL_THRS		8

#define NFP_QMSTAT_SYM_NAME	"_abi_nfdqm%u_stats"
#define NFP_QMSTAT_STRIDE	32
#define NFP_QMSTAT_NON_STO	0
#define NFP_QMSTAT_STO		8
#define NFP_QMSTAT_DROP		16
#define NFP_QMSTAT_ECN		24

static unsigned long long
nfp_abm_q_lvl_thrs(struct nfp_abm_link *alink, unsigned int queue)
{
	return alink->abm->q_lvls->addr +
		(alink->queue_base + queue) * NFP_QLVL_STRIDE + NFP_QLVL_THRS;
}

static int
nfp_abm_ctrl_stat(struct nfp_abm_link *alink, const struct nfp_rtsym *sym,
		  unsigned int stride, unsigned int offset, unsigned int i,
		  bool is_u64, u64 *res)
{
	struct nfp_cpp *cpp = alink->abm->app->cpp;
	u32 val32, mur;
	u64 val, addr;
	int err;

	mur = NFP_CPP_ATOMIC_RD(sym->target, sym->domain);

	addr = sym->addr + (alink->queue_base + i) * stride + offset;
	if (is_u64)
		err = nfp_cpp_readq(cpp, mur, addr, &val);
	else
		err = nfp_cpp_readl(cpp, mur, addr, &val32);
	if (err) {
		nfp_err(cpp,
			"RED offload reading stat failed on vNIC %d queue %d\n",
			alink->id, i);
		return err;
	}

	*res = is_u64 ? val : val32;
	return 0;
}

static int
nfp_abm_ctrl_stat_all(struct nfp_abm_link *alink, const struct nfp_rtsym *sym,
		      unsigned int stride, unsigned int offset, bool is_u64,
		      u64 *res)
{
	u64 val, sum = 0;
	unsigned int i;
	int err;

	for (i = 0; i < alink->vnic->max_rx_rings; i++) {
		err = nfp_abm_ctrl_stat(alink, sym, stride, offset, i,
					is_u64, &val);
		if (err)
			return err;
		sum += val;
	}

	*res = sum;
	return 0;
}

int nfp_abm_ctrl_set_q_lvl(struct nfp_abm_link *alink, unsigned int i, u32 val)
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

u64 nfp_abm_ctrl_stat_non_sto(struct nfp_abm_link *alink, unsigned int i)
{
	u64 val;

	if (nfp_abm_ctrl_stat(alink, alink->abm->qm_stats, NFP_QMSTAT_STRIDE,
			      NFP_QMSTAT_NON_STO, i, true, &val))
		return 0;
	return val;
}

u64 nfp_abm_ctrl_stat_sto(struct nfp_abm_link *alink, unsigned int i)
{
	u64 val;

	if (nfp_abm_ctrl_stat(alink, alink->abm->qm_stats, NFP_QMSTAT_STRIDE,
			      NFP_QMSTAT_STO, i, true, &val))
		return 0;
	return val;
}

int nfp_abm_ctrl_read_q_stats(struct nfp_abm_link *alink, unsigned int i,
			      struct nfp_alink_stats *stats)
{
	int err;

	stats->tx_pkts = nn_readq(alink->vnic, NFP_NET_CFG_RXR_STATS(i));
	stats->tx_bytes = nn_readq(alink->vnic, NFP_NET_CFG_RXR_STATS(i) + 8);

	err = nfp_abm_ctrl_stat(alink, alink->abm->q_lvls,
				NFP_QLVL_STRIDE, NFP_QLVL_BLOG_BYTES,
				i, false, &stats->backlog_bytes);
	if (err)
		return err;

	err = nfp_abm_ctrl_stat(alink, alink->abm->q_lvls,
				NFP_QLVL_STRIDE, NFP_QLVL_BLOG_PKTS,
				i, false, &stats->backlog_pkts);
	if (err)
		return err;

	err = nfp_abm_ctrl_stat(alink, alink->abm->qm_stats,
				NFP_QMSTAT_STRIDE, NFP_QMSTAT_DROP,
				i, true, &stats->drops);
	if (err)
		return err;

	return nfp_abm_ctrl_stat(alink, alink->abm->qm_stats,
				 NFP_QMSTAT_STRIDE, NFP_QMSTAT_ECN,
				 i, true, &stats->overlimits);
}

int nfp_abm_ctrl_read_stats(struct nfp_abm_link *alink,
			    struct nfp_alink_stats *stats)
{
	u64 pkts = 0, bytes = 0;
	int i, err;

	for (i = 0; i < alink->vnic->max_rx_rings; i++) {
		pkts += nn_readq(alink->vnic, NFP_NET_CFG_RXR_STATS(i));
		bytes += nn_readq(alink->vnic, NFP_NET_CFG_RXR_STATS(i) + 8);
	}
	stats->tx_pkts = pkts;
	stats->tx_bytes = bytes;

	err = nfp_abm_ctrl_stat_all(alink, alink->abm->q_lvls,
				    NFP_QLVL_STRIDE, NFP_QLVL_BLOG_BYTES,
				    false, &stats->backlog_bytes);
	if (err)
		return err;

	err = nfp_abm_ctrl_stat_all(alink, alink->abm->q_lvls,
				    NFP_QLVL_STRIDE, NFP_QLVL_BLOG_PKTS,
				    false, &stats->backlog_pkts);
	if (err)
		return err;

	err = nfp_abm_ctrl_stat_all(alink, alink->abm->qm_stats,
				    NFP_QMSTAT_STRIDE, NFP_QMSTAT_DROP,
				    true, &stats->drops);
	if (err)
		return err;

	return nfp_abm_ctrl_stat_all(alink, alink->abm->qm_stats,
				     NFP_QMSTAT_STRIDE, NFP_QMSTAT_ECN,
				     true, &stats->overlimits);
}

int nfp_abm_ctrl_read_q_xstats(struct nfp_abm_link *alink, unsigned int i,
			       struct nfp_alink_xstats *xstats)
{
	int err;

	err = nfp_abm_ctrl_stat(alink, alink->abm->qm_stats,
				NFP_QMSTAT_STRIDE, NFP_QMSTAT_DROP,
				i, true, &xstats->pdrop);
	if (err)
		return err;

	return nfp_abm_ctrl_stat(alink, alink->abm->qm_stats,
				 NFP_QMSTAT_STRIDE, NFP_QMSTAT_ECN,
				 i, true, &xstats->ecn_marked);
}

int nfp_abm_ctrl_read_xstats(struct nfp_abm_link *alink,
			     struct nfp_alink_xstats *xstats)
{
	int err;

	err = nfp_abm_ctrl_stat_all(alink, alink->abm->qm_stats,
				    NFP_QMSTAT_STRIDE, NFP_QMSTAT_DROP,
				    true, &xstats->pdrop);
	if (err)
		return err;

	return nfp_abm_ctrl_stat_all(alink, alink->abm->qm_stats,
				     NFP_QMSTAT_STRIDE, NFP_QMSTAT_ECN,
				     true, &xstats->ecn_marked);
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

	snprintf(pf_symbol, sizeof(pf_symbol), NFP_QMSTAT_SYM_NAME, pf_id);
	sym = nfp_abm_ctrl_find_q_rtsym(pf, pf_symbol, NFP_QMSTAT_STRIDE);
	if (IS_ERR(sym))
		return PTR_ERR(sym);
	abm->qm_stats = sym;

	return 0;
}
