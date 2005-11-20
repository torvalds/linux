#
# Makefile for USB Network drivers
#

obj-$(CONFIG_USB_CATC)		+= catc.o
obj-$(CONFIG_USB_KAWETH)	+= kaweth.o
obj-$(CONFIG_USB_PEGASUS)	+= pegasus.o
obj-$(CONFIG_USB_RTL8150)	+= rtl8150.o
obj-$(CONFIG_USB_NET_AX8817X)	+= asix.o
obj-$(CONFIG_USB_NET_CDCETHER)	+= cdc_ether.o
obj-$(CONFIG_USB_NET_GL620A)	+= gl620a.o
obj-$(CONFIG_USB_NET_NET1080)	+= net1080.o
obj-$(CONFIG_USB_NET_PLUSB)	+= plusb.o
obj-$(CONFIG_USB_NET_RNDIS_HOST)	+= rndis_host.o
obj-$(CONFIG_USB_NET_CDC_SUBSET)	+= cdc_subset.o
obj-$(CONFIG_USB_NET_ZAURUS)	+= zaurus.o
obj-$(CONFIG_USB_USBNET)	+= usbnet.o
obj-$(CONFIG_USB_ZD1201)	+= zd1201.o

ifeq ($(CONFIG_USB_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
