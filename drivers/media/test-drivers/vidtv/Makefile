# SPDX-License-Identifier: GPL-2.0

dvb-vidtv-tuner-objs := vidtv_tuner.o
dvb-vidtv-demod-objs := vidtv_demod.o
dvb-vidtv-bridge-objs := vidtv_bridge.o vidtv_common.o vidtv_ts.o vidtv_psi.o \
			 vidtv_pes.o vidtv_s302m.o vidtv_channel.o vidtv_mux.o

obj-$(CONFIG_DVB_VIDTV)	+= dvb-vidtv-tuner.o dvb-vidtv-demod.o \
			   dvb-vidtv-bridge.o
