# SPDX-License-Identifier: GPL-2.0
smsmdtv-objs := smscoreapi.o sms-cards.o smsendian.o
smsdvb-objs := smsdvb-main.o

obj-$(CONFIG_SMS_SIANO_MDTV) += smsmdtv.o smsdvb.o

ifeq ($(CONFIG_SMS_SIANO_RC),y)
  smsmdtv-objs += smsir.o
endif

ifeq ($(CONFIG_SMS_SIANO_DEBUGFS),y)
  smsdvb-objs += smsdvb-debugfs.o
endif

ccflags-y += -Idrivers/media/dvb-core
ccflags-y += $(extra-cflags-y) $(extra-cflags-m)

