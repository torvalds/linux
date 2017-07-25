/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2016-2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_VFR_H
#define BNXT_VFR_H

#ifdef CONFIG_BNXT_SRIOV

#define	MAX_CFA_CODE			65536

/* Struct to hold housekeeping info needed by devlink interface */
struct bnxt_dl {
	struct bnxt *bp;	/* back ptr to the controlling dev */
};

static inline struct bnxt *bnxt_get_bp_from_dl(struct devlink *dl)
{
	return ((struct bnxt_dl *)devlink_priv(dl))->bp;
}

static inline void bnxt_link_bp_to_dl(struct devlink *dl, struct bnxt *bp)
{
	struct bnxt_dl *bp_dl = devlink_priv(dl);

	bp_dl->bp = bp;
	if (bp)
		bp->dl = dl;
}

int bnxt_dl_register(struct bnxt *bp);
void bnxt_dl_unregister(struct bnxt *bp);
void bnxt_vf_reps_destroy(struct bnxt *bp);
void bnxt_vf_reps_close(struct bnxt *bp);
void bnxt_vf_reps_open(struct bnxt *bp);
void bnxt_vf_rep_rx(struct bnxt *bp, struct sk_buff *skb);
struct net_device *bnxt_get_vf_rep(struct bnxt *bp, u16 cfa_code);

#else

static inline int bnxt_dl_register(struct bnxt *bp)
{
	return 0;
}

static inline void bnxt_dl_unregister(struct bnxt *bp)
{
}

static inline void bnxt_vf_reps_close(struct bnxt *bp)
{
}

static inline void bnxt_vf_reps_open(struct bnxt *bp)
{
}

static inline void bnxt_vf_rep_rx(struct bnxt *bp, struct sk_buff *skb)
{
}

static inline struct net_device *bnxt_get_vf_rep(struct bnxt *bp, u16 cfa_code)
{
	return NULL;
}
#endif /* CONFIG_BNXT_SRIOV */
#endif /* BNXT_VFR_H */
