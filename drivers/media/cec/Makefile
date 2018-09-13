# SPDX-License-Identifier: GPL-2.0
cec-objs := cec-core.o cec-adap.o cec-api.o

ifeq ($(CONFIG_CEC_NOTIFIER),y)
  cec-objs += cec-notifier.o
endif

ifeq ($(CONFIG_CEC_PIN),y)
  cec-objs += cec-pin.o
endif

ifeq ($(CONFIG_CEC_PIN_ERROR_INJ),y)
  cec-objs += cec-pin-error-inj.o
endif

obj-$(CONFIG_CEC_CORE) += cec.o
