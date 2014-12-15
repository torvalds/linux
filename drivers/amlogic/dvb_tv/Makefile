#
# Makefile for the DVB driver.
#

obj-$(CONFIG_AM_DVB) += aml.o

aml-objs=aml_dvb.o aml_dmx.o

obj-$(CONFIG_AM_DVB) += aml_fe.o

obj-$(CONFIG_AM_CI) += amlci/

obj-$(CONFIG_AM_M6_DEMOD) += amldemod/

ifneq ($(wildcard $(srctree)/../hardware/dvb),)
	obj-$(CONFIG_AM_ATBM8869) += ../../../../hardware/dvb/altobeam/drivers/atbm887x/
	obj-$(CONFIG_AM_SI2177)   += ../../../../hardware/dvb/silabs/drivers/si2177/
	obj-$(CONFIG_AM_AVL6211)  += ../../../../hardware/dvb/availink/drivers/avl6211/
endif

EXTRA_CFLAGS += -I.
