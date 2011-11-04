#
# Makefile for the ddbridge device driver
#

ddbridge-objs := ddbridge-core.o

obj-$(CONFIG_DVB_DDBRIDGE) += ddbridge.o

ccflags-y += -Idrivers/media/dvb/dvb-core/
ccflags-y += -Idrivers/media/dvb/frontends/
ccflags-y += -Idrivers/media/common/tuners/

# For the staging CI driver cxd2099
ccflags-y += -Idrivers/staging/media/cxd2099/
