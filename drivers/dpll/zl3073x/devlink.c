// SPDX-License-Identifier: GPL-2.0-only

#include <linux/device/devres.h>
#include <linux/netlink.h>
#include <linux/sprintf.h>
#include <linux/types.h>
#include <net/devlink.h>

#include "core.h"
#include "devlink.h"
#include "dpll.h"
#include "flash.h"
#include "fw.h"
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

	if (action != DEVLINK_RELOAD_ACTION_DRIVER_REINIT)
		return -EOPNOTSUPP;

	/* Stop normal operation */
	zl3073x_dev_stop(zldev);

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

	/* Restart normal operation */
	rc = zl3073x_dev_start(zldev, false);
	if (rc)
		dev_warn(zldev->dev, "Failed to re-start normal operation\n");

	*actions_performed = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT);

	return 0;
}

void zl3073x_devlink_flash_notify(struct zl3073x_dev *zldev, const char *msg,
				  const char *component, u32 done, u32 total)
{
	struct devlink *devlink = priv_to_devlink(zldev);

	devlink_flash_update_status_notify(devlink, msg, component, done,
					   total);
}

/**
 * zl3073x_devlink_flash_prepare - Prepare and enter flash mode
 * @zldev: zl3073x device pointer
 * @zlfw: pointer to loaded firmware
 * @extack: netlink extack pointer to report errors
 *
 * The function stops normal operation and switches the device to flash mode.
 * If an error occurs the normal operation is resumed.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_devlink_flash_prepare(struct zl3073x_dev *zldev,
			      struct zl3073x_fw *zlfw,
			      struct netlink_ext_ack *extack)
{
	struct zl3073x_fw_component *util;
	int rc;

	util = zlfw->component[ZL_FW_COMPONENT_UTIL];
	if (!util) {
		zl3073x_devlink_flash_notify(zldev,
					     "Utility is missing in firmware",
					     NULL, 0, 0);
		return -ENOEXEC;
	}

	/* Stop normal operation prior entering flash mode */
	zl3073x_dev_stop(zldev);

	rc = zl3073x_flash_mode_enter(zldev, util->data, util->size, extack);
	if (rc) {
		zl3073x_devlink_flash_notify(zldev,
					     "Failed to enter flash mode",
					     NULL, 0, 0);

		/* Resume normal operation */
		zl3073x_dev_start(zldev, true);

		return rc;
	}

	return 0;
}

/**
 * zl3073x_devlink_flash_finish - Leave flash mode and resume normal operation
 * @zldev: zl3073x device pointer
 * @extack: netlink extack pointer to report errors
 *
 * The function switches the device back to standard mode and resumes normal
 * operation.
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_devlink_flash_finish(struct zl3073x_dev *zldev,
			     struct netlink_ext_ack *extack)
{
	int rc;

	/* Reset device CPU to normal mode */
	zl3073x_flash_mode_leave(zldev, extack);

	/* Resume normal operation */
	rc = zl3073x_dev_start(zldev, true);
	if (rc)
		zl3073x_devlink_flash_notify(zldev,
					     "Failed to start normal operation",
					     NULL, 0, 0);

	return rc;
}

/**
 * zl3073x_devlink_flash_update - Devlink flash update callback
 * @devlink: devlink structure pointer
 * @params: flashing parameters pointer
 * @extack: netlink extack pointer to report errors
 *
 * Return: 0 on success, <0 on error
 */
static int
zl3073x_devlink_flash_update(struct devlink *devlink,
			     struct devlink_flash_update_params *params,
			     struct netlink_ext_ack *extack)
{
	struct zl3073x_dev *zldev = devlink_priv(devlink);
	struct zl3073x_fw *zlfw;
	int rc = 0;

	zlfw = zl3073x_fw_load(zldev, params->fw->data, params->fw->size,
			       extack);
	if (IS_ERR(zlfw)) {
		zl3073x_devlink_flash_notify(zldev, "Failed to load firmware",
					     NULL, 0, 0);
		rc = PTR_ERR(zlfw);
		goto finish;
	}

	/* Stop normal operation and enter flash mode */
	rc = zl3073x_devlink_flash_prepare(zldev, zlfw, extack);
	if (rc)
		goto finish;

	rc = zl3073x_fw_flash(zldev, zlfw, extack);
	if (rc) {
		zl3073x_devlink_flash_finish(zldev, extack);
		goto finish;
	}

	/* Resume normal mode */
	rc = zl3073x_devlink_flash_finish(zldev, extack);

finish:
	if (!IS_ERR(zlfw))
		zl3073x_fw_free(zlfw);

	zl3073x_devlink_flash_notify(zldev,
				     rc ? "Flashing failed" : "Flashing done",
				     NULL, 0, 0);

	return rc;
}

static const struct devlink_ops zl3073x_devlink_ops = {
	.info_get = zl3073x_devlink_info_get,
	.reload_actions = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT),
	.reload_down = zl3073x_devlink_reload_down,
	.reload_up = zl3073x_devlink_reload_up,
	.flash_update = zl3073x_devlink_flash_update,
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
