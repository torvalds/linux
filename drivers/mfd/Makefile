#
# Makefile for multifunction miscellaneous devices
#

88pm860x-objs			:= 88pm860x-core.o 88pm860x-i2c.o
obj-$(CONFIG_MFD_88PM860X)	+= 88pm860x.o
obj-$(CONFIG_MFD_88PM800)	+= 88pm800.o 88pm80x.o
obj-$(CONFIG_MFD_88PM805)	+= 88pm805.o 88pm80x.o
obj-$(CONFIG_MFD_SM501)		+= sm501.o
obj-$(CONFIG_MFD_ASIC3)		+= asic3.o tmio_core.o

obj-$(CONFIG_HTC_EGPIO)		+= htc-egpio.o
obj-$(CONFIG_HTC_PASIC3)	+= htc-pasic3.o
obj-$(CONFIG_HTC_I2CPLD)	+= htc-i2cpld.o

obj-$(CONFIG_MFD_DAVINCI_VOICECODEC)	+= davinci_voicecodec.o
obj-$(CONFIG_MFD_DM355EVM_MSP)	+= dm355evm_msp.o
obj-$(CONFIG_MFD_TI_SSP)	+= ti-ssp.o

obj-$(CONFIG_MFD_STA2X11)	+= sta2x11-mfd.o
obj-$(CONFIG_MFD_STMPE)		+= stmpe.o
obj-$(CONFIG_STMPE_I2C)		+= stmpe-i2c.o
obj-$(CONFIG_STMPE_SPI)		+= stmpe-spi.o
obj-$(CONFIG_MFD_TC3589X)	+= tc3589x.o
obj-$(CONFIG_MFD_T7L66XB)	+= t7l66xb.o tmio_core.o
obj-$(CONFIG_MFD_TC6387XB)	+= tc6387xb.o tmio_core.o
obj-$(CONFIG_MFD_TC6393XB)	+= tc6393xb.o tmio_core.o

obj-$(CONFIG_MFD_ARIZONA)	+= arizona-core.o
obj-$(CONFIG_MFD_ARIZONA)	+= arizona-irq.o
obj-$(CONFIG_MFD_ARIZONA_I2C)	+= arizona-i2c.o
obj-$(CONFIG_MFD_ARIZONA_SPI)	+= arizona-spi.o
ifneq ($(CONFIG_MFD_WM5102),n)
obj-$(CONFIG_MFD_ARIZONA)	+= wm5102-tables.o
endif
ifneq ($(CONFIG_MFD_WM5110),n)
obj-$(CONFIG_MFD_ARIZONA)	+= wm5110-tables.o
endif
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
obj-$(CONFIG_MFD_WM8994)	+= wm8994-core.o wm8994-irq.o wm8994-regmap.o

obj-$(CONFIG_TPS6105X)		+= tps6105x.o
obj-$(CONFIG_TPS65010)		+= tps65010.o
obj-$(CONFIG_TPS6507X)		+= tps6507x.o
obj-$(CONFIG_MFD_TPS65217)	+= tps65217.o
obj-$(CONFIG_MFD_TPS65910)	+= tps65910.o tps65910-irq.o
tps65912-objs                   := tps65912-core.o tps65912-irq.o
obj-$(CONFIG_MFD_TPS65912)	+= tps65912.o
obj-$(CONFIG_MFD_TPS65912_I2C)	+= tps65912-i2c.o
obj-$(CONFIG_MFD_TPS65912_SPI)  += tps65912-spi.o
obj-$(CONFIG_MENELAUS)		+= menelaus.o

obj-$(CONFIG_TWL4030_CORE)	+= twl-core.o twl4030-irq.o twl6030-irq.o
obj-$(CONFIG_TWL4030_MADC)      += twl4030-madc.o
obj-$(CONFIG_TWL4030_POWER)    += twl4030-power.o
obj-$(CONFIG_MFD_TWL4030_AUDIO)	+= twl4030-audio.o
obj-$(CONFIG_TWL6040_CORE)	+= twl6040-core.o twl6040-irq.o

obj-$(CONFIG_MFD_MC13XXX)	+= mc13xxx-core.o
obj-$(CONFIG_MFD_MC13XXX_SPI)	+= mc13xxx-spi.o
obj-$(CONFIG_MFD_MC13XXX_I2C)	+= mc13xxx-i2c.o

obj-$(CONFIG_MFD_CORE)		+= mfd-core.o

obj-$(CONFIG_EZX_PCAP)		+= ezx-pcap.o

obj-$(CONFIG_MCP)		+= mcp-core.o
obj-$(CONFIG_MCP_SA11X0)	+= mcp-sa11x0.o
obj-$(CONFIG_MCP_UCB1200)	+= ucb1x00-core.o
obj-$(CONFIG_MFD_SMSC)        += smsc-ece1099.o
obj-$(CONFIG_MCP_UCB1200_TS)	+= ucb1x00-ts.o

