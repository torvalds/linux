#
# Makefile for the HID driver
#
hid-objs			:= hid-core.o hid-input.o

ifdef CONFIG_DEBUG_FS
	hid-objs		+= hid-debug.o
endif

obj-$(CONFIG_HID)		+= hid.o

hid-$(CONFIG_HIDRAW)		+= hidraw.o

hid-logitech-objs		:= hid-lg.o
ifdef CONFIG_LOGITECH_FF
	hid-logitech-objs	+= hid-lgff.o
endif
ifdef CONFIG_LOGIRUMBLEPAD2_FF
	hid-logitech-objs	+= hid-lg2ff.o
endif
ifdef CONFIG_LOGIG940_FF
	hid-logitech-objs	+= hid-lg3ff.o
endif

obj-$(CONFIG_HID_3M_PCT)	+= hid-3m-pct.o
obj-$(CONFIG_HID_A4TECH)	+= hid-a4tech.o
obj-$(CONFIG_HID_ACRUX_FF)	+= hid-axff.o
obj-$(CONFIG_HID_APPLE)		+= hid-apple.o
obj-$(CONFIG_HID_BELKIN)	+= hid-belkin.o
obj-$(CONFIG_HID_CANDO)		+= hid-cando.o
obj-$(CONFIG_HID_CHERRY)	+= hid-cherry.o
obj-$(CONFIG_HID_CHICONY)	+= hid-chicony.o
obj-$(CONFIG_HID_CYPRESS)	+= hid-cypress.o
obj-$(CONFIG_HID_DRAGONRISE)	+= hid-drff.o
obj-$(CONFIG_HID_EGALAX)	+= hid-egalax.o
obj-$(CONFIG_HID_ELECOM)	+= hid-elecom.o
obj-$(CONFIG_HID_EZKEY)		+= hid-ezkey.o
obj-$(CONFIG_HID_GYRATION)	+= hid-gyration.o
obj-$(CONFIG_HID_KENSINGTON)	+= hid-kensington.o
obj-$(CONFIG_HID_KYE)		+= hid-kye.o
obj-$(CONFIG_HID_LOGITECH)	+= hid-logitech.o
obj-$(CONFIG_HID_MAGICMOUSE)    += hid-magicmouse.o
obj-$(CONFIG_HID_MICROSOFT)	+= hid-microsoft.o
obj-$(CONFIG_HID_MONTEREY)	+= hid-monterey.o
obj-$(CONFIG_HID_MOSART)	+= hid-mosart.o
obj-$(CONFIG_HID_NTRIG)		+= hid-ntrig.o
obj-$(CONFIG_HID_ORTEK)		+= hid-ortek.o
obj-$(CONFIG_HID_PRODIKEYS)	+= hid-prodikeys.o
obj-$(CONFIG_HID_QUANTA)	+= hid-quanta.o
obj-$(CONFIG_HID_PANTHERLORD)	+= hid-pl.o
obj-$(CONFIG_HID_PETALYNX)	+= hid-petalynx.o
obj-$(CONFIG_HID_PICOLCD)	+= hid-picolcd.o
obj-$(CONFIG_HID_ROCCAT)	+= hid-roccat.o
obj-$(CONFIG_HID_ROCCAT_KONE)	+= hid-roccat-kone.o
obj-$(CONFIG_HID_SAMSUNG)	+= hid-samsung.o
obj-$(CONFIG_HID_SMARTJOYPLUS)	+= hid-sjoy.o
obj-$(CONFIG_HID_SONY)		+= hid-sony.o
obj-$(CONFIG_HID_STANTUM)	+= hid-stantum.o
obj-$(CONFIG_HID_SUNPLUS)	+= hid-sunplus.o
obj-$(CONFIG_HID_GREENASIA)	+= hid-gaff.o
obj-$(CONFIG_HID_THRUSTMASTER)	+= hid-tmff.o
obj-$(CONFIG_HID_TOPSEED)	+= hid-topseed.o
obj-$(CONFIG_HID_TWINHAN)	+= hid-twinhan.o
obj-$(CONFIG_HID_ZEROPLUS)	+= hid-zpff.o
obj-$(CONFIG_HID_ZYDACRON)	+= hid-zydacron.o
obj-$(CONFIG_HID_WACOM)		+= hid-wacom.o

obj-$(CONFIG_USB_HID)		+= usbhid/
obj-$(CONFIG_USB_MOUSE)		+= usbhid/
obj-$(CONFIG_USB_KBD)		+= usbhid/

