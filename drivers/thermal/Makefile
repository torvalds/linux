# SPDX-License-Identifier: GPL-2.0
#
# Makefile for sensor chip drivers.
#
CFLAGS_thermal_core.o		:= -I$(src)
obj-$(CONFIG_THERMAL)		+= thermal_sys.o
thermal_sys-y			+= thermal_core.o thermal_sysfs.o
thermal_sys-y			+= thermal_trip.o thermal_helpers.o
thermal_sys-y			+= thermal_thresholds.o

# netlink interface to manage the thermal framework
thermal_sys-$(CONFIG_THERMAL_NETLINK)		+= thermal_netlink.o

thermal_sys-$(CONFIG_THERMAL_DEBUGFS)	+= thermal_debugfs.o

# interface to/from other layers providing sensors
thermal_sys-$(CONFIG_THERMAL_HWMON)		+= thermal_hwmon.o
thermal_sys-$(CONFIG_THERMAL_OF)		+= thermal_of.o

# governors
CFLAGS_gov_power_allocator.o			:= -I$(src)
thermal_sys-$(CONFIG_THERMAL_GOV_FAIR_SHARE)	+= gov_fair_share.o
thermal_sys-$(CONFIG_THERMAL_GOV_BANG_BANG)	+= gov_bang_bang.o
thermal_sys-$(CONFIG_THERMAL_GOV_STEP_WISE)	+= gov_step_wise.o
thermal_sys-$(CONFIG_THERMAL_GOV_USER_SPACE)	+= gov_user_space.o
thermal_sys-$(CONFIG_THERMAL_GOV_POWER_ALLOCATOR)	+= gov_power_allocator.o

# cpufreq cooling
thermal_sys-$(CONFIG_CPU_FREQ_THERMAL)	+= cpufreq_cooling.o
thermal_sys-$(CONFIG_CPU_IDLE_THERMAL)	+= cpuidle_cooling.o

# devfreq cooling
thermal_sys-$(CONFIG_DEVFREQ_THERMAL) += devfreq_cooling.o

thermal_sys-$(CONFIG_PCIE_THERMAL) += pcie_cooling.o

obj-$(CONFIG_K3_THERMAL)	+= k3_bandgap.o k3_j72xx_bandgap.o
# platform thermal drivers
obj-y				+= broadcom/
obj-$(CONFIG_THERMAL_MMIO)		+= thermal_mmio.o
obj-$(CONFIG_AIROHA_THERMAL)	+= airoha_thermal.o
obj-$(CONFIG_SPEAR_THERMAL)	+= spear_thermal.o
obj-$(CONFIG_SUN8I_THERMAL)     += sun8i_thermal.o
obj-$(CONFIG_ROCKCHIP_THERMAL)	+= rockchip_thermal.o
obj-y				+= renesas/
obj-$(CONFIG_KIRKWOOD_THERMAL)  += kirkwood_thermal.o
obj-y				+= samsung/
obj-$(CONFIG_DOVE_THERMAL)	+= dove_thermal.o
obj-$(CONFIG_DB8500_THERMAL)	+= db8500_thermal.o
obj-$(CONFIG_ARMADA_THERMAL)	+= armada_thermal.o
obj-$(CONFIG_IMX_THERMAL)	+= imx_thermal.o
obj-$(CONFIG_IMX_SC_THERMAL)	+= imx_sc_thermal.o
obj-$(CONFIG_IMX8MM_THERMAL)	+= imx8mm_thermal.o
obj-$(CONFIG_MAX77620_THERMAL)	+= max77620_thermal.o
obj-$(CONFIG_QORIQ_THERMAL)	+= qoriq_thermal.o
obj-$(CONFIG_DA9062_THERMAL)	+= da9062-thermal.o
obj-y				+= intel/
obj-$(CONFIG_TI_SOC_THERMAL)	+= ti-soc-thermal/
obj-y				+= st/
obj-y				+= qcom/
obj-y				+= tegra/
obj-$(CONFIG_HISI_THERMAL)     += hisi_thermal.o
obj-y				+= mediatek/
obj-$(CONFIG_GENERIC_ADC_THERMAL)	+= thermal-generic-adc.o
obj-$(CONFIG_UNIPHIER_THERMAL)	+= uniphier_thermal.o
obj-$(CONFIG_AMLOGIC_THERMAL)     += amlogic_thermal.o
obj-$(CONFIG_SPRD_THERMAL)	+= sprd_thermal.o
obj-$(CONFIG_KHADAS_MCU_FAN_THERMAL)	+= khadas_mcu_fan.o
obj-$(CONFIG_LOONGSON2_THERMAL)	+= loongson2_thermal.o
obj-$(CONFIG_THERMAL_CORE_TESTING)	+= testing/
