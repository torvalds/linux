/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "hns_dsaf_mac.h"
#include "hns_dsaf_misc.h"
#include "hns_dsaf_ppe.h"
#include "hns_dsaf_reg.h"

enum _dsm_op_index {
	HNS_OP_RESET_FUNC               = 0x1,
	HNS_OP_SERDES_LP_FUNC           = 0x2,
	HNS_OP_LED_SET_FUNC             = 0x3,
	HNS_OP_GET_PORT_TYPE_FUNC       = 0x4,
	HNS_OP_GET_SFP_STAT_FUNC        = 0x5,
};

enum _dsm_rst_type {
	HNS_DSAF_RESET_FUNC     = 0x1,
	HNS_PPE_RESET_FUNC      = 0x2,
	HNS_XGE_CORE_RESET_FUNC = 0x3,
	HNS_XGE_RESET_FUNC      = 0x4,
	HNS_GE_RESET_FUNC       = 0x5,
};

const u8 hns_dsaf_acpi_dsm_uuid[] = {
	0x1A, 0xAA, 0x85, 0x1A, 0x93, 0xE2, 0x5E, 0x41,
	0x8E, 0x28, 0x8D, 0x69, 0x0A, 0x0F, 0x82, 0x0A
};

static void dsaf_write_sub(struct dsaf_device *dsaf_dev, u32 reg, u32 val)
{
	if (dsaf_dev->sub_ctrl)
		dsaf_write_syscon(dsaf_dev->sub_ctrl, reg, val);
	else
		dsaf_write_reg(dsaf_dev->sc_base, reg, val);
}

static u32 dsaf_read_sub(struct dsaf_device *dsaf_dev, u32 reg)
{
	u32 ret;

	if (dsaf_dev->sub_ctrl)
		ret = dsaf_read_syscon(dsaf_dev->sub_ctrl, reg);
	else
		ret = dsaf_read_reg(dsaf_dev->sc_base, reg);

	return ret;
}

static void hns_cpld_set_led(struct hns_mac_cb *mac_cb, int link_status,
			     u16 speed, int data)
{
	int speed_reg = 0;
	u8 value;

	if (!mac_cb) {
		pr_err("sfp_led_opt mac_dev is null!\n");
		return;
	}
	if (!mac_cb->cpld_ctrl) {
		dev_err(mac_cb->dev, "mac_id=%d, cpld syscon is null !\n",
			mac_cb->mac_id);
		return;
	}

	if (speed == MAC_SPEED_10000)
		speed_reg = 1;

	value = mac_cb->cpld_led_value;

	if (link_status) {
		dsaf_set_bit(value, DSAF_LED_LINK_B, link_status);
		dsaf_set_field(value, DSAF_LED_SPEED_M,
			       DSAF_LED_SPEED_S, speed_reg);
		dsaf_set_bit(value, DSAF_LED_DATA_B, data);

		if (value != mac_cb->cpld_led_value) {
			dsaf_write_syscon(mac_cb->cpld_ctrl,
					  mac_cb->cpld_ctrl_reg, value);
			mac_cb->cpld_led_value = value;
		}
	} else {
		value = (mac_cb->cpld_led_value) & (0x1 << DSAF_LED_ANCHOR_B);
		dsaf_write_syscon(mac_cb->cpld_ctrl,
				  mac_cb->cpld_ctrl_reg, value);
		mac_cb->cpld_led_value = value;
	}
}

static void cpld_led_reset(struct hns_mac_cb *mac_cb)
{
	if (!mac_cb || !mac_cb->cpld_ctrl)
		return;

	dsaf_write_syscon(mac_cb->cpld_ctrl, mac_cb->cpld_ctrl_reg,
			  CPLD_LED_DEFAULT_VALUE);
	mac_cb->cpld_led_value = CPLD_LED_DEFAULT_VALUE;
}

static int cpld_set_led_id(struct hns_mac_cb *mac_cb,
			   enum hnae_led_state status)
{
	switch (status) {
	case HNAE_LED_ACTIVE:
		mac_cb->cpld_led_value =
			dsaf_read_syscon(mac_cb->cpld_ctrl,
					 mac_cb->cpld_ctrl_reg);
		dsaf_set_bit(mac_cb->cpld_led_value, DSAF_LED_ANCHOR_B,
			     CPLD_LED_ON_VALUE);
		dsaf_write_syscon(mac_cb->cpld_ctrl, mac_cb->cpld_ctrl_reg,
				  mac_cb->cpld_led_value);
		break;
	case HNAE_LED_INACTIVE:
		dsaf_set_bit(mac_cb->cpld_led_value, DSAF_LED_ANCHOR_B,
			     CPLD_LED_DEFAULT_VALUE);
		dsaf_write_syscon(mac_cb->cpld_ctrl, mac_cb->cpld_ctrl_reg,
				  mac_cb->cpld_led_value);
		break;
	default:
		dev_err(mac_cb->dev, "invalid led state: %d!", status);
		return -EINVAL;
	}

