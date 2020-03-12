// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"
#include "ice_devlink.h"

static int ice_info_get_dsn(struct ice_pf *pf, char *buf, size_t len)
{
	u8 dsn[8];

	/* Copy the DSN into an array in Big Endian format */
	put_unaligned_be64(pci_get_dsn(pf->pdev), dsn);

	snprintf(buf, len, "%02x-%02x-%02x-%02x-%02x-%02x-%02x-%02x",
		 dsn[0], dsn[1], dsn[2], dsn[3],
		 dsn[4], dsn[5], dsn[6], dsn[7]);

	return 0;
}

static int ice_info_fw_mgmt(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(buf, len, "%u.%u.%u", hw->fw_maj_ver, hw->fw_min_ver,
		 hw->fw_patch);

	return 0;
}

static int ice_info_fw_api(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(buf, len, "%u.%u", hw->api_maj_ver, hw->api_min_ver);

	return 0;
}

static int ice_info_fw_build(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(buf, len, "0x%08x", hw->fw_build);

	return 0;
}

static int ice_info_orom_ver(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_orom_info *orom = &pf->hw.nvm.orom;

	snprintf(buf, len, "%u.%u.%u", orom->major, orom->build, orom->patch);

	return 0;
}

static int ice_info_nvm_ver(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_nvm_info *nvm = &pf->hw.nvm;

	snprintf(buf, len, "%x.%02x", nvm->major_ver, nvm->minor_ver);

	return 0;
}

static int ice_info_eetrack(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_nvm_info *nvm = &pf->hw.nvm;

	snprintf(buf, len, "0x%08x", nvm->eetrack);

	return 0;
}

static int ice_info_ddp_pkg_name(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(buf, len, "%s", hw->active_pkg_name);

	return 0;
}

static int ice_info_ddp_pkg_version(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_pkg_ver *pkg = &pf->hw.active_pkg_ver;

	snprintf(buf, len, "%u.%u.%u.%u", pkg->major, pkg->minor, pkg->update,
		 pkg->draft);

	return 0;
}

#define running(key, getter) { ICE_VERSION_RUNNING, key, getter }

enum ice_version_type {
	ICE_VERSION_FIXED,
	ICE_VERSION_RUNNING,
	ICE_VERSION_STORED,
};

static const struct ice_devlink_version {
	enum ice_version_type type;
	const char *key;
	int (*getter)(struct ice_pf *pf, char *buf, size_t len);
} ice_devlink_versions[] = {
	running(DEVLINK_INFO_VERSION_GENERIC_FW_MGMT, ice_info_fw_mgmt),
	running("fw.mgmt.api", ice_info_fw_api),
	running("fw.mgmt.build", ice_info_fw_build),
	running(DEVLINK_INFO_VERSION_GENERIC_FW_UNDI, ice_info_orom_ver),
	running("fw.psid.api", ice_info_nvm_ver),
	running(DEVLINK_INFO_VERSION_GENERIC_FW_BUNDLE_ID, ice_info_eetrack),
	running("fw.app.name", ice_info_ddp_pkg_name),
	running(DEVLINK_INFO_VERSION_GENERIC_FW_APP, ice_info_ddp_pkg_version),
};

/**
 * ice_devlink_info_get - .info_get devlink handler
 * @devlink: devlink instance structure
 * @req: the devlink info request
 * @extack: extended netdev ack structure
 *
 * Callback for the devlink .info_get operation. Reports information about the
 * device.
 *
 * @returns zero on success or an error code on failure.
 */
