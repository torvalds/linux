#
# Makefile for the linux kernel.
#

ifneq ($(ARCH),m68k)
obj-y    := sbus.o dvma.o
endif

obj-$(CONFIG_SBUSCHAR) += char/
