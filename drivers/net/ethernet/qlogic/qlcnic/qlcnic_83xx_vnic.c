/*
 * QLogic qlcnic NIC Driver
 * Copyright (c) 2009-2013 QLogic Corporation
 *
 * See LICENSE.qlcnic for copyright and licensing details.
 */

#include "qlcnic.h"
#include "qlcnic_hw.h"

static int qlcnic_83xx_enable_vnic_mode(struct qlcnic_adapter *adapter, int lock)
{
	if (lock) {
		if (qlcnic_83xx_lock_driver(adapter))
			return -EBUSY;
	}
	QLCWRX(adapter->ahw, QLC_83XX_VNIC_STATE, QLCNIC_DEV_NPAR_OPER);
	if (lock)
		qlcnic_83xx_unlock_driver(adapter);

	return 0;
}

int qlcnic_83xx_disable_vnic_mode(struct qlcnic_adapter *adapter, int lock)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	if (lock) {
		if (qlcnic_83xx_lock_driver(adapter))
			return -EBUSY;
	}

	QLCWRX(adapter->ahw, QLC_83XX_VNIC_STATE, QLCNIC_DEV_NPAR_NON_OPER);
	ahw->idc.vnic_state = QLCNIC_DEV_NPAR_NON_OPER;

	if (lock)
		qlcnic_83xx_unlock_driver(adapter);

	return 0;
}

int qlcnic_83xx_set_vnic_opmode(struct qlcnic_adapter *adapter)
{
	u8 id;
	int ret = -EBUSY;
	u32 data = QLCNIC_MGMT_FUNC;
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	if (qlcnic_83xx_lock_driver(adapter))
		return ret;

	id = ahw->pci_func;
	data = QLCRDX(adapter->ahw, QLC_83XX_DRV_OP_MODE);
	data = (data & ~QLC_83XX_SET_FUNC_OPMODE(0x3, id)) |
	       QLC_83XX_SET_FUNC_OPMODE(QLCNIC_MGMT_FUNC, id);

	QLCWRX(adapter->ahw, QLC_83XX_DRV_OP_MODE, data);

	qlcnic_83xx_unlock_driver(adapter);

	return 0;
}

static void
qlcnic_83xx_config_vnic_buff_descriptors(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;

	if (ahw->port_type == QLCNIC_XGBE) {
		adapter->num_rxd = DEFAULT_RCV_DESCRIPTORS_VF;
		adapter->max_rxd = MAX_RCV_DESCRIPTORS_VF;
		adapter->num_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_10G;
		adapter->max_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_10G;

	} else if (ahw->port_type == QLCNIC_GBE) {
		adapter->num_rxd = DEFAULT_RCV_DESCRIPTORS_1G;
		adapter->num_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_1G;
		adapter->max_jumbo_rxd = MAX_JUMBO_RCV_DESCRIPTORS_1G;
		adapter->max_rxd = MAX_RCV_DESCRIPTORS_1G;
	}
	adapter->num_txd = MAX_CMD_DESCRIPTORS;
	adapter->max_rds_rings = MAX_RDS_RINGS;
}


/**
 * qlcnic_83xx_init_mgmt_vnic
 *
 * @adapter: adapter structure
 * Management virtual NIC sets the operational mode of other vNIC's and
 * configures embedded switch (ESWITCH).
 * Returns: Success(0) or error code.
 *
 **/
static int qlcnic_83xx_init_mgmt_vnic(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct device *dev = &adapter->pdev->dev;
	struct qlcnic_npar_info *npar;
	int i, err = -EIO;

	qlcnic_83xx_get_minidump_template(adapter);

	if (!(adapter->flags & QLCNIC_ADAPTER_INITIALIZED)) {
		if (qlcnic_init_pci_info(adapter))
			return err;

		npar = adapter->npars;

		for (i = 0; i < ahw->total_nic_func; i++, npar++) {
			dev_info(dev, "id:%d active:%d type:%d port:%d min_bw:%d max_bw:%d mac_addr:%pM\n",
				 npar->pci_func, npar->active, npar->type,
				 npar->phy_port, npar->min_bw, npar->max_bw,
				 npar->mac);
		}

		dev_info(dev, "Max functions = %d, active functions = %d\n",
			 ahw->max_pci_func, ahw->total_nic_func);

		if (qlcnic_83xx_set_vnic_opmode(adapter))
			return err;

		if (qlcnic_set_default_offload_settings(adapter))
			return err;
	} else {
		if (qlcnic_reset_npar_config(adapter))
			return err;
	}

	if (qlcnic_83xx_get_port_info(adapter))
		return err;

	qlcnic_83xx_config_vnic_buff_descriptors(adapter);
	ahw->msix_supported = qlcnic_use_msi_x ? 1 : 0;
	adapter->flags |= QLCNIC_ADAPTER_INITIALIZED;
	qlcnic_83xx_enable_vnic_mode(adapter, 1);

	dev_info(dev, "HAL Version: %d, Management function\n",
		 ahw->fw_hal_version);

	return 0;
}

