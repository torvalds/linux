# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_TEGRA_SOCTHERM)		+= tegra-soctherm.o
obj-$(CONFIG_TEGRA_BPMP_THERMAL)	+= tegra-bpmp-thermal.o

tegra-soctherm-y				:= soctherm.o soctherm-fuse.o
tegra-soctherm-$(CONFIG_ARCH_TEGRA_124_SOC)	+= tegra124-soctherm.o
tegra-soctherm-$(CONFIG_ARCH_TEGRA_132_SOC)	+= tegra132-soctherm.o
tegra-soctherm-$(CONFIG_ARCH_TEGRA_210_SOC)	+= tegra210-soctherm.o
