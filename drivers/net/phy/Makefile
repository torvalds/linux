# SPDX-License-Identifier: GPL-2.0
# Makefile for Linux PHY drivers

libphy-y			:= phy.o phy-c45.o phy-core.o phy_device.o \
				   linkmode.o
mdio-bus-y			+= mdio_bus.o mdio_device.o

ifdef CONFIG_MDIO_DEVICE
obj-y				+= mdio-boardinfo.o
endif

# PHYLIB implies MDIO_DEVICE, in that case, we have a bunch of circular
# dependencies that does not make it possible to split mdio-bus objects into a
# dedicated loadable module, so we bundle them all together into libphy.ko
ifdef CONFIG_PHYLIB
libphy-y			+= $(mdio-bus-y)
else
obj-$(CONFIG_MDIO_DEVICE)	+= mdio-bus.o
endif
obj-$(CONFIG_MDIO_DEVRES)	+= mdio_devres.o
libphy-$(CONFIG_SWPHY)		+= swphy.o
libphy-$(CONFIG_LED_TRIGGER_PHY)	+= phy_led_triggers.o

obj-$(CONFIG_PHYLINK)		+= phylink.o
obj-$(CONFIG_PHYLIB)		+= libphy.o

obj-$(CONFIG_NETWORK_PHY_TIMESTAMPING) += mii_timestamper.o

obj-$(CONFIG_SFP)		+= sfp.o
sfp-obj-$(CONFIG_SFP)		+= sfp-bus.o
obj-y				+= $(sfp-obj-y) $(sfp-obj-m)

obj-$(CONFIG_ADIN_PHY)		+= adin.o
obj-$(CONFIG_AMD_PHY)		+= amd.o
aquantia-objs			+= aquantia_main.o
ifdef CONFIG_HWMON
aquantia-objs			+= aquantia_hwmon.o
endif
obj-$(CONFIG_AQUANTIA_PHY)	+= aquantia.o
obj-$(CONFIG_AT803X_PHY)	+= at803x.o
obj-$(CONFIG_AX88796B_PHY)	+= ax88796b.o
obj-$(CONFIG_BCM54140_PHY)	+= bcm54140.o
obj-$(CONFIG_BCM63XX_PHY)	+= bcm63xx.o
obj-$(CONFIG_BCM7XXX_PHY)	+= bcm7xxx.o
obj-$(CONFIG_BCM84881_PHY)	+= bcm84881.o
obj-$(CONFIG_BCM87XX_PHY)	+= bcm87xx.o
obj-$(CONFIG_BCM_CYGNUS_PHY)	+= bcm-cygnus.o
obj-$(CONFIG_BCM_NET_PHYLIB)	+= bcm-phy-lib.o
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
obj-$(CONFIG_FIXED_PHY)		+= fixed_phy.o
obj-$(CONFIG_ICPLUS_PHY)	+= icplus.o
obj-$(CONFIG_INTEL_XWAY_PHY)	+= intel-xway.o
obj-$(CONFIG_LSI_ET1011C_PHY)	+= et1011c.o
obj-$(CONFIG_LXT_PHY)		+= lxt.o
obj-$(CONFIG_MARVELL_10G_PHY)	+= marvell10g.o
obj-$(CONFIG_MARVELL_PHY)	+= marvell.o
obj-$(CONFIG_MARVELL_88X2222_PHY)	+= marvell-88x2222.o
obj-$(CONFIG_MESON_GXL_PHY)	+= meson-gxl.o
obj-$(CONFIG_MICREL_KS8995MA)	+= spi_ks8995.o
obj-$(CONFIG_MICREL_PHY)	+= micrel.o
obj-$(CONFIG_MICROCHIP_PHY)	+= microchip.o
obj-$(CONFIG_MICROCHIP_T1_PHY)	+= microchip_t1.o
obj-$(CONFIG_MICROSEMI_PHY)	+= mscc/
obj-$(CONFIG_NATIONAL_PHY)	+= national.o
obj-$(CONFIG_NXP_C45_TJA11XX_PHY)	+= nxp-c45-tja11xx.o
obj-$(CONFIG_NXP_TJA11XX_PHY)	+= nxp-tja11xx.o
obj-$(CONFIG_QSEMI_PHY)		+= qsemi.o
obj-$(CONFIG_REALTEK_PHY)	+= realtek.o
obj-$(CONFIG_RENESAS_PHY)	+= uPD60620.o
obj-$(CONFIG_ROCKCHIP_PHY)	+= rockchip.o
obj-$(CONFIG_SMSC_PHY)		+= smsc.o
obj-$(CONFIG_STE10XP)		+= ste10Xp.o
obj-$(CONFIG_TERANETICS_PHY)	+= teranetics.o
obj-$(CONFIG_VITESSE_PHY)	+= vitesse.o
obj-$(CONFIG_XILINX_GMII2RGMII) += xilinx_gmii2rgmii.o
