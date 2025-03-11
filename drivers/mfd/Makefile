# SPDX-License-Identifier: GPL-2.0
#
# Makefile for multifunction miscellaneous devices
#

88pm860x-objs			:= 88pm860x-core.o 88pm860x-i2c.o
obj-$(CONFIG_MFD_88PM860X)	+= 88pm860x.o
obj-$(CONFIG_MFD_88PM800)	+= 88pm800.o 88pm80x.o
obj-$(CONFIG_MFD_88PM805)	+= 88pm805.o 88pm80x.o
obj-$(CONFIG_MFD_88PM886_PMIC)	+= 88pm886.o
obj-$(CONFIG_MFD_ACT8945A)	+= act8945a.o
obj-$(CONFIG_MFD_SM501)		+= sm501.o
obj-$(CONFIG_ARCH_BCM2835)	+= bcm2835-pm.o
obj-$(CONFIG_MFD_BCM590XX)	+= bcm590xx.o
obj-$(CONFIG_MFD_BD9571MWV)	+= bd9571mwv.o
obj-$(CONFIG_MFD_CGBC)		+= cgbc-core.o
obj-$(CONFIG_MFD_CROS_EC_DEV)	+= cros_ec_dev.o
obj-$(CONFIG_MFD_CS42L43)	+= cs42l43.o
obj-$(CONFIG_MFD_CS42L43_I2C)	+= cs42l43-i2c.o
obj-$(CONFIG_MFD_CS42L43_SDW)	+= cs42l43-sdw.o
obj-$(CONFIG_MFD_ENE_KB3930)	+= ene-kb3930.o
obj-$(CONFIG_MFD_EXYNOS_LPASS)	+= exynos-lpass.o
obj-$(CONFIG_MFD_GATEWORKS_GSC)	+= gateworks-gsc.o

obj-$(CONFIG_MFD_TI_LP873X)	+= lp873x.o
obj-$(CONFIG_MFD_TI_LP87565)	+= lp87565.o
obj-$(CONFIG_MFD_TI_AM335X_TSCADC)	+= ti_am335x_tscadc.o

obj-$(CONFIG_MFD_STMPE)		+= stmpe.o
obj-$(CONFIG_STMPE_I2C)		+= stmpe-i2c.o
obj-$(CONFIG_STMPE_SPI)		+= stmpe-spi.o
obj-$(CONFIG_MFD_SUN6I_PRCM)	+= sun6i-prcm.o
obj-$(CONFIG_MFD_TC3589X)	+= tc3589x.o
obj-$(CONFIG_MFD_TQMX86)	+= tqmx86.o

obj-$(CONFIG_MFD_LOCHNAGAR)	+= lochnagar-i2c.o

arizona-objs			:= arizona-core.o arizona-irq.o
obj-$(CONFIG_MFD_ARIZONA)	+= arizona.o
obj-$(CONFIG_MFD_ARIZONA_I2C)	+= arizona-i2c.o
obj-$(CONFIG_MFD_ARIZONA_SPI)	+= arizona-spi.o
ifeq ($(CONFIG_MFD_WM5102),y)
arizona-objs			+= wm5102-tables.o
endif
ifeq ($(CONFIG_MFD_WM5110),y)
arizona-objs			+= wm5110-tables.o
endif
ifeq ($(CONFIG_MFD_WM8997),y)
arizona-objs			+= wm8997-tables.o
endif
ifeq ($(CONFIG_MFD_WM8998),y)
arizona-objs			+= wm8998-tables.o
endif
ifeq ($(CONFIG_MFD_CS47L24),y)
arizona-objs			+= cs47l24-tables.o
endif
obj-$(CONFIG_MFD_WCD934X)	+= wcd934x.o
obj-$(CONFIG_MFD_WM8400)	+= wm8400-core.o
wm831x-objs			:= wm831x-core.o wm831x-irq.o wm831x-otp.o
wm831x-objs			+= wm831x-auxadc.o
obj-$(CONFIG_MFD_WM831X)	+= wm831x.o
obj-$(CONFIG_MFD_WM831X_I2C)	+= wm831x-i2c.o
obj-$(CONFIG_MFD_WM831X_SPI)	+= wm831x-spi.o
wm8350-objs			:= wm8350-core.o wm8350-regmap.o wm8350-gpio.o
wm8350-objs			+= wm8350-irq.o
obj-$(CONFIG_MFD_WM8350)	+= wm8350.o
obj-$(CONFIG_MFD_WM8350_I2C)	+= wm8350-i2c.o
wm8994-objs			:= wm8994-core.o wm8994-irq.o wm8994-regmap.o
obj-$(CONFIG_MFD_WM8994)	+= wm8994.o
obj-$(CONFIG_MFD_WM97xx)	+= wm97xx-core.o

