#
# Makefile for common code of the NEC VR4100 series.
#

obj-y				+= bcu.o cmu.o icu.o init.o int-handler.o irq.o pmu.o type.o
obj-$(CONFIG_VRC4173)		+= vrc4173.o

EXTRA_AFLAGS := $(CFLAGS)
