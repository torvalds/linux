# SPDX-License-Identifier: GPL-2.0
#
# Makefile for sensor chip drivers.
#

obj-$(CONFIG_THERMAL)		+= thermal_sys.o
thermal_sys-y			+= thermal_core.o thermal_sysfs.o \
					thermal_helpers.o

# interface to/from other layers providing sensors
thermal_sys-$(CONFIG_THERMAL_HWMON)		+= thermal_hwmon.o
thermal_sys-$(CONFIG_THERMAL_OF)		+= of-thermal.o

# governors
thermal_sys-$(CONFIG_THERMAL_GOV_FAIR_SHARE)	+= fair_share.o
thermal_sys-$(CONFIG_THERMAL_GOV_BANG_BANG)	+= gov_bang_bang.o
thermal_sys-$(CONFIG_THERMAL_GOV_STEP_WISE)	+= step_wise.o
thermal_sys-$(CONFIG_THERMAL_GOV_USER_SPACE)	+= user_space.o
thermal_sys-$(CONFIG_THERMAL_GOV_POWER_ALLOCATOR)	+= power_allocator.o

# cpufreq cooling
thermal_sys-$(CONFIG_CPU_THERMAL)	+= cpu_cooling.o

# clock cooling
thermal_sys-$(CONFIG_CLOCK_THERMAL)	+= clock_cooling.o

# devfreq cooling
thermal_sys-$(CONFIG_DEVFREQ_THERMAL) += devfreq_cooling.o

# platform thermal drivers
obj-y				+= broadcom/
obj-$(CONFIG_SPEAR_THERMAL)	+= spear_thermal.o
obj-$(CONFIG_ROCKCHIP_THERMAL)	+= rockchip_thermal.o
obj-$(CONFIG_RCAR_THERMAL)	+= rcar_thermal.o
obj-$(CONFIG_RCAR_GEN3_THERMAL)	+= rcar_gen3_thermal.o
obj-$(CONFIG_KIRKWOOD_THERMAL)  += kirkwood_thermal.o
obj-y				+= samsung/
obj-$(CONFIG_DOVE_THERMAL)  	+= dove_thermal.o
obj-$(CONFIG_DB8500_THERMAL)	+= db8500_thermal.o
obj-$(CONFIG_ARMADA_THERMAL)	+= armada_thermal.o
obj-$(CONFIG_TANGO_THERMAL)	+= tango_thermal.o
obj-$(CONFIG_IMX_THERMAL)	+= imx_thermal.o
obj-$(CONFIG_MAX77620_THERMAL)	+= max77620_thermal.o
obj-$(CONFIG_QORIQ_THERMAL)	+= qoriq_thermal.o
obj-$(CONFIG_DA9062_THERMAL)	+= da9062-thermal.o
obj-y				+= intel/
obj-$(CONFIG_TI_SOC_THERMAL)	+= ti-soc-thermal/
obj-y				+= st/
obj-$(CONFIG_QCOM_TSENS)	+= qcom/
obj-y				+= tegra/
obj-$(CONFIG_HISI_THERMAL)     += hisi_thermal.o
obj-$(CONFIG_MTK_THERMAL)	+= mtk_thermal.o
obj-$(CONFIG_GENERIC_ADC_THERMAL)	+= thermal-generic-adc.o
obj-$(CONFIG_ZX2967_THERMAL)	+= zx2967_thermal.o
obj-$(CONFIG_UNIPHIER_THERMAL)	+= uniphier_thermal.o
