#
# Makefile for the USB input drivers
#

# Multipart objects.
wacom-objs	:= wacom_wac.o wacom_sys.o

obj-$(CONFIG_USB_AIPTEK)	+= aiptek.o
obj-$(CONFIG_USB_ATI_REMOTE)	+= ati_remote.o
obj-$(CONFIG_USB_ATI_REMOTE2)	+= ati_remote2.o
obj-$(CONFIG_USB_KBTAB)		+= kbtab.o
obj-$(CONFIG_USB_KEYSPAN_REMOTE)	+= keyspan_remote.o
obj-$(CONFIG_USB_TOUCHSCREEN)	+= usbtouchscreen.o
obj-$(CONFIG_USB_POWERMATE)	+= powermate.o
obj-$(CONFIG_USB_WACOM)		+= wacom.o
obj-$(CONFIG_USB_ACECAD)	+= acecad.o
obj-$(CONFIG_USB_YEALINK)	+= yealink.o
obj-$(CONFIG_USB_XPAD)		+= xpad.o
obj-$(CONFIG_USB_APPLETOUCH)	+= appletouch.o
obj-$(CONFIG_USB_GTCO)		+= gtco.o

ifeq ($(CONFIG_USB_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