static int ice_devlink_info_get(struct devlink *devlink,
				struct devlink_info_req *req,
				struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_priv(devlink);
	char buf[100];
	size_t i;
	int err;

	err = devlink_info_driver_name_put(req, KBUILD_MODNAME);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Unable to set driver name");
		return err;
	}

	err = ice_info_get_dsn(pf, buf, sizeof(buf));
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Unable to obtain serial number");
		return err;
	}

	err = devlink_info_serial_number_put(req, buf);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Unable to set serial number");
		return err;
	}

	for (i = 0; i < ARRAY_SIZE(ice_devlink_versions); i++) {
		enum ice_version_type type = ice_devlink_versions[i].type;
		const char *key = ice_devlink_versions[i].key;

		err = ice_devlink_versions[i].getter(pf, buf, sizeof(buf));
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Unable to obtain version info");
			return err;
		}

		switch (type) {
		case ICE_VERSION_FIXED:
			err = devlink_info_version_fixed_put(req, key, buf);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Unable to set fixed version");
				return err;
			}
			break;
		case ICE_VERSION_RUNNING:
			err = devlink_info_version_running_put(req, key, buf);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Unable to set running version");
				return err;
			}
			break;
		case ICE_VERSION_STORED:
			err = devlink_info_version_stored_put(req, key, buf);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Unable to set stored version");
				return err;
			}
			break;
		}
	}

	return 0;
}

static const struct devlink_ops ice_devlink_ops = {
	.info_get = ice_devlink_info_get,
};

static void ice_devlink_free(void *devlink_ptr)
{
	devlink_free((struct devlink *)devlink_ptr);
}

/**
 * ice_allocate_pf - Allocate devlink and return PF structure pointer
 * @dev: the device to allocate for
 *
 * Allocate a devlink instance for this device and return the private area as
 * the PF structure. The devlink memory is kept track of through devres by
 * adding an action to remove it when unwinding.
 */
struct ice_pf *ice_allocate_pf(struct device *dev)
{
	struct devlink *devlink;

	devlink = devlink_alloc(&ice_devlink_ops, sizeof(struct ice_pf));
	if (!devlink)
		return NULL;

	/* Add an action to teardown the devlink when unwinding the driver */
	if (devm_add_action(dev, ice_devlink_free, devlink)) {
		devlink_free(devlink);
		return NULL;
	}

	return devlink_priv(devlink);
}

/**
 * ice_devlink_register - Register devlink interface for this PF
 * @pf: the PF to register the devlink for.
 *
 * Register the devlink instance associated with this physical function.
 *
 * Return: zero on success or an error code on failure.
 */
int ice_devlink_register(struct ice_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct device *dev = ice_pf_to_dev(pf);
	int err;

	err = devlink_register(devlink, dev);
	if (err) {
		dev_err(dev, "devlink registration failed: %d\n", err);
		return err;
	}

	return 0;
}

/**
 * ice_devlink_unregister - Unregister devlink resources for this PF.
 * @pf: the PF structure to cleanup
 *
 * Releases resources used by devlink and cleans up associated memory.
 */
void ice_devlink_unregister(struct ice_pf *pf)
{
	devlink_unregister(priv_to_devlink(pf));
}

/**
 * ice_devlink_create_port - Create a devlink port for this PF
 * @pf: the PF to create a port for
 *
 * Create and register a devlink_port for this PF. Note that although each
 * physical function is connected to a separate devlink instance, the port
 * will still be numbered according to the physical function id.
 *
 * Return: zero on success or an error code on failure.
 */
int ice_devlink_create_port(struct ice_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct ice_vsi *vsi = ice_get_main_vsi(pf);
	struct device *dev = ice_pf_to_dev(pf);
	int err;

	if (!vsi) {
		dev_err(dev, "%s: unable to find main VSI\n", __func__);
		return -EIO;
	}

	devlink_port_attrs_set(&pf->devlink_port, DEVLINK_PORT_FLAVOUR_PHYSICAL,
			       pf->hw.pf_id, false, 0, NULL, 0);
	err = devlink_port_register(devlink, &pf->devlink_port, pf->hw.pf_id);
	if (err) {
		dev_err(dev, "devlink_port_register failed: %d\n", err);
		return err;
	}

	return 0;
}

/**
 * ice_devlink_destroy_port - Destroy the devlink_port for this PF
 * @pf: the PF to cleanup
 *
 * Unregisters the devlink_port structure associated with this PF.
 */
void ice_devlink_destroy_port(struct ice_pf *pf)
{
	devlink_port_type_clear(&pf->devlink_port);
	devlink_port_unregister(&pf->devlink_port);
}