	return 0;
}

#define RESET_REQ_OR_DREQ 1

static void hns_dsaf_acpi_srst_by_port(struct dsaf_device *dsaf_dev, u8 op_type,
				       u32 port_type, u32 port, u32 val)
{
	union acpi_object *obj;
	union acpi_object obj_args[3], argv4;

	obj_args[0].integer.type = ACPI_TYPE_INTEGER;
	obj_args[0].integer.value = port_type;
	obj_args[1].integer.type = ACPI_TYPE_INTEGER;
	obj_args[1].integer.value = port;
	obj_args[2].integer.type = ACPI_TYPE_INTEGER;
	obj_args[2].integer.value = val;

	argv4.type = ACPI_TYPE_PACKAGE;
	argv4.package.count = 3;
	argv4.package.elements = obj_args;

	obj = acpi_evaluate_dsm(ACPI_HANDLE(dsaf_dev->dev),
				hns_dsaf_acpi_dsm_uuid, 0, op_type, &argv4);
	if (!obj) {
		dev_warn(dsaf_dev->dev, "reset port_type%d port%d fail!",
			 port_type, port);
		return;
	}

	ACPI_FREE(obj);
}

static void hns_dsaf_rst(struct dsaf_device *dsaf_dev, bool dereset)
{
	u32 xbar_reg_addr;
	u32 nt_reg_addr;

	if (!dereset) {
		xbar_reg_addr = DSAF_SUB_SC_XBAR_RESET_REQ_REG;
		nt_reg_addr = DSAF_SUB_SC_NT_RESET_REQ_REG;
	} else {
		xbar_reg_addr = DSAF_SUB_SC_XBAR_RESET_DREQ_REG;
		nt_reg_addr = DSAF_SUB_SC_NT_RESET_DREQ_REG;
	}

	dsaf_write_sub(dsaf_dev, xbar_reg_addr, RESET_REQ_OR_DREQ);
	dsaf_write_sub(dsaf_dev, nt_reg_addr, RESET_REQ_OR_DREQ);
}

static void hns_dsaf_rst_acpi(struct dsaf_device *dsaf_dev, bool dereset)
{
	hns_dsaf_acpi_srst_by_port(dsaf_dev, HNS_OP_RESET_FUNC,
				   HNS_DSAF_RESET_FUNC,
				   0, dereset);
}

static void hns_dsaf_xge_srst_by_port(struct dsaf_device *dsaf_dev, u32 port,
				      bool dereset)
{
	u32 reg_val = 0;
	u32 reg_addr;

	if (port >= DSAF_XGE_NUM)
		return;

	reg_val |= RESET_REQ_OR_DREQ;
	reg_val |= 0x2082082 << dsaf_dev->mac_cb[port]->port_rst_off;

	if (!dereset)
		reg_addr = DSAF_SUB_SC_XGE_RESET_REQ_REG;
	else
		reg_addr = DSAF_SUB_SC_XGE_RESET_DREQ_REG;

	dsaf_write_sub(dsaf_dev, reg_addr, reg_val);
}

static void hns_dsaf_xge_srst_by_port_acpi(struct dsaf_device *dsaf_dev,
					   u32 port, bool dereset)
{
	hns_dsaf_acpi_srst_by_port(dsaf_dev, HNS_OP_RESET_FUNC,
				   HNS_XGE_RESET_FUNC, port, dereset);
}

static void hns_dsaf_xge_core_srst_by_port(struct dsaf_device *dsaf_dev,
					   u32 port, bool dereset)
{
	u32 reg_val = 0;
	u32 reg_addr;

	if (port >= DSAF_XGE_NUM)
		return;

	reg_val |= XGMAC_TRX_CORE_SRST_M
		<< dsaf_dev->mac_cb[port]->port_rst_off;

	if (!dereset)
		reg_addr = DSAF_SUB_SC_XGE_RESET_REQ_REG;
	else
		reg_addr = DSAF_SUB_SC_XGE_RESET_DREQ_REG;

	dsaf_write_sub(dsaf_dev, reg_addr, reg_val);
}

static void
hns_dsaf_xge_core_srst_by_port_acpi(struct dsaf_device *dsaf_dev,
				    u32 port, bool dereset)
{
	hns_dsaf_acpi_srst_by_port(dsaf_dev, HNS_OP_RESET_FUNC,
				   HNS_XGE_CORE_RESET_FUNC, port, dereset);
}

