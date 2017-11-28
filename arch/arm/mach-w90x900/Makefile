# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y				:= irq.o time.o mfp.o gpio.o clock.o
obj-y				+= clksel.o dev.o cpu.o
# W90X900 CPU support files

obj-$(CONFIG_CPU_W90P910)	+= nuc910.o
obj-$(CONFIG_CPU_NUC950)	+= nuc950.o
obj-$(CONFIG_CPU_NUC960)	+= nuc960.o

# machine support

obj-$(CONFIG_MACH_W90P910EVB)	+= mach-nuc910evb.o
obj-$(CONFIG_MACH_W90P950EVB)	+= mach-nuc950evb.o
obj-$(CONFIG_MACH_W90N960EVB)	+= mach-nuc960evb.o
