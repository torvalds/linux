# SPDX-License-Identifier: GPL-2.0
# define_trace.h needs to know how to find our header
CFLAGS_cdns3-trace.o				:= -I$(src)
CFLAGS_cdnsp-trace.o				:= -I$(src)

obj-$(CONFIG_USB_CDNS3)			+= cdns.o

cdns-y					:= core.o drd.o cdns3-plat.o
cdns-$(CONFIG_USB_CDNS3_HOST)		+= host.o

ifneq ($(CONFIG_USB_CDNS3_GADGET),)
cdns-y					+= cdns3-gadget.o cdns3-ep0.o \
					   cdnsp-ring.o cdnsp-gadget.o \
					   cdnsp-mem.o cdnsp-ep0.o
endif

ifneq ($(CONFIG_TRACING),)
ifneq ($(CONFIG_USB_CDNS3_GADGET),)
cdns-y					+= cdns3-trace.o cdnsp-trace.o
endif
endif

##
# Platform-specific glue layers (PCI wrappers, SoC integration)
##
obj-$(CONFIG_USB_CDNS3_PCI_WRAP)		+= cdns3-pci-wrap.o
obj-$(CONFIG_USB_CDNSP_PCI)			+= cdnsp-pci.o
obj-$(CONFIG_USB_CDNS3_TI)			+= cdns3-ti.o
obj-$(CONFIG_USB_CDNS3_IMX)			+= cdns3-imx.o
obj-$(CONFIG_USB_CDNS3_STARFIVE)		+= cdns3-starfive.o
