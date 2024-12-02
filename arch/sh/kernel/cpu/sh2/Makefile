# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux/SuperH SH-2 backends.
#

obj-y	:= ex.o probe.o entry.o

obj-$(CONFIG_CPU_SUBTYPE_SH7619) += setup-sh7619.o clock-sh7619.o

# SMP setup
smp-$(CONFIG_CPU_J2)			:= smp-j2.o
obj-$(CONFIG_SMP)			+= $(smp-y)
