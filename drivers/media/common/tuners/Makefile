#
# Makefile for common V4L/DVB tuners
#

tda18271-objs := tda18271-maps.o tda18271-common.o tda18271-fe.o

obj-$(CONFIG_TUNER_XC2028) += tuner-xc2028.o
obj-$(CONFIG_TUNER_SIMPLE) += tuner-simple.o
# tuner-types will be merged into tuner-simple, in the future
obj-$(CONFIG_TUNER_SIMPLE) += tuner-types.o
obj-$(CONFIG_TUNER_MT20XX) += mt20xx.o
obj-$(CONFIG_TUNER_TDA8290) += tda8290.o
obj-$(CONFIG_TUNER_TEA5767) += tea5767.o
obj-$(CONFIG_TUNER_TEA5761) += tea5761.o
obj-$(CONFIG_TUNER_TDA9887) += tda9887.o
obj-$(CONFIG_DVB_TDA827X) += tda827x.o
obj-$(CONFIG_DVB_TDA18271) += tda18271.o
obj-$(CONFIG_DVB_TUNER_XC5000) += xc5000.o

EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
EXTRA_CFLAGS += -Idrivers/media/dvb/frontends
