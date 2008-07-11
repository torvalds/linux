#
# Makefile for the kernel USB device drivers.
#

# Object files in subdirectories

obj-$(CONFIG_USB)		+= core/

obj-$(CONFIG_USB_MON)		+= mon/

obj-$(CONFIG_PCI)		+= host/
obj-$(CONFIG_USB_EHCI_HCD)	+= host/
obj-$(CONFIG_USB_ISP116X_HCD)	+= host/
obj-$(CONFIG_USB_OHCI_HCD)	+= host/
obj-$(CONFIG_USB_UHCI_HCD)	+= host/
obj-$(CONFIG_USB_SL811_HCD)	+= host/
obj-$(CONFIG_USB_U132_HCD)	+= host/
obj-$(CONFIG_USB_R8A66597_HCD)	+= host/

obj-$(CONFIG_USB_C67X00_HCD)	+= c67x00/

obj-$(CONFIG_USB_ACM)		+= class/
obj-$(CONFIG_USB_PRINTER)	+= class/

obj-$(CONFIG_USB_STORAGE)	+= storage/
obj-$(CONFIG_USB)		+= storage/

obj-$(CONFIG_USB_MDC800)	+= image/
obj-$(CONFIG_USB_MICROTEK)	+= image/

obj-$(CONFIG_USB_SERIAL)	+= serial/

obj-$(CONFIG_USB)		+= misc/

obj-$(CONFIG_USB_ATM)		+= atm/
obj-$(CONFIG_USB_SPEEDTOUCH)	+= atm/
