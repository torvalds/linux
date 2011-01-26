ccflags-$(CONFIG_POWER_SUPPLY_DEBUG) := -DDEBUG

power_supply-y				:= power_supply_core.o
power_supply-$(CONFIG_SYSFS)		+= power_supply_sysfs.o
power_supply-$(CONFIG_LEDS_TRIGGERS)	+= power_supply_leds.o

obj-$(CONFIG_POWER_SUPPLY)	+= power_supply.o

obj-$(CONFIG_PDA_POWER)		+= pda_power.o
obj-$(CONFIG_APM_POWER)		+= apm_power.o
obj-$(CONFIG_MAX8925_POWER)	+= max8925_power.o
obj-$(CONFIG_WM831X_BACKUP)	+= wm831x_backup.o
obj-$(CONFIG_WM831X_POWER)	+= wm831x_power.o
obj-$(CONFIG_WM8350_POWER)	+= wm8350_power.o
obj-$(CONFIG_TEST_POWER)	+= test_power.o

obj-$(CONFIG_BATTERY_DS2760)	+= ds2760_battery.o
obj-$(CONFIG_BATTERY_DS2782)	+= ds2782_battery.o
obj-$(CONFIG_BATTERY_PMU)	+= pmu_battery.o
obj-$(CONFIG_BATTERY_OLPC)	+= olpc_battery.o
obj-$(CONFIG_BATTERY_TOSA)	+= tosa_battery.o
obj-$(CONFIG_BATTERY_COLLIE)	+= collie_battery.o
obj-$(CONFIG_BATTERY_WM97XX)	+= wm97xx_battery.o
obj-$(CONFIG_BATTERY_BQ20Z75)	+= bq20z75.o
obj-$(CONFIG_BATTERY_BQ27x00)	+= bq27x00_battery.o
obj-$(CONFIG_BATTERY_DA9030)	+= da9030_battery.o
obj-$(CONFIG_BATTERY_MAX17040)	+= max17040_battery.o
obj-$(CONFIG_BATTERY_MAX17042)	+= max17042_battery.o
obj-$(CONFIG_BATTERY_Z2)	+= z2_battery.o
obj-$(CONFIG_BATTERY_S3C_ADC)	+= s3c_adc_battery.o
obj-$(CONFIG_CHARGER_PCF50633)	+= pcf50633-charger.o
obj-$(CONFIG_BATTERY_JZ4740)	+= jz4740-battery.o
obj-$(CONFIG_BATTERY_INTEL_MID)	+= intel_mid_battery.o
obj-$(CONFIG_CHARGER_ISP1704)	+= isp1704_charger.o
obj-$(CONFIG_CHARGER_TWL4030)	+= twl4030_charger.o
obj-$(CONFIG_CHARGER_GPIO)	+= gpio-charger.o
