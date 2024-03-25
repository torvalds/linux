// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024, Intel Corporation. */

#include <linux/vmalloc.h>

#include "ice.h"
#include "devlink.h"

static int ice_active_port_option = -1;

/**
 * ice_devlink_port_opt_speed_str - convert speed to a string
 * @speed: speed value
 */
static const char *ice_devlink_port_opt_speed_str(u8 speed)
{
	switch (speed & ICE_AQC_PORT_OPT_MAX_LANE_M) {
	case ICE_AQC_PORT_OPT_MAX_LANE_100M:
		return "0.1";
	case ICE_AQC_PORT_OPT_MAX_LANE_1G:
		return "1";
	case ICE_AQC_PORT_OPT_MAX_LANE_2500M:
		return "2.5";
	case ICE_AQC_PORT_OPT_MAX_LANE_5G:
		return "5";
	case ICE_AQC_PORT_OPT_MAX_LANE_10G:
		return "10";
	case ICE_AQC_PORT_OPT_MAX_LANE_25G:
		return "25";
	case ICE_AQC_PORT_OPT_MAX_LANE_50G:
		return "50";
	case ICE_AQC_PORT_OPT_MAX_LANE_100G:
		return "100";
	}

	return "-";
}

#define ICE_PORT_OPT_DESC_LEN	50
/**
 * ice_devlink_port_options_print - Print available port split options
 * @pf: the PF to print split port options
 *
 * Prints a table with available port split options and max port speeds
 */
static void ice_devlink_port_options_print(struct ice_pf *pf)
{
	u8 i, j, options_count, cnt, speed, pending_idx, active_idx;
	struct ice_aqc_get_port_options_elem *options, *opt;
	struct device *dev = ice_pf_to_dev(pf);
	bool active_valid, pending_valid;
	char desc[ICE_PORT_OPT_DESC_LEN];
	const char *str;
	int status;

	options = kcalloc(ICE_AQC_PORT_OPT_MAX * ICE_MAX_PORT_PER_PCI_DEV,
			  sizeof(*options), GFP_KERNEL);
	if (!options)
		return;

	for (i = 0; i < ICE_MAX_PORT_PER_PCI_DEV; i++) {
		opt = options + i * ICE_AQC_PORT_OPT_MAX;
		options_count = ICE_AQC_PORT_OPT_MAX;
		active_valid = 0;

		status = ice_aq_get_port_options(&pf->hw, opt, &options_count,
						 i, true, &active_idx,
						 &active_valid, &pending_idx,
						 &pending_valid);
		if (status) {
			dev_dbg(dev, "Couldn't read port option for port %d, err %d\n",
				i, status);
			goto err;
		}
	}

	dev_dbg(dev, "Available port split options and max port speeds (Gbps):\n");
	dev_dbg(dev, "Status  Split      Quad 0          Quad 1\n");
	dev_dbg(dev, "        count  L0  L1  L2  L3  L4  L5  L6  L7\n");

	for (i = 0; i < options_count; i++) {
		cnt = 0;

		if (i == ice_active_port_option)
			str = "Active";
		else if ((i == pending_idx) && pending_valid)
			str = "Pending";
		else
			str = "";

		cnt += snprintf(&desc[cnt], ICE_PORT_OPT_DESC_LEN - cnt,
				"%-8s", str);

		cnt += snprintf(&desc[cnt], ICE_PORT_OPT_DESC_LEN - cnt,
				"%-6u", options[i].pmd);

		for (j = 0; j < ICE_MAX_PORT_PER_PCI_DEV; ++j) {
			speed = options[i + j * ICE_AQC_PORT_OPT_MAX].max_lane_speed;
			str = ice_devlink_port_opt_speed_str(speed);
			cnt += snprintf(&desc[cnt], ICE_PORT_OPT_DESC_LEN - cnt,
					"%3s ", str);
		}

		dev_dbg(dev, "%s\n", desc);
	}

err:
	kfree(options);
}

/**
 * ice_devlink_aq_set_port_option - Send set port option admin queue command
 * @pf: the PF to print split port options
 * @option_idx: selected port option
 * @extack: extended netdev ack structure
 *
 * Sends set port option admin queue command with selected port option and
 * calls NVM write activate.
 */
static int
ice_devlink_aq_set_port_option(struct ice_pf *pf, u8 option_idx,
			       struct netlink_ext_ack *extack)
{
	struct device *dev = ice_pf_to_dev(pf);
	int status;

	status = ice_aq_set_port_option(&pf->hw, 0, true, option_idx);
	if (status) {
		dev_dbg(dev, "ice_aq_set_port_option, err %d aq_err %d\n",
			status, pf->hw.adminq.sq_last_status);
		NL_SET_ERR_MSG_MOD(extack, "Port split request failed");
		return -EIO;
	}

