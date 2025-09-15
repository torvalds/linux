// SPDX-License-Identifier: GPL-2.0-only

#include <linux/device/devres.h>
#include <linux/netlink.h>
#include <linux/sprintf.h>
#include <linux/types.h>
#include <net/devlink.h>

#include "core.h"
#include "devlink.h"
#include "dpll.h"
#include "regs.h"

/**
 * zl3073x_devlink_info_get - Devlink device info callback
 * @devlink: devlink structure pointer
 * @req: devlink request pointer to store information
 * @extack: netlink extack pointer to report errors
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_devlink_info_get(struct devlink *devlink, struct devlink_info_req *req,
			 struct netlink_ext_ack *extack)
{
	struct zl3073x_dev *zldev = devlink_priv(devlink);
	u16 id, revision, fw_ver;
	char buf[16];
	u32 cfg_ver;
	int rc;

	rc = zl3073x_read_u16(zldev, ZL_REG_ID, &id);
	if (rc)
		return rc;

	snprintf(buf, sizeof(buf), "%X", id);
	rc = devlink_info_version_fixed_put(req,
					    DEVLINK_INFO_VERSION_GENERIC_ASIC_ID,
					    buf);
	if (rc)
		return rc;

	rc = zl3073x_read_u16(zldev, ZL_REG_REVISION, &revision);
	if (rc)
		return rc;

	snprintf(buf, sizeof(buf), "%X", revision);
	rc = devlink_info_version_fixed_put(req,
					    DEVLINK_INFO_VERSION_GENERIC_ASIC_REV,
					    buf);
	if (rc)
		return rc;

	rc = zl3073x_read_u16(zldev, ZL_REG_FW_VER, &fw_ver);
	if (rc)
		return rc;

	snprintf(buf, sizeof(buf), "%u", fw_ver);
	rc = devlink_info_version_running_put(req,
					      DEVLINK_INFO_VERSION_GENERIC_FW,
					      buf);
	if (rc)
		return rc;

	rc = zl3073x_read_u32(zldev, ZL_REG_CUSTOM_CONFIG_VER, &cfg_ver);
	if (rc)
		return rc;

	/* No custom config version */
	if (cfg_ver == U32_MAX)
		return 0;

	snprintf(buf, sizeof(buf), "%lu.%lu.%lu.%lu",
		 FIELD_GET(GENMASK(31, 24), cfg_ver),
		 FIELD_GET(GENMASK(23, 16), cfg_ver),
		 FIELD_GET(GENMASK(15, 8), cfg_ver),
		 FIELD_GET(GENMASK(7, 0), cfg_ver));

	return devlink_info_version_running_put(req, "custom_cfg", buf);
}

static int
zl3073x_devlink_reload_down(struct devlink *devlink, bool netns_change,
			    enum devlink_reload_action action,
			    enum devlink_reload_limit limit,
			    struct netlink_ext_ack *extack)
{
	struct zl3073x_dev *zldev = devlink_priv(devlink);
	struct zl3073x_dpll *zldpll;

	if (action != DEVLINK_RELOAD_ACTION_DRIVER_REINIT)
		return -EOPNOTSUPP;

	/* Unregister all DPLLs */
	list_for_each_entry(zldpll, &zldev->dplls, list)
		zl3073x_dpll_unregister(zldpll);

	return 0;
}

static int
zl3073x_devlink_reload_up(struct devlink *devlink,
			  enum devlink_reload_action action,
			  enum devlink_reload_limit limit,
			  u32 *actions_performed,
			  struct netlink_ext_ack *extack)
{
	struct zl3073x_dev *zldev = devlink_priv(devlink);
	union devlink_param_value val;
	struct zl3073x_dpll *zldpll;
	int rc;

	if (action != DEVLINK_RELOAD_ACTION_DRIVER_REINIT)
		return -EOPNOTSUPP;

	rc = devl_param_driverinit_value_get(devlink,
					     DEVLINK_PARAM_GENERIC_ID_CLOCK_ID,
					     &val);
	if (rc)
		return rc;

