/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 * Copyright (c) 2016-2017 Broadcom Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_ETHTOOL_H
#define BNXT_ETHTOOL_H

struct bnxt_led_cfg {
	u8 led_id;
	u8 led_state;
	u8 led_color;
	u8 unused;
	__le16 led_blink_on;
	__le16 led_blink_off;
	u8 led_group_id;
	u8 rsvd;
};

#define BNXT_LED_DFLT_ENA				\
	(PORT_LED_CFG_REQ_ENABLES_LED0_ID |		\
	 PORT_LED_CFG_REQ_ENABLES_LED0_STATE |		\
	 PORT_LED_CFG_REQ_ENABLES_LED0_BLINK_ON |	\
	 PORT_LED_CFG_REQ_ENABLES_LED0_BLINK_OFF |	\
	 PORT_LED_CFG_REQ_ENABLES_LED0_GROUP_ID)

#define BNXT_LED_DFLT_ENA_SHIFT	6

#define BNXT_LED_DFLT_ENABLES(x)			\
	cpu_to_le32(BNXT_LED_DFLT_ENA << (BNXT_LED_DFLT_ENA_SHIFT * (x)))

#define BNXT_FW_RESET_AP	(ETH_RESET_AP << ETH_RESET_SHARED_SHIFT)
#define BNXT_FW_RESET_CHIP	((ETH_RESET_MGMT | ETH_RESET_IRQ |	\
				  ETH_RESET_DMA | ETH_RESET_FILTER |	\
				  ETH_RESET_OFFLOAD | ETH_RESET_MAC |	\
				  ETH_RESET_PHY | ETH_RESET_RAM)	\
				 << ETH_RESET_SHARED_SHIFT)

#define BNXT_PXP_REG_LEN	0x3110

#define BNXT_IP_PROTO_FULL_MASK	0xFF
#define BNXT_IP_PROTO_WILDCARD	0x0

extern const struct ethtool_ops bnxt_ethtool_ops;

u32 bnxt_get_rxfh_indir_size(struct net_device *dev);
void _bnxt_fw_to_linkmode(unsigned long *mode, u16 fw_speeds);
u32 bnxt_fw_to_ethtool_speed(u16);
u16 bnxt_get_fw_auto_link_speeds(const unsigned long *mode);
int bnxt_hwrm_nvm_get_dev_info(struct bnxt *bp,
			       struct hwrm_nvm_get_dev_info_output *nvm_dev_info);
int bnxt_hwrm_firmware_reset(struct net_device *dev, u8 proc_type,
			     u8 self_reset, u8 flags);
int bnxt_flash_package_from_fw_obj(struct net_device *dev, const struct firmware *fw,
				   u32 install_type, struct netlink_ext_ack *extack);
int bnxt_get_pkginfo(struct net_device *dev, char *ver, int size);
void bnxt_ethtool_init(struct bnxt *bp);
void bnxt_ethtool_free(struct bnxt *bp);
int bnxt_find_nvram_item(struct net_device *dev, u16 type, u16 ordinal,
			 u16 ext, u16 *index, u32 *item_length,
			 u32 *data_length);
int bnxt_find_nvram_item(struct net_device *dev, u16 type, u16 ordinal,
			 u16 ext, u16 *index, u32 *item_length,
			 u32 *data_length);
int bnxt_flash_nvram(struct net_device *dev, u16 dir_type,
		     u16 dir_ordinal, u16 dir_ext, u16 dir_attr,
		     u32 dir_item_len, const u8 *data,
		     size_t data_len);
int bnxt_get_nvram_item(struct net_device *dev, u32 index, u32 offset,
			u32 length, u8 *data);

#endif
