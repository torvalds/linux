#############################################################
#
# NOTE:
#
# AMLOGIC_BOARD_HAS_PMU is general switch to compile on-board
# PMU drivers, ALL new added PMU driver should be depend on 
# this config
#
############################################################

ifeq ($(CONFIG_AMLOGIC_BOARD_HAS_PMU), y)
ifneq ($(KBUILD_SRC),)
TOP_KBUILD_SRC := $(KBUILD_SRC)/
endif

#obj-$(CONFIG_SMBA10XX_BATTERY)             += smba10xx_battery/
#obj-$(CONFIG_BQ27x00_BATTERY)              += bq27x00_battery.o
#obj-$(CONFIG_PMU_ACT8xxx)                  += act8xxx_pmu/
obj-$(CONFIG_AW_AXP)                        += axp_power/
#obj-$(CONFIG_CW2015)                       += cw2015/cw2015_gasgauge.o
obj-$(CONFIG_AML_PMU)                       += aml_pmu/
obj-$(CONFIG_RICOH_PMU)                     += ricoh/
obj-$(CONFIG_UBOOT_BATTERY_PARAMETERS)      += battery_parameter.o
obj-$(CONFIG_AMLOGIC_PMU_OF)                += aml_pmu_of_common.o pmu_mutex.o 

endif

obj-$(CONFIG_MESON_CS_DCDC_REGULATOR)       += meson_cs_dcdc_regulator/
obj-$(CONFIG_AML_DVFS)                      += aml_dvfs/
