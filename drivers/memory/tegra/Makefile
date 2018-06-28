# SPDX-License-Identifier: GPL-2.0
tegra-mc-y := mc.o

tegra-mc-$(CONFIG_ARCH_TEGRA_2x_SOC)  += tegra20.o
tegra-mc-$(CONFIG_ARCH_TEGRA_3x_SOC)  += tegra30.o
tegra-mc-$(CONFIG_ARCH_TEGRA_114_SOC) += tegra114.o
tegra-mc-$(CONFIG_ARCH_TEGRA_124_SOC) += tegra124.o
tegra-mc-$(CONFIG_ARCH_TEGRA_132_SOC) += tegra124.o
tegra-mc-$(CONFIG_ARCH_TEGRA_210_SOC) += tegra210.o

obj-$(CONFIG_TEGRA_MC) += tegra-mc.o

obj-$(CONFIG_TEGRA124_EMC) += tegra124-emc.o
obj-$(CONFIG_ARCH_TEGRA_186_SOC) += tegra186.o
