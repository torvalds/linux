#
# Makefile for the USB Mass Storage device drivers.
#
# 15 Aug 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
#

EXTRA_CFLAGS	:= -Idrivers/scsi

obj-$(CONFIG_USB_STORAGE)	+= usb-storage.o

usb-storage-obj-$(CONFIG_USB_STORAGE_DEBUG)	+= debug.o

usb-storage-objs :=	scsiglue.o protocol.o transport.o usb.o \
			initializers.o sierra_ms.o option_ms.o $(usb-storage-obj-y)

ifeq ($(CONFIG_USB_LIBUSUAL),)
	usb-storage-objs	+= usual-tables.o
else
	obj-$(CONFIG_USB)	+= usb-libusual.o
	usb-libusual-objs	:= libusual.o usual-tables.o
endif

obj-$(CONFIG_USB_STORAGE_ALAUDA)	+= ums-alauda.o
obj-$(CONFIG_USB_STORAGE_CYPRESS_ATACB) += ums-cypress.o
obj-$(CONFIG_USB_STORAGE_DATAFAB)	+= ums-datafab.o
obj-$(CONFIG_USB_STORAGE_FREECOM)	+= ums-freecom.o
obj-$(CONFIG_USB_STORAGE_ISD200)	+= ums-isd200.o
obj-$(CONFIG_USB_STORAGE_JUMPSHOT)	+= ums-jumpshot.o
obj-$(CONFIG_USB_STORAGE_KARMA)		+= ums-karma.o
obj-$(CONFIG_USB_STORAGE_ONETOUCH)	+= ums-onetouch.o
obj-$(CONFIG_USB_STORAGE_SDDR09)	+= ums-sddr09.o
obj-$(CONFIG_USB_STORAGE_SDDR55)	+= ums-sddr55.o
obj-$(CONFIG_USB_STORAGE_USBAT)		+= ums-usbat.o

ums-alauda-objs		:= alauda.o
ums-cypress-objs	:= cypress_atacb.o
ums-datafab-objs	:= datafab.o
ums-freecom-objs	:= freecom.o
ums-isd200-objs		:= isd200.o
ums-jumpshot-objs	:= jumpshot.o
ums-karma-objs		:= karma.o
ums-onetouch-objs	:= onetouch.o
ums-sddr09-objs		:= sddr09.o
ums-sddr55-objs		:= sddr55.o
ums-usbat-objs		:= shuttle_usbat.o
