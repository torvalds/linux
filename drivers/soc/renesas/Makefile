# SPDX-License-Identifier: GPL-2.0
# Generic, must be first because of soc_device_register()
obj-$(CONFIG_SOC_RENESAS)	+= renesas-soc.o

# SoC
ifdef CONFIG_SMP
obj-$(CONFIG_ARCH_R9A06G032)	+= r9a06g032-smp.o
endif
obj-$(CONFIG_SYSC_R9A08G045)	+= r9a08g045-sysc.o
obj-$(CONFIG_SYS_R9A09G047)	+= r9a09g047-sys.o
obj-$(CONFIG_SYS_R9A09G056)	+= r9a09g056-sys.o
obj-$(CONFIG_SYS_R9A09G057)	+= r9a09g057-sys.o

# Family
obj-$(CONFIG_PWC_RZV2M)		+= pwc-rzv2m.o
obj-$(CONFIG_RST_RCAR)		+= rcar-rst.o
obj-$(CONFIG_SYSC_RZ)		+= rz-sysc.o
