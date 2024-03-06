// SPDX-License-Identifier: GPL-2.0

#include <net/macsec.h>
#include "netdevsim.h"

static int nsim_macsec_find_secy(struct netdevsim *ns, sci_t sci)
{
	int i;

	for (i = 0; i < NSIM_MACSEC_MAX_SECY_COUNT; i++) {
		if (ns->macsec.nsim_secy[i].sci == sci)
			return i;
	}

	return -1;
}

static int nsim_macsec_find_rxsc(struct nsim_secy *ns_secy, sci_t sci)
{
	int i;

	for (i = 0; i < NSIM_MACSEC_MAX_RXSC_COUNT; i++) {
		if (ns_secy->nsim_rxsc[i].sci == sci)
			return i;
	}

	return -1;
}

static int nsim_macsec_add_secy(struct macsec_context *ctx)
{
	struct netdevsim *ns = netdev_priv(ctx->netdev);
	int idx;

	if (ns->macsec.nsim_secy_count == NSIM_MACSEC_MAX_SECY_COUNT)
		return -ENOSPC;

	for (idx = 0; idx < NSIM_MACSEC_MAX_SECY_COUNT; idx++) {
		if (!ns->macsec.nsim_secy[idx].used)
			break;
	}

	if (idx == NSIM_MACSEC_MAX_SECY_COUNT) {
		netdev_err(ctx->netdev, "%s: nsim_secy_count not full but all SecYs used\n",
			   __func__);
		return -ENOSPC;
	}

	netdev_dbg(ctx->netdev, "%s: adding new secy with sci %08llx at index %d\n",
		   __func__, sci_to_cpu(ctx->secy->sci), idx);
	ns->macsec.nsim_secy[idx].used = true;
	ns->macsec.nsim_secy[idx].nsim_rxsc_count = 0;
	ns->macsec.nsim_secy[idx].sci = ctx->secy->sci;
	ns->macsec.nsim_secy_count++;

	return 0;
}

static int nsim_macsec_upd_secy(struct macsec_context *ctx)
{
	struct netdevsim *ns = netdev_priv(ctx->netdev);
	int idx;

	idx = nsim_macsec_find_secy(ns, ctx->secy->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in secy table\n",
			   __func__, sci_to_cpu(ctx->secy->sci));
		return -ENOENT;
	}

	netdev_dbg(ctx->netdev, "%s: updating secy with sci %08llx at index %d\n",
		   __func__, sci_to_cpu(ctx->secy->sci), idx);

	return 0;
}

static int nsim_macsec_del_secy(struct macsec_context *ctx)
{
	struct netdevsim *ns = netdev_priv(ctx->netdev);
	int idx;

	idx = nsim_macsec_find_secy(ns, ctx->secy->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in secy table\n",
			   __func__, sci_to_cpu(ctx->secy->sci));
		return -ENOENT;
	}

	netdev_dbg(ctx->netdev, "%s: removing SecY with SCI %08llx at index %d\n",
		   __func__, sci_to_cpu(ctx->secy->sci), idx);

	ns->macsec.nsim_secy[idx].used = false;
	memset(&ns->macsec.nsim_secy[idx], 0, sizeof(ns->macsec.nsim_secy[idx]));
	ns->macsec.nsim_secy_count--;

	return 0;
}

static int nsim_macsec_add_rxsc(struct macsec_context *ctx)
{
	struct netdevsim *ns = netdev_priv(ctx->netdev);
	struct nsim_secy *secy;
	int idx;

	idx = nsim_macsec_find_secy(ns, ctx->secy->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in secy table\n",
			   __func__, sci_to_cpu(ctx->secy->sci));
		return -ENOENT;
	}
	secy = &ns->macsec.nsim_secy[idx];

	if (secy->nsim_rxsc_count == NSIM_MACSEC_MAX_RXSC_COUNT)
		return -ENOSPC;

	for (idx = 0; idx < NSIM_MACSEC_MAX_RXSC_COUNT; idx++) {
		if (!secy->nsim_rxsc[idx].used)
			break;
	}

	if (idx == NSIM_MACSEC_MAX_RXSC_COUNT)
		netdev_err(ctx->netdev, "%s: nsim_rxsc_count not full but all RXSCs used\n",
			   __func__);

	netdev_dbg(ctx->netdev, "%s: adding new rxsc with sci %08llx at index %d\n",
		   __func__, sci_to_cpu(ctx->rx_sc->sci), idx);
	secy->nsim_rxsc[idx].used = true;
	secy->nsim_rxsc[idx].sci = ctx->rx_sc->sci;
	secy->nsim_rxsc_count++;

	return 0;
}

