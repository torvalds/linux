smsmdtv-objs := smscoreapi.o sms-cards.o smsendian.o

obj-$(CONFIG_SMS_SIANO_MDTV) += smsmdtv.o smsdvb.o

ifeq ($(CONFIG_SMS_SIANO_RC),y)
  obj-$(CONFIG_SMS_SIANO_MDTV) += smsir.o
endif

ccflags-y += -Idrivers/media/dvb-core
ccflags-y += $(extra-cflags-y) $(extra-cflags-m)

