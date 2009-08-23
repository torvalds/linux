#
# Makefile for the HID driver
#
hid-objs			:= hid-core.o hid-input.o

obj-$(CONFIG_HID)		+= hid.o

hid-$(CONFIG_HID_DEBUG)		+= hid-debug.o
hid-$(CONFIG_HIDRAW)		+= hidraw.o

hid-logitech-objs		:= hid-lg.o
ifdef CONFIG_LOGITECH_FF
	hid-logitech-objs	+= hid-lgff.o
endif
ifdef CONFIG_LOGIRUMBLEPAD2_FF
	hid-logitech-objs	+= hid-lg2ff.o
endif

obj-$(CONFIG_HID_A4TECH)	+= hid-a4tech.o
obj-$(CONFIG_HID_APPLE)		+= hid-apple.o
obj-$(CONFIG_HID_BELKIN)	+= hid-belkin.o
obj-$(CONFIG_HID_CHERRY)	+= hid-cherry.o
obj-$(CONFIG_HID_CHICONY)	+= hid-chicony.o
obj-$(CONFIG_HID_CYPRESS)	+= hid-cypress.o
obj-$(CONFIG_HID_DRAGONRISE)	+= hid-drff.o
obj-$(CONFIG_HID_EZKEY)		+= hid-ezkey.o
obj-$(CONFIG_HID_GYRATION)	+= hid-gyration.o
obj-$(CONFIG_HID_KENSINGTON)	+= hid-kensington.o
obj-$(CONFIG_HID_KYE)		+= hid-kye.o
obj-$(CONFIG_HID_LOGITECH)	+= hid-logitech.o
obj-$(CONFIG_HID_MICROSOFT)	+= hid-microsoft.o
obj-$(CONFIG_HID_MONTEREY)	+= hid-monterey.o
obj-$(CONFIG_HID_NTRIG)		+= hid-ntrig.o
obj-$(CONFIG_HID_PANTHERLORD)	+= hid-pl.o
obj-$(CONFIG_HID_PETALYNX)	+= hid-petalynx.o
obj-$(CONFIG_HID_SAMSUNG)	+= hid-samsung.o
obj-$(CONFIG_HID_SMARTJOYPLUS)	+= hid-sjoy.o
obj-$(CONFIG_HID_SONY)		+= hid-sony.o
obj-$(CONFIG_HID_SUNPLUS)	+= hid-sunplus.o
obj-$(CONFIG_HID_GREENASIA)	+= hid-gaff.o
obj-$(CONFIG_HID_THRUSTMASTER)	+= hid-tmff.o
obj-$(CONFIG_HID_TOPSEED)	+= hid-topseed.o
obj-$(CONFIG_HID_ZEROPLUS)	+= hid-zpff.o
obj-$(CONFIG_HID_WACOM)		+= hid-wacom.o

obj-$(CONFIG_USB_HID)		+= usbhid/
obj-$(CONFIG_USB_MOUSE)		+= usbhid/
obj-$(CONFIG_USB_KBD)		+= usbhid/

