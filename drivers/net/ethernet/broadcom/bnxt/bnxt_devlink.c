/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>
#include <net/devlink.h>
#include "bnxt_hsi.h"
#include "bnxt.h"
#include "bnxt_hwrm.h"
#include "bnxt_vfr.h"
#include "bnxt_devlink.h"
#include "bnxt_ethtool.h"
#include "bnxt_ulp.h"
#include "bnxt_ptp.h"
#include "bnxt_coredump.h"
#include "bnxt_nvm_defs.h"

static void __bnxt_fw_recover(struct bnxt *bp)
{
	if (test_bit(BNXT_STATE_FW_FATAL_COND, &bp->state) ||
	    test_bit(BNXT_STATE_FW_NON_FATAL_COND, &bp->state))
		bnxt_fw_reset(bp);
	else
		bnxt_fw_exception(bp);
}

static int
bnxt_dl_flash_update(struct devlink *dl,
		     struct devlink_flash_update_params *params,
		     struct netlink_ext_ack *extack)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);
	int rc;

	if (!BNXT_PF(bp)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "flash update not supported from a VF");
		return -EPERM;
	}

	devlink_flash_update_status_notify(dl, "Preparing to flash", NULL, 0, 0);
	rc = bnxt_flash_package_from_fw_obj(bp->dev, params->fw, 0, extack);
	if (!rc)
		devlink_flash_update_status_notify(dl, "Flashing done", NULL, 0, 0);
	else
		devlink_flash_update_status_notify(dl, "Flashing failed", NULL, 0, 0);
	return rc;
}

static int bnxt_hwrm_remote_dev_reset_set(struct bnxt *bp, bool remote_reset)
{
	struct hwrm_func_cfg_input *req;
	int rc;

	if (~bp->fw_cap & BNXT_FW_CAP_HOT_RESET_IF)
		return -EOPNOTSUPP;

	rc = bnxt_hwrm_func_cfg_short_req_init(bp, &req);
	if (rc)
		return rc;

	req->fid = cpu_to_le16(0xffff);
	req->enables = cpu_to_le32(FUNC_CFG_REQ_ENABLES_HOT_RESET_IF_SUPPORT);
	if (remote_reset)
		req->flags = cpu_to_le32(FUNC_CFG_REQ_FLAGS_HOT_RESET_IF_EN_DIS);

	return hwrm_req_send(bp, req);
}

static char *bnxt_health_severity_str(enum bnxt_health_severity severity)
{
	switch (severity) {
	case SEVERITY_NORMAL: return "normal";
	case SEVERITY_WARNING: return "warning";
	case SEVERITY_RECOVERABLE: return "recoverable";
	case SEVERITY_FATAL: return "fatal";
	default: return "unknown";
	}
}

static char *bnxt_health_remedy_str(enum bnxt_health_remedy remedy)
{
	switch (remedy) {
	case REMEDY_DEVLINK_RECOVER: return "devlink recover";
	case REMEDY_POWER_CYCLE_DEVICE: return "device power cycle";
	case REMEDY_POWER_CYCLE_HOST: return "host power cycle";
	case REMEDY_FW_UPDATE: return "update firmware";
	case REMEDY_HW_REPLACE: return "replace hardware";
	default: return "unknown";
	}
}

static int bnxt_fw_diagnose(struct devlink_health_reporter *reporter,
			    struct devlink_fmsg *fmsg,
			    struct netlink_ext_ack *extack)
{
	struct bnxt *bp = devlink_health_reporter_priv(reporter);
	struct bnxt_fw_health *h = bp->fw_health;
	u32 fw_status, fw_resets;

	if (test_bit(BNXT_STATE_IN_FW_RESET, &bp->state)) {
		devlink_fmsg_string_pair_put(fmsg, "Status", "recovering");
		return 0;
	}

	if (!h->status_reliable) {
		devlink_fmsg_string_pair_put(fmsg, "Status", "unknown");
		return 0;
	}

	mutex_lock(&h->lock);
	fw_status = bnxt_fw_health_readl(bp, BNXT_FW_HEALTH_REG);
	if (BNXT_FW_IS_BOOTING(fw_status)) {
		devlink_fmsg_string_pair_put(fmsg, "Status", "initializing");
	} else if (h->severity || fw_status != BNXT_FW_STATUS_HEALTHY) {
		if (!h->severity) {
			h->severity = SEVERITY_FATAL;
			h->remedy = REMEDY_POWER_CYCLE_DEVICE;
			h->diagnoses++;
			devlink_health_report(h->fw_reporter,
					      "FW error diagnosed", h);
		}
		devlink_fmsg_string_pair_put(fmsg, "Status", "error");
		devlink_fmsg_u32_pair_put(fmsg, "Syndrome", fw_status);
	} else {
		devlink_fmsg_string_pair_put(fmsg, "Status", "healthy");
	}

	devlink_fmsg_string_pair_put(fmsg, "Severity",
				     bnxt_health_severity_str(h->severity));

	if (h->severity) {
		devlink_fmsg_string_pair_put(fmsg, "Remedy",
					     bnxt_health_remedy_str(h->remedy));
		if (h->remedy == REMEDY_DEVLINK_RECOVER)
			devlink_fmsg_string_pair_put(fmsg, "Impact",
						     "traffic+ntuple_cfg");
	}

	mutex_unlock(&h->lock);
	if (!h->resets_reliable)
		return 0;

	fw_resets = bnxt_fw_health_readl(bp, BNXT_FW_RESET_CNT_REG);
	devlink_fmsg_u32_pair_put(fmsg, "Resets", fw_resets);
	devlink_fmsg_u32_pair_put(fmsg, "Arrests", h->arrests);
	devlink_fmsg_u32_pair_put(fmsg, "Survivals", h->survivals);
	devlink_fmsg_u32_pair_put(fmsg, "Discoveries", h->discoveries);
	devlink_fmsg_u32_pair_put(fmsg, "Fatalities", h->fatalities);
	devlink_fmsg_u32_pair_put(fmsg, "Diagnoses", h->diagnoses);
	return 0;
}

