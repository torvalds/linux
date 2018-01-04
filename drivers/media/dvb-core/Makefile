# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the kernel DVB device drivers.
#

dvb-net-$(CONFIG_DVB_NET) := dvb_net.o
dvb-vb2-$(CONFIG_DVB_MMSP) := dvb_vb2.o

dvb-core-objs := dvbdev.o dmxdev.o dvb_demux.o			\
		 dvb_ca_en50221.o dvb_frontend.o		\
		 $(dvb-net-y) dvb_ringbuffer.o $(dvb-vb2-y) dvb_math.o

obj-$(CONFIG_DVB_CORE) += dvb-core.o
