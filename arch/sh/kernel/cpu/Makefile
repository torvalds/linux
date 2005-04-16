#
# Makefile for the Linux/SuperH CPU-specifc backends.
#

obj-y	:= irq_ipr.o irq_imask.o init.o bus.o

obj-$(CONFIG_CPU_SH2)		+= sh2/
obj-$(CONFIG_CPU_SH3)		+= sh3/
obj-$(CONFIG_CPU_SH4)		+= sh4/

obj-$(CONFIG_SH_RTC)            += rtc.o
obj-$(CONFIG_UBC_WAKEUP)	+= ubc.o
obj-$(CONFIG_SH_ADC)            += adc.o

USE_STANDARD_AS_RULE := true