static int bnxt_fw_dump(struct devlink_health_reporter *reporter,
			struct devlink_fmsg *fmsg, void *priv_ctx,
			struct netlink_ext_ack *extack)
{
	struct bnxt *bp = devlink_health_reporter_priv(reporter);
	u32 dump_len;
	void *data;
	int rc;

	/* TODO: no firmware dump support in devlink_health_report() context */
	if (priv_ctx)
		return -EOPNOTSUPP;

	dump_len = bnxt_get_coredump_length(bp, BNXT_DUMP_LIVE);
	if (!dump_len)
		return -EIO;

	data = vmalloc(dump_len);
	if (!data)
		return -ENOMEM;

	rc = bnxt_get_coredump(bp, BNXT_DUMP_LIVE, data, &dump_len);
	if (!rc) {
		devlink_fmsg_pair_nest_start(fmsg, "core");
		devlink_fmsg_binary_pair_put(fmsg, "data", data, dump_len);
		devlink_fmsg_u32_pair_put(fmsg, "size", dump_len);
		devlink_fmsg_pair_nest_end(fmsg);
	}

	vfree(data);
	return rc;
}

static int bnxt_fw_recover(struct devlink_health_reporter *reporter,
			   void *priv_ctx,
			   struct netlink_ext_ack *extack)
{
	struct bnxt *bp = devlink_health_reporter_priv(reporter);

	if (bp->fw_health->severity == SEVERITY_FATAL)
		return -ENODEV;

	set_bit(BNXT_STATE_RECOVER, &bp->state);
	__bnxt_fw_recover(bp);

	return -EINPROGRESS;
}

static const struct devlink_health_reporter_ops bnxt_dl_fw_reporter_ops = {
	.name = "fw",
	.diagnose = bnxt_fw_diagnose,
	.dump = bnxt_fw_dump,
	.recover = bnxt_fw_recover,
};

static struct devlink_health_reporter *
__bnxt_dl_reporter_create(struct bnxt *bp,
			  const struct devlink_health_reporter_ops *ops)
{
	struct devlink_health_reporter *reporter;

	reporter = devlink_health_reporter_create(bp->dl, ops, 0, bp);
	if (IS_ERR(reporter)) {
		netdev_warn(bp->dev, "Failed to create %s health reporter, rc = %ld\n",
			    ops->name, PTR_ERR(reporter));
		return NULL;
	}

	return reporter;
}

void bnxt_dl_fw_reporters_create(struct bnxt *bp)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;

	if (fw_health && !fw_health->fw_reporter)
		fw_health->fw_reporter = __bnxt_dl_reporter_create(bp, &bnxt_dl_fw_reporter_ops);
}

void bnxt_dl_fw_reporters_destroy(struct bnxt *bp)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;

	if (fw_health && fw_health->fw_reporter) {
		devlink_health_reporter_destroy(fw_health->fw_reporter);
		fw_health->fw_reporter = NULL;
	}
}

void bnxt_devlink_health_fw_report(struct bnxt *bp)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	int rc;

	if (!fw_health)
		return;

	if (!fw_health->fw_reporter) {
		__bnxt_fw_recover(bp);
		return;
	}

	mutex_lock(&fw_health->lock);
	fw_health->severity = SEVERITY_RECOVERABLE;
	fw_health->remedy = REMEDY_DEVLINK_RECOVER;
	mutex_unlock(&fw_health->lock);
	rc = devlink_health_report(fw_health->fw_reporter, "FW error reported",
				   fw_health);
	if (rc == -ECANCELED)
		__bnxt_fw_recover(bp);
}

void bnxt_dl_health_fw_status_update(struct bnxt *bp, bool healthy)
{
	struct bnxt_fw_health *fw_health = bp->fw_health;
	u8 state;

	mutex_lock(&fw_health->lock);
	if (healthy) {
		fw_health->severity = SEVERITY_NORMAL;
		state = DEVLINK_HEALTH_REPORTER_STATE_HEALTHY;
	} else {
		fw_health->severity = SEVERITY_FATAL;
		fw_health->remedy = REMEDY_POWER_CYCLE_DEVICE;
		state = DEVLINK_HEALTH_REPORTER_STATE_ERROR;
	}
	mutex_unlock(&fw_health->lock);
	devlink_health_reporter_state_update(fw_health->fw_reporter, state);
}

void bnxt_dl_health_fw_recovery_done(struct bnxt *bp)
{
	struct bnxt_dl *dl = devlink_priv(bp->dl);

	devlink_health_reporter_recovery_done(bp->fw_health->fw_reporter);
	bnxt_hwrm_remote_dev_reset_set(bp, dl->remote_reset);
}

static int bnxt_dl_info_get(struct devlink *dl, struct devlink_info_req *req,
			    struct netlink_ext_ack *extack);

static void
bnxt_dl_livepatch_report_err(struct bnxt *bp, struct netlink_ext_ack *extack,
			     struct hwrm_fw_livepatch_output *resp)
{
	int err = ((struct hwrm_err_output *)resp)->cmd_err;

	switch (err) {
	case FW_LIVEPATCH_CMD_ERR_CODE_INVALID_OPCODE:
		netdev_err(bp->dev, "Illegal live patch opcode");
		NL_SET_ERR_MSG_MOD(extack, "Invalid opcode");
		break;
	case FW_LIVEPATCH_CMD_ERR_CODE_NOT_SUPPORTED:
		NL_SET_ERR_MSG_MOD(extack, "Live patch operation not supported");
		break;
	case FW_LIVEPATCH_CMD_ERR_CODE_NOT_INSTALLED:
		NL_SET_ERR_MSG_MOD(extack, "Live patch not found");
		break;
	case FW_LIVEPATCH_CMD_ERR_CODE_NOT_PATCHED:
		NL_SET_ERR_MSG_MOD(extack,
				   "Live patch deactivation failed. Firmware not patched.");
		break;
	case FW_LIVEPATCH_CMD_ERR_CODE_AUTH_FAIL:
		NL_SET_ERR_MSG_MOD(extack, "Live patch not authenticated");
		break;
	case FW_LIVEPATCH_CMD_ERR_CODE_INVALID_HEADER:
		NL_SET_ERR_MSG_MOD(extack, "Incompatible live patch");
		break;
	case FW_LIVEPATCH_CMD_ERR_CODE_INVALID_SIZE:
		NL_SET_ERR_MSG_MOD(extack, "Live patch has invalid size");
		break;
	case FW_LIVEPATCH_CMD_ERR_CODE_ALREADY_PATCHED:
		NL_SET_ERR_MSG_MOD(extack, "Live patch already applied");
		break;
	default:
		netdev_err(bp->dev, "Unexpected live patch error: %d\n", err);
		NL_SET_ERR_MSG_MOD(extack, "Failed to activate live patch");
		break;
	}
}

