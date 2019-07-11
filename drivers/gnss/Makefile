# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the GNSS subsystem.
#

obj-$(CONFIG_GNSS)			+= gnss.o
gnss-y := core.o

obj-$(CONFIG_GNSS_SERIAL)		+= gnss-serial.o
gnss-serial-y := serial.o

obj-$(CONFIG_GNSS_MTK_SERIAL)		+= gnss-mtk.o
gnss-mtk-y := mtk.o

obj-$(CONFIG_GNSS_SIRF_SERIAL)		+= gnss-sirf.o
gnss-sirf-y := sirf.o

obj-$(CONFIG_GNSS_UBX_SERIAL)		+= gnss-ubx.o
gnss-ubx-y := ubx.o
