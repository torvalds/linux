#
# Makefile for the generic architecture
#

EXTRA_CFLAGS			:= -Iarch/x86/kernel

obj-y				:= probe.o default.o
obj-$(CONFIG_X86_NUMAQ)		+= numaq.o
obj-$(CONFIG_X86_SUMMIT)	+= summit.o
obj-$(CONFIG_X86_BIGSMP)	+= bigsmp.o
obj-$(CONFIG_X86_ES7000)	+= es7000.o
