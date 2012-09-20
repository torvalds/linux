zreladdr-$(CONFIG_ARCH_TEGRA_2x_SOC)	+= 0x00008000
params_phys-$(CONFIG_ARCH_TEGRA_2x_SOC)	:= 0x00000100
initrd_phys-$(CONFIG_ARCH_TEGRA_2x_SOC)	:= 0x00800000

dtb-$(CONFIG_ARCH_TEGRA_2x_SOC) += tegra20-harmony.dtb
dtb-$(CONFIG_ARCH_TEGRA_2x_SOC) += tegra20-paz00.dtb
dtb-$(CONFIG_ARCH_TEGRA_2x_SOC) += tegra20-plutux.dtb
dtb-$(CONFIG_ARCH_TEGRA_2x_SOC) += tegra20-seaboard.dtb
dtb-$(CONFIG_ARCH_TEGRA_2x_SOC) += tegra20-trimslice.dtb
dtb-$(CONFIG_ARCH_TEGRA_2x_SOC) += tegra20-ventana.dtb
dtb-$(CONFIG_ARCH_TEGRA_2x_SOC) += tegra20-whistler.dtb
dtb-$(CONFIG_ARCH_TEGRA_3x_SOC) += tegra30-cardhu-a02.dtb
dtb-$(CONFIG_ARCH_TEGRA_3x_SOC) += tegra30-cardhu-a04.dtb