/* Live patch status in NVM */
#define BNXT_LIVEPATCH_NOT_INSTALLED	0
#define BNXT_LIVEPATCH_INSTALLED	FW_LIVEPATCH_QUERY_RESP_STATUS_FLAGS_INSTALL
#define BNXT_LIVEPATCH_REMOVED		FW_LIVEPATCH_QUERY_RESP_STATUS_FLAGS_ACTIVE
#define BNXT_LIVEPATCH_MASK		(FW_LIVEPATCH_QUERY_RESP_STATUS_FLAGS_INSTALL | \
					 FW_LIVEPATCH_QUERY_RESP_STATUS_FLAGS_ACTIVE)
#define BNXT_LIVEPATCH_ACTIVATED	BNXT_LIVEPATCH_MASK

#define BNXT_LIVEPATCH_STATE(flags)	((flags) & BNXT_LIVEPATCH_MASK)

static int
bnxt_dl_livepatch_activate(struct bnxt *bp, struct netlink_ext_ack *extack)
{
	struct hwrm_fw_livepatch_query_output *query_resp;
	struct hwrm_fw_livepatch_query_input *query_req;
	struct hwrm_fw_livepatch_output *patch_resp;
	struct hwrm_fw_livepatch_input *patch_req;
	u16 flags, live_patch_state;
	bool activated = false;
	u32 installed = 0;
	u8 target;
	int rc;

	if (~bp->fw_cap & BNXT_FW_CAP_LIVEPATCH) {
		NL_SET_ERR_MSG_MOD(extack, "Device does not support live patch");
		return -EOPNOTSUPP;
	}

	rc = hwrm_req_init(bp, query_req, HWRM_FW_LIVEPATCH_QUERY);
	if (rc)
		return rc;
	query_resp = hwrm_req_hold(bp, query_req);

	rc = hwrm_req_init(bp, patch_req, HWRM_FW_LIVEPATCH);
	if (rc) {
		hwrm_req_drop(bp, query_req);
		return rc;
	}
	patch_req->loadtype = FW_LIVEPATCH_REQ_LOADTYPE_NVM_INSTALL;
	patch_resp = hwrm_req_hold(bp, patch_req);

	for (target = 1; target <= FW_LIVEPATCH_REQ_FW_TARGET_LAST; target++) {
		query_req->fw_target = target;
		rc = hwrm_req_send(bp, query_req);
		if (rc) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to query packages");
			break;
		}

		flags = le16_to_cpu(query_resp->status_flags);
		live_patch_state = BNXT_LIVEPATCH_STATE(flags);

		if (live_patch_state == BNXT_LIVEPATCH_NOT_INSTALLED)
			continue;

		if (live_patch_state == BNXT_LIVEPATCH_ACTIVATED) {
			activated = true;
			continue;
		}

		if (live_patch_state == BNXT_LIVEPATCH_INSTALLED)
			patch_req->opcode = FW_LIVEPATCH_REQ_OPCODE_ACTIVATE;
		else if (live_patch_state == BNXT_LIVEPATCH_REMOVED)
			patch_req->opcode = FW_LIVEPATCH_REQ_OPCODE_DEACTIVATE;

		patch_req->fw_target = target;
		rc = hwrm_req_send(bp, patch_req);
		if (rc) {
			bnxt_dl_livepatch_report_err(bp, extack, patch_resp);
			break;
		}
		installed++;
	}

	if (!rc && !installed) {
		if (activated) {
			NL_SET_ERR_MSG_MOD(extack, "Live patch already activated");
			rc = -EEXIST;
		} else {
			NL_SET_ERR_MSG_MOD(extack, "No live patches found");
			rc = -ENOENT;
		}
	}
	hwrm_req_drop(bp, query_req);
	hwrm_req_drop(bp, patch_req);
	return rc;
}

static int bnxt_dl_reload_down(struct devlink *dl, bool netns_change,
			       enum devlink_reload_action action,
			       enum devlink_reload_limit limit,
			       struct netlink_ext_ack *extack)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);
	int rc = 0;

	switch (action) {
	case DEVLINK_RELOAD_ACTION_DRIVER_REINIT: {
		rtnl_lock();
		if (bnxt_sriov_cfg(bp)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "reload is unsupported while VFs are allocated or being configured");
			rtnl_unlock();
			return -EOPNOTSUPP;
		}
		if (bp->dev->reg_state == NETREG_UNREGISTERED) {
			rtnl_unlock();
			return -ENODEV;
		}
		bnxt_ulp_stop(bp);
		if (netif_running(bp->dev)) {
			rc = bnxt_close_nic(bp, true, true);
			if (rc) {
				NL_SET_ERR_MSG_MOD(extack, "Failed to close");
				dev_close(bp->dev);
				rtnl_unlock();
				break;
			}
		}
		bnxt_vf_reps_free(bp);
		rc = bnxt_hwrm_func_drv_unrgtr(bp);
		if (rc) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to deregister");
			if (netif_running(bp->dev))
				dev_close(bp->dev);
			rtnl_unlock();
			break;
		}
		bnxt_cancel_reservations(bp, false);
		bnxt_free_ctx_mem(bp);
		kfree(bp->ctx);
		bp->ctx = NULL;
		break;
	}
	case DEVLINK_RELOAD_ACTION_FW_ACTIVATE: {
		if (limit == DEVLINK_RELOAD_LIMIT_NO_RESET)
			return bnxt_dl_livepatch_activate(bp, extack);
		if (~bp->fw_cap & BNXT_FW_CAP_HOT_RESET) {
			NL_SET_ERR_MSG_MOD(extack, "Device not capable, requires reboot");
			return -EOPNOTSUPP;
		}
		if (!bnxt_hwrm_reset_permitted(bp)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Reset denied by firmware, it may be inhibited by remote driver");
			return -EPERM;
		}
		rtnl_lock();
		if (bp->dev->reg_state == NETREG_UNREGISTERED) {
			rtnl_unlock();
			return -ENODEV;
		}
		if (netif_running(bp->dev))
			set_bit(BNXT_STATE_FW_ACTIVATE, &bp->state);
		rc = bnxt_hwrm_firmware_reset(bp->dev,
					      FW_RESET_REQ_EMBEDDED_PROC_TYPE_CHIP,
					      FW_RESET_REQ_SELFRST_STATUS_SELFRSTASAP,
					      FW_RESET_REQ_FLAGS_RESET_GRACEFUL |
					      FW_RESET_REQ_FLAGS_FW_ACTIVATION);
		if (rc) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to activate firmware");
			clear_bit(BNXT_STATE_FW_ACTIVATE, &bp->state);
			rtnl_unlock();
		}
		break;
	}
	default:
		rc = -EOPNOTSUPP;
	}

	return rc;
}

