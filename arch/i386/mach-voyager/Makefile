#
# Makefile for the linux kernel.
#

EXTRA_CFLAGS	+= -I../kernel
obj-y			:= setup.o voyager_basic.o voyager_thread.o

obj-$(CONFIG_SMP)	+= voyager_smp.o voyager_cat.o
