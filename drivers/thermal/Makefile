#
# Makefile for sensor chip drivers.
#

obj-$(CONFIG_THERMAL)		+= thermal_sys.o
thermal_sys-y			+= thermal_core.o

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

# platform thermal drivers
obj-$(CONFIG_QCOM_SPMI_TEMP_ALARM)	+= qcom-spmi-temp-alarm.o
obj-$(CONFIG_SPEAR_THERMAL)	+= spear_thermal.o
obj-$(CONFIG_ROCKCHIP_THERMAL)	+= rockchip_thermal.o
obj-$(CONFIG_RCAR_THERMAL)	+= rcar_thermal.o
obj-$(CONFIG_KIRKWOOD_THERMAL)  += kirkwood_thermal.o
obj-y				+= samsung/
obj-$(CONFIG_DOVE_THERMAL)  	+= dove_thermal.o
obj-$(CONFIG_DB8500_THERMAL)	+= db8500_thermal.o
obj-$(CONFIG_ARMADA_THERMAL)	+= armada_thermal.o
obj-$(CONFIG_IMX_THERMAL)	+= imx_thermal.o
obj-$(CONFIG_DB8500_CPUFREQ_COOLING)	+= db8500_cpufreq_cooling.o
obj-$(CONFIG_INTEL_POWERCLAMP)	+= intel_powerclamp.o
obj-$(CONFIG_X86_PKG_TEMP_THERMAL)	+= x86_pkg_temp_thermal.o
obj-$(CONFIG_INTEL_SOC_DTS_THERMAL)	+= intel_soc_dts_thermal.o
obj-$(CONFIG_TI_SOC_THERMAL)	+= ti-soc-thermal/
obj-$(CONFIG_INT340X_THERMAL)  += int340x_thermal/
obj-$(CONFIG_ST_THERMAL)	+= st/
obj-$(CONFIG_TEGRA_SOCTHERM)	+= tegra_soctherm.o