madera-objs			:= madera-core.o
ifeq ($(CONFIG_MFD_CS47L15),y)
madera-objs			+= cs47l15-tables.o
endif
ifeq ($(CONFIG_MFD_CS47L35),y)
madera-objs			+= cs47l35-tables.o
endif
ifeq ($(CONFIG_MFD_CS47L85),y)
madera-objs			+= cs47l85-tables.o
endif
ifeq ($(CONFIG_MFD_CS47L90),y)
madera-objs			+= cs47l90-tables.o
endif
ifeq ($(CONFIG_MFD_CS47L92),y)
madera-objs			+= cs47l92-tables.o
endif
obj-$(CONFIG_MFD_MADERA)	+= madera.o
obj-$(CONFIG_MFD_MADERA_I2C)	+= madera-i2c.o
obj-$(CONFIG_MFD_MADERA_SPI)	+= madera-spi.o

obj-$(CONFIG_MFD_CS40L50_CORE)	+= cs40l50-core.o
obj-$(CONFIG_MFD_CS40L50_I2C)	+= cs40l50-i2c.o
obj-$(CONFIG_MFD_CS40L50_SPI)	+= cs40l50-spi.o

obj-$(CONFIG_TPS6105X)		+= tps6105x.o
obj-$(CONFIG_TPS65010)		+= tps65010.o
obj-$(CONFIG_TPS6507X)		+= tps6507x.o
obj-$(CONFIG_MFD_TPS65086)	+= tps65086.o
obj-$(CONFIG_MFD_TPS65217)	+= tps65217.o
obj-$(CONFIG_MFD_TPS65218)	+= tps65218.o
obj-$(CONFIG_MFD_TPS65219)	+= tps65219.o
obj-$(CONFIG_MFD_TPS65910)	+= tps65910.o
obj-$(CONFIG_MFD_TPS65912)	+= tps65912-core.o
obj-$(CONFIG_MFD_TPS65912_I2C)	+= tps65912-i2c.o
obj-$(CONFIG_MFD_TPS65912_SPI)  += tps65912-spi.o
obj-$(CONFIG_MFD_TPS6594)	+= tps6594-core.o
obj-$(CONFIG_MFD_TPS6594_I2C)	+= tps6594-i2c.o
obj-$(CONFIG_MFD_TPS6594_SPI)	+= tps6594-spi.o
obj-$(CONFIG_MENELAUS)		+= menelaus.o

obj-$(CONFIG_TWL4030_CORE)	+= twl-core.o twl4030-irq.o twl6030-irq.o
obj-$(CONFIG_TWL4030_POWER)    += twl4030-power.o
obj-$(CONFIG_MFD_TWL4030_AUDIO)	+= twl4030-audio.o
obj-$(CONFIG_TWL6040_CORE)	+= twl6040.o

obj-$(CONFIG_MFD_MX25_TSADC)	+= fsl-imx25-tsadc.o

obj-$(CONFIG_MFD_MC13XXX)	+= mc13xxx-core.o
obj-$(CONFIG_MFD_MC13XXX_SPI)	+= mc13xxx-spi.o
obj-$(CONFIG_MFD_MC13XXX_I2C)	+= mc13xxx-i2c.o

obj-$(CONFIG_MFD_CORE)		+= mfd-core.o

ocelot-soc-objs			:= ocelot-core.o ocelot-spi.o
obj-$(CONFIG_MFD_OCELOT)	+= ocelot-soc.o

obj-$(CONFIG_EZX_PCAP)		+= ezx-pcap.o
obj-$(CONFIG_MFD_CPCAP)		+= motorola-cpcap.o

