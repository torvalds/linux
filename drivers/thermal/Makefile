#
# Makefile for sensor chip drivers.
#

obj-$(CONFIG_THERMAL)		+= thermal_sys.o

# governors
obj-$(CONFIG_FAIR_SHARE)	+= fair_share.o
obj-$(CONFIG_STEP_WISE)		+= step_wise.o
obj-$(CONFIG_USER_SPACE)	+= user_space.o

# cpufreq cooling
obj-$(CONFIG_CPU_THERMAL)	+= cpu_cooling.o

# platform thermal drivers
obj-$(CONFIG_SPEAR_THERMAL)	+= spear_thermal.o
obj-$(CONFIG_RCAR_THERMAL)	+= rcar_thermal.o
obj-$(CONFIG_EXYNOS_THERMAL)	+= exynos_thermal.o