static int bnxt_dl_reload_up(struct devlink *dl, enum devlink_reload_action action,
			     enum devlink_reload_limit limit, u32 *actions_performed,
			     struct netlink_ext_ack *extack)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);
	int rc = 0;

	*actions_performed = 0;
	switch (action) {
	case DEVLINK_RELOAD_ACTION_DRIVER_REINIT: {
		bnxt_fw_init_one(bp);
		bnxt_vf_reps_alloc(bp);
		if (netif_running(bp->dev))
			rc = bnxt_open_nic(bp, true, true);
		bnxt_ulp_start(bp, rc);
		if (!rc) {
			bnxt_reenable_sriov(bp);
			bnxt_ptp_reapply_pps(bp);
		}
		break;
	}
	case DEVLINK_RELOAD_ACTION_FW_ACTIVATE: {
		unsigned long start = jiffies;
		unsigned long timeout = start + BNXT_DFLT_FW_RST_MAX_DSECS * HZ / 10;

		if (limit == DEVLINK_RELOAD_LIMIT_NO_RESET)
			break;
		if (bp->fw_cap & BNXT_FW_CAP_ERROR_RECOVERY)
			timeout = start + bp->fw_health->normal_func_wait_dsecs * HZ / 10;
		if (!netif_running(bp->dev))
			NL_SET_ERR_MSG_MOD(extack,
					   "Device is closed, not waiting for reset notice that will never come");
		rtnl_unlock();
		while (test_bit(BNXT_STATE_FW_ACTIVATE, &bp->state)) {
			if (time_after(jiffies, timeout)) {
				NL_SET_ERR_MSG_MOD(extack, "Activation incomplete");
				rc = -ETIMEDOUT;
				break;
			}
			if (test_bit(BNXT_STATE_ABORT_ERR, &bp->state)) {
				NL_SET_ERR_MSG_MOD(extack, "Activation aborted");
				rc = -ENODEV;
				break;
			}
			msleep(50);
		}
		rtnl_lock();
		if (!rc)
			*actions_performed |= BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT);
		clear_bit(BNXT_STATE_FW_ACTIVATE, &bp->state);
		break;
	}
	default:
		return -EOPNOTSUPP;
	}

	if (!rc) {
		bnxt_print_device_info(bp);
		if (netif_running(bp->dev)) {
			mutex_lock(&bp->link_lock);
			bnxt_report_link(bp);
			mutex_unlock(&bp->link_lock);
		}
		*actions_performed |= BIT(action);
	} else if (netif_running(bp->dev)) {
		dev_close(bp->dev);
	}
	rtnl_unlock();
	return rc;
}

static bool bnxt_nvm_test(struct bnxt *bp, struct netlink_ext_ack *extack)
{
	bool rc = false;
	u32 datalen;
	u16 index;
	u8 *buf;

	if (bnxt_find_nvram_item(bp->dev, BNX_DIR_TYPE_VPD,
				 BNX_DIR_ORDINAL_FIRST, BNX_DIR_EXT_NONE,
				 &index, NULL, &datalen) || !datalen) {
		NL_SET_ERR_MSG_MOD(extack, "nvm test vpd entry error");
		return false;
	}

	buf = kzalloc(datalen, GFP_KERNEL);
	if (!buf) {
		NL_SET_ERR_MSG_MOD(extack, "insufficient memory for nvm test");
		return false;
	}

	if (bnxt_get_nvram_item(bp->dev, index, 0, datalen, buf)) {
		NL_SET_ERR_MSG_MOD(extack, "nvm test vpd read error");
		goto done;
	}

	if (bnxt_flash_nvram(bp->dev, BNX_DIR_TYPE_VPD, BNX_DIR_ORDINAL_FIRST,
			     BNX_DIR_EXT_NONE, 0, 0, buf, datalen)) {
		NL_SET_ERR_MSG_MOD(extack, "nvm test vpd write error");
		goto done;
	}

	rc = true;

done:
	kfree(buf);
	return rc;
}

static bool bnxt_dl_selftest_check(struct devlink *dl, unsigned int id,
				   struct netlink_ext_ack *extack)
{
	return id == DEVLINK_ATTR_SELFTEST_ID_FLASH;
}

static enum devlink_selftest_status bnxt_dl_selftest_run(struct devlink *dl,
							 unsigned int id,
							 struct netlink_ext_ack *extack)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);

	if (id == DEVLINK_ATTR_SELFTEST_ID_FLASH)
		return bnxt_nvm_test(bp, extack) ?
				DEVLINK_SELFTEST_STATUS_PASS :
				DEVLINK_SELFTEST_STATUS_FAIL;

	return DEVLINK_SELFTEST_STATUS_SKIP;
}

