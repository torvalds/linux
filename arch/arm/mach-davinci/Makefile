# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#
#
#
# Common objects
obj-y 					:= serial.o usb.o common.o sram.o

obj-$(CONFIG_DAVINCI_MUX)		+= mux.o

# Chip specific
obj-$(CONFIG_ARCH_DAVINCI_DA830)	+= da830.o devices-da8xx.o usb-da8xx.o
obj-$(CONFIG_ARCH_DAVINCI_DA850)	+= da850.o devices-da8xx.o usb-da8xx.o

# Board specific
obj-$(CONFIG_MACH_DA8XX_DT)		+= da8xx-dt.o pdata-quirks.o

# Power Management
obj-$(CONFIG_CPU_IDLE)			+= cpuidle.o
obj-$(CONFIG_HAVE_CLK)			+= pm_domain.o
ifeq ($(CONFIG_SUSPEND),y)
obj-$(CONFIG_ARCH_DAVINCI_DA850)	+= pm.o sleep.o
endif
