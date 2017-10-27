cec-objs := cec-core.o cec-adap.o cec-api.o cec-edid.o

ifeq ($(CONFIG_CEC_NOTIFIER),y)
  cec-objs += cec-notifier.o
endif

ifeq ($(CONFIG_CEC_PIN),y)
  cec-objs += cec-pin.o
endif

obj-$(CONFIG_CEC_CORE) += cec.o
