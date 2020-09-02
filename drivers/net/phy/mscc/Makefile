# SPDX-License-Identifier: GPL-2.0
#
# Makefile for MSCC networking PHY driver

obj-$(CONFIG_MICROSEMI_PHY) := mscc.o
mscc-objs := mscc_main.o

ifdef CONFIG_MACSEC
mscc-objs += mscc_macsec.o
endif

ifdef CONFIG_NETWORK_PHY_TIMESTAMPING
mscc-objs += mscc_ptp.o
endif