	if (zldev->clock_id != val.vu64) {
		dev_dbg(zldev->dev,
			"'clock_id' changed to %016llx\n", val.vu64);
		zldev->clock_id = val.vu64;
	}

	/* Re-register all DPLLs */
	list_for_each_entry(zldpll, &zldev->dplls, list) {
		rc = zl3073x_dpll_register(zldpll);
		if (rc)
			dev_warn(zldev->dev,
				 "Failed to re-register DPLL%u\n", zldpll->id);
	}

	*actions_performed = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT);

	return 0;
}

static const struct devlink_ops zl3073x_devlink_ops = {
	.info_get = zl3073x_devlink_info_get,
	.reload_actions = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT),
	.reload_down = zl3073x_devlink_reload_down,
	.reload_up = zl3073x_devlink_reload_up,
};

static void
zl3073x_devlink_free(void *ptr)
{
	devlink_free(ptr);
}

/**
 * zl3073x_devm_alloc - allocates zl3073x device structure
 * @dev: pointer to device structure
 *
 * Allocates zl3073x device structure as device resource.
 *
 * Return: pointer to zl3073x device on success, error pointer on error
 */
struct zl3073x_dev *zl3073x_devm_alloc(struct device *dev)
{
	struct zl3073x_dev *zldev;
	struct devlink *devlink;
	int rc;

	devlink = devlink_alloc(&zl3073x_devlink_ops, sizeof(*zldev), dev);
	if (!devlink)
		return ERR_PTR(-ENOMEM);

	/* Add devres action to free devlink device */
	rc = devm_add_action_or_reset(dev, zl3073x_devlink_free, devlink);
	if (rc)
		return ERR_PTR(rc);

	zldev = devlink_priv(devlink);
	zldev->dev = dev;
	dev_set_drvdata(zldev->dev, zldev);

	return zldev;
}
EXPORT_SYMBOL_NS_GPL(zl3073x_devm_alloc, "ZL3073X");

static int
zl3073x_devlink_param_clock_id_validate(struct devlink *devlink, u32 id,
					union devlink_param_value val,
					struct netlink_ext_ack *extack)
{
	if (!val.vu64) {
		NL_SET_ERR_MSG_MOD(extack, "'clock_id' must be non-zero");
		return -EINVAL;
	}

	return 0;
}

static const struct devlink_param zl3073x_devlink_params[] = {
	DEVLINK_PARAM_GENERIC(CLOCK_ID, BIT(DEVLINK_PARAM_CMODE_DRIVERINIT),
			      NULL, NULL,
			      zl3073x_devlink_param_clock_id_validate),
};

static void
zl3073x_devlink_unregister(void *ptr)
{
	struct devlink *devlink = priv_to_devlink(ptr);

	devl_lock(devlink);

	/* Unregister devlink params */
	devl_params_unregister(devlink, zl3073x_devlink_params,
			       ARRAY_SIZE(zl3073x_devlink_params));

	/* Unregister devlink instance */
	devl_unregister(devlink);

	devl_unlock(devlink);
}

/**
 * zl3073x_devlink_register - register devlink instance and params
 * @zldev: zl3073x device to register the devlink for
 *
 * Register the devlink instance and parameters associated with the device.
 *
 * Return: 0 on success, <0 on error
 */
int zl3073x_devlink_register(struct zl3073x_dev *zldev)
{
	struct devlink *devlink = priv_to_devlink(zldev);
	union devlink_param_value value;
	int rc;

	devl_lock(devlink);

	/* Register devlink params */
	rc = devl_params_register(devlink, zl3073x_devlink_params,
				  ARRAY_SIZE(zl3073x_devlink_params));
	if (rc) {
		devl_unlock(devlink);

		return rc;
	}

	value.vu64 = zldev->clock_id;
	devl_param_driverinit_value_set(devlink,
					DEVLINK_PARAM_GENERIC_ID_CLOCK_ID,
					value);

	/* Register devlink instance */
	devl_register(devlink);

	devl_unlock(devlink);

	/* Add devres action to unregister devlink device */
	return devm_add_action_or_reset(zldev->dev, zl3073x_devlink_unregister,
					zldev);
}
