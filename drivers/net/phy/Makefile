# Makefile for Linux PHY drivers and MDIO bus drivers

libphy-y			:= phy.o phy-c45.o phy-core.o phy_device.o
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
libphy-$(CONFIG_SWPHY)		+= swphy.o
libphy-$(CONFIG_LED_TRIGGER_PHY)	+= phy_led_triggers.o

obj-$(CONFIG_PHYLIB)		+= libphy.o

obj-$(CONFIG_MDIO_BCM_IPROC)	+= mdio-bcm-iproc.o
obj-$(CONFIG_MDIO_BCM_UNIMAC)	+= mdio-bcm-unimac.o
obj-$(CONFIG_MDIO_BITBANG)	+= mdio-bitbang.o
obj-$(CONFIG_MDIO_BUS_MUX)	+= mdio-mux.o
obj-$(CONFIG_MDIO_BUS_MUX_BCM_IPROC)	+= mdio-mux-bcm-iproc.o
obj-$(CONFIG_MDIO_BUS_MUX_GPIO)	+= mdio-mux-gpio.o
obj-$(CONFIG_MDIO_BUS_MUX_MMIOREG) += mdio-mux-mmioreg.o
obj-$(CONFIG_MDIO_CAVIUM)	+= mdio-cavium.o
obj-$(CONFIG_MDIO_GPIO)		+= mdio-gpio.o
obj-$(CONFIG_MDIO_HISI_FEMAC)	+= mdio-hisi-femac.o
obj-$(CONFIG_MDIO_MOXART)	+= mdio-moxart.o
obj-$(CONFIG_MDIO_OCTEON)	+= mdio-octeon.o
obj-$(CONFIG_MDIO_SUN4I)	+= mdio-sun4i.o
obj-$(CONFIG_MDIO_THUNDER)	+= mdio-thunder.o
obj-$(CONFIG_MDIO_XGENE)	+= mdio-xgene.o

obj-$(CONFIG_AMD_PHY)		+= amd.o
obj-$(CONFIG_AQUANTIA_PHY)	+= aquantia.o
obj-$(CONFIG_AT803X_PHY)	+= at803x.o
obj-$(CONFIG_BCM63XX_PHY)	+= bcm63xx.o
obj-$(CONFIG_BCM7XXX_PHY)	+= bcm7xxx.o
obj-$(CONFIG_BCM87XX_PHY)	+= bcm87xx.o
obj-$(CONFIG_BCM_CYGNUS_PHY)	+= bcm-cygnus.o
obj-$(CONFIG_BCM_NET_PHYLIB)	+= bcm-phy-lib.o
obj-$(CONFIG_BROADCOM_PHY)	+= broadcom.o
obj-$(CONFIG_CICADA_PHY)	+= cicada.o
obj-$(CONFIG_CORTINA_PHY)	+= cortina.o
obj-$(CONFIG_DAVICOM_PHY)	+= davicom.o
obj-$(CONFIG_DP83640_PHY)	+= dp83640.o
obj-$(CONFIG_DP83848_PHY)	+= dp83848.o
obj-$(CONFIG_DP83867_PHY)	+= dp83867.o
obj-$(CONFIG_FIXED_PHY)		+= fixed_phy.o
obj-$(CONFIG_ICPLUS_PHY)	+= icplus.o
obj-$(CONFIG_INTEL_XWAY_PHY)	+= intel-xway.o
obj-$(CONFIG_LSI_ET1011C_PHY)	+= et1011c.o
obj-$(CONFIG_LXT_PHY)		+= lxt.o
obj-$(CONFIG_MARVELL_PHY)	+= marvell.o
obj-$(CONFIG_MARVELL_10G_PHY)	+= marvell10g.o
obj-$(CONFIG_MESON_GXL_PHY)	+= meson-gxl.o
obj-$(CONFIG_MICREL_KS8995MA)	+= spi_ks8995.o
obj-$(CONFIG_MICREL_PHY)	+= micrel.o
obj-$(CONFIG_MICROCHIP_PHY)	+= microchip.o
obj-$(CONFIG_MICROSEMI_PHY)	+= mscc.o
obj-$(CONFIG_NATIONAL_PHY)	+= national.o
obj-$(CONFIG_QSEMI_PHY)		+= qsemi.o
obj-$(CONFIG_REALTEK_PHY)	+= realtek.o
obj-$(CONFIG_SMSC_PHY)		+= smsc.o
obj-$(CONFIG_STE10XP)		+= ste10Xp.o
obj-$(CONFIG_TERANETICS_PHY)	+= teranetics.o
obj-$(CONFIG_VITESSE_PHY)	+= vitesse.o
obj-$(CONFIG_XILINX_GMII2RGMII) += xilinx_gmii2rgmii.o
