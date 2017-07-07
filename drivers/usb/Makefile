#
# Makefile for the kernel USB device drivers.
#

# Object files in subdirectories

obj-$(CONFIG_USB)		+= core/
obj-$(CONFIG_USB_SUPPORT)	+= phy/

obj-$(CONFIG_USB_DWC3)		+= dwc3/
obj-$(CONFIG_USB_DWC2)		+= dwc2/
obj-$(CONFIG_USB_ISP1760)	+= isp1760/

obj-$(CONFIG_USB_MON)		+= mon/
obj-$(CONFIG_USB_MTU3)		+= mtu3/

obj-$(CONFIG_USB_PCI)		+= host/
obj-$(CONFIG_USB_EHCI_HCD)	+= host/
obj-$(CONFIG_USB_ISP116X_HCD)	+= host/
obj-$(CONFIG_USB_OHCI_HCD)	+= host/
obj-$(CONFIG_USB_UHCI_HCD)	+= host/
obj-$(CONFIG_USB_FHCI_HCD)	+= host/
obj-$(CONFIG_USB_XHCI_HCD)	+= host/
obj-$(CONFIG_USB_SL811_HCD)	+= host/
obj-$(CONFIG_USB_ISP1362_HCD)	+= host/
obj-$(CONFIG_USB_U132_HCD)	+= host/
obj-$(CONFIG_USB_R8A66597_HCD)	+= host/
obj-$(CONFIG_USB_HWA_HCD)	+= host/
obj-$(CONFIG_USB_IMX21_HCD)	+= host/
obj-$(CONFIG_USB_FSL_USB2)	+= host/
obj-$(CONFIG_USB_FOTG210_HCD)	+= host/
obj-$(CONFIG_USB_MAX3421_HCD)	+= host/

obj-$(CONFIG_USB_C67X00_HCD)	+= c67x00/

obj-$(CONFIG_USB_WUSB)		+= wusbcore/

obj-$(CONFIG_USB_ACM)		+= class/
obj-$(CONFIG_USB_PRINTER)	+= class/
obj-$(CONFIG_USB_WDM)		+= class/
obj-$(CONFIG_USB_TMC)		+= class/

obj-$(CONFIG_USB_STORAGE)	+= storage/
obj-$(CONFIG_USB)		+= storage/

obj-$(CONFIG_USB_MDC800)	+= image/
obj-$(CONFIG_USB_MICROTEK)	+= image/

obj-$(CONFIG_USB_SERIAL)	+= serial/

obj-$(CONFIG_USB)		+= misc/
obj-$(CONFIG_EARLY_PRINTK_USB)	+= early/

obj-$(CONFIG_USB_ATM)		+= atm/
obj-$(CONFIG_USB_SPEEDTOUCH)	+= atm/

obj-$(CONFIG_USB_MUSB_HDRC)	+= musb/
obj-$(CONFIG_USB_CHIPIDEA)	+= chipidea/
obj-$(CONFIG_USB_RENESAS_USBHS)	+= renesas_usbhs/
obj-$(CONFIG_USB_GADGET)	+= gadget/

obj-$(CONFIG_USB_COMMON)	+= common/

obj-$(CONFIG_USBIP_CORE)	+= usbip/

obj-$(CONFIG_TYPEC)		+= typec/