obj-$(CONFIG_MCP)		+= mcp-core.o
obj-$(CONFIG_MCP_SA11X0)	+= mcp-sa11x0.o
obj-$(CONFIG_MCP_UCB1200)	+= ucb1x00-core.o
obj-$(CONFIG_MCP_UCB1200_TS)	+= ucb1x00-ts.o

ifeq ($(CONFIG_SA1100_ASSABET),y)
obj-$(CONFIG_MCP_UCB1200)	+= ucb1x00-assabet.o
endif

obj-$(CONFIG_PMIC_DA903X)	+= da903x.o

obj-$(CONFIG_PMIC_DA9052)	+= da9052-irq.o
obj-$(CONFIG_PMIC_DA9052)	+= da9052-core.o
obj-$(CONFIG_MFD_DA9052_SPI)	+= da9052-spi.o
obj-$(CONFIG_MFD_DA9052_I2C)	+= da9052-i2c.o

obj-$(CONFIG_MFD_AC100)		+= ac100.o
obj-$(CONFIG_MFD_AXP20X)	+= axp20x.o
obj-$(CONFIG_MFD_AXP20X_I2C)	+= axp20x-i2c.o
obj-$(CONFIG_MFD_AXP20X_RSB)	+= axp20x-rsb.o

obj-$(CONFIG_MFD_LP3943)	+= lp3943.o
obj-$(CONFIG_MFD_LP8788)	+= lp8788.o lp8788-irq.o

obj-$(CONFIG_MFD_TI_LMU)	+= ti-lmu.o

da9055-objs			:= da9055-core.o da9055-i2c.o
obj-$(CONFIG_MFD_DA9055)	+= da9055.o
obj-$(CONFIG_MFD_DA9062)	+= da9062-core.o
da9063-objs			:= da9063-core.o da9063-irq.o da9063-i2c.o
obj-$(CONFIG_MFD_DA9063)	+= da9063.o
obj-$(CONFIG_MFD_DA9150)	+= da9150-core.o

obj-$(CONFIG_MFD_MAX14577)	+= max14577.o
obj-$(CONFIG_MFD_MAX77541)	+= max77541.o
obj-$(CONFIG_MFD_MAX77620)	+= max77620.o
obj-$(CONFIG_MFD_MAX77650)	+= max77650.o
obj-$(CONFIG_MFD_MAX77686)	+= max77686.o
obj-$(CONFIG_MFD_MAX77693)	+= max77693.o
obj-$(CONFIG_MFD_MAX77705)	+= max77705.o
obj-$(CONFIG_MFD_MAX77714)	+= max77714.o
obj-$(CONFIG_MFD_MAX77843)	+= max77843.o
obj-$(CONFIG_MFD_MAX8907)	+= max8907.o
max8925-objs			:= max8925-core.o max8925-i2c.o
obj-$(CONFIG_MFD_MAX8925)	+= max8925.o
obj-$(CONFIG_MFD_MAX8997)	+= max8997.o max8997-irq.o
obj-$(CONFIG_MFD_MAX8998)	+= max8998.o max8998-irq.o

obj-$(CONFIG_MFD_MP2629)	+= mp2629.o

obj-$(CONFIG_MFD_MT6360)	+= mt6360-core.o
obj-$(CONFIG_MFD_MT6370)	+= mt6370.o
mt6397-objs			:= mt6397-core.o mt6397-irq.o mt6358-irq.o
obj-$(CONFIG_MFD_MT6397)	+= mt6397.o

obj-$(CONFIG_RZ_MTU3)		+= rz-mtu3.o
obj-$(CONFIG_ABX500_CORE)	+= abx500-core.o
obj-$(CONFIG_MFD_DB8500_PRCMU)	+= db8500-prcmu.o
# ab8500-core need to come after db8500-prcmu (which provides the channel)
obj-$(CONFIG_AB8500_CORE)	+= ab8500-core.o ab8500-sysctrl.o
obj-$(CONFIG_MFD_TIMBERDALE)    += timberdale.o
obj-$(CONFIG_PMIC_ADP5520)	+= adp5520.o
obj-$(CONFIG_MFD_ADP5585)	+= adp5585.o
obj-$(CONFIG_MFD_KEMPLD)	+= kempld-core.o
obj-$(CONFIG_MFD_INTEL_QUARK_I2C_GPIO)	+= intel_quark_i2c_gpio.o
obj-$(CONFIG_LPC_SCH)		+= lpc_sch.o
obj-$(CONFIG_LPC_ICH)		+= lpc_ich.o
obj-$(CONFIG_MFD_RDC321X)	+= rdc321x-southbridge.o
obj-$(CONFIG_MFD_JANZ_CMODIO)	+= janz-cmodio.o
obj-$(CONFIG_MFD_TPS6586X)	+= tps6586x.o
obj-$(CONFIG_MFD_VX855)		+= vx855.o
obj-$(CONFIG_MFD_WL1273_CORE)	+= wl1273-core.o

