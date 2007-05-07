#
# Makefile for the USB input drivers
#

obj-$(CONFIG_USB_ATI_REMOTE)	+= ati_remote.o
obj-$(CONFIG_USB_ATI_REMOTE2)	+= ati_remote2.o
obj-$(CONFIG_USB_KEYSPAN_REMOTE)	+= keyspan_remote.o
obj-$(CONFIG_USB_TOUCHSCREEN)	+= usbtouchscreen.o
obj-$(CONFIG_USB_POWERMATE)	+= powermate.o
obj-$(CONFIG_USB_YEALINK)	+= yealink.o
obj-$(CONFIG_USB_XPAD)		+= xpad.o
obj-$(CONFIG_USB_APPLETOUCH)	+= appletouch.o

ifeq ($(CONFIG_USB_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
