// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024, Intel Corporation. */

#include <linux/vmalloc.h>

#include "ice.h"
#include "devlink.h"
#include "port.h"
#include "ice_lib.h"
#include "ice_fltr.h"

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
	attrs.phys.port_number = pf->hw.pf_id;

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
 * ice_devlink_port_get_vf_fn_mac - .port_fn_hw_addr_get devlink handler
 * @port: devlink port structure
 * @hw_addr: MAC address of the port
 * @hw_addr_len: length of MAC address
 * @extack: extended netdev ack structure
 *
 * Callback for the devlink .port_fn_hw_addr_get operation
 * Return: zero on success or an error code on failure.
 */
static int ice_devlink_port_get_vf_fn_mac(struct devlink_port *port,
					  u8 *hw_addr, int *hw_addr_len,
					  struct netlink_ext_ack *extack)
{
	struct ice_vf *vf = container_of(port, struct ice_vf, devlink_port);

	ether_addr_copy(hw_addr, vf->dev_lan_addr);
	*hw_addr_len = ETH_ALEN;

	return 0;
}

/**
 * ice_devlink_port_set_vf_fn_mac - .port_fn_hw_addr_set devlink handler
 * @port: devlink port structure
 * @hw_addr: MAC address of the port
 * @hw_addr_len: length of MAC address
 * @extack: extended netdev ack structure
 *
 * Callback for the devlink .port_fn_hw_addr_set operation
 * Return: zero on success or an error code on failure.
 */
static int ice_devlink_port_set_vf_fn_mac(struct devlink_port *port,
					  const u8 *hw_addr,
					  int hw_addr_len,
					  struct netlink_ext_ack *extack)

{
	struct devlink_port_attrs *attrs = &port->attrs;
	struct devlink_port_pci_vf_attrs *pci_vf;
	struct devlink *devlink = port->devlink;
	struct ice_pf *pf;
	u16 vf_id;

	pf = devlink_priv(devlink);
	pci_vf = &attrs->pci_vf;
	vf_id = pci_vf->vf;

	return __ice_set_vf_mac(pf, vf_id, hw_addr);
}

static const struct devlink_port_ops ice_devlink_vf_port_ops = {
	.port_fn_hw_addr_get = ice_devlink_port_get_vf_fn_mac,
	.port_fn_hw_addr_set = ice_devlink_port_set_vf_fn_mac,
};

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
	attrs.pci_vf.pf = pf->hw.pf_id;
	attrs.pci_vf.vf = vf->vf_id;

	ice_devlink_set_switch_id(pf, &attrs.switch_id);

	devlink_port_attrs_set(devlink_port, &attrs);
	devlink = priv_to_devlink(pf);

	err = devl_port_register_with_ops(devlink, devlink_port, vsi->idx,
					  &ice_devlink_vf_port_ops);
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
	devl_port_unregister(&vf->devlink_port);
}

/**
 * ice_devlink_create_sf_dev_port - Register virtual port for a subfunction
 * @sf_dev: the subfunction device to create a devlink port for
 *
 * Register virtual flavour devlink port for the subfunction auxiliary device
 * created after activating a dynamically added devlink port.
 *
 * Return: zero on success or an error code on failure.
 */
int ice_devlink_create_sf_dev_port(struct ice_sf_dev *sf_dev)
{
	struct devlink_port_attrs attrs = {};
	struct ice_dynamic_port *dyn_port;
	struct devlink_port *devlink_port;
	struct devlink *devlink;
	struct ice_vsi *vsi;

	dyn_port = sf_dev->dyn_port;
	vsi = dyn_port->vsi;

	devlink_port = &sf_dev->priv->devlink_port;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_VIRTUAL;

	devlink_port_attrs_set(devlink_port, &attrs);
	devlink = priv_to_devlink(sf_dev->priv);

	return devl_port_register(devlink, devlink_port, vsi->idx);
}

/**
 * ice_devlink_destroy_sf_dev_port - Destroy virtual port for a subfunction
 * @sf_dev: the subfunction device to create a devlink port for
 *
 * Unregisters the virtual port associated with this subfunction.
 */
void ice_devlink_destroy_sf_dev_port(struct ice_sf_dev *sf_dev)
{
	devl_port_unregister(&sf_dev->priv->devlink_port);
}

/**
 * ice_activate_dynamic_port - Activate a dynamic port
 * @dyn_port: dynamic port instance to activate
 * @extack: extack for reporting error messages
 *
 * Activate the dynamic port based on its flavour.
 *
 * Return: zero on success or an error code on failure.
 */
static int
ice_activate_dynamic_port(struct ice_dynamic_port *dyn_port,
			  struct netlink_ext_ack *extack)
{
	int err;

	if (dyn_port->active)
		return 0;

	err = ice_sf_eth_activate(dyn_port, extack);
	if (err)
		return err;

