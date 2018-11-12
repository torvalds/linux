// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc. */

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

static int
nfp_abm_ctrl_stat(struct nfp_abm_link *alink, const struct nfp_rtsym *sym,
		  unsigned int stride, unsigned int offset, unsigned int i,
		  bool is_u64, u64 *res)
{
	struct nfp_cpp *cpp = alink->abm->app->cpp;
	u64 val, sym_offset;
	u32 val32;
	int err;

	sym_offset = (alink->queue_base + i) * stride + offset;
	if (is_u64)
		err = __nfp_rtsym_readq(cpp, sym, 3, 0, sym_offset, &val);
	else
		err = __nfp_rtsym_readl(cpp, sym, 3, 0, sym_offset, &val32);
	if (err) {
		nfp_err(cpp,
			"RED offload reading stat failed on vNIC %d queue %d\n",
			alink->id, i);
		return err;
	}

	*res = is_u64 ? val : val32;
	return 0;
}

int __nfp_abm_ctrl_set_q_lvl(struct nfp_abm *abm, unsigned int id, u32 val)
{
	struct nfp_cpp *cpp = abm->app->cpp;
	u64 sym_offset;
	int err;

	__clear_bit(id, abm->threshold_undef);
	if (abm->thresholds[id] == val)
		return 0;

	sym_offset = id * NFP_QLVL_STRIDE + NFP_QLVL_THRS;
	err = __nfp_rtsym_writel(cpp, abm->q_lvls, 4, 0, sym_offset, val);
	if (err) {
		nfp_err(cpp,
			"RED offload setting level failed on subqueue %d\n",
			id);
		return err;
	}

	abm->thresholds[id] = val;
	return 0;
}

int nfp_abm_ctrl_set_q_lvl(struct nfp_abm_link *alink, unsigned int queue,
			   u32 val)
{
	unsigned int threshold;

	threshold = alink->queue_base + queue;

	return __nfp_abm_ctrl_set_q_lvl(alink->abm, threshold, val);
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
	if (nfp_rtsym_size(sym) != size) {
		nfp_err(pf->cpp,
			"Symbol '%s' wrong size: expected %u got %llu\n",
			name, size, nfp_rtsym_size(sym));
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
