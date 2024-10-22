# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#
#
#
# Common objects
obj-y 					:= common.o sram.o devices-da8xx.o

obj-$(CONFIG_DAVINCI_MUX)		+= mux.o

# Chip specific
obj-$(CONFIG_ARCH_DAVINCI_DA830)	+= da830.o
obj-$(CONFIG_ARCH_DAVINCI_DA850)	+= da850.o pdata-quirks.o

obj-y					+= da8xx-dt.o

# Power Management
obj-$(CONFIG_HAVE_CLK)			+= pm_domain.o
ifeq ($(CONFIG_SUSPEND),y)
obj-$(CONFIG_ARCH_DAVINCI_DA850)	+= pm.o sleep.o
endif