	dyn_port->active = true;

	return 0;
}

/**
 * ice_deactivate_dynamic_port - Deactivate a dynamic port
 * @dyn_port: dynamic port instance to deactivate
 *
 * Undo activation of a dynamic port.
 */
static void ice_deactivate_dynamic_port(struct ice_dynamic_port *dyn_port)
{
	if (!dyn_port->active)
		return;

	ice_sf_eth_deactivate(dyn_port);
	dyn_port->active = false;
}

/**
 * ice_dealloc_dynamic_port - Deallocate and remove a dynamic port
 * @dyn_port: dynamic port instance to deallocate
 *
 * Free resources associated with a dynamically added devlink port. Will
 * deactivate the port if its currently active.
 */
static void ice_dealloc_dynamic_port(struct ice_dynamic_port *dyn_port)
{
	struct devlink_port *devlink_port = &dyn_port->devlink_port;
	struct ice_pf *pf = dyn_port->pf;

	ice_deactivate_dynamic_port(dyn_port);

	xa_erase(&pf->sf_nums, devlink_port->attrs.pci_sf.sf);
	ice_eswitch_detach_sf(pf, dyn_port);
	ice_vsi_free(dyn_port->vsi);
	xa_erase(&pf->dyn_ports, dyn_port->vsi->idx);
	kfree(dyn_port);
}

/**
 * ice_dealloc_all_dynamic_ports - Deallocate all dynamic devlink ports
 * @pf: pointer to the pf structure
 */
void ice_dealloc_all_dynamic_ports(struct ice_pf *pf)
{
	struct ice_dynamic_port *dyn_port;
	unsigned long index;

	xa_for_each(&pf->dyn_ports, index, dyn_port)
		ice_dealloc_dynamic_port(dyn_port);
}

/**
 * ice_devlink_port_new_check_attr - Check that new port attributes are valid
 * @pf: pointer to the PF structure
 * @new_attr: the attributes for the new port
 * @extack: extack for reporting error messages
 *
 * Check that the attributes for the new port are valid before continuing to
 * allocate the devlink port.
 *
 * Return: zero on success or an error code on failure.
 */
static int
ice_devlink_port_new_check_attr(struct ice_pf *pf,
				const struct devlink_port_new_attrs *new_attr,
				struct netlink_ext_ack *extack)
{
	if (new_attr->flavour != DEVLINK_PORT_FLAVOUR_PCI_SF) {
		NL_SET_ERR_MSG_MOD(extack, "Flavour other than pcisf is not supported");
		return -EOPNOTSUPP;
	}

	if (new_attr->controller_valid) {
		NL_SET_ERR_MSG_MOD(extack, "Setting controller is not supported");
		return -EOPNOTSUPP;
	}

	if (new_attr->port_index_valid) {
		NL_SET_ERR_MSG_MOD(extack, "Driver does not support user defined port index assignment");
		return -EOPNOTSUPP;
	}

	if (new_attr->pfnum != pf->hw.pf_id) {
		NL_SET_ERR_MSG_MOD(extack, "Incorrect pfnum supplied");
		return -EINVAL;
	}

	if (!pci_msix_can_alloc_dyn(pf->pdev)) {
		NL_SET_ERR_MSG_MOD(extack, "Dynamic MSIX-X interrupt allocation is not supported");
		return -EOPNOTSUPP;
	}

	return 0;
}

/**
 * ice_devlink_port_del - devlink handler for port delete
 * @devlink: pointer to devlink
 * @port: devlink port to be deleted
 * @extack: pointer to extack
 *
 * Deletes devlink port and deallocates all resources associated with
 * created subfunction.
 *
 * Return: zero on success or an error code on failure.
 */
static int
ice_devlink_port_del(struct devlink *devlink, struct devlink_port *port,
		     struct netlink_ext_ack *extack)
{
	struct ice_dynamic_port *dyn_port;

	dyn_port = ice_devlink_port_to_dyn(port);
	ice_dealloc_dynamic_port(dyn_port);

	return 0;
}

/**
 * ice_devlink_port_fn_hw_addr_set - devlink handler for mac address set
 * @port: pointer to devlink port
 * @hw_addr: hw address to set
 * @hw_addr_len: hw address length
 * @extack: extack for reporting error messages
 *
 * Sets mac address for the port, verifies arguments and copies address
 * to the subfunction structure.
 *
 * Return: zero on success or an error code on failure.
 */
static int
ice_devlink_port_fn_hw_addr_set(struct devlink_port *port, const u8 *hw_addr,
				int hw_addr_len,
				struct netlink_ext_ack *extack)
{
	struct ice_dynamic_port *dyn_port;

	dyn_port = ice_devlink_port_to_dyn(port);

