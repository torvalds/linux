#
# Makefile for the HID driver
#
hid-y			:= hid-core.o hid-input.o

ifdef CONFIG_DEBUG_FS
	hid-objs		+= hid-debug.o
endif

obj-$(CONFIG_HID)		+= hid.o
obj-$(CONFIG_UHID)		+= uhid.o

obj-$(CONFIG_HID_GENERIC)	+= hid-generic.o

hid-$(CONFIG_HIDRAW)		+= hidraw.o

hid-logitech-y		:= hid-lg.o
ifdef CONFIG_LOGITECH_FF
	hid-logitech-y	+= hid-lgff.o
endif
ifdef CONFIG_LOGIRUMBLEPAD2_FF
	hid-logitech-y	+= hid-lg2ff.o
endif
ifdef CONFIG_LOGIG940_FF
	hid-logitech-y	+= hid-lg3ff.o
endif
ifdef CONFIG_LOGIWHEELS_FF
	hid-logitech-y	+= hid-lg4ff.o
endif

hid-wiimote-y		:= hid-wiimote-core.o
ifdef CONFIG_HID_WIIMOTE_EXT
	hid-wiimote-y	+= hid-wiimote-ext.o
endif
ifdef CONFIG_DEBUG_FS
	hid-wiimote-y	+= hid-wiimote-debug.o
endif

obj-$(CONFIG_HID_A4TECH)	+= hid-a4tech.o
obj-$(CONFIG_HID_ACRUX)		+= hid-axff.o
obj-$(CONFIG_HID_APPLE)		+= hid-apple.o
obj-$(CONFIG_HID_APPLEIR)	+= hid-appleir.o
obj-$(CONFIG_HID_AUREAL)        += hid-aureal.o
obj-$(CONFIG_HID_BELKIN)	+= hid-belkin.o
obj-$(CONFIG_HID_CHERRY)	+= hid-cherry.o
obj-$(CONFIG_HID_CHICONY)	+= hid-chicony.o
obj-$(CONFIG_HID_CYPRESS)	+= hid-cypress.o
obj-$(CONFIG_HID_DRAGONRISE)	+= hid-dr.o
obj-$(CONFIG_HID_EMS_FF)	+= hid-emsff.o
obj-$(CONFIG_HID_ELECOM)	+= hid-elecom.o
obj-$(CONFIG_HID_EZKEY)		+= hid-ezkey.o
obj-$(CONFIG_HID_GYRATION)	+= hid-gyration.o
obj-$(CONFIG_HID_HOLTEK)	+= hid-holtek-kbd.o
obj-$(CONFIG_HID_HOLTEK)	+= hid-holtekff.o
obj-$(CONFIG_HID_HYPERV_MOUSE)	+= hid-hyperv.o
obj-$(CONFIG_HID_ICADE)		+= hid-icade.o
obj-$(CONFIG_HID_KENSINGTON)	+= hid-kensington.o
obj-$(CONFIG_HID_KEYTOUCH)	+= hid-keytouch.o
obj-$(CONFIG_HID_KYE)		+= hid-kye.o
obj-$(CONFIG_HID_LCPOWER)       += hid-lcpower.o
obj-$(CONFIG_HID_LENOVO_TPKBD)	+= hid-lenovo-tpkbd.o
obj-$(CONFIG_HID_LOGITECH)	+= hid-logitech.o
obj-$(CONFIG_HID_LOGITECH_DJ)	+= hid-logitech-dj.o
obj-$(CONFIG_HID_MAGICMOUSE)    += hid-magicmouse.o
obj-$(CONFIG_HID_MICROSOFT)	+= hid-microsoft.o
obj-$(CONFIG_HID_MONTEREY)	+= hid-monterey.o
obj-$(CONFIG_HID_MULTITOUCH)	+= hid-multitouch.o
obj-$(CONFIG_HID_NTRIG)		+= hid-ntrig.o
obj-$(CONFIG_HID_ORTEK)		+= hid-ortek.o
obj-$(CONFIG_HID_PRODIKEYS)	+= hid-prodikeys.o
obj-$(CONFIG_HID_PANTHERLORD)	+= hid-pl.o
obj-$(CONFIG_HID_PETALYNX)	+= hid-petalynx.o
obj-$(CONFIG_HID_PICOLCD)	+= hid-picolcd.o
hid-picolcd-y			+= hid-picolcd_core.o
ifdef CONFIG_HID_PICOLCD_FB
hid-picolcd-y			+= hid-picolcd_fb.o
endif
ifdef CONFIG_HID_PICOLCD_BACKLIGHT
hid-picolcd-y			+= hid-picolcd_backlight.o
endif
ifdef CONFIG_HID_PICOLCD_LCD
hid-picolcd-y			+= hid-picolcd_lcd.o
endif
ifdef CONFIG_HID_PICOLCD_LEDS
hid-picolcd-y			+= hid-picolcd_leds.o
endif
ifdef CONFIG_HID_PICOLCD_CIR
hid-picolcd-y			+= hid-picolcd_cir.o
endif
ifdef CONFIG_DEBUG_FS
hid-picolcd-y			+= hid-picolcd_debugfs.o
endif

