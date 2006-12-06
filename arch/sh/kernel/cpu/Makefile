#
# Makefile for the Linux/SuperH CPU-specifc backends.
#

obj-$(CONFIG_CPU_SH2)		= sh2/
obj-$(CONFIG_CPU_SH2A)		= sh2a/
obj-$(CONFIG_CPU_SH3)		= sh3/
obj-$(CONFIG_CPU_SH4)		= sh4/

obj-$(CONFIG_UBC_WAKEUP)	+= ubc.o
obj-$(CONFIG_SH_ADC)		+= adc.o

obj-y	+= irq/ init.o clock.o
