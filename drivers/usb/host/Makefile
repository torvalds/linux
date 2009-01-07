#
# Makefile for USB Host Controller Drivers
#

ifeq ($(CONFIG_USB_DEBUG),y)
	EXTRA_CFLAGS		+= -DDEBUG
endif

isp1760-objs := isp1760-hcd.o isp1760-if.o

obj-$(CONFIG_USB_WHCI_HCD)	+= whci/

obj-$(CONFIG_PCI)		+= pci-quirks.o

obj-$(CONFIG_USB_EHCI_HCD)	+= ehci-hcd.o
obj-$(CONFIG_USB_OXU210HP_HCD)	+= oxu210hp-hcd.o
obj-$(CONFIG_USB_ISP116X_HCD)	+= isp116x-hcd.o
obj-$(CONFIG_USB_OHCI_HCD)	+= ohci-hcd.o
obj-$(CONFIG_USB_UHCI_HCD)	+= uhci-hcd.o
obj-$(CONFIG_USB_SL811_HCD)	+= sl811-hcd.o
obj-$(CONFIG_USB_SL811_CS)	+= sl811_cs.o
obj-$(CONFIG_USB_U132_HCD)	+= u132-hcd.o
obj-$(CONFIG_USB_R8A66597_HCD)	+= r8a66597-hcd.o
obj-$(CONFIG_USB_ISP1760_HCD)	+= isp1760.o
obj-$(CONFIG_USB_HWA_HCD)	+= hwa-hc.o
