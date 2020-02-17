# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the GNSS subsystem.
#

obj-$(CONFIG_GNSS)			+= gnss.o
gnss-y := core.o

obj-$(CONFIG_GNSS_SERIAL)		+= gnss-serial.o
gnss-serial-y := serial.o

obj-$(CONFIG_GNSS_SIRF_SERIAL)		+= gnss-sirf.o
gnss-sirf-y := sirf.o

obj-$(CONFIG_GNSS_UBX_SERIAL)		+= gnss-ubx.o
gnss-ubx-y := ubx.o

obj-$(CONFIG_GNSS_CMDLINE_SERIAL)	+= gnss-cmdline.o
gnss-cmdline-y := cmdline.o