static const struct devlink_ops bnxt_dl_ops = {
#ifdef CONFIG_BNXT_SRIOV
	.eswitch_mode_set = bnxt_dl_eswitch_mode_set,
	.eswitch_mode_get = bnxt_dl_eswitch_mode_get,
#endif /* CONFIG_BNXT_SRIOV */
	.info_get	  = bnxt_dl_info_get,
	.flash_update	  = bnxt_dl_flash_update,
	.reload_actions	  = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT) |
			    BIT(DEVLINK_RELOAD_ACTION_FW_ACTIVATE),
	.reload_limits	  = BIT(DEVLINK_RELOAD_LIMIT_NO_RESET),
	.reload_down	  = bnxt_dl_reload_down,
	.reload_up	  = bnxt_dl_reload_up,
	.selftest_check	  = bnxt_dl_selftest_check,
	.selftest_run	  = bnxt_dl_selftest_run,
};

static const struct devlink_ops bnxt_vf_dl_ops;

enum bnxt_dl_param_id {
	BNXT_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	BNXT_DEVLINK_PARAM_ID_GRE_VER_CHECK,
};

static const struct bnxt_dl_nvm_param nvm_params[] = {
	{DEVLINK_PARAM_GENERIC_ID_ENABLE_SRIOV, NVM_OFF_ENABLE_SRIOV,
	 BNXT_NVM_SHARED_CFG, 1, 1},
	{DEVLINK_PARAM_GENERIC_ID_IGNORE_ARI, NVM_OFF_IGNORE_ARI,
	 BNXT_NVM_SHARED_CFG, 1, 1},
	{DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MAX,
	 NVM_OFF_MSIX_VEC_PER_PF_MAX, BNXT_NVM_SHARED_CFG, 10, 4},
	{DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MIN,
	 NVM_OFF_MSIX_VEC_PER_PF_MIN, BNXT_NVM_SHARED_CFG, 7, 4},
	{BNXT_DEVLINK_PARAM_ID_GRE_VER_CHECK, NVM_OFF_DIS_GRE_VER_CHECK,
	 BNXT_NVM_SHARED_CFG, 1, 1},
};

union bnxt_nvm_data {
	u8	val8;
	__le32	val32;
};

static void bnxt_copy_to_nvm_data(union bnxt_nvm_data *dst,
				  union devlink_param_value *src,
				  int nvm_num_bits, int dl_num_bytes)
{
	u32 val32 = 0;

	if (nvm_num_bits == 1) {
		dst->val8 = src->vbool;
		return;
	}
	if (dl_num_bytes == 4)
		val32 = src->vu32;
	else if (dl_num_bytes == 2)
		val32 = (u32)src->vu16;
	else if (dl_num_bytes == 1)
		val32 = (u32)src->vu8;
	dst->val32 = cpu_to_le32(val32);
}

static void bnxt_copy_from_nvm_data(union devlink_param_value *dst,
				    union bnxt_nvm_data *src,
				    int nvm_num_bits, int dl_num_bytes)
{
	u32 val32;

	if (nvm_num_bits == 1) {
		dst->vbool = src->val8;
		return;
	}
	val32 = le32_to_cpu(src->val32);
	if (dl_num_bytes == 4)
		dst->vu32 = val32;
	else if (dl_num_bytes == 2)
		dst->vu16 = (u16)val32;
	else if (dl_num_bytes == 1)
		dst->vu8 = (u8)val32;
}

static int bnxt_hwrm_get_nvm_cfg_ver(struct bnxt *bp, u32 *nvm_cfg_ver)
{
	struct hwrm_nvm_get_variable_input *req;
	u16 bytes = BNXT_NVM_CFG_VER_BYTES;
	u16 bits = BNXT_NVM_CFG_VER_BITS;
	union devlink_param_value ver;
	union bnxt_nvm_data *data;
	dma_addr_t data_dma_addr;
	int rc, i = 2;
	u16 dim = 1;

	rc = hwrm_req_init(bp, req, HWRM_NVM_GET_VARIABLE);
	if (rc)
		return rc;

	data = hwrm_req_dma_slice(bp, req, sizeof(*data), &data_dma_addr);
	if (!data) {
		rc = -ENOMEM;
		goto exit;
	}

	/* earlier devices present as an array of raw bytes */
	if (!BNXT_CHIP_P5(bp)) {
		dim = 0;
		i = 0;
		bits *= 3;  /* array of 3 version components */
		bytes *= 4; /* copy whole word */
	}

	hwrm_req_hold(bp, req);
	req->dest_data_addr = cpu_to_le64(data_dma_addr);
	req->data_len = cpu_to_le16(bits);
	req->option_num = cpu_to_le16(NVM_OFF_NVM_CFG_VER);
	req->dimensions = cpu_to_le16(dim);

	while (i >= 0) {
		req->index_0 = cpu_to_le16(i--);
		rc = hwrm_req_send_silent(bp, req);
		if (rc)
			goto exit;
		bnxt_copy_from_nvm_data(&ver, data, bits, bytes);

		if (BNXT_CHIP_P5(bp)) {
			*nvm_cfg_ver <<= 8;
			*nvm_cfg_ver |= ver.vu8;
		} else {
			*nvm_cfg_ver = ver.vu32;
		}
	}

exit:
	hwrm_req_drop(bp, req);
	return rc;
}

static int bnxt_dl_info_put(struct bnxt *bp, struct devlink_info_req *req,
			    enum bnxt_dl_version_type type, const char *key,
			    char *buf)
{
	if (!strlen(buf))
		return 0;

	if ((bp->flags & BNXT_FLAG_CHIP_P5) &&
	    (!strcmp(key, DEVLINK_INFO_VERSION_GENERIC_FW_NCSI) ||
	     !strcmp(key, DEVLINK_INFO_VERSION_GENERIC_FW_ROCE)))
		return 0;

	switch (type) {
	case BNXT_VERSION_FIXED:
		return devlink_info_version_fixed_put(req, key, buf);
	case BNXT_VERSION_RUNNING:
		return devlink_info_version_running_put(req, key, buf);
	case BNXT_VERSION_STORED:
		return devlink_info_version_stored_put(req, key, buf);
	}
	return 0;
}

#define BNXT_FW_SRT_PATCH	"fw.srt.patch"
#define BNXT_FW_CRT_PATCH	"fw.crt.patch"

