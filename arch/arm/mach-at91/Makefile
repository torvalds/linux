# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

# CPU-specific support
obj-$(CONFIG_SOC_AT91RM9200)	+= at91rm9200.o
obj-$(CONFIG_SOC_AT91SAM9)	+= at91sam9.o
obj-$(CONFIG_SOC_SAMA5)		+= sama5.o
obj-$(CONFIG_SOC_SAMV7)		+= samv7.o

# Power Management
obj-$(CONFIG_ATMEL_PM)		+= pm.o pm_suspend.o

ifeq ($(CONFIG_CPU_V7),y)
AFLAGS_pm_suspend.o := -march=armv7-a
endif
ifeq ($(CONFIG_PM_DEBUG),y)
CFLAGS_pm.o += -DDEBUG
endif

include/generated/at91_pm_data-offsets.h: arch/arm/mach-at91/pm_data-offsets.s FORCE
	$(call filechk,offsets,__PM_DATA_OFFSETS_H__)

arch/arm/mach-at91/pm_suspend.o: include/generated/at91_pm_data-offsets.h

targets += pm_data-offsets.s
