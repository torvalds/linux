# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the TI network device drivers.
#

obj-$(CONFIG_TI_CPSW) += cpsw-common.o
obj-$(CONFIG_TI_DAVINCI_EMAC) += cpsw-common.o

obj-$(CONFIG_TLAN) += tlan.o
obj-$(CONFIG_CPMAC) += cpmac.o
obj-$(CONFIG_TI_DAVINCI_EMAC) += ti_davinci_emac.o
ti_davinci_emac-y := davinci_emac.o davinci_cpdma.o
obj-$(CONFIG_TI_DAVINCI_MDIO) += davinci_mdio.o
obj-$(CONFIG_TI_CPSW_PHY_SEL) += cpsw-phy-sel.o
obj-$(CONFIG_TI_CPTS_MOD) += cpts.o
obj-$(CONFIG_TI_CPSW) += ti_cpsw.o
ti_cpsw-y := cpsw.o davinci_cpdma.o cpsw_ale.o cpsw_priv.o cpsw_sl.o cpsw_ethtool.o

obj-$(CONFIG_TI_KEYSTONE_NETCP) += keystone_netcp.o
keystone_netcp-y := netcp_core.o cpsw_ale.o
obj-$(CONFIG_TI_KEYSTONE_NETCP_ETHSS) += keystone_netcp_ethss.o
keystone_netcp_ethss-y := netcp_ethss.o netcp_sgmii.o netcp_xgbepcsr.o cpsw_ale.o
