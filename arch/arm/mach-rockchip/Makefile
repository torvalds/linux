# SPDX-License-Identifier: GPL-2.0-only
CFLAGS_platsmp.o := -march=armv7-a

obj-$(CONFIG_ARCH_ROCKCHIP) += rockchip.o

ifdef CONFIG_PM_SLEEP
obj-$(CONFIG_CPU_RK3288) += pm.o sleep.o
obj-$(CONFIG_CPU_RV1106) += rv1106_pm.o rv1106_sleep.o rkpm_helpers.o	\
			    rkpm_uart.o rkpm_gicv2.o rockchip_hptimer.o
endif

obj-$(CONFIG_SMP) += headsmp.o platsmp.o