obj-$(CONFIG_HID_PRIMAX)	+= hid-primax.o
obj-$(CONFIG_HID_PS3REMOTE)	+= hid-ps3remote.o
obj-$(CONFIG_HID_ROCCAT)	+= hid-roccat.o hid-roccat-common.o \
	hid-roccat-arvo.o hid-roccat-isku.o hid-roccat-kone.o \
	hid-roccat-koneplus.o hid-roccat-konepure.o hid-roccat-kovaplus.o \
	hid-roccat-lua.o hid-roccat-pyra.o hid-roccat-savu.o
obj-$(CONFIG_HID_SAITEK)	+= hid-saitek.o
obj-$(CONFIG_HID_SAMSUNG)	+= hid-samsung.o
obj-$(CONFIG_HID_SMARTJOYPLUS)	+= hid-sjoy.o
obj-$(CONFIG_HID_SONY)		+= hid-sony.o
obj-$(CONFIG_HID_SPEEDLINK)	+= hid-speedlink.o
obj-$(CONFIG_HID_STEELSERIES)	+= hid-steelseries.o
obj-$(CONFIG_HID_SUNPLUS)	+= hid-sunplus.o
obj-$(CONFIG_HID_GREENASIA)	+= hid-gaff.o
obj-$(CONFIG_HID_THINGM)	+= hid-thingm.o
obj-$(CONFIG_HID_THRUSTMASTER)	+= hid-tmff.o
obj-$(CONFIG_HID_TIVO)		+= hid-tivo.o
obj-$(CONFIG_HID_TOPSEED)	+= hid-topseed.o
obj-$(CONFIG_HID_TWINHAN)	+= hid-twinhan.o
obj-$(CONFIG_HID_UCLOGIC)	+= hid-uclogic.o
obj-$(CONFIG_HID_ZEROPLUS)	+= hid-zpff.o
obj-$(CONFIG_HID_ZYDACRON)	+= hid-zydacron.o
obj-$(CONFIG_HID_WACOM)		+= hid-wacom.o
obj-$(CONFIG_HID_WALTOP)	+= hid-waltop.o
obj-$(CONFIG_HID_WIIMOTE)	+= hid-wiimote.o
obj-$(CONFIG_HID_SENSOR_HUB)	+= hid-sensor-hub.o

obj-$(CONFIG_USB_HID)		+= usbhid/
obj-$(CONFIG_USB_MOUSE)		+= usbhid/
obj-$(CONFIG_USB_KBD)		+= usbhid/

obj-$(CONFIG_I2C_HID)		+= i2c-hid/