	status = ice_acquire_nvm(&pf->hw, ICE_RES_WRITE);
	if (status) {
		dev_dbg(dev, "ice_acquire_nvm failed, err %d aq_err %d\n",
			status, pf->hw.adminq.sq_last_status);
		NL_SET_ERR_MSG_MOD(extack, "Failed to acquire NVM semaphore");
		return -EIO;
	}

	status = ice_nvm_write_activate(&pf->hw, ICE_AQC_NVM_ACTIV_REQ_EMPR, NULL);
	if (status) {
		dev_dbg(dev, "ice_nvm_write_activate failed, err %d aq_err %d\n",
			status, pf->hw.adminq.sq_last_status);
		NL_SET_ERR_MSG_MOD(extack, "Port split request failed to save data");
		ice_release_nvm(&pf->hw);
		return -EIO;
	}

	ice_release_nvm(&pf->hw);

	NL_SET_ERR_MSG_MOD(extack, "Reboot required to finish port split");
	return 0;
}

/**
 * ice_devlink_port_split - .port_split devlink handler
 * @devlink: devlink instance structure
 * @port: devlink port structure
 * @count: number of ports to split to
 * @extack: extended netdev ack structure
 *
 * Callback for the devlink .port_split operation.
 *
 * Unfortunately, the devlink expression of available options is limited
 * to just a number, so search for an FW port option which supports
 * the specified number. As there could be multiple FW port options with
 * the same port split count, allow switching between them. When the same
 * port split count request is issued again, switch to the next FW port
 * option with the same port split count.
 *
 * Return: zero on success or an error code on failure.
 */
static int
ice_devlink_port_split(struct devlink *devlink, struct devlink_port *port,
		       unsigned int count, struct netlink_ext_ack *extack)
{
	struct ice_aqc_get_port_options_elem options[ICE_AQC_PORT_OPT_MAX];
	u8 i, j, active_idx, pending_idx, new_option;
	struct ice_pf *pf = devlink_priv(devlink);
	u8 option_count = ICE_AQC_PORT_OPT_MAX;
	struct device *dev = ice_pf_to_dev(pf);
	bool active_valid, pending_valid;
	int status;

	status = ice_aq_get_port_options(&pf->hw, options, &option_count,
					 0, true, &active_idx, &active_valid,
					 &pending_idx, &pending_valid);
	if (status) {
		dev_dbg(dev, "Couldn't read port split options, err = %d\n",
			status);
		NL_SET_ERR_MSG_MOD(extack, "Failed to get available port split options");
		return -EIO;
	}

	new_option = ICE_AQC_PORT_OPT_MAX;
	active_idx = pending_valid ? pending_idx : active_idx;
	for (i = 1; i <= option_count; i++) {
		/* In order to allow switching between FW port options with
		 * the same port split count, search for a new option starting
		 * from the active/pending option (with array wrap around).
		 */
		j = (active_idx + i) % option_count;

		if (count == options[j].pmd) {
			new_option = j;
			break;
		}
	}

	if (new_option == active_idx) {
		dev_dbg(dev, "request to split: count: %u is already set and there are no other options\n",
			count);
		NL_SET_ERR_MSG_MOD(extack, "Requested split count is already set");
		ice_devlink_port_options_print(pf);
		return -EINVAL;
	}

	if (new_option == ICE_AQC_PORT_OPT_MAX) {
		dev_dbg(dev, "request to split: count: %u not found\n", count);
		NL_SET_ERR_MSG_MOD(extack, "Port split requested unsupported port config");
		ice_devlink_port_options_print(pf);
		return -EINVAL;
	}

	status = ice_devlink_aq_set_port_option(pf, new_option, extack);
	if (status)
		return status;

	ice_devlink_port_options_print(pf);

	return 0;
}

/**
 * ice_devlink_port_unsplit - .port_unsplit devlink handler
 * @devlink: devlink instance structure
 * @port: devlink port structure
 * @extack: extended netdev ack structure
 *
 * Callback for the devlink .port_unsplit operation.
 * Calls ice_devlink_port_split with split count set to 1.
 * There could be no FW option available with split count 1.
 *
 * Return: zero on success or an error code on failure.
 */
static int
ice_devlink_port_unsplit(struct devlink *devlink, struct devlink_port *port,
			 struct netlink_ext_ack *extack)
{
	return ice_devlink_port_split(devlink, port, 1, extack);
}

/**
 * ice_devlink_set_port_split_options - Set port split options
 * @pf: the PF to set port split options
 * @attrs: devlink attributes
 *
 * Sets devlink port split options based on available FW port options
 */
static void
ice_devlink_set_port_split_options(struct ice_pf *pf,
				   struct devlink_port_attrs *attrs)
{
	struct ice_aqc_get_port_options_elem options[ICE_AQC_PORT_OPT_MAX];
	u8 i, active_idx, pending_idx, option_count = ICE_AQC_PORT_OPT_MAX;
	bool active_valid, pending_valid;
	int status;

