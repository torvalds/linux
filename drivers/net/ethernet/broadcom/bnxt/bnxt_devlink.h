/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_DEVLINK_H
#define BNXT_DEVLINK_H

/* Struct to hold housekeeping info needed by devlink interface */
struct bnxt_dl {
	struct bnxt *bp;	/* back ptr to the controlling dev */
};

static inline struct bnxt *bnxt_get_bp_from_dl(struct devlink *dl)
{
	return ((struct bnxt_dl *)devlink_priv(dl))->bp;
}

/* To clear devlink pointer from bp, pass NULL dl */
static inline void bnxt_link_bp_to_dl(struct bnxt *bp, struct devlink *dl)
{
	bp->dl = dl;

	/* add a back pointer in dl to bp */
	if (dl) {
		struct bnxt_dl *bp_dl = devlink_priv(dl);

		bp_dl->bp = bp;
	}
}

#define NVM_OFF_MSIX_VEC_PER_PF_MAX	108
#define NVM_OFF_MSIX_VEC_PER_PF_MIN	114
#define NVM_OFF_IGNORE_ARI		164
#define NVM_OFF_DIS_GRE_VER_CHECK	171
#define NVM_OFF_ENABLE_SRIOV		401

#define BNXT_MSIX_VEC_MAX	1280
#define BNXT_MSIX_VEC_MIN_MAX	128

enum bnxt_nvm_dir_type {
	BNXT_NVM_SHARED_CFG = 40,
	BNXT_NVM_PORT_CFG,
	BNXT_NVM_FUNC_CFG,
};

struct bnxt_dl_nvm_param {
	u16 id;
	u16 offset;
	u16 dir_type;
	u16 num_bits;
};

int bnxt_dl_register(struct bnxt *bp);
void bnxt_dl_unregister(struct bnxt *bp);

#endif /* BNXT_DEVLINK_H */
