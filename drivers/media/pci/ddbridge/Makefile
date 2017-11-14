# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the ddbridge device driver
#

ddbridge-objs := ddbridge-main.o ddbridge-core.o ddbridge-hw.o \
		ddbridge-i2c.o ddbridge-maxs8.o

obj-$(CONFIG_DVB_DDBRIDGE) += ddbridge.o

ccflags-y += -Idrivers/media/dvb-core/
ccflags-y += -Idrivers/media/dvb-frontends/
ccflags-y += -Idrivers/media/tuners/

# For the staging CI driver cxd2099
ccflags-y += -Idrivers/staging/media/cxd2099/