	status = ice_aq_get_port_options(&pf->hw, options, &option_count,
					 0, true, &active_idx, &active_valid,
					 &pending_idx, &pending_valid);
	if (status) {
		dev_dbg(ice_pf_to_dev(pf), "Couldn't read port split options, err = %d\n",
			status);
		return;
	}

	/* find the biggest available port split count */
	for (i = 0; i < option_count; i++)
		attrs->lanes = max_t(int, attrs->lanes, options[i].pmd);

	attrs->splittable = attrs->lanes ? 1 : 0;
	ice_active_port_option = active_idx;
}

static const struct devlink_port_ops ice_devlink_port_ops = {
	.port_split = ice_devlink_port_split,
	.port_unsplit = ice_devlink_port_unsplit,
};

/**
 * ice_devlink_set_switch_id - Set unique switch id based on pci dsn
 * @pf: the PF to create a devlink port for
 * @ppid: struct with switch id information
 */
static void
ice_devlink_set_switch_id(struct ice_pf *pf, struct netdev_phys_item_id *ppid)
{
	struct pci_dev *pdev = pf->pdev;
	u64 id;

	id = pci_get_dsn(pdev);

	ppid->id_len = sizeof(id);
	put_unaligned_be64(id, &ppid->id);
}

/**
 * ice_devlink_create_pf_port - Create a devlink port for this PF
 * @pf: the PF to create a devlink port for
 *
 * Create and register a devlink_port for this PF.
 * This function has to be called under devl_lock.
 *
 * Return: zero on success or an error code on failure.
 */
int ice_devlink_create_pf_port(struct ice_pf *pf)
{
	struct devlink_port_attrs attrs = {};
	struct devlink_port *devlink_port;
	struct devlink *devlink;
	struct ice_vsi *vsi;
	struct device *dev;
	int err;

	devlink = priv_to_devlink(pf);

	dev = ice_pf_to_dev(pf);

	devlink_port = &pf->devlink_port;

	vsi = ice_get_main_vsi(pf);
	if (!vsi)
		return -EIO;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	attrs.phys.port_number = pf->hw.bus.func;

	/* As FW supports only port split options for whole device,
	 * set port split options only for first PF.
	 */
	if (pf->hw.pf_id == 0)
		ice_devlink_set_port_split_options(pf, &attrs);

	ice_devlink_set_switch_id(pf, &attrs.switch_id);

	devlink_port_attrs_set(devlink_port, &attrs);

	err = devl_port_register_with_ops(devlink, devlink_port, vsi->idx,
					  &ice_devlink_port_ops);
	if (err) {
		dev_err(dev, "Failed to create devlink port for PF %d, error %d\n",
			pf->hw.pf_id, err);
		return err;
	}

	return 0;
}

/**
 * ice_devlink_destroy_pf_port - Destroy the devlink_port for this PF
 * @pf: the PF to cleanup
 *
 * Unregisters the devlink_port structure associated with this PF.
 * This function has to be called under devl_lock.
 */
void ice_devlink_destroy_pf_port(struct ice_pf *pf)
{
	devl_port_unregister(&pf->devlink_port);
}

/**
 * ice_devlink_create_vf_port - Create a devlink port for this VF
 * @vf: the VF to create a port for
 *
 * Create and register a devlink_port for this VF.
 *
 * Return: zero on success or an error code on failure.
 */
int ice_devlink_create_vf_port(struct ice_vf *vf)
{
	struct devlink_port_attrs attrs = {};
	struct devlink_port *devlink_port;
	struct devlink *devlink;
	struct ice_vsi *vsi;
	struct device *dev;
	struct ice_pf *pf;
	int err;

	pf = vf->pf;
	dev = ice_pf_to_dev(pf);
	devlink_port = &vf->devlink_port;

	vsi = ice_get_vf_vsi(vf);
	if (!vsi)
		return -EINVAL;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PCI_VF;
	attrs.pci_vf.pf = pf->hw.bus.func;
	attrs.pci_vf.vf = vf->vf_id;

	ice_devlink_set_switch_id(pf, &attrs.switch_id);

	devlink_port_attrs_set(devlink_port, &attrs);
	devlink = priv_to_devlink(pf);

	err = devlink_port_register(devlink, devlink_port, vsi->idx);
	if (err) {
		dev_err(dev, "Failed to create devlink port for VF %d, error %d\n",
			vf->vf_id, err);
		return err;
	}

	return 0;
}

/**
 * ice_devlink_destroy_vf_port - Destroy the devlink_port for this VF
 * @vf: the VF to cleanup
 *
 * Unregisters the devlink_port structure associated with this VF.
 */
void ice_devlink_destroy_vf_port(struct ice_vf *vf)
{
	devl_rate_leaf_destroy(&vf->devlink_port);
	devlink_port_unregister(&vf->devlink_port);
}
