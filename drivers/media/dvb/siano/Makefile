
smsmdtv-objs := smscoreapi.o sms-cards.o smsendian.o smsir.o

obj-$(CONFIG_SMS_SIANO_MDTV) += smsmdtv.o smsdvb.o
obj-$(CONFIG_SMS_USB_DRV) += smsusb.o
obj-$(CONFIG_SMS_SDIO_DRV) += smssdio.o

EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core

EXTRA_CFLAGS += $(extra-cflags-y) $(extra-cflags-m)