static int nsim_macsec_upd_rxsc(struct macsec_context *ctx)
{
	struct netdevsim *ns = netdev_priv(ctx->netdev);
	struct nsim_secy *secy;
	int idx;

	idx = nsim_macsec_find_secy(ns, ctx->secy->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in secy table\n",
			   __func__, sci_to_cpu(ctx->secy->sci));
		return -ENOENT;
	}
	secy = &ns->macsec.nsim_secy[idx];

	idx = nsim_macsec_find_rxsc(secy, ctx->rx_sc->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in RXSC table\n",
			   __func__, sci_to_cpu(ctx->rx_sc->sci));
		return -ENOENT;
	}

	netdev_dbg(ctx->netdev, "%s: updating RXSC with sci %08llx at index %d\n",
		   __func__, sci_to_cpu(ctx->rx_sc->sci), idx);

	return 0;
}

static int nsim_macsec_del_rxsc(struct macsec_context *ctx)
{
	struct netdevsim *ns = netdev_priv(ctx->netdev);
	struct nsim_secy *secy;
	int idx;

	idx = nsim_macsec_find_secy(ns, ctx->secy->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in secy table\n",
			   __func__, sci_to_cpu(ctx->secy->sci));
		return -ENOENT;
	}
	secy = &ns->macsec.nsim_secy[idx];

	idx = nsim_macsec_find_rxsc(secy, ctx->rx_sc->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in RXSC table\n",
			   __func__, sci_to_cpu(ctx->rx_sc->sci));
		return -ENOENT;
	}

	netdev_dbg(ctx->netdev, "%s: removing RXSC with sci %08llx at index %d\n",
		   __func__, sci_to_cpu(ctx->rx_sc->sci), idx);

	secy->nsim_rxsc[idx].used = false;
	memset(&secy->nsim_rxsc[idx], 0, sizeof(secy->nsim_rxsc[idx]));
	secy->nsim_rxsc_count--;

	return 0;
}

static int nsim_macsec_add_rxsa(struct macsec_context *ctx)
{
	struct netdevsim *ns = netdev_priv(ctx->netdev);
	struct nsim_secy *secy;
	int idx;

	idx = nsim_macsec_find_secy(ns, ctx->secy->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in secy table\n",
			   __func__, sci_to_cpu(ctx->secy->sci));
		return -ENOENT;
	}
	secy = &ns->macsec.nsim_secy[idx];

	idx = nsim_macsec_find_rxsc(secy, ctx->sa.rx_sa->sc->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in RXSC table\n",
			   __func__, sci_to_cpu(ctx->sa.rx_sa->sc->sci));
		return -ENOENT;
	}

	netdev_dbg(ctx->netdev, "%s: RXSC with sci %08llx, AN %u\n",
		   __func__, sci_to_cpu(ctx->sa.rx_sa->sc->sci), ctx->sa.assoc_num);

	return 0;
}

static int nsim_macsec_upd_rxsa(struct macsec_context *ctx)
{
	struct netdevsim *ns = netdev_priv(ctx->netdev);
	struct nsim_secy *secy;
	int idx;

	idx = nsim_macsec_find_secy(ns, ctx->secy->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in secy table\n",
			   __func__, sci_to_cpu(ctx->secy->sci));
		return -ENOENT;
	}
	secy = &ns->macsec.nsim_secy[idx];

	idx = nsim_macsec_find_rxsc(secy, ctx->sa.rx_sa->sc->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in RXSC table\n",
			   __func__, sci_to_cpu(ctx->sa.rx_sa->sc->sci));
		return -ENOENT;
	}

	netdev_dbg(ctx->netdev, "%s: RXSC with sci %08llx, AN %u\n",
		   __func__, sci_to_cpu(ctx->sa.rx_sa->sc->sci), ctx->sa.assoc_num);

	return 0;
}

