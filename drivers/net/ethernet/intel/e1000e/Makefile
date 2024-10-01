# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 1999 - 2018 Intel Corporation.

#
# Makefile for the Intel(R) PRO/1000 ethernet driver
#

ccflags-y += -I$(src)
subdir-ccflags-y += -I$(src)

obj-$(CONFIG_E1000E) += e1000e.o

e1000e-y := 82571.o ich8lan.o 80003es2lan.o \
	    mac.o manage.o nvm.o phy.o \
	    param.o ethtool.o netdev.o ptp.o