static int bnxt_dl_livepatch_info_put(struct bnxt *bp,
				      struct devlink_info_req *req,
				      const char *key)
{
	struct hwrm_fw_livepatch_query_input *query;
	struct hwrm_fw_livepatch_query_output *resp;
	u16 flags;
	int rc;

	if (~bp->fw_cap & BNXT_FW_CAP_LIVEPATCH)
		return 0;

	rc = hwrm_req_init(bp, query, HWRM_FW_LIVEPATCH_QUERY);
	if (rc)
		return rc;

	if (!strcmp(key, BNXT_FW_SRT_PATCH))
		query->fw_target = FW_LIVEPATCH_QUERY_REQ_FW_TARGET_SECURE_FW;
	else if (!strcmp(key, BNXT_FW_CRT_PATCH))
		query->fw_target = FW_LIVEPATCH_QUERY_REQ_FW_TARGET_COMMON_FW;
	else
		goto exit;

	resp = hwrm_req_hold(bp, query);
	rc = hwrm_req_send(bp, query);
	if (rc)
		goto exit;

	flags = le16_to_cpu(resp->status_flags);
	if (flags & FW_LIVEPATCH_QUERY_RESP_STATUS_FLAGS_ACTIVE) {
		resp->active_ver[sizeof(resp->active_ver) - 1] = '\0';
		rc = devlink_info_version_running_put(req, key, resp->active_ver);
		if (rc)
			goto exit;
	}

	if (flags & FW_LIVEPATCH_QUERY_RESP_STATUS_FLAGS_INSTALL) {
		resp->install_ver[sizeof(resp->install_ver) - 1] = '\0';
		rc = devlink_info_version_stored_put(req, key, resp->install_ver);
		if (rc)
			goto exit;
	}

exit:
	hwrm_req_drop(bp, query);
	return rc;
}

#define HWRM_FW_VER_STR_LEN	16

static int bnxt_dl_info_get(struct devlink *dl, struct devlink_info_req *req,
			    struct netlink_ext_ack *extack)
{
	struct hwrm_nvm_get_dev_info_output nvm_dev_info;
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);
	struct hwrm_ver_get_output *ver_resp;
	char mgmt_ver[FW_VER_STR_LEN];
	char roce_ver[FW_VER_STR_LEN];
	char ncsi_ver[FW_VER_STR_LEN];
	char buf[32];
	u32 ver = 0;
	int rc;

	if (BNXT_PF(bp) && (bp->flags & BNXT_FLAG_DSN_VALID)) {
		sprintf(buf, "%02X-%02X-%02X-%02X-%02X-%02X-%02X-%02X",
			bp->dsn[7], bp->dsn[6], bp->dsn[5], bp->dsn[4],
			bp->dsn[3], bp->dsn[2], bp->dsn[1], bp->dsn[0]);
		rc = devlink_info_serial_number_put(req, buf);
		if (rc)
			return rc;
	}

	if (strlen(bp->board_serialno)) {
		rc = devlink_info_board_serial_number_put(req, bp->board_serialno);
		if (rc)
			return rc;
	}

	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_FIXED,
			      DEVLINK_INFO_VERSION_GENERIC_BOARD_ID,
			      bp->board_partno);
	if (rc)
		return rc;

	sprintf(buf, "%X", bp->chip_num);
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_FIXED,
			      DEVLINK_INFO_VERSION_GENERIC_ASIC_ID, buf);
	if (rc)
		return rc;

	ver_resp = &bp->ver_resp;
	sprintf(buf, "%c%d", 'A' + ver_resp->chip_rev, ver_resp->chip_metal);
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_FIXED,
			      DEVLINK_INFO_VERSION_GENERIC_ASIC_REV, buf);
	if (rc)
		return rc;

	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_PSID,
			      bp->nvm_cfg_ver);
	if (rc)
		return rc;

	buf[0] = 0;
	strncat(buf, ver_resp->active_pkg_name, HWRM_FW_VER_STR_LEN);
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW, buf);
	if (rc)
		return rc;

	if (BNXT_PF(bp) && !bnxt_hwrm_get_nvm_cfg_ver(bp, &ver)) {
		sprintf(buf, "%d.%d.%d", (ver >> 16) & 0xff, (ver >> 8) & 0xff,
			ver & 0xff);
		rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_STORED,
				      DEVLINK_INFO_VERSION_GENERIC_FW_PSID,
				      buf);
		if (rc)
			return rc;
	}

	if (ver_resp->flags & VER_GET_RESP_FLAGS_EXT_VER_AVAIL) {
		snprintf(mgmt_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
			 ver_resp->hwrm_fw_major, ver_resp->hwrm_fw_minor,
			 ver_resp->hwrm_fw_build, ver_resp->hwrm_fw_patch);

		snprintf(ncsi_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
			 ver_resp->mgmt_fw_major, ver_resp->mgmt_fw_minor,
			 ver_resp->mgmt_fw_build, ver_resp->mgmt_fw_patch);

		snprintf(roce_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
			 ver_resp->roce_fw_major, ver_resp->roce_fw_minor,
			 ver_resp->roce_fw_build, ver_resp->roce_fw_patch);
	} else {
		snprintf(mgmt_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
			 ver_resp->hwrm_fw_maj_8b, ver_resp->hwrm_fw_min_8b,
			 ver_resp->hwrm_fw_bld_8b, ver_resp->hwrm_fw_rsvd_8b);

		snprintf(ncsi_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
			 ver_resp->mgmt_fw_maj_8b, ver_resp->mgmt_fw_min_8b,
			 ver_resp->mgmt_fw_bld_8b, ver_resp->mgmt_fw_rsvd_8b);

		snprintf(roce_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
			 ver_resp->roce_fw_maj_8b, ver_resp->roce_fw_min_8b,
			 ver_resp->roce_fw_bld_8b, ver_resp->roce_fw_rsvd_8b);
	}
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_MGMT, mgmt_ver);
	if (rc)
		return rc;

	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_MGMT_API,
			      bp->hwrm_ver_supp);
	if (rc)
		return rc;

	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_NCSI, ncsi_ver);
	if (rc)
		return rc;

	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_RUNNING,
			      DEVLINK_INFO_VERSION_GENERIC_FW_ROCE, roce_ver);
	if (rc)
		return rc;

	rc = bnxt_hwrm_nvm_get_dev_info(bp, &nvm_dev_info);
	if (rc ||
	    !(nvm_dev_info.flags & NVM_GET_DEV_INFO_RESP_FLAGS_FW_VER_VALID)) {
		if (!bnxt_get_pkginfo(bp->dev, buf, sizeof(buf)))
			return bnxt_dl_info_put(bp, req, BNXT_VERSION_STORED,
						DEVLINK_INFO_VERSION_GENERIC_FW,
						buf);
		return 0;
	}

	buf[0] = 0;
	strncat(buf, nvm_dev_info.pkg_name, HWRM_FW_VER_STR_LEN);
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_STORED,
			      DEVLINK_INFO_VERSION_GENERIC_FW, buf);
	if (rc)
		return rc;

	snprintf(mgmt_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
		 nvm_dev_info.hwrm_fw_major, nvm_dev_info.hwrm_fw_minor,
		 nvm_dev_info.hwrm_fw_build, nvm_dev_info.hwrm_fw_patch);
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_STORED,
			      DEVLINK_INFO_VERSION_GENERIC_FW_MGMT, mgmt_ver);
	if (rc)
		return rc;

	snprintf(ncsi_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
		 nvm_dev_info.mgmt_fw_major, nvm_dev_info.mgmt_fw_minor,
		 nvm_dev_info.mgmt_fw_build, nvm_dev_info.mgmt_fw_patch);
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_STORED,
			      DEVLINK_INFO_VERSION_GENERIC_FW_NCSI, ncsi_ver);
	if (rc)
		return rc;

	snprintf(roce_ver, FW_VER_STR_LEN, "%d.%d.%d.%d",
		 nvm_dev_info.roce_fw_major, nvm_dev_info.roce_fw_minor,
		 nvm_dev_info.roce_fw_build, nvm_dev_info.roce_fw_patch);
	rc = bnxt_dl_info_put(bp, req, BNXT_VERSION_STORED,
			      DEVLINK_INFO_VERSION_GENERIC_FW_ROCE, roce_ver);
	if (rc)
		return rc;

	if (BNXT_CHIP_P5(bp)) {
		rc = bnxt_dl_livepatch_info_put(bp, req, BNXT_FW_SRT_PATCH);
		if (rc)
			return rc;
	}
	return bnxt_dl_livepatch_info_put(bp, req, BNXT_FW_CRT_PATCH);

}

