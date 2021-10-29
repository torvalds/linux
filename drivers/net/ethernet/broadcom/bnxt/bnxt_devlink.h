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
	bool remote_reset;
};

static inline struct bnxt *bnxt_get_bp_from_dl(struct devlink *dl)
{
	return ((struct bnxt_dl *)devlink_priv(dl))->bp;
}

static inline void bnxt_dl_remote_reload(struct bnxt *bp)
{
	devlink_remote_reload_actions_performed(bp->dl, 0,
						BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT) |
						BIT(DEVLINK_RELOAD_ACTION_FW_ACTIVATE));
}

static inline bool bnxt_dl_get_remote_reset(struct devlink *dl)
{
	return ((struct bnxt_dl *)devlink_priv(dl))->remote_reset;
}

static inline void bnxt_dl_set_remote_reset(struct devlink *dl, bool value)
{
	((struct bnxt_dl *)devlink_priv(dl))->remote_reset = value;
}

#define NVM_OFF_MSIX_VEC_PER_PF_MAX	108
#define NVM_OFF_MSIX_VEC_PER_PF_MIN	114
#define NVM_OFF_IGNORE_ARI		164
#define NVM_OFF_DIS_GRE_VER_CHECK	171
#define NVM_OFF_ENABLE_SRIOV		401
#define NVM_OFF_NVM_CFG_VER		602

#define BNXT_NVM_CFG_VER_BITS		8
#define BNXT_NVM_CFG_VER_BYTES		1

#define BNXT_MSIX_VEC_MAX	512
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
	u16 nvm_num_bits;
	u8 dl_num_bytes;
};

enum bnxt_dl_version_type {
	BNXT_VERSION_FIXED,
	BNXT_VERSION_RUNNING,
	BNXT_VERSION_STORED,
};

void bnxt_devlink_health_fw_report(struct bnxt *bp);
void bnxt_dl_health_fw_status_update(struct bnxt *bp, bool healthy);
void bnxt_dl_health_fw_recovery_done(struct bnxt *bp);
void bnxt_dl_fw_reporters_create(struct bnxt *bp);
void bnxt_dl_fw_reporters_destroy(struct bnxt *bp, bool all);
int bnxt_dl_register(struct bnxt *bp);
void bnxt_dl_unregister(struct bnxt *bp);

#endif /* BNXT_DEVLINK_H */
