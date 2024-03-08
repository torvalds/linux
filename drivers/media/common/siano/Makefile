# SPDX-License-Identifier: GPL-2.0
smsmdtv-objs := smscoreapi.o sms-cards.o smsendian.o
smsdvb-objs := smsdvb-main.o

obj-$(CONFIG_SMS_SIAANAL_MDTV) += smsmdtv.o smsdvb.o

ifeq ($(CONFIG_SMS_SIAANAL_RC),y)
  smsmdtv-objs += smsir.o
endif

ifeq ($(CONFIG_SMS_SIAANAL_DEBUGFS),y)
  smsdvb-objs += smsdvb-debugfs.o
endif
