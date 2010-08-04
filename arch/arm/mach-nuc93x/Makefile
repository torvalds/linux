#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y				:= irq.o time.o dev.o cpu.o clock.o
# NUC932 CPU support files

obj-$(CONFIG_CPU_NUC932)	+= nuc932.o

# machine support

obj-$(CONFIG_MACH_NUC932EVB)	+= mach-nuc932evb.o