static int bnxt_hwrm_nvm_req(struct bnxt *bp, u32 param_id, void *msg,
			     union devlink_param_value *val)
{
	struct hwrm_nvm_get_variable_input *req = msg;
	struct bnxt_dl_nvm_param nvm_param;
	struct hwrm_err_output *resp;
	union bnxt_nvm_data *data;
	dma_addr_t data_dma_addr;
	int idx = 0, rc, i;

	/* Get/Set NVM CFG parameter is supported only on PFs */
	if (BNXT_VF(bp)) {
		hwrm_req_drop(bp, req);
		return -EPERM;
	}

	for (i = 0; i < ARRAY_SIZE(nvm_params); i++) {
		if (nvm_params[i].id == param_id) {
			nvm_param = nvm_params[i];
			break;
		}
	}

	if (i == ARRAY_SIZE(nvm_params)) {
		hwrm_req_drop(bp, req);
		return -EOPNOTSUPP;
	}

	if (nvm_param.dir_type == BNXT_NVM_PORT_CFG)
		idx = bp->pf.port_id;
	else if (nvm_param.dir_type == BNXT_NVM_FUNC_CFG)
		idx = bp->pf.fw_fid - BNXT_FIRST_PF_FID;

	data = hwrm_req_dma_slice(bp, req, sizeof(*data), &data_dma_addr);

	if (!data) {
		hwrm_req_drop(bp, req);
		return -ENOMEM;
	}

	req->dest_data_addr = cpu_to_le64(data_dma_addr);
	req->data_len = cpu_to_le16(nvm_param.nvm_num_bits);
	req->option_num = cpu_to_le16(nvm_param.offset);
	req->index_0 = cpu_to_le16(idx);
	if (idx)
		req->dimensions = cpu_to_le16(1);

	resp = hwrm_req_hold(bp, req);
	if (req->req_type == cpu_to_le16(HWRM_NVM_SET_VARIABLE)) {
		bnxt_copy_to_nvm_data(data, val, nvm_param.nvm_num_bits,
				      nvm_param.dl_num_bytes);
		rc = hwrm_req_send(bp, msg);
	} else {
		rc = hwrm_req_send_silent(bp, msg);
		if (!rc) {
			bnxt_copy_from_nvm_data(val, data,
						nvm_param.nvm_num_bits,
						nvm_param.dl_num_bytes);
		} else {
			if (resp->cmd_err ==
				NVM_GET_VARIABLE_CMD_ERR_CODE_VAR_NOT_EXIST)
				rc = -EOPNOTSUPP;
		}
	}
	hwrm_req_drop(bp, req);
	if (rc == -EACCES)
		netdev_err(bp->dev, "PF does not have admin privileges to modify NVM config\n");
	return rc;
}

static int bnxt_dl_nvm_param_get(struct devlink *dl, u32 id,
				 struct devlink_param_gset_ctx *ctx)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);
	struct hwrm_nvm_get_variable_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_NVM_GET_VARIABLE);
	if (rc)
		return rc;

	rc = bnxt_hwrm_nvm_req(bp, id, req, &ctx->val);
	if (!rc && id == BNXT_DEVLINK_PARAM_ID_GRE_VER_CHECK)
		ctx->val.vbool = !ctx->val.vbool;

	return rc;
}

static int bnxt_dl_nvm_param_set(struct devlink *dl, u32 id,
				 struct devlink_param_gset_ctx *ctx)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);
	struct hwrm_nvm_set_variable_input *req;
	int rc;

	rc = hwrm_req_init(bp, req, HWRM_NVM_SET_VARIABLE);
	if (rc)
		return rc;

	if (id == BNXT_DEVLINK_PARAM_ID_GRE_VER_CHECK)
		ctx->val.vbool = !ctx->val.vbool;

	return bnxt_hwrm_nvm_req(bp, id, req, &ctx->val);
}

static int bnxt_dl_msix_validate(struct devlink *dl, u32 id,
				 union devlink_param_value val,
				 struct netlink_ext_ack *extack)
{
	int max_val = -1;

