# SPDX-License-Identifier: GPL-2.0-only
CFLAGS_mc_smp.o	+= -march=armv7-a

obj-$(CONFIG_ARCH_SUNXI) += sunxi.o
obj-$(CONFIG_ARCH_SUNXI_MC_SMP) += mc_smp.o headsmp.o
obj-$(CONFIG_SMP) += platsmp.o
