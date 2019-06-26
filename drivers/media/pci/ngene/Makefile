# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the nGene device driver
#

ngene-objs := ngene-core.o ngene-i2c.o ngene-cards.o ngene-dvb.o

obj-$(CONFIG_DVB_NGENE) += ngene.o

ccflags-y += -I $(srctree)/drivers/media/dvb-frontends/
ccflags-y += -I $(srctree)/drivers/media/tuners/
