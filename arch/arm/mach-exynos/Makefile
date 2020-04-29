# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
#		http://www.samsung.com/

ccflags-$(CONFIG_ARCH_MULTIPLATFORM) += -I$(srctree)/$(src)/include -I$(srctree)/arch/arm/plat-samsung/include

# Core

obj-$(CONFIG_ARCH_EXYNOS)	+= exynos.o exynos-smc.o firmware.o

obj-$(CONFIG_EXYNOS_CPU_SUSPEND) += pm.o sleep.o
obj-$(CONFIG_PM_SLEEP)		+= suspend.o

obj-$(CONFIG_SMP)		+= platsmp.o headsmp.o

obj-$(CONFIG_EXYNOS_MCPM)	+= mcpm-exynos.o
CFLAGS_mcpm-exynos.o		+= -march=armv7-a
