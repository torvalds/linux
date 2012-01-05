#
# Makefile for the nGene device driver
#

ngene-objs := ngene-core.o ngene-i2c.o ngene-cards.o ngene-dvb.o

obj-$(CONFIG_DVB_NGENE) += ngene.o

ccflags-y += -Idrivers/media/dvb/dvb-core/
ccflags-y += -Idrivers/media/dvb/frontends/
ccflags-y += -Idrivers/media/common/tuners/

# For the staging CI driver cxd2099
ccflags-y += -Idrivers/staging/media/cxd2099/
