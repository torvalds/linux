obj-$(CONFIG_USB_CHIPIDEA)		+= ci_hdrc.o

ci_hdrc-y				:= core.o otg.o debug.o
ci_hdrc-$(CONFIG_USB_CHIPIDEA_UDC)	+= udc.o
ci_hdrc-$(CONFIG_USB_CHIPIDEA_HOST)	+= host.o
ci_hdrc-$(CONFIG_USB_OTG_FSM)		+= otg_fsm.o
ci_hdrc-$(CONFIG_USB_CHIPIDEA_ULPI)	+= ulpi.o

# Glue/Bridge layers go here

obj-$(CONFIG_USB_CHIPIDEA)	+= ci_hdrc_usb2.o
obj-$(CONFIG_USB_CHIPIDEA)	+= ci_hdrc_msm.o
obj-$(CONFIG_USB_CHIPIDEA)	+= ci_hdrc_zevio.o

obj-$(CONFIG_USB_CHIPIDEA_PCI)	+= ci_hdrc_pci.o

obj-$(CONFIG_USB_CHIPIDEA_OF)	+= usbmisc_imx.o ci_hdrc_imx.o
obj-$(CONFIG_USB_CHIPIDEA_OF)	+= ci_hdrc_tegra.o