	if (id == DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MAX)
		max_val = BNXT_MSIX_VEC_MAX;

	if (id == DEVLINK_PARAM_GENERIC_ID_MSIX_VEC_PER_PF_MIN)
		max_val = BNXT_MSIX_VEC_MIN_MAX;

	if (val.vu32 > max_val) {
		NL_SET_ERR_MSG_MOD(extack, "MSIX value is exceeding the range");
		return -EINVAL;
	}

	return 0;
}

static int bnxt_remote_dev_reset_get(struct devlink *dl, u32 id,
				     struct devlink_param_gset_ctx *ctx)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);

	if (~bp->fw_cap & BNXT_FW_CAP_HOT_RESET_IF)
		return -EOPNOTSUPP;

	ctx->val.vbool = bnxt_dl_get_remote_reset(dl);
	return 0;
}

static int bnxt_remote_dev_reset_set(struct devlink *dl, u32 id,
				     struct devlink_param_gset_ctx *ctx)
{
	struct bnxt *bp = bnxt_get_bp_from_dl(dl);
	int rc;

	rc = bnxt_hwrm_remote_dev_reset_set(bp, ctx->val.vbool);
	if (rc)
		return rc;

	bnxt_dl_set_remote_reset(dl, ctx->val.vbool);
	return rc;
}

static const struct devlink_param bnxt_dl_params[] = {
	DEVLINK_PARAM_GENERIC(ENABLE_SRIOV,
			      BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			      bnxt_dl_nvm_param_get, bnxt_dl_nvm_param_set,
			      NULL),
	DEVLINK_PARAM_GENERIC(IGNORE_ARI,
			      BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			      bnxt_dl_nvm_param_get, bnxt_dl_nvm_param_set,
			      NULL),
	DEVLINK_PARAM_GENERIC(MSIX_VEC_PER_PF_MAX,
			      BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			      bnxt_dl_nvm_param_get, bnxt_dl_nvm_param_set,
			      bnxt_dl_msix_validate),
	DEVLINK_PARAM_GENERIC(MSIX_VEC_PER_PF_MIN,
			      BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			      bnxt_dl_nvm_param_get, bnxt_dl_nvm_param_set,
			      bnxt_dl_msix_validate),
	DEVLINK_PARAM_DRIVER(BNXT_DEVLINK_PARAM_ID_GRE_VER_CHECK,
			     "gre_ver_check", DEVLINK_PARAM_TYPE_BOOL,
			     BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			     bnxt_dl_nvm_param_get, bnxt_dl_nvm_param_set,
			     NULL),
	/* keep REMOTE_DEV_RESET last, it is excluded based on caps */
	DEVLINK_PARAM_GENERIC(ENABLE_REMOTE_DEV_RESET,
			      BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			      bnxt_remote_dev_reset_get,
			      bnxt_remote_dev_reset_set, NULL),
};

static int bnxt_dl_params_register(struct bnxt *bp)
{
	int num_params = ARRAY_SIZE(bnxt_dl_params);
	int rc;

	if (bp->hwrm_spec_code < 0x10600)
		return 0;

	if (~bp->fw_cap & BNXT_FW_CAP_HOT_RESET_IF)
		num_params--;

	rc = devlink_params_register(bp->dl, bnxt_dl_params, num_params);
	if (rc)
		netdev_warn(bp->dev, "devlink_params_register failed. rc=%d\n",
			    rc);
	return rc;
}

static void bnxt_dl_params_unregister(struct bnxt *bp)
{
	int num_params = ARRAY_SIZE(bnxt_dl_params);

	if (bp->hwrm_spec_code < 0x10600)
		return;

	if (~bp->fw_cap & BNXT_FW_CAP_HOT_RESET_IF)
		num_params--;

	devlink_params_unregister(bp->dl, bnxt_dl_params, num_params);
}

int bnxt_dl_register(struct bnxt *bp)
{
	const struct devlink_ops *devlink_ops;
	struct devlink_port_attrs attrs = {};
	struct bnxt_dl *bp_dl;
	struct devlink *dl;
	int rc;

	if (BNXT_PF(bp))
		devlink_ops = &bnxt_dl_ops;
	else
		devlink_ops = &bnxt_vf_dl_ops;

	dl = devlink_alloc(devlink_ops, sizeof(struct bnxt_dl), &bp->pdev->dev);
	if (!dl) {
		netdev_warn(bp->dev, "devlink_alloc failed\n");
		return -ENOMEM;
	}

	bp->dl = dl;
	bp_dl = devlink_priv(dl);
	bp_dl->bp = bp;
	bnxt_dl_set_remote_reset(dl, true);

	/* Add switchdev eswitch mode setting, if SRIOV supported */
	if (pci_find_ext_capability(bp->pdev, PCI_EXT_CAP_ID_SRIOV) &&
	    bp->hwrm_spec_code > 0x10803)
		bp->eswitch_mode = DEVLINK_ESWITCH_MODE_LEGACY;

	if (!BNXT_PF(bp))
		goto out;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	attrs.phys.port_number = bp->pf.port_id;
	memcpy(attrs.switch_id.id, bp->dsn, sizeof(bp->dsn));
	attrs.switch_id.id_len = sizeof(bp->dsn);
	devlink_port_attrs_set(&bp->dl_port, &attrs);
	rc = devlink_port_register(dl, &bp->dl_port, bp->pf.port_id);
	if (rc) {
		netdev_err(bp->dev, "devlink_port_register failed\n");
		goto err_dl_free;
	}

	rc = bnxt_dl_params_register(bp);
	if (rc)
		goto err_dl_port_unreg;

out:
	devlink_register(dl);
	return 0;

err_dl_port_unreg:
	devlink_port_unregister(&bp->dl_port);
err_dl_free:
	devlink_free(dl);
	return rc;
}

void bnxt_dl_unregister(struct bnxt *bp)
{
	struct devlink *dl = bp->dl;

	devlink_unregister(dl);
	if (BNXT_PF(bp)) {
		bnxt_dl_params_unregister(bp);
		devlink_port_unregister(&bp->dl_port);
	}
	devlink_free(dl);
}
