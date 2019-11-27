# SPDX-License-Identifier: GPL-2.0
# define_trace.h needs to know how to find our header
CFLAGS_trace.o				:= -I$(src)

cdns3-y					:= core.o drd.o

obj-$(CONFIG_USB_CDNS3)			+= cdns3.o
cdns3-$(CONFIG_USB_CDNS3_GADGET)	+= gadget.o ep0.o

ifneq ($(CONFIG_USB_CDNS3_GADGET),)
cdns3-$(CONFIG_TRACING)			+= trace.o
endif

cdns3-$(CONFIG_USB_CDNS3_HOST)		+= host.o

obj-$(CONFIG_USB_CDNS3_PCI_WRAP)	+= cdns3-pci-wrap.o
obj-$(CONFIG_USB_CDNS3_TI)		+= cdns3-ti.o
