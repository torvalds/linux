#
# Makefile for physical layer USB drivers
#

ccflags-$(CONFIG_USB_DEBUG) := -DDEBUG

obj-$(CONFIG_USB_OTG_UTILS)		+= phy.o

# transceiver drivers, keep the list sorted

obj-$(CONFIG_AB8500_USB)		+= ab8500-usb.o
fsl_usb2_otg-objs			:= fsl_otg.o otg_fsm.o
obj-$(CONFIG_FSL_USB2_OTG)		+= fsl_usb2_otg.o
obj-$(CONFIG_ISP1301_OMAP)		+= isp1301_omap.o
obj-$(CONFIG_MV_U3D_PHY)		+= mv_u3d_phy.o
obj-$(CONFIG_NOP_USB_XCEIV)		+= nop-usb-xceiv.o
obj-$(CONFIG_OMAP_CONTROL_USB)		+= omap-control-usb.o
obj-$(CONFIG_OMAP_USB2)			+= omap-usb2.o
obj-$(CONFIG_OMAP_USB3)			+= omap-usb3.o
obj-$(CONFIG_SAMSUNG_USBPHY)		+= samsung-usbphy.o
obj-$(CONFIG_TWL4030_USB)		+= twl4030-usb.o
obj-$(CONFIG_TWL6030_USB)		+= twl6030-usb.o
obj-$(CONFIG_USB_EHCI_TEGRA)		+= tegra_usb_phy.o
obj-$(CONFIG_USB_GPIO_VBUS)		+= gpio_vbus.o
obj-$(CONFIG_USB_ISP1301)		+= isp1301.o
obj-$(CONFIG_USB_MSM_OTG)		+= msm_otg.o
obj-$(CONFIG_USB_MV_OTG)		+= mv_otg.o
obj-$(CONFIG_USB_MXS_PHY)		+= mxs-phy.o
obj-$(CONFIG_USB_RCAR_PHY)		+= rcar-phy.o
obj-$(CONFIG_USB_ULPI)			+= ulpi.o
obj-$(CONFIG_USB_ULPI_VIEWPORT)		+= ulpi_viewport.o
