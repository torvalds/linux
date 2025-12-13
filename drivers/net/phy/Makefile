# SPDX-License-Identifier: GPL-2.0
# Makefile for Linux PHY drivers

libphy-y			:= phy.o phy-c45.o phy-core.o phy_device.o \
				   linkmode.o phy_link_topology.o \
				   phy_caps.o mdio_bus_provider.o
mdio-bus-y			+= mdio_bus.o mdio_device.o

ifdef CONFIG_PHYLIB
# built-in whenever PHYLIB is built-in or module
obj-y				+= stubs.o
endif

libphy-$(CONFIG_SWPHY)		+= swphy.o
libphy-$(CONFIG_LED_TRIGGER_PHY)	+= phy_led_triggers.o
libphy-$(CONFIG_OPEN_ALLIANCE_HELPERS) += open_alliance_helpers.o

obj-$(CONFIG_MDIO_BUS)		+= mdio-bus.o
obj-$(CONFIG_PHYLINK)		+= phylink.o
obj-$(CONFIG_PHYLIB)		+= libphy.o
obj-$(CONFIG_PHYLIB)		+= mdio_devres.o
obj-$(CONFIG_PHY_PACKAGE)	+= phy_package.o

obj-$(CONFIG_NETWORK_PHY_TIMESTAMPING) += mii_timestamper.o

obj-$(CONFIG_SFP)		+= sfp.o
sfp-obj-$(CONFIG_SFP)		+= sfp-bus.o
obj-y				+= $(sfp-obj-y) $(sfp-obj-m)

obj-$(CONFIG_ADIN_PHY)		+= adin.o
obj-$(CONFIG_ADIN1100_PHY)	+= adin1100.o
obj-$(CONFIG_AIR_EN8811H_PHY)   += air_en8811h.o
obj-$(CONFIG_AMD_PHY)		+= amd.o
obj-$(CONFIG_AMCC_QT2025_PHY)	+= qt2025.o
obj-$(CONFIG_AQUANTIA_PHY)	+= aquantia/
obj-$(CONFIG_AS21XXX_PHY)	+= as21xxx.o
ifdef CONFIG_AX88796B_RUST_PHY
  obj-$(CONFIG_AX88796B_PHY)	+= ax88796b_rust.o
else
  obj-$(CONFIG_AX88796B_PHY)	+= ax88796b.o
endif
obj-$(CONFIG_BCM54140_PHY)	+= bcm54140.o
obj-$(CONFIG_BCM63XX_PHY)	+= bcm63xx.o
obj-$(CONFIG_BCM7XXX_PHY)	+= bcm7xxx.o
obj-$(CONFIG_BCM84881_PHY)	+= bcm84881.o
obj-$(CONFIG_BCM87XX_PHY)	+= bcm87xx.o
obj-$(CONFIG_BCM_CYGNUS_PHY)	+= bcm-cygnus.o
obj-$(CONFIG_BCM_NET_PHYLIB)	+= bcm-phy-lib.o
obj-$(CONFIG_BCM_NET_PHYPTP)	+= bcm-phy-ptp.o
obj-$(CONFIG_BROADCOM_PHY)	+= broadcom.o
obj-$(CONFIG_CICADA_PHY)	+= cicada.o
obj-$(CONFIG_CORTINA_PHY)	+= cortina.o
obj-$(CONFIG_DAVICOM_PHY)	+= davicom.o
obj-$(CONFIG_DP83640_PHY)	+= dp83640.o
obj-$(CONFIG_DP83822_PHY)	+= dp83822.o
obj-$(CONFIG_DP83848_PHY)	+= dp83848.o
obj-$(CONFIG_DP83867_PHY)	+= dp83867.o
obj-$(CONFIG_DP83869_PHY)	+= dp83869.o
obj-$(CONFIG_DP83TC811_PHY)	+= dp83tc811.o
obj-$(CONFIG_DP83TD510_PHY)	+= dp83td510.o
obj-$(CONFIG_DP83TG720_PHY)	+= dp83tg720.o
obj-$(CONFIG_FIXED_PHY)		+= fixed_phy.o
obj-$(CONFIG_ICPLUS_PHY)	+= icplus.o
obj-$(CONFIG_INTEL_XWAY_PHY)	+= intel-xway.o
obj-$(CONFIG_LSI_ET1011C_PHY)	+= et1011c.o
obj-$(CONFIG_LXT_PHY)		+= lxt.o
obj-$(CONFIG_MARVELL_10G_PHY)	+= marvell10g.o
obj-$(CONFIG_MARVELL_PHY)	+= marvell.o
obj-$(CONFIG_MARVELL_88Q2XXX_PHY)	+= marvell-88q2xxx.o
obj-$(CONFIG_MARVELL_88X2222_PHY)	+= marvell-88x2222.o
obj-$(CONFIG_MAXLINEAR_GPHY)	+= mxl-gpy.o
obj-$(CONFIG_MAXLINEAR_86110_PHY)	+= mxl-86110.o
obj-y				+= mediatek/
obj-$(CONFIG_MESON_GXL_PHY)	+= meson-gxl.o
obj-$(CONFIG_MICREL_PHY)	+= micrel.o
obj-$(CONFIG_MICROCHIP_PHY)	+= microchip.o
obj-$(CONFIG_MICROCHIP_PHY_RDS_PTP)	+= microchip_rds_ptp.o
obj-$(CONFIG_MICROCHIP_T1_PHY)	+= microchip_t1.o
obj-$(CONFIG_MICROCHIP_T1S_PHY) += microchip_t1s.o
obj-$(CONFIG_MICROSEMI_PHY)	+= mscc/
obj-$(CONFIG_MOTORCOMM_PHY)	+= motorcomm.o
obj-$(CONFIG_NATIONAL_PHY)	+= national.o
obj-$(CONFIG_NCN26000_PHY)	+= ncn26000.o
nxp-c45-tja-objs		+= nxp-c45-tja11xx.o
ifdef CONFIG_MACSEC
nxp-c45-tja-objs		+= nxp-c45-tja11xx-macsec.o
endif
obj-$(CONFIG_NXP_C45_TJA11XX_PHY)	+= nxp-c45-tja.o
obj-$(CONFIG_NXP_CBTX_PHY)	+= nxp-cbtx.o
obj-$(CONFIG_NXP_TJA11XX_PHY)	+= nxp-tja11xx.o
obj-y				+= qcom/
obj-$(CONFIG_QSEMI_PHY)		+= qsemi.o
obj-$(CONFIG_REALTEK_PHY)	+= realtek/
obj-$(CONFIG_RENESAS_PHY)	+= uPD60620.o
obj-$(CONFIG_ROCKCHIP_PHY)	+= rockchip.o
obj-$(CONFIG_SMSC_PHY)		+= smsc.o
obj-$(CONFIG_STE10XP)		+= ste10Xp.o
obj-$(CONFIG_TERANETICS_PHY)	+= teranetics.o
obj-$(CONFIG_VITESSE_PHY)	+= vitesse.o
obj-$(CONFIG_XILINX_GMII2RGMII) += xilinx_gmii2rgmii.o
