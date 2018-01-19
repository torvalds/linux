# SPDX-License-Identifier: GPL-2.0

smipcie-objs	:= smipcie-main.o smipcie-ir.o

obj-$(CONFIG_DVB_SMIPCIE) += smipcie.o

ccflags-y += -Idrivers/media/tuners
ccflags-y += -Idrivers/media/dvb-core
ccflags-y += -Idrivers/media/dvb-frontends

