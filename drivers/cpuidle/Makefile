#
# Makefile for cpuidle.
#

obj-y += cpuidle.o driver.o governor.o sysfs.o governors/
obj-$(CONFIG_ARCH_NEEDS_CPU_IDLE_COUPLED) += coupled.o
obj-$(CONFIG_DT_IDLE_STATES)		  += dt_idle_states.o

##################################################################################
# ARM SoC drivers
obj-$(CONFIG_ARM_MVEBU_V7_CPUIDLE) += cpuidle-mvebu-v7.o
obj-$(CONFIG_ARM_BIG_LITTLE_CPUIDLE)	+= cpuidle-big_little.o
obj-$(CONFIG_ARM_CLPS711X_CPUIDLE)	+= cpuidle-clps711x.o
obj-$(CONFIG_ARM_HIGHBANK_CPUIDLE)	+= cpuidle-calxeda.o
obj-$(CONFIG_ARM_KIRKWOOD_CPUIDLE)	+= cpuidle-kirkwood.o
obj-$(CONFIG_ARM_ZYNQ_CPUIDLE)		+= cpuidle-zynq.o
obj-$(CONFIG_ARM_U8500_CPUIDLE)         += cpuidle-ux500.o
obj-$(CONFIG_ARM_AT91_CPUIDLE)          += cpuidle-at91.o
obj-$(CONFIG_ARM_EXYNOS_CPUIDLE)        += cpuidle-exynos.o

###############################################################################
# MIPS drivers
obj-$(CONFIG_MIPS_CPS_CPUIDLE)		+= cpuidle-cps.o

###############################################################################
# POWERPC drivers
obj-$(CONFIG_PSERIES_CPUIDLE)		+= cpuidle-pseries.o
obj-$(CONFIG_POWERNV_CPUIDLE)		+= cpuidle-powernv.o
