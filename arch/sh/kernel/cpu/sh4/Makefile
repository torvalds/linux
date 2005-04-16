#
# Makefile for the Linux/SuperH SH-4 backends.
#

obj-y	:= ex.o probe.o

obj-$(CONFIG_SH_FPU)                    += fpu.o
obj-$(CONFIG_CPU_SUBTYPE_ST40STB1)	+= irq_intc2.o
obj-$(CONFIG_SH_STORE_QUEUES)		+= sq.o

