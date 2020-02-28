# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

CFLAGS_ptrace.o		+= -DUTS_MACHINE='"$(UTS_MACHINE)"'

obj-y				+= ptrace.o
obj-$(CONFIG_PPC64)		+= ptrace32.o
obj-$(CONFIG_VSX)		+= ptrace-vsx.o
ifneq ($(CONFIG_VSX),y)
obj-y				+= ptrace-novsx.o
endif
obj-$(CONFIG_ALTIVEC)		+= ptrace-altivec.o
