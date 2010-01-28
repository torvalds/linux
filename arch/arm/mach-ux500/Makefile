#
# Makefile for the linux kernel, U8500 machine.
#

obj-y				:= clock.o
obj-$(CONFIG_ARCH_U8500)	+= cpu-u8500.o
obj-$(CONFIG_MACH_U8500_MOP)	+= board-mop500.o
obj-$(CONFIG_SMP)		+= platsmp.o headsmp.o localtimer.o
