# SPDX-License-Identifier: GPL-2.0

ifeq ($(CONFIG_COMMON_CLK), y)
obj-$(CONFIG_ARCH_DAVINCI_DA8XX)	+= da8xx-cfgchip.o

obj-y += pll.o
obj-$(CONFIG_ARCH_DAVINCI_DA850)	+= pll-da850.o

obj-y += psc.o
obj-$(CONFIG_ARCH_DAVINCI_DA850)	+= psc-da850.o
endif
