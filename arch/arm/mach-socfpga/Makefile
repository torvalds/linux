# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

obj-y					:= socfpga.o
obj-$(CONFIG_SMP)	+= headsmp.o platsmp.o
obj-$(CONFIG_SOCFPGA_SUSPEND)	+= pm.o self-refresh.o
obj-$(CONFIG_EDAC_ALTERA_L2C)	+= l2_cache.o
obj-$(CONFIG_EDAC_ALTERA_OCRAM)	+= ocram.o
