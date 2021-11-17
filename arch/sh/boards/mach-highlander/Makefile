# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Highlander specific parts of the kernel
#
obj-y				:= setup.o
obj-$(CONFIG_SH_R7780RP)	+= irq-r7780rp.o
obj-$(CONFIG_SH_R7780MP)	+= irq-r7780mp.o
obj-$(CONFIG_SH_R7785RP)	+= irq-r7785rp.o pinmux-r7785rp.o

ifneq ($(CONFIG_SH_R7785RP),y)
obj-$(CONFIG_PUSH_SWITCH)	+= psw.o
endif
