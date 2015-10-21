#
# Makefile for the HID driver
#
hid-y			:= hid-core.o hid-input.o
hid-$(CONFIG_DEBUG_FS)		+= hid-debug.o

obj-$(CONFIG_HID)		+= hid.o
obj-$(CONFIG_UHID)		+= uhid.o

obj-$(CONFIG_HID_GENERIC)	+= hid-generic.o

hid-$(CONFIG_HIDRAW)		+= hidraw.o

hid-logitech-y		:= hid-lg.o
hid-logitech-$(CONFIG_LOGITECH_FF)	+= hid-lgff.o
hid-logitech-$(CONFIG_LOGIRUMBLEPAD2_FF)	+= hid-lg2ff.o
hid-logitech-$(CONFIG_LOGIG940_FF)	+= hid-lg3ff.o
hid-logitech-$(CONFIG_LOGIWHEELS_FF)	+= hid-lg4ff.o

hid-wiimote-y		:= hid-wiimote-core.o hid-wiimote-modules.o
hid-wiimote-$(CONFIG_DEBUG_FS)	+= hid-wiimote-debug.o

obj-$(CONFIG_HID_A4TECH)	+= hid-a4tech.o
obj-$(CONFIG_HID_ACRUX)		+= hid-axff.o
obj-$(CONFIG_HID_APPLE)		+= hid-apple.o
obj-$(CONFIG_HID_APPLEIR)	+= hid-appleir.o
obj-$(CONFIG_HID_AUREAL)	+= hid-aureal.o
obj-$(CONFIG_HID_BELKIN)	+= hid-belkin.o
obj-$(CONFIG_HID_BETOP_FF)	+= hid-betopff.o
obj-$(CONFIG_HID_CHERRY)	+= hid-cherry.o
obj-$(CONFIG_HID_CHICONY)	+= hid-chicony.o
obj-$(CONFIG_HID_CP2112)	+= hid-cp2112.o
obj-$(CONFIG_HID_CYPRESS)	+= hid-cypress.o
obj-$(CONFIG_HID_DRAGONRISE)	+= hid-dr.o
obj-$(CONFIG_HID_EMS_FF)	+= hid-emsff.o
obj-$(CONFIG_HID_ELECOM)	+= hid-elecom.o
obj-$(CONFIG_HID_ELO)		+= hid-elo.o
obj-$(CONFIG_HID_EZKEY)		+= hid-ezkey.o
obj-$(CONFIG_HID_GEMBIRD)	+= hid-gembird.o
obj-$(CONFIG_HID_GT683R)	+= hid-gt683r.o
obj-$(CONFIG_HID_GYRATION)	+= hid-gyration.o
obj-$(CONFIG_HID_HOLTEK)	+= hid-holtek-kbd.o
obj-$(CONFIG_HID_HOLTEK)	+= hid-holtek-mouse.o
obj-$(CONFIG_HID_HOLTEK)	+= hid-holtekff.o
obj-$(CONFIG_HID_HYPERV_MOUSE)	+= hid-hyperv.o
obj-$(CONFIG_HID_ICADE)		+= hid-icade.o
obj-$(CONFIG_HID_KENSINGTON)	+= hid-kensington.o
obj-$(CONFIG_HID_KEYTOUCH)	+= hid-keytouch.o
obj-$(CONFIG_HID_KYE)		+= hid-kye.o
obj-$(CONFIG_HID_LCPOWER)	+= hid-lcpower.o
obj-$(CONFIG_HID_LENOVO)	+= hid-lenovo.o
obj-$(CONFIG_HID_LOGITECH)	+= hid-logitech.o
obj-$(CONFIG_HID_LOGITECH_DJ)	+= hid-logitech-dj.o
obj-$(CONFIG_HID_LOGITECH_HIDPP)	+= hid-logitech-hidpp.o
obj-$(CONFIG_HID_MAGICMOUSE)	+= hid-magicmouse.o
obj-$(CONFIG_HID_MICROSOFT)	+= hid-microsoft.o
obj-$(CONFIG_HID_MONTEREY)	+= hid-monterey.o
obj-$(CONFIG_HID_MULTITOUCH)	+= hid-multitouch.o
obj-$(CONFIG_HID_NTRIG)		+= hid-ntrig.o
obj-$(CONFIG_HID_ORTEK)		+= hid-ortek.o
obj-$(CONFIG_HID_PRODIKEYS)	+= hid-prodikeys.o
obj-$(CONFIG_HID_PANTHERLORD)	+= hid-pl.o
obj-$(CONFIG_HID_PENMOUNT)	+= hid-penmount.o
obj-$(CONFIG_HID_PETALYNX)	+= hid-petalynx.o
obj-$(CONFIG_HID_PICOLCD)	+= hid-picolcd.o
hid-picolcd-y			+= hid-picolcd_core.o
hid-picolcd-$(CONFIG_HID_PICOLCD_FB)	+= hid-picolcd_fb.o
hid-picolcd-$(CONFIG_HID_PICOLCD_BACKLIGHT)	+= hid-picolcd_backlight.o
hid-picolcd-$(CONFIG_HID_PICOLCD_LCD)	+= hid-picolcd_lcd.o
hid-picolcd-$(CONFIG_HID_PICOLCD_LEDS)	+= hid-picolcd_leds.o
hid-picolcd-$(CONFIG_HID_PICOLCD_CIR)	+= hid-picolcd_cir.o
hid-picolcd-$(CONFIG_DEBUG_FS)		+= hid-picolcd_debugfs.o

obj-$(CONFIG_HID_PLANTRONICS)	+= hid-plantronics.o
obj-$(CONFIG_HID_PRIMAX)	+= hid-primax.o
obj-$(CONFIG_HID_ROCCAT)	+= hid-roccat.o hid-roccat-common.o \
	hid-roccat-arvo.o hid-roccat-isku.o hid-roccat-kone.o \
	hid-roccat-koneplus.o hid-roccat-konepure.o hid-roccat-kovaplus.o \
	hid-roccat-lua.o hid-roccat-pyra.o hid-roccat-ryos.o hid-roccat-savu.o
obj-$(CONFIG_HID_RMI)		+= hid-rmi.o
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
obj-$(CONFIG_HID_XINMO)		+= hid-xinmo.o
obj-$(CONFIG_HID_ZEROPLUS)	+= hid-zpff.o
obj-$(CONFIG_HID_ZYDACRON)	+= hid-zydacron.o

wacom-objs			:= wacom_wac.o wacom_sys.o
obj-$(CONFIG_HID_WACOM)		+= wacom.o
obj-$(CONFIG_HID_WALTOP)	+= hid-waltop.o
obj-$(CONFIG_HID_WIIMOTE)	+= hid-wiimote.o
obj-$(CONFIG_HID_SENSOR_HUB)	+= hid-sensor-hub.o
obj-$(CONFIG_HID_SENSOR_CUSTOM_SENSOR)	+= hid-sensor-custom.o

obj-$(CONFIG_USB_HID)		+= usbhid/
obj-$(CONFIG_USB_MOUSE)		+= usbhid/
obj-$(CONFIG_USB_KBD)		+= usbhid/

obj-$(CONFIG_I2C_HID)		+= i2c-hid/