	if (dyn_port->attached) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Ethernet address can be change only in detached state");
		return -EBUSY;
	}

	if (hw_addr_len != ETH_ALEN || !is_valid_ether_addr(hw_addr)) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid ethernet address");
		return -EADDRNOTAVAIL;
	}

	ether_addr_copy(dyn_port->hw_addr, hw_addr);

	return 0;
}

/**
 * ice_devlink_port_fn_hw_addr_get - devlink handler for mac address get
 * @port: pointer to devlink port
 * @hw_addr: hw address to set
 * @hw_addr_len: hw address length
 * @extack: extack for reporting error messages
 *
 * Returns mac address for the port.
 *
 * Return: zero on success or an error code on failure.
 */
static int
ice_devlink_port_fn_hw_addr_get(struct devlink_port *port, u8 *hw_addr,
				int *hw_addr_len,
				struct netlink_ext_ack *extack)
{
	struct ice_dynamic_port *dyn_port;

	dyn_port = ice_devlink_port_to_dyn(port);

	ether_addr_copy(hw_addr, dyn_port->hw_addr);
	*hw_addr_len = ETH_ALEN;

	return 0;
}

/**
 * ice_devlink_port_fn_state_set - devlink handler for port state set
 * @port: pointer to devlink port
 * @state: state to set
 * @extack: extack for reporting error messages
 *
 * Activates or deactivates the port.
 *
 * Return: zero on success or an error code on failure.
 */
static int
ice_devlink_port_fn_state_set(struct devlink_port *port,
			      enum devlink_port_fn_state state,
			      struct netlink_ext_ack *extack)
{
	struct ice_dynamic_port *dyn_port;

	dyn_port = ice_devlink_port_to_dyn(port);

	switch (state) {
	case DEVLINK_PORT_FN_STATE_ACTIVE:
		return ice_activate_dynamic_port(dyn_port, extack);

	case DEVLINK_PORT_FN_STATE_INACTIVE:
		ice_deactivate_dynamic_port(dyn_port);
		break;
	}

	return 0;
}

/**
 * ice_devlink_port_fn_state_get - devlink handler for port state get
 * @port: pointer to devlink port
 * @state: admin configured state of the port
 * @opstate: current port operational state
 * @extack: extack for reporting error messages
 *
 * Gets port state.
 *
 * Return: zero on success or an error code on failure.
 */
static int
ice_devlink_port_fn_state_get(struct devlink_port *port,
			      enum devlink_port_fn_state *state,
			      enum devlink_port_fn_opstate *opstate,
			      struct netlink_ext_ack *extack)
{
	struct ice_dynamic_port *dyn_port;

	dyn_port = ice_devlink_port_to_dyn(port);

	if (dyn_port->active)
		*state = DEVLINK_PORT_FN_STATE_ACTIVE;
	else
		*state = DEVLINK_PORT_FN_STATE_INACTIVE;

	if (dyn_port->attached)
		*opstate = DEVLINK_PORT_FN_OPSTATE_ATTACHED;
	else
		*opstate = DEVLINK_PORT_FN_OPSTATE_DETACHED;

	return 0;
}

static const struct devlink_port_ops ice_devlink_port_sf_ops = {
	.port_del = ice_devlink_port_del,
	.port_fn_hw_addr_get = ice_devlink_port_fn_hw_addr_get,
	.port_fn_hw_addr_set = ice_devlink_port_fn_hw_addr_set,
	.port_fn_state_get = ice_devlink_port_fn_state_get,
	.port_fn_state_set = ice_devlink_port_fn_state_set,
};

/**
 * ice_reserve_sf_num - Reserve a subfunction number for this port
 * @pf: pointer to the pf structure
 * @new_attr: devlink port attributes requested
 * @extack: extack for reporting error messages
 * @sfnum: on success, the sf number reserved
 *
 * Reserve a subfunction number for this port. Only called for
 * DEVLINK_PORT_FLAVOUR_PCI_SF ports.
 *
 * Return: zero on success or an error code on failure.
 */
static int
ice_reserve_sf_num(struct ice_pf *pf,
		   const struct devlink_port_new_attrs *new_attr,
		   struct netlink_ext_ack *extack, u32 *sfnum)
{
	int err;

	/* If user didn't request an explicit number, pick one */
	if (!new_attr->sfnum_valid)
		return xa_alloc(&pf->sf_nums, sfnum, NULL, xa_limit_32b,
				GFP_KERNEL);

	/* Otherwise, check and use the number provided */
	err = xa_insert(&pf->sf_nums, new_attr->sfnum, NULL, GFP_KERNEL);
	if (err) {
		if (err == -EBUSY)
			NL_SET_ERR_MSG_MOD(extack, "Subfunction with given sfnum already exists");
		return err;
	}

	*sfnum = new_attr->sfnum;

	return 0;
}

