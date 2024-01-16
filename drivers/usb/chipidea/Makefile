# SPDX-License-Identifier: GPL-2.0

# define_trace.h needs to know how to find our header
CFLAGS_trace.o			:= -I$(src)
obj-$(CONFIG_USB_CHIPIDEA)		+= ci_hdrc.o

ci_hdrc-y				:= core.o otg.o debug.o ulpi.o
ci_hdrc-$(CONFIG_USB_CHIPIDEA_UDC)	+= udc.o trace.o
ci_hdrc-$(CONFIG_USB_CHIPIDEA_HOST)	+= host.o
ci_hdrc-$(CONFIG_USB_OTG_FSM)		+= otg_fsm.o

# Glue/Bridge layers go here

obj-$(CONFIG_USB_CHIPIDEA_GENERIC)	+= ci_hdrc_usb2.o
obj-$(CONFIG_USB_CHIPIDEA_MSM)		+= ci_hdrc_msm.o
obj-$(CONFIG_USB_CHIPIDEA_PCI)		+= ci_hdrc_pci.o
obj-$(CONFIG_USB_CHIPIDEA_IMX)		+= ci_hdrc_imx.o usbmisc_imx.o
obj-$(CONFIG_USB_CHIPIDEA_TEGRA)	+= ci_hdrc_tegra.o
