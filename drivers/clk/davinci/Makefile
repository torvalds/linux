# SPDX-License-Identifier: GPL-2.0

ifeq ($(CONFIG_COMMON_CLK), y)
obj-$(CONFIG_ARCH_DAVINCI_DA8XX)	+= da8xx-cfgchip.o

obj-y += pll.o
obj-$(CONFIG_ARCH_DAVINCI_DA830)	+= pll-da830.o
obj-$(CONFIG_ARCH_DAVINCI_DA850)	+= pll-da850.o
obj-$(CONFIG_ARCH_DAVINCI_DM355)	+= pll-dm355.o
obj-$(CONFIG_ARCH_DAVINCI_DM365)	+= pll-dm365.o
obj-$(CONFIG_ARCH_DAVINCI_DM644x)	+= pll-dm644x.o
obj-$(CONFIG_ARCH_DAVINCI_DM646x)	+= pll-dm646x.o

obj-y += psc.o
obj-$(CONFIG_ARCH_DAVINCI_DA830)	+= psc-da830.o
obj-$(CONFIG_ARCH_DAVINCI_DA850)	+= psc-da850.o
obj-$(CONFIG_ARCH_DAVINCI_DM355)	+= psc-dm355.o
obj-$(CONFIG_ARCH_DAVINCI_DM365)	+= psc-dm365.o
obj-$(CONFIG_ARCH_DAVINCI_DM644x)	+= psc-dm644x.o
obj-$(CONFIG_ARCH_DAVINCI_DM646x)	+= psc-dm646x.o
endif