si476x-core-y := si476x-cmd.o si476x-prop.o si476x-i2c.o
obj-$(CONFIG_MFD_SI476X_CORE)	+= si476x-core.o

obj-$(CONFIG_MFD_CS5535)	+= cs5535-mfd.o
obj-$(CONFIG_MFD_OMAP_USB_HOST)	+= omap-usb-host.o omap-usb-tll.o
obj-$(CONFIG_MFD_PM8XXX) 	+= qcom-pm8xxx.o ssbi.o
obj-$(CONFIG_MFD_QCOM_RPM)	+= qcom_rpm.o
obj-$(CONFIG_MFD_SPMI_PMIC)	+= qcom-spmi-pmic.o
obj-$(CONFIG_TPS65911_COMPARATOR)	+= tps65911-comparator.o
obj-$(CONFIG_MFD_TPS65090)	+= tps65090.o
obj-$(CONFIG_MFD_AAT2870_CORE)	+= aat2870-core.o
obj-$(CONFIG_MFD_AT91_USART)	+= at91-usart.o
obj-$(CONFIG_MFD_ATMEL_FLEXCOM)	+= atmel-flexcom.o
obj-$(CONFIG_MFD_ATMEL_HLCDC)	+= atmel-hlcdc.o
obj-$(CONFIG_MFD_ATMEL_SMC)	+= atmel-smc.o
obj-$(CONFIG_MFD_INTEL_LPSS)	+= intel-lpss.o
obj-$(CONFIG_MFD_INTEL_LPSS_PCI)	+= intel-lpss-pci.o
obj-$(CONFIG_MFD_INTEL_LPSS_ACPI)	+= intel-lpss-acpi.o
obj-$(CONFIG_MFD_INTEL_PMC_BXT)	+= intel_pmc_bxt.o
obj-$(CONFIG_MFD_PALMAS)	+= palmas.o
obj-$(CONFIG_MFD_VIPERBOARD)    += viperboard.o
obj-$(CONFIG_MFD_NTXEC)		+= ntxec.o
obj-$(CONFIG_MFD_RC5T583)	+= rc5t583.o rc5t583-irq.o
obj-$(CONFIG_MFD_RK8XX)		+= rk8xx-core.o
obj-$(CONFIG_MFD_RK8XX_I2C)	+= rk8xx-i2c.o
obj-$(CONFIG_MFD_RK8XX_SPI)	+= rk8xx-spi.o
obj-$(CONFIG_MFD_RN5T618)	+= rn5t618.o
obj-$(CONFIG_MFD_SEC_CORE)	+= sec-core.o sec-irq.o
obj-$(CONFIG_MFD_SYSCON)	+= syscon.o
obj-$(CONFIG_MFD_LM3533)	+= lm3533-core.o lm3533-ctrlbank.o
obj-$(CONFIG_MFD_VEXPRESS_SYSREG)	+= vexpress-sysreg.o
obj-$(CONFIG_MFD_RETU)		+= retu-mfd.o
obj-$(CONFIG_MFD_AS3711)	+= as3711.o
obj-$(CONFIG_MFD_AS3722)	+= as3722.o
obj-$(CONFIG_MFD_STW481X)	+= stw481x.o
obj-$(CONFIG_MFD_IPAQ_MICRO)	+= ipaq-micro.o
obj-$(CONFIG_MFD_IQS62X)	+= iqs62x.o
obj-$(CONFIG_MFD_MENF21BMC)	+= menf21bmc.o
obj-$(CONFIG_MFD_HI6421_PMIC)	+= hi6421-pmic-core.o
obj-$(CONFIG_MFD_HI6421_SPMI)	+= hi6421-spmi-pmic.o
obj-$(CONFIG_MFD_HI655X_PMIC)   += hi655x-pmic.o
obj-$(CONFIG_MFD_DLN2)		+= dln2.o
obj-$(CONFIG_MFD_RT4831)	+= rt4831.o
obj-$(CONFIG_MFD_RT5033)	+= rt5033.o
obj-$(CONFIG_MFD_RT5120)	+= rt5120.o
obj-$(CONFIG_MFD_SKY81452)	+= sky81452.o