ifeq ($(CONFIG_SA1100_ASSABET),y)
obj-$(CONFIG_MCP_UCB1200)	+= ucb1x00-assabet.o
endif
obj-$(CONFIG_UCB1400_CORE)	+= ucb1400_core.o

obj-$(CONFIG_PMIC_DA903X)	+= da903x.o

obj-$(CONFIG_PMIC_DA9052)	+= da9052-core.o
obj-$(CONFIG_MFD_DA9052_SPI)	+= da9052-spi.o
obj-$(CONFIG_MFD_DA9052_I2C)	+= da9052-i2c.o

obj-$(CONFIG_MFD_LP8788)	+= lp8788.o lp8788-irq.o

da9055-objs			:= da9055-core.o da9055-i2c.o
obj-$(CONFIG_MFD_DA9055)	+= da9055.o

obj-$(CONFIG_MFD_MAX77686)	+= max77686.o max77686-irq.o
obj-$(CONFIG_MFD_MAX77693)	+= max77693.o max77693-irq.o
obj-$(CONFIG_MFD_MAX8907)	+= max8907.o
max8925-objs			:= max8925-core.o max8925-i2c.o
obj-$(CONFIG_MFD_MAX8925)	+= max8925.o
obj-$(CONFIG_MFD_MAX8997)	+= max8997.o max8997-irq.o
obj-$(CONFIG_MFD_MAX8998)	+= max8998.o max8998-irq.o

pcf50633-objs			:= pcf50633-core.o pcf50633-irq.o
obj-$(CONFIG_MFD_PCF50633)	+= pcf50633.o
obj-$(CONFIG_PCF50633_ADC)	+= pcf50633-adc.o
obj-$(CONFIG_PCF50633_GPIO)	+= pcf50633-gpio.o
obj-$(CONFIG_ABX500_CORE)	+= abx500-core.o
obj-$(CONFIG_AB3100_CORE)	+= ab3100-core.o
obj-$(CONFIG_AB3100_OTP)	+= ab3100-otp.o
obj-$(CONFIG_AB8500_DEBUG)	+= ab8500-debugfs.o
obj-$(CONFIG_AB8500_GPADC)	+= ab8500-gpadc.o
obj-$(CONFIG_MFD_DB8500_PRCMU)	+= db8500-prcmu.o
# ab8500-core need to come after db8500-prcmu (which provides the channel)
obj-$(CONFIG_AB8500_CORE)	+= ab8500-core.o ab8500-sysctrl.o
obj-$(CONFIG_MFD_TIMBERDALE)    += timberdale.o
obj-$(CONFIG_PMIC_ADP5520)	+= adp5520.o
obj-$(CONFIG_LPC_SCH)		+= lpc_sch.o
obj-$(CONFIG_LPC_ICH)		+= lpc_ich.o
obj-$(CONFIG_MFD_RDC321X)	+= rdc321x-southbridge.o
obj-$(CONFIG_MFD_JANZ_CMODIO)	+= janz-cmodio.o
obj-$(CONFIG_MFD_JZ4740_ADC)	+= jz4740-adc.o
obj-$(CONFIG_MFD_TPS6586X)	+= tps6586x.o
obj-$(CONFIG_MFD_VX855)		+= vx855.o
obj-$(CONFIG_MFD_WL1273_CORE)	+= wl1273-core.o
obj-$(CONFIG_MFD_CS5535)	+= cs5535-mfd.o
obj-$(CONFIG_MFD_OMAP_USB_HOST)	+= omap-usb-host.o omap-usb-tll.o
obj-$(CONFIG_MFD_PM8921_CORE) 	+= pm8921-core.o
obj-$(CONFIG_MFD_PM8XXX_IRQ) 	+= pm8xxx-irq.o
obj-$(CONFIG_TPS65911_COMPARATOR)	+= tps65911-comparator.o
obj-$(CONFIG_MFD_TPS65090)	+= tps65090.o
obj-$(CONFIG_MFD_AAT2870_CORE)	+= aat2870-core.o
obj-$(CONFIG_MFD_INTEL_MSIC)	+= intel_msic.o
obj-$(CONFIG_MFD_PALMAS)	+= palmas.o
obj-$(CONFIG_MFD_RC5T583)	+= rc5t583.o rc5t583-irq.o
obj-$(CONFIG_MFD_SEC_CORE)	+= sec-core.o sec-irq.o
obj-$(CONFIG_MFD_SYSCON)	+= syscon.o
obj-$(CONFIG_MFD_LM3533)	+= lm3533-core.o lm3533-ctrlbank.o
obj-$(CONFIG_VEXPRESS_CONFIG)	+= vexpress-config.o vexpress-sysreg.o