/**
 * ice_devlink_create_sf_port - Register PCI subfunction devlink port
 * @dyn_port: the dynamic port instance structure for this subfunction
 *
 * Register PCI subfunction flavour devlink port for a dynamically added
 * subfunction port.
 *
 * Return: zero on success or an error code on failure.
 */
int ice_devlink_create_sf_port(struct ice_dynamic_port *dyn_port)
{
	struct devlink_port_attrs attrs = {};
	struct devlink_port *devlink_port;
	struct devlink *devlink;
	struct ice_vsi *vsi;
	struct ice_pf *pf;

	vsi = dyn_port->vsi;
	pf = dyn_port->pf;

	devlink_port = &dyn_port->devlink_port;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PCI_SF;
	attrs.pci_sf.pf = pf->hw.pf_id;
	attrs.pci_sf.sf = dyn_port->sfnum;

	devlink_port_attrs_set(devlink_port, &attrs);
	devlink = priv_to_devlink(pf);

	return devl_port_register_with_ops(devlink, devlink_port, vsi->idx,
					   &ice_devlink_port_sf_ops);
}

/**
 * ice_devlink_destroy_sf_port - Destroy the devlink_port for this SF
 * @dyn_port: the dynamic port instance structure for this subfunction
 *
 * Unregisters the devlink_port structure associated with this SF.
 */
void ice_devlink_destroy_sf_port(struct ice_dynamic_port *dyn_port)
{
	devl_rate_leaf_destroy(&dyn_port->devlink_port);
	devl_port_unregister(&dyn_port->devlink_port);
}

/**
 * ice_alloc_dynamic_port - Allocate new dynamic port
 * @pf: pointer to the pf structure
 * @new_attr: devlink port attributes requested
 * @extack: extack for reporting error messages
 * @devlink_port: index of newly created devlink port
 *
 * Allocate a new dynamic port instance and prepare it for configuration
 * with devlink.
 *
 * Return: zero on success or an error code on failure.
 */
static int
ice_alloc_dynamic_port(struct ice_pf *pf,
		       const struct devlink_port_new_attrs *new_attr,
		       struct netlink_ext_ack *extack,
		       struct devlink_port **devlink_port)
{
	struct ice_dynamic_port *dyn_port;
	struct ice_vsi *vsi;
	u32 sfnum;
	int err;

	err = ice_reserve_sf_num(pf, new_attr, extack, &sfnum);
	if (err)
		return err;

	dyn_port = kzalloc(sizeof(*dyn_port), GFP_KERNEL);
	if (!dyn_port) {
		err = -ENOMEM;
		goto unroll_reserve_sf_num;
	}

	vsi = ice_vsi_alloc(pf);
	if (!vsi) {
		NL_SET_ERR_MSG_MOD(extack, "Unable to allocate VSI");
		err = -ENOMEM;
		goto unroll_dyn_port_alloc;
	}

	dyn_port->vsi = vsi;
	dyn_port->pf = pf;
	dyn_port->sfnum = sfnum;
	eth_random_addr(dyn_port->hw_addr);

	err = xa_insert(&pf->dyn_ports, vsi->idx, dyn_port, GFP_KERNEL);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Port index reservation failed");
		goto unroll_vsi_alloc;
	}

	err = ice_eswitch_attach_sf(pf, dyn_port);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to attach SF to eswitch");
		goto unroll_xa_insert;
	}

	*devlink_port = &dyn_port->devlink_port;

	return 0;

unroll_xa_insert:
	xa_erase(&pf->dyn_ports, vsi->idx);
unroll_vsi_alloc:
	ice_vsi_free(vsi);
unroll_dyn_port_alloc:
	kfree(dyn_port);
unroll_reserve_sf_num:
	xa_erase(&pf->sf_nums, sfnum);

	return err;
}

/**
 * ice_devlink_port_new - devlink handler for the new port
 * @devlink: pointer to devlink
 * @new_attr: pointer to the port new attributes
 * @extack: extack for reporting error messages
 * @devlink_port: pointer to a new port
 *
 * Creates new devlink port, checks new port attributes and reject
 * any unsupported parameters, allocates new subfunction for that port.
 *
 * Return: zero on success or an error code on failure.
 */
int
ice_devlink_port_new(struct devlink *devlink,
		     const struct devlink_port_new_attrs *new_attr,
		     struct netlink_ext_ack *extack,
		     struct devlink_port **devlink_port)
{
	struct ice_pf *pf = devlink_priv(devlink);
	int err;

	err = ice_devlink_port_new_check_attr(pf, new_attr, extack);
	if (err)
		return err;

	if (!ice_is_eswitch_mode_switchdev(pf)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "SF ports are only supported in eswitch switchdev mode");
		return -EOPNOTSUPP;
	}

	return ice_alloc_dynamic_port(pf, new_attr, extack, devlink_port);
}
