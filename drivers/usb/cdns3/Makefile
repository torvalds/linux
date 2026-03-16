# SPDX-License-Identifier: GPL-2.0
# define_trace.h needs to know how to find our header
CFLAGS_cdns3-trace.o				:= -I$(src)
CFLAGS_cdnsp-trace.o				:= -I$(src)

cdns-usb-common-y				:= core.o drd.o

ifeq ($(CONFIG_USB),m)
obj-m						+= cdns-usb-common.o
obj-m						+= cdns3-plat.o
else
obj-$(CONFIG_USB_CDNS_SUPPORT)			+= cdns-usb-common.o
obj-$(CONFIG_USB_CDNS_SUPPORT)			+= cdns3-plat.o
endif

cdns-usb-common-$(CONFIG_USB_CDNS_HOST) 	+= host.o

# For CDNS3 gadget
ifneq ($(CONFIG_USB_CDNS3_GADGET),)
cdns3-y						:= cdns3-gadget.o cdns3-ep0.o
cdns3-$(CONFIG_TRACING)				+= cdns3-trace.o
obj-$(CONFIG_USB_CDNS3)				+= cdns3.o
endif
obj-$(CONFIG_USB_CDNS3_PCI_WRAP)		+= cdns3-pci-wrap.o
obj-$(CONFIG_USB_CDNS3_TI)			+= cdns3-ti.o
obj-$(CONFIG_USB_CDNS3_IMX)			+= cdns3-imx.o
obj-$(CONFIG_USB_CDNS3_STARFIVE)		+= cdns3-starfive.o

# For CDNSP gadget
ifneq ($(CONFIG_USB_CDNSP_GADGET),)
cdnsp-y						:= cdnsp-ring.o cdnsp-gadget.o \
						   cdnsp-mem.o cdnsp-ep0.o
cdnsp-$(CONFIG_TRACING)				+= cdnsp-trace.o
obj-$(CONFIG_USB_CDNSP)				+= cdnsp.o
endif
obj-$(CONFIG_USB_CDNSP_PCI)			+= cdnsp-pci.o
