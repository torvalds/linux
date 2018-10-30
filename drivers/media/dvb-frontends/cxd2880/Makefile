# SPDX-License-Identifier: GPL-2.0

cxd2880-objs := cxd2880_common.o \
		cxd2880_devio_spi.o \
		cxd2880_integ.o \
		cxd2880_io.o \
		cxd2880_spi_device.o \
		cxd2880_tnrdmd.o \
		cxd2880_tnrdmd_dvbt2.o \
		cxd2880_tnrdmd_dvbt2_mon.o \
		cxd2880_tnrdmd_dvbt.o \
		cxd2880_tnrdmd_dvbt_mon.o\
		cxd2880_tnrdmd_mon.o\
		cxd2880_top.o

obj-$(CONFIG_DVB_CXD2880) += cxd2880.o

ccflags-y += -Idrivers/media/dvb-frontends
