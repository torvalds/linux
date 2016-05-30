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

void hns_cpld_set_led(struct hns_mac_cb *mac_cb, int link_status,
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
		dsaf_write_syscon(mac_cb->cpld_ctrl, mac_cb->cpld_ctrl_reg,
				  CPLD_LED_DEFAULT_VALUE);
		mac_cb->cpld_led_value = CPLD_LED_DEFAULT_VALUE;
	}
}

void cpld_led_reset(struct hns_mac_cb *mac_cb)
{
	if (!mac_cb || !mac_cb->cpld_ctrl)
		return;

	dsaf_write_syscon(mac_cb->cpld_ctrl, mac_cb->cpld_ctrl_reg,
			  CPLD_LED_DEFAULT_VALUE);
	mac_cb->cpld_led_value = CPLD_LED_DEFAULT_VALUE;
}

int cpld_set_led_id(struct hns_mac_cb *mac_cb,
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
		return 2;
	case HNAE_LED_INACTIVE:
		dsaf_set_bit(mac_cb->cpld_led_value, DSAF_LED_ANCHOR_B,
			     CPLD_LED_DEFAULT_VALUE);
		dsaf_write_syscon(mac_cb->cpld_ctrl, mac_cb->cpld_ctrl_reg,
				  mac_cb->cpld_led_value);
		break;
	default:
		break;
	}

	return 0;
}

#define RESET_REQ_OR_DREQ 1

void hns_dsaf_rst(struct dsaf_device *dsaf_dev, u32 val)
{
	u32 xbar_reg_addr;
	u32 nt_reg_addr;

	if (!val) {
		xbar_reg_addr = DSAF_SUB_SC_XBAR_RESET_REQ_REG;
		nt_reg_addr = DSAF_SUB_SC_NT_RESET_REQ_REG;
	} else {
		xbar_reg_addr = DSAF_SUB_SC_XBAR_RESET_DREQ_REG;
		nt_reg_addr = DSAF_SUB_SC_NT_RESET_DREQ_REG;
	}

	dsaf_write_sub(dsaf_dev, xbar_reg_addr, RESET_REQ_OR_DREQ);
	dsaf_write_sub(dsaf_dev, nt_reg_addr, RESET_REQ_OR_DREQ);
}

void hns_dsaf_xge_srst_by_port(struct dsaf_device *dsaf_dev, u32 port, u32 val)
{
	u32 reg_val = 0;
	u32 reg_addr;

	if (port >= DSAF_XGE_NUM)
		return;

	reg_val |= RESET_REQ_OR_DREQ;
	reg_val |= 0x2082082 << dsaf_dev->mac_cb[port]->port_rst_off;

	if (val == 0)
		reg_addr = DSAF_SUB_SC_XGE_RESET_REQ_REG;
	else
		reg_addr = DSAF_SUB_SC_XGE_RESET_DREQ_REG;

	dsaf_write_sub(dsaf_dev, reg_addr, reg_val);
}

void hns_dsaf_xge_core_srst_by_port(struct dsaf_device *dsaf_dev,
				    u32 port, u32 val)
{
	u32 reg_val = 0;
	u32 reg_addr;

	if (port >= DSAF_XGE_NUM)
		return;

	reg_val |= XGMAC_TRX_CORE_SRST_M
		<< dsaf_dev->mac_cb[port]->port_rst_off;

	if (val == 0)
		reg_addr = DSAF_SUB_SC_XGE_RESET_REQ_REG;
	else
		reg_addr = DSAF_SUB_SC_XGE_RESET_DREQ_REG;

	dsaf_write_sub(dsaf_dev, reg_addr, reg_val);
}

void hns_dsaf_ge_srst_by_port(struct dsaf_device *dsaf_dev, u32 port, u32 val)
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
		if (AE_IS_VER1(dsaf_dev->dsaf_ver))
			reg_val_2  = 0x1041041 << port_rst_off;
		else
			reg_val_2  = 0x2082082 << port_rst_off;

		if (val == 0) {
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
		reg_val_1 = 0x15540 << dsaf_dev->reset_offset;
		reg_val_2 = 0x100 << dsaf_dev->reset_offset;

		if (val == 0) {
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

void hns_ppe_srst_by_port(struct dsaf_device *dsaf_dev, u32 port, u32 val)
{
	u32 reg_val = 0;
	u32 reg_addr;

	reg_val |= RESET_REQ_OR_DREQ <<	dsaf_dev->mac_cb[port]->port_rst_off;

	if (val == 0)
		reg_addr = DSAF_SUB_SC_PPE_RESET_REQ_REG;
	else
		reg_addr = DSAF_SUB_SC_PPE_RESET_DREQ_REG;

	dsaf_write_sub(dsaf_dev, reg_addr, reg_val);
}

void hns_ppe_com_srst(struct ppe_common_cb *ppe_common, u32 val)
{
	struct dsaf_device *dsaf_dev = ppe_common->dsaf_dev;
	u32 reg_val;
	u32 reg_addr;

	if (!HNS_DSAF_IS_DEBUG(dsaf_dev)) {
		reg_val = RESET_REQ_OR_DREQ;
		if (val == 0)
			reg_addr = DSAF_SUB_SC_RCB_PPE_COM_RESET_REQ_REG;
		else
			reg_addr = DSAF_SUB_SC_RCB_PPE_COM_RESET_DREQ_REG;

	} else {
		reg_val = 0x100 << dsaf_dev->reset_offset;

		if (val == 0)
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
phy_interface_t hns_mac_get_phy_if(struct hns_mac_cb *mac_cb)
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
int hns_mac_config_sds_loopback(struct hns_mac_cb *mac_cb, u8 en)
{
	/* port 0-3 hilink4 base is serdes_vaddr + 0x00280000
	 * port 4-7 hilink3 base is serdes_vaddr + 0x00200000
	 */
	u8 *base_addr = (u8 *)mac_cb->serdes_vaddr +
		       (mac_cb->mac_id <= 3 ? 0x00280000 : 0x00200000);
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

	if (!mac_cb->phy_node) {
		if (ret)
			pr_info("please confirm sfp is present or not\n");
		else
			if (!sfp_prsnt)
				pr_info("no sfp in this eth\n");
	}

	if (mac_cb->serdes_ctrl) {
		u32 origin = dsaf_read_syscon(mac_cb->serdes_ctrl, reg_offset);

		dsaf_set_field(origin, 1ull << 10, 10, !!en);
		dsaf_write_syscon(mac_cb->serdes_ctrl, reg_offset, origin);
	} else {
		dsaf_set_reg_field(base_addr, reg_offset, 1ull << 10, 10, !!en);
	}

	return 0;
}
