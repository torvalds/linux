# arch/arm/plat-s5p/Makefile
#
# Copyright (c) 2009 Samsung Electronics Co., Ltd.
# 		http://www.samsung.com/
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:= dummy.o
obj-				:=

# Core files

obj-y				+= dev-pmu.o
obj-y				+= dev-uart.o
obj-y				+= cpu.o
obj-y				+= clock.o
obj-y				+= irq.o
obj-$(CONFIG_S5P_EXT_INT)	+= irq-eint.o
obj-$(CONFIG_S5P_GPIO_INT)	+= irq-gpioint.o
obj-$(CONFIG_S5P_SYSTEM_MMU)	+= sysmmu.o
obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_PM)		+= irq-pm.o
obj-$(CONFIG_S5P_HRT) 		+= s5p-time.o

# devices
obj-$(CONFIG_S5P_DEV_MFC)	+= dev-mfc.o
obj-$(CONFIG_S5P_DEV_FIMC0)	+= dev-fimc0.o
obj-$(CONFIG_S5P_DEV_FIMC1)	+= dev-fimc1.o
obj-$(CONFIG_S5P_DEV_FIMC2)	+= dev-fimc2.o
obj-$(CONFIG_S5P_DEV_FIMC3)	+= dev-fimc3.o
obj-$(CONFIG_S5P_DEV_FIMD0)	+= dev-fimd0.o
obj-$(CONFIG_S5P_DEV_ONENAND)	+= dev-onenand.o
obj-$(CONFIG_S5P_DEV_CSIS0)	+= dev-csis0.o
obj-$(CONFIG_S5P_DEV_CSIS1)	+= dev-csis1.o
obj-$(CONFIG_S5P_DEV_USB_EHCI)	+= dev-ehci.o
obj-$(CONFIG_S5P_SETUP_MIPIPHY)	+= setup-mipiphy.o
