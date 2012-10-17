smsmdtv-objs := smscoreapi.o sms-cards.o smsendian.o

obj-$(CONFIG_SMS_SIANO_MDTV) += smsmdtv.o smsdvb.o
obj-$(CONFIG_SMS_SIANO_RC) += smsir.o

ccflags-y += -Idrivers/media/dvb-core
ccflags-y += $(extra-cflags-y) $(extra-cflags-m)

