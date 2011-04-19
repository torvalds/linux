#
# for Renesas USB
#

obj-$(CONFIG_USB_RENESAS_USBHS)	+= renesas_usbhs.o

renesas_usbhs-y			:= common.o mod.o pipe.o

renesas_usbhs-$(CONFIG_USB_RENESAS_USBHS_UDC)	+= mod_gadget.o
