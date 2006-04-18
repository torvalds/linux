#
# Makefile for the USB input drivers
#

# Multipart objects.
usbhid-objs	:= hid-core.o

# Optional parts of multipart objects.

ifeq ($(CONFIG_USB_HIDDEV),y)
	usbhid-objs	+= hiddev.o
endif
ifeq ($(CONFIG_USB_HIDINPUT),y)
	usbhid-objs	+= hid-input.o
endif
ifeq ($(CONFIG_HID_PID),y)
	usbhid-objs	+= pid.o
endif
ifeq ($(CONFIG_LOGITECH_FF),y)
	usbhid-objs	+= hid-lgff.o
endif
ifeq ($(CONFIG_THRUSTMASTER_FF),y)
	usbhid-objs	+= hid-tmff.o
endif
ifeq ($(CONFIG_HID_FF),y)
	usbhid-objs	+= hid-ff.o
endif

obj-$(CONFIG_USB_AIPTEK)	+= aiptek.o
obj-$(CONFIG_USB_ATI_REMOTE)	+= ati_remote.o
obj-$(CONFIG_USB_ATI_REMOTE2)	+= ati_remote2.o
obj-$(CONFIG_USB_HID)		+= usbhid.o
obj-$(CONFIG_USB_KBD)		+= usbkbd.o
obj-$(CONFIG_USB_KBTAB)		+= kbtab.o
obj-$(CONFIG_USB_KEYSPAN_REMOTE)	+= keyspan_remote.o
obj-$(CONFIG_USB_MOUSE)		+= usbmouse.o
obj-$(CONFIG_USB_MTOUCH)	+= mtouchusb.o
obj-$(CONFIG_USB_ITMTOUCH)	+= itmtouch.o
obj-$(CONFIG_USB_EGALAX)	+= touchkitusb.o
obj-$(CONFIG_USB_TOUCHSCREEN)	+= usbtouchscreen.o
obj-$(CONFIG_USB_POWERMATE)	+= powermate.o
obj-$(CONFIG_USB_WACOM)		+= wacom.o
obj-$(CONFIG_USB_ACECAD)	+= acecad.o
obj-$(CONFIG_USB_YEALINK)	+= yealink.o
obj-$(CONFIG_USB_XPAD)		+= xpad.o
obj-$(CONFIG_USB_APPLETOUCH)	+= appletouch.o

ifeq ($(CONFIG_USB_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