static int nsim_macsec_del_rxsa(struct macsec_context *ctx)
{
	struct netdevsim *ns = netdev_priv(ctx->netdev);
	struct nsim_secy *secy;
	int idx;

	idx = nsim_macsec_find_secy(ns, ctx->secy->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in secy table\n",
			   __func__, sci_to_cpu(ctx->secy->sci));
		return -ENOENT;
	}
	secy = &ns->macsec.nsim_secy[idx];

	idx = nsim_macsec_find_rxsc(secy, ctx->sa.rx_sa->sc->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in RXSC table\n",
			   __func__, sci_to_cpu(ctx->sa.rx_sa->sc->sci));
		return -ENOENT;
	}

	netdev_dbg(ctx->netdev, "%s: RXSC with sci %08llx, AN %u\n",
		   __func__, sci_to_cpu(ctx->sa.rx_sa->sc->sci), ctx->sa.assoc_num);

	return 0;
}

static int nsim_macsec_add_txsa(struct macsec_context *ctx)
{
	struct netdevsim *ns = netdev_priv(ctx->netdev);
	int idx;

	idx = nsim_macsec_find_secy(ns, ctx->secy->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in secy table\n",
			   __func__, sci_to_cpu(ctx->secy->sci));
		return -ENOENT;
	}

	netdev_dbg(ctx->netdev, "%s: SECY with sci %08llx, AN %u\n",
		   __func__, sci_to_cpu(ctx->secy->sci), ctx->sa.assoc_num);

	return 0;
}

static int nsim_macsec_upd_txsa(struct macsec_context *ctx)
{
	struct netdevsim *ns = netdev_priv(ctx->netdev);
	int idx;

	idx = nsim_macsec_find_secy(ns, ctx->secy->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in secy table\n",
			   __func__, sci_to_cpu(ctx->secy->sci));
		return -ENOENT;
	}

	netdev_dbg(ctx->netdev, "%s: SECY with sci %08llx, AN %u\n",
		   __func__, sci_to_cpu(ctx->secy->sci), ctx->sa.assoc_num);

	return 0;
}

static int nsim_macsec_del_txsa(struct macsec_context *ctx)
{
	struct netdevsim *ns = netdev_priv(ctx->netdev);
	int idx;

	idx = nsim_macsec_find_secy(ns, ctx->secy->sci);
	if (idx < 0) {
		netdev_err(ctx->netdev, "%s: sci %08llx not found in secy table\n",
			   __func__, sci_to_cpu(ctx->secy->sci));
		return -ENOENT;
	}

	netdev_dbg(ctx->netdev, "%s: SECY with sci %08llx, AN %u\n",
		   __func__, sci_to_cpu(ctx->secy->sci), ctx->sa.assoc_num);

	return 0;
}

static const struct macsec_ops nsim_macsec_ops = {
	.mdo_add_secy = nsim_macsec_add_secy,
	.mdo_upd_secy = nsim_macsec_upd_secy,
	.mdo_del_secy = nsim_macsec_del_secy,
	.mdo_add_rxsc = nsim_macsec_add_rxsc,
	.mdo_upd_rxsc = nsim_macsec_upd_rxsc,
	.mdo_del_rxsc = nsim_macsec_del_rxsc,
	.mdo_add_rxsa = nsim_macsec_add_rxsa,
	.mdo_upd_rxsa = nsim_macsec_upd_rxsa,
	.mdo_del_rxsa = nsim_macsec_del_rxsa,
	.mdo_add_txsa = nsim_macsec_add_txsa,
	.mdo_upd_txsa = nsim_macsec_upd_txsa,
	.mdo_del_txsa = nsim_macsec_del_txsa,
};

void nsim_macsec_init(struct netdevsim *ns)
{
	ns->netdev->macsec_ops = &nsim_macsec_ops;
	ns->netdev->features |= NETIF_F_HW_MACSEC;
	memset(&ns->macsec, 0, sizeof(ns->macsec));
}

void nsim_macsec_teardown(struct netdevsim *ns)
{
}