static void hns_dsaf_ge_srst_by_port(struct dsaf_device *dsaf_dev, u32 port,
				     bool dereset)
{
	u32 reg_val_1;
	u32 reg_val_2;
	u32 port_rst_off;

	if (port >= DSAF_GE_NUM)
		return;

	if (!HNS_DSAF_IS_DEBUG(dsaf_dev)) {
		reg_val_1  = 0x1 << port;
		port_rst_off = dsaf_dev->mac_cb[port]->port_rst_off;
		/* there is difference between V1 and V2 in register.*/
		reg_val_2 = AE_IS_VER1(dsaf_dev->dsaf_ver) ?
				0x1041041 : 0x2082082;
		reg_val_2 <<= port_rst_off;

		if (!dereset) {
			dsaf_write_sub(dsaf_dev, DSAF_SUB_SC_GE_RESET_REQ1_REG,
				       reg_val_1);

			dsaf_write_sub(dsaf_dev, DSAF_SUB_SC_GE_RESET_REQ0_REG,
				       reg_val_2);
		} else {
			dsaf_write_sub(dsaf_dev, DSAF_SUB_SC_GE_RESET_DREQ0_REG,
				       reg_val_2);

			dsaf_write_sub(dsaf_dev, DSAF_SUB_SC_GE_RESET_DREQ1_REG,
				       reg_val_1);
		}
	} else {
		reg_val_1 = 0x15540;
		reg_val_2 = AE_IS_VER1(dsaf_dev->dsaf_ver) ? 0x100 : 0x40;

		reg_val_1 <<= dsaf_dev->reset_offset;
		reg_val_2 <<= dsaf_dev->reset_offset;

		if (!dereset) {
			dsaf_write_sub(dsaf_dev, DSAF_SUB_SC_GE_RESET_REQ1_REG,
				       reg_val_1);

			dsaf_write_sub(dsaf_dev, DSAF_SUB_SC_PPE_RESET_REQ_REG,
				       reg_val_2);
		} else {
			dsaf_write_sub(dsaf_dev, DSAF_SUB_SC_GE_RESET_DREQ1_REG,
				       reg_val_1);

			dsaf_write_sub(dsaf_dev, DSAF_SUB_SC_PPE_RESET_DREQ_REG,
				       reg_val_2);
		}
	}
}

static void hns_dsaf_ge_srst_by_port_acpi(struct dsaf_device *dsaf_dev,
					  u32 port, bool dereset)
{
	hns_dsaf_acpi_srst_by_port(dsaf_dev, HNS_OP_RESET_FUNC,
				   HNS_GE_RESET_FUNC, port, dereset);
}

static void hns_ppe_srst_by_port(struct dsaf_device *dsaf_dev, u32 port,
				 bool dereset)
{
	u32 reg_val = 0;
	u32 reg_addr;

	reg_val |= RESET_REQ_OR_DREQ <<	dsaf_dev->mac_cb[port]->port_rst_off;

	if (!dereset)
		reg_addr = DSAF_SUB_SC_PPE_RESET_REQ_REG;
	else
		reg_addr = DSAF_SUB_SC_PPE_RESET_DREQ_REG;

	dsaf_write_sub(dsaf_dev, reg_addr, reg_val);
}

static void
hns_ppe_srst_by_port_acpi(struct dsaf_device *dsaf_dev, u32 port, bool dereset)
{
	hns_dsaf_acpi_srst_by_port(dsaf_dev, HNS_OP_RESET_FUNC,
				   HNS_PPE_RESET_FUNC, port, dereset);
}

static void hns_ppe_com_srst(struct dsaf_device *dsaf_dev, bool dereset)
{
	u32 reg_val;
	u32 reg_addr;

	if (!(dev_of_node(dsaf_dev->dev)))
		return;

	if (!HNS_DSAF_IS_DEBUG(dsaf_dev)) {
		reg_val = RESET_REQ_OR_DREQ;
		if (!dereset)
			reg_addr = DSAF_SUB_SC_RCB_PPE_COM_RESET_REQ_REG;
		else
			reg_addr = DSAF_SUB_SC_RCB_PPE_COM_RESET_DREQ_REG;

	} else {
		reg_val = 0x100 << dsaf_dev->reset_offset;

		if (!dereset)
			reg_addr = DSAF_SUB_SC_PPE_RESET_REQ_REG;
		else
			reg_addr = DSAF_SUB_SC_PPE_RESET_DREQ_REG;
	}

	dsaf_write_sub(dsaf_dev, reg_addr, reg_val);
}

