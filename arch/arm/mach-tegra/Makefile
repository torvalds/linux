# SPDX-License-Identifier: GPL-2.0
obj-y                                   += io.o
obj-y                                   += irq.o
obj-y					+= pm.o
obj-y					+= reset.o
obj-y					+= reset-handler.o
obj-y					+= sleep.o
obj-y					+= tegra.o
obj-y					+= sleep-tegra20.o
obj-y					+= sleep-tegra30.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= pm-tegra20.o
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)		+= pm-tegra30.o
obj-$(CONFIG_SMP)			+= platsmp.o
obj-$(CONFIG_HOTPLUG_CPU)               += hotplug.o

obj-$(CONFIG_ARCH_TEGRA_114_SOC)	+= pm-tegra30.o
obj-$(CONFIG_ARCH_TEGRA_124_SOC)	+= pm-tegra30.o

obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= board-paz00.o
