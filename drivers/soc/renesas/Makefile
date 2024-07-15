# SPDX-License-Identifier: GPL-2.0
# Generic, must be first because of soc_device_register()
obj-$(CONFIG_SOC_RENESAS)	+= renesas-soc.o

# SoC
ifdef CONFIG_SMP
obj-$(CONFIG_ARCH_R9A06G032)	+= r9a06g032-smp.o
endif

# Family
obj-$(CONFIG_PWC_RZV2M)		+= pwc-rzv2m.o
obj-$(CONFIG_RST_RCAR)		+= rcar-rst.o