/**
 * hns_mac_get_sds_mode - get phy ifterface form serdes mode
 * @mac_cb: mac control block
 * retuen phy interface
 */
static phy_interface_t hns_mac_get_phy_if(struct hns_mac_cb *mac_cb)
{
	u32 mode;
	u32 reg;
	bool is_ver1 = AE_IS_VER1(mac_cb->dsaf_dev->dsaf_ver);
	int mac_id = mac_cb->mac_id;
	phy_interface_t phy_if;

	if (is_ver1) {
		if (HNS_DSAF_IS_DEBUG(mac_cb->dsaf_dev))
			return PHY_INTERFACE_MODE_SGMII;

		if (mac_id >= 0 && mac_id <= 3)
			reg = HNS_MAC_HILINK4_REG;
		else
			reg = HNS_MAC_HILINK3_REG;
	} else{
		if (!HNS_DSAF_IS_DEBUG(mac_cb->dsaf_dev) && mac_id <= 3)
			reg = HNS_MAC_HILINK4V2_REG;
		else
			reg = HNS_MAC_HILINK3V2_REG;
	}

	mode = dsaf_read_sub(mac_cb->dsaf_dev, reg);
	if (dsaf_get_bit(mode, mac_cb->port_mode_off))
		phy_if = PHY_INTERFACE_MODE_XGMII;
	else
		phy_if = PHY_INTERFACE_MODE_SGMII;

	return phy_if;
}

static phy_interface_t hns_mac_get_phy_if_acpi(struct hns_mac_cb *mac_cb)
{
	phy_interface_t phy_if = PHY_INTERFACE_MODE_NA;
	union acpi_object *obj;
	union acpi_object obj_args, argv4;

	obj_args.integer.type = ACPI_TYPE_INTEGER;
	obj_args.integer.value = mac_cb->mac_id;

	argv4.type = ACPI_TYPE_PACKAGE,
	argv4.package.count = 1,
	argv4.package.elements = &obj_args,

	obj = acpi_evaluate_dsm(ACPI_HANDLE(mac_cb->dev),
				hns_dsaf_acpi_dsm_uuid, 0,
				HNS_OP_GET_PORT_TYPE_FUNC, &argv4);

	if (!obj || obj->type != ACPI_TYPE_INTEGER)
		return phy_if;

	phy_if = obj->integer.value ?
		PHY_INTERFACE_MODE_XGMII : PHY_INTERFACE_MODE_SGMII;

	dev_dbg(mac_cb->dev, "mac_id=%d, phy_if=%d\n", mac_cb->mac_id, phy_if);

	ACPI_FREE(obj);

	return phy_if;
}

int hns_mac_get_sfp_prsnt(struct hns_mac_cb *mac_cb, int *sfp_prsnt)
{
	if (!mac_cb->cpld_ctrl)
		return -ENODEV;

	*sfp_prsnt = !dsaf_read_syscon(mac_cb->cpld_ctrl, mac_cb->cpld_ctrl_reg
					+ MAC_SFP_PORT_OFFSET);

	return 0;
}

/**
 * hns_mac_config_sds_loopback - set loop back for serdes
 * @mac_cb: mac control block
 * retuen 0 == success
 */
static int hns_mac_config_sds_loopback(struct hns_mac_cb *mac_cb, bool en)
{
	const u8 lane_id[] = {
		0,	/* mac 0 -> lane 0 */
		1,	/* mac 1 -> lane 1 */
		2,	/* mac 2 -> lane 2 */
		3,	/* mac 3 -> lane 3 */
		2,	/* mac 4 -> lane 2 */
		3,	/* mac 5 -> lane 3 */
		0,	/* mac 6 -> lane 0 */
		1	/* mac 7 -> lane 1 */
	};
#define RX_CSR(lane, reg) ((0x4080 + (reg) * 0x0002 + (lane) * 0x0200) * 2)
	u64 reg_offset = RX_CSR(lane_id[mac_cb->mac_id], 0);

	int sfp_prsnt;
	int ret = hns_mac_get_sfp_prsnt(mac_cb, &sfp_prsnt);

	if (!mac_cb->phy_dev) {
		if (ret)
			pr_info("please confirm sfp is present or not\n");
		else
			if (!sfp_prsnt)
				pr_info("no sfp in this eth\n");
	}

	if (mac_cb->serdes_ctrl) {
		u32 origin;

		if (!AE_IS_VER1(mac_cb->dsaf_dev->dsaf_ver)) {
#define HILINK_ACCESS_SEL_CFG		0x40008
			/* hilink4 & hilink3 use the same xge training and
			 * xge u adaptor. There is a hilink access sel cfg
			 * register to select which one to be configed
			 */
			if ((!HNS_DSAF_IS_DEBUG(mac_cb->dsaf_dev)) &&
			    (mac_cb->mac_id <= 3))
				dsaf_write_syscon(mac_cb->serdes_ctrl,
						  HILINK_ACCESS_SEL_CFG, 0);
			else
				dsaf_write_syscon(mac_cb->serdes_ctrl,
						  HILINK_ACCESS_SEL_CFG, 3);
		}

		origin = dsaf_read_syscon(mac_cb->serdes_ctrl, reg_offset);

		dsaf_set_field(origin, 1ull << 10, 10, en);
		dsaf_write_syscon(mac_cb->serdes_ctrl, reg_offset, origin);
	} else {
		u8 *base_addr = (u8 *)mac_cb->serdes_vaddr +
				(mac_cb->mac_id <= 3 ? 0x00280000 : 0x00200000);
		dsaf_set_reg_field(base_addr, reg_offset, 1ull << 10, 10, en);
	}

	return 0;
}

