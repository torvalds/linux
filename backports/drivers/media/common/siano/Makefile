smsmdtv-objs := smscoreapi.o sms-cards.o smsendian.o
smsdvb-objs := smsdvb-main.o

obj-$(CONFIG_BACKPORT_SMS_SIANO_MDTV) += smsmdtv.o smsdvb.o

ifeq ($(CONFIG_BACKPORT_SMS_SIANO_RC),y)
  smsmdtv-objs += smsir.o
endif

ifeq ($(CONFIG_BACKPORT_SMS_SIANO_DEBUGFS),y)
  smsdvb-objs += smsdvb-debugfs.o
endif

ccflags-y += -I$(backport_srctree)/drivers/media/dvb-core
ccflags-y += $(extra-cflags-y) $(extra-cflags-m)