static int qlcnic_83xx_init_privileged_vnic(struct qlcnic_adapter *adapter)
{
	int err = -EIO;

	qlcnic_83xx_get_minidump_template(adapter);
	if (qlcnic_83xx_get_port_info(adapter))
		return err;

	qlcnic_83xx_config_vnic_buff_descriptors(adapter);
	adapter->ahw->msix_supported = !!qlcnic_use_msi_x;
	adapter->flags |= QLCNIC_ADAPTER_INITIALIZED;

	dev_info(&adapter->pdev->dev,
		 "HAL Version: %d, Privileged function\n",
		 adapter->ahw->fw_hal_version);
	return 0;
}

static int qlcnic_83xx_init_non_privileged_vnic(struct qlcnic_adapter *adapter)
{
	int err = -EIO;

	qlcnic_83xx_get_fw_version(adapter);
	if (qlcnic_set_eswitch_port_config(adapter))
		return err;

	if (qlcnic_83xx_get_port_info(adapter))
		return err;

	qlcnic_83xx_config_vnic_buff_descriptors(adapter);
	adapter->ahw->msix_supported = !!qlcnic_use_msi_x;
	adapter->flags |= QLCNIC_ADAPTER_INITIALIZED;

	dev_info(&adapter->pdev->dev, "HAL Version: %d, Virtual function\n",
		 adapter->ahw->fw_hal_version);

	return 0;
}

/**
 * qlcnic_83xx_vnic_opmode
 *
 * @adapter: adapter structure
 * Identify virtual NIC operational modes.
 *
 * Returns: Success(0) or error code.
 *
 **/
int qlcnic_83xx_config_vnic_opmode(struct qlcnic_adapter *adapter)
{
	u32 op_mode, priv_level;
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlcnic_nic_template *nic_ops = adapter->nic_ops;

	qlcnic_get_func_no(adapter);
	op_mode = QLCRDX(adapter->ahw, QLC_83XX_DRV_OP_MODE);

	if (op_mode == QLC_83XX_DEFAULT_OPMODE)
		priv_level = QLCNIC_MGMT_FUNC;
	else
		priv_level = QLC_83XX_GET_FUNC_PRIVILEGE(op_mode,
							 ahw->pci_func);
	switch (priv_level) {
	case QLCNIC_NON_PRIV_FUNC:
		ahw->op_mode = QLCNIC_NON_PRIV_FUNC;
		ahw->idc.state_entry = qlcnic_83xx_idc_ready_state_entry;
		nic_ops->init_driver = qlcnic_83xx_init_non_privileged_vnic;
		break;
	case QLCNIC_PRIV_FUNC:
		ahw->op_mode = QLCNIC_PRIV_FUNC;
		ahw->idc.state_entry = qlcnic_83xx_idc_vnic_pf_entry;
		nic_ops->init_driver = qlcnic_83xx_init_privileged_vnic;
		break;
	case QLCNIC_MGMT_FUNC:
		ahw->op_mode = QLCNIC_MGMT_FUNC;
		ahw->idc.state_entry = qlcnic_83xx_idc_ready_state_entry;
		nic_ops->init_driver = qlcnic_83xx_init_mgmt_vnic;
		break;
	default:
		dev_err(&adapter->pdev->dev, "Invalid Virtual NIC opmode\n");
		return -EIO;
	}

	if (ahw->capabilities & QLC_83XX_ESWITCH_CAPABILITY) {
		adapter->flags |= QLCNIC_ESWITCH_ENABLED;
		if (adapter->drv_mac_learn)
			adapter->rx_mac_learn = true;
	} else {
		adapter->flags &= ~QLCNIC_ESWITCH_ENABLED;
		adapter->rx_mac_learn = false;
	}

	ahw->idc.vnic_state = QLCNIC_DEV_NPAR_NON_OPER;
	ahw->idc.vnic_wait_limit = QLCNIC_DEV_NPAR_OPER_TIMEO;

	return 0;
}

int qlcnic_83xx_check_vnic_state(struct qlcnic_adapter *adapter)
{
	struct qlcnic_hardware_context *ahw = adapter->ahw;
	struct qlc_83xx_idc *idc = &ahw->idc;
	u32 state;

	state = QLCRDX(ahw, QLC_83XX_VNIC_STATE);
	while (state != QLCNIC_DEV_NPAR_OPER && idc->vnic_wait_limit--) {
		msleep(1000);
		state = QLCRDX(ahw, QLC_83XX_VNIC_STATE);
	}

	if (!idc->vnic_wait_limit) {
		dev_err(&adapter->pdev->dev,
			"vNIC mode not operational, state check timed out.\n");
		return -EIO;
	}

	return 0;
}

int qlcnic_83xx_set_port_eswitch_status(struct qlcnic_adapter *adapter,
					int func, int *port_id)
{
	struct qlcnic_info nic_info;
	int err = 0;

	memset(&nic_info, 0, sizeof(struct qlcnic_info));

	err = qlcnic_get_nic_info(adapter, &nic_info, func);
	if (err)
		return err;

	if (nic_info.capabilities & QLC_83XX_ESWITCH_CAPABILITY)
		*port_id = nic_info.phys_port;
	else
		err = -EIO;

	if (!err)
		adapter->eswitch[*port_id].flags |= QLCNIC_SWITCH_ENABLE;

	return err;
}