static int
hns_mac_config_sds_loopback_acpi(struct hns_mac_cb *mac_cb, bool en)
{
	union acpi_object *obj;
	union acpi_object obj_args[3], argv4;

	obj_args[0].integer.type = ACPI_TYPE_INTEGER;
	obj_args[0].integer.value = mac_cb->mac_id;
	obj_args[1].integer.type = ACPI_TYPE_INTEGER;
	obj_args[1].integer.value = !!en;

	argv4.type = ACPI_TYPE_PACKAGE;
	argv4.package.count = 2;
	argv4.package.elements = obj_args;

	obj = acpi_evaluate_dsm(ACPI_HANDLE(mac_cb->dsaf_dev->dev),
				hns_dsaf_acpi_dsm_uuid, 0,
				HNS_OP_SERDES_LP_FUNC, &argv4);
	if (!obj) {
		dev_warn(mac_cb->dsaf_dev->dev, "set port%d serdes lp fail!",
			 mac_cb->mac_id);

		return -ENOTSUPP;
	}

	ACPI_FREE(obj);

	return 0;
}

struct dsaf_misc_op *hns_misc_op_get(struct dsaf_device *dsaf_dev)
{
	struct dsaf_misc_op *misc_op;

	misc_op = devm_kzalloc(dsaf_dev->dev, sizeof(*misc_op), GFP_KERNEL);
	if (!misc_op)
		return NULL;

	if (dev_of_node(dsaf_dev->dev)) {
		misc_op->cpld_set_led = hns_cpld_set_led;
		misc_op->cpld_reset_led = cpld_led_reset;
		misc_op->cpld_set_led_id = cpld_set_led_id;

		misc_op->dsaf_reset = hns_dsaf_rst;
		misc_op->xge_srst = hns_dsaf_xge_srst_by_port;
		misc_op->xge_core_srst = hns_dsaf_xge_core_srst_by_port;
		misc_op->ge_srst = hns_dsaf_ge_srst_by_port;
		misc_op->ppe_srst = hns_ppe_srst_by_port;
		misc_op->ppe_comm_srst = hns_ppe_com_srst;

		misc_op->get_phy_if = hns_mac_get_phy_if;
		misc_op->get_sfp_prsnt = hns_mac_get_sfp_prsnt;

		misc_op->cfg_serdes_loopback = hns_mac_config_sds_loopback;
	} else if (is_acpi_node(dsaf_dev->dev->fwnode)) {
		misc_op->cpld_set_led = hns_cpld_set_led;
		misc_op->cpld_reset_led = cpld_led_reset;
		misc_op->cpld_set_led_id = cpld_set_led_id;

		misc_op->dsaf_reset = hns_dsaf_rst_acpi;
		misc_op->xge_srst = hns_dsaf_xge_srst_by_port_acpi;
		misc_op->xge_core_srst = hns_dsaf_xge_core_srst_by_port_acpi;
		misc_op->ge_srst = hns_dsaf_ge_srst_by_port_acpi;
		misc_op->ppe_srst = hns_ppe_srst_by_port_acpi;
		misc_op->ppe_comm_srst = hns_ppe_com_srst;

		misc_op->get_phy_if = hns_mac_get_phy_if_acpi;
		misc_op->get_sfp_prsnt = hns_mac_get_sfp_prsnt;

		misc_op->cfg_serdes_loopback = hns_mac_config_sds_loopback_acpi;
	} else {
		devm_kfree(dsaf_dev->dev, (void *)misc_op);
		misc_op = NULL;
	}

	return (void *)misc_op;
}
