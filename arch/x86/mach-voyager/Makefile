#
# Makefile for the linux kernel.
#

EXTRA_CFLAGS	:= -Iarch/x86/kernel
obj-y			:= setup.o voyager_basic.o voyager_thread.o

obj-$(CONFIG_SMP)	+= voyager_smp.o voyager_cat.o