obj-$(CONFIG_INTEL_SOC_PMIC)		+= intel_soc_pmic_crc.o
obj-$(CONFIG_INTEL_SOC_PMIC_BXTWC)	+= intel_soc_pmic_bxtwc.o
obj-$(CONFIG_INTEL_SOC_PMIC_CHTWC)	+= intel_soc_pmic_chtwc.o
obj-$(CONFIG_INTEL_SOC_PMIC_CHTDC_TI)	+= intel_soc_pmic_chtdc_ti.o
obj-$(CONFIG_INTEL_SOC_PMIC_MRFLD)	+= intel_soc_pmic_mrfld.o

obj-$(CONFIG_MFD_ALTERA_A10SR)	+= altera-a10sr.o
obj-$(CONFIG_MFD_ALTERA_SYSMGR) += altera-sysmgr.o
obj-$(CONFIG_MFD_STPMIC1)	+= stpmic1.o
obj-$(CONFIG_MFD_SUN4I_GPADC)	+= sun4i-gpadc.o

obj-$(CONFIG_MFD_STM32_LPTIMER)	+= stm32-lptimer.o
obj-$(CONFIG_MFD_STM32_TIMERS) 	+= stm32-timers.o
obj-$(CONFIG_MFD_MXS_LRADC)     += mxs-lradc.o
obj-$(CONFIG_MFD_SC27XX_PMIC)	+= sprd-sc27xx-spi.o
obj-$(CONFIG_RAVE_SP_CORE)	+= rave-sp.o
obj-$(CONFIG_MFD_ROHM_BD71828)	+= rohm-bd71828.o
obj-$(CONFIG_MFD_ROHM_BD718XX)	+= rohm-bd718x7.o
obj-$(CONFIG_MFD_ROHM_BD957XMUF)	+= rohm-bd9576.o
obj-$(CONFIG_MFD_ROHM_BD96801)	+= rohm-bd96801.o
obj-$(CONFIG_MFD_STMFX) 	+= stmfx.o
obj-$(CONFIG_MFD_KHADAS_MCU) 	+= khadas-mcu.o
obj-$(CONFIG_MFD_ACER_A500_EC)	+= acer-ec-a500.o
obj-$(CONFIG_MFD_QCOM_PM8008)	+= qcom-pm8008.o

obj-$(CONFIG_SGI_MFD_IOC3)	+= ioc3.o
obj-$(CONFIG_MFD_SIMPLE_MFD_I2C)	+= simple-mfd-i2c.o
obj-$(CONFIG_MFD_SMPRO)		+= smpro-core.o

obj-$(CONFIG_MFD_INTEL_M10_BMC_CORE)   += intel-m10-bmc-core.o
obj-$(CONFIG_MFD_INTEL_M10_BMC_SPI)    += intel-m10-bmc-spi.o
obj-$(CONFIG_MFD_INTEL_M10_BMC_PMCI)   += intel-m10-bmc-pmci.o

obj-$(CONFIG_MFD_ATC260X)	+= atc260x-core.o
obj-$(CONFIG_MFD_ATC260X_I2C)	+= atc260x-i2c.o

obj-$(CONFIG_MFD_QNAP_MCU)	+= qnap-mcu.o

obj-$(CONFIG_MFD_RSMU_I2C)	+= rsmu_i2c.o rsmu_core.o
obj-$(CONFIG_MFD_RSMU_SPI)	+= rsmu_spi.o rsmu_core.o

obj-$(CONFIG_MFD_UPBOARD_FPGA)	+= upboard-fpga.o
