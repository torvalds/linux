sms1xxx-objs := smscoreapi.o sms-cards.o

obj-$(CONFIG_DVB_SIANO_SMS1XXX) += sms1xxx.o
obj-$(CONFIG_DVB_SIANO_SMS1XXX) += smsusb.o
obj-$(CONFIG_DVB_SIANO_SMS1XXX) += smsdvb.o

EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core

EXTRA_CFLAGS += $(extra-cflags-y) $(extra-cflags-m)

