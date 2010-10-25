#
# Makefile for the USB Mass Storage device drivers.
#
# 15 Aug 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
#

ccflags-y := -Idrivers/scsi

obj-$(CONFIG_USB_UAS)		+= uas.o
obj-$(CONFIG_USB_STORAGE)	+= usb-storage.o

usb-storage-y := scsiglue.o protocol.o transport.o usb.o
usb-storage-y += initializers.o sierra_ms.o option_ms.o

usb-storage-$(CONFIG_USB_STORAGE_DEBUG) += debug.o

ifeq ($(CONFIG_USB_LIBUSUAL),)
	usb-storage-y		+= usual-tables.o
else
	obj-$(CONFIG_USB)	+= usb-libusual.o
	usb-libusual-y		:= libusual.o usual-tables.o
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

ums-alauda-y		:= alauda.o
ums-cypress-y		:= cypress_atacb.o
ums-datafab-y		:= datafab.o
ums-freecom-y		:= freecom.o
ums-isd200-y		:= isd200.o
ums-jumpshot-y		:= jumpshot.o
ums-karma-y		:= karma.o
ums-onetouch-y		:= onetouch.o
ums-sddr09-y		:= sddr09.o
ums-sddr55-y		:= sddr55.o
ums-usbat-y		:= shuttle_usbat.o
