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

obj-y				+= dev-uart.o
obj-y				+= cpu.o
obj-y				+= clock.o
obj-y				+= irq.o
obj-$(CONFIG_S5P_EXT_INT)	+= irq-eint.o
obj-$(CONFIG_S5P_GPIO_INT)	+= irq-gpioint.o
obj-$(CONFIG_S5P_SYSTEM_MMU)	+= sysmmu.o
obj-$(CONFIG_S5P_PM)		+= pm.o irq-pm.o
obj-$(CONFIG_S5P_SLEEP)		+= sleep.o
obj-$(CONFIG_S5P_HRT) 		+= s5p-time.o

# devices
obj-$(CONFIG_S5P_DEV_MFC)	+= dev-mfc.o
obj-$(CONFIG_S5P_SETUP_MIPIPHY)	+= setup-mipiphy.o
