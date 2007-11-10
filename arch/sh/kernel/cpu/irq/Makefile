#
# Makefile for the Linux/SuperH CPU-specifc IRQ handlers.
#
obj-y	+= intc.o

obj-$(CONFIG_SUPERH32)			+= imask.o
obj-$(CONFIG_CPU_HAS_IPR_IRQ)		+= ipr.o
obj-$(CONFIG_CPU_HAS_MASKREG_IRQ)	+= maskreg.o
