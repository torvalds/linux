#
# Makefile for multifunction miscellaneous devices
#

obj-$(CONFIG_MFD_SM501)		+= sm501.o
obj-$(CONFIG_MFD_ASIC3)		+= asic3.o tmio_core.o
obj-$(CONFIG_MFD_SH_MOBILE_SDHI)		+= sh_mobile_sdhi.o

obj-$(CONFIG_HTC_EGPIO)		+= htc-egpio.o
obj-$(CONFIG_HTC_PASIC3)	+= htc-pasic3.o

obj-$(CONFIG_MFD_DM355EVM_MSP)	+= dm355evm_msp.o

obj-$(CONFIG_MFD_T7L66XB)	+= t7l66xb.o tmio_core.o
obj-$(CONFIG_MFD_TC6387XB)	+= tc6387xb.o tmio_core.o
obj-$(CONFIG_MFD_TC6393XB)	+= tc6393xb.o tmio_core.o

obj-$(CONFIG_MFD_WM8400)	+= wm8400-core.o
wm831x-objs			:= wm831x-core.o wm831x-irq.o wm831x-otp.o
obj-$(CONFIG_MFD_WM831X)	+= wm831x.o
wm8350-objs			:= wm8350-core.o wm8350-regmap.o wm8350-gpio.o
wm8350-objs			+= wm8350-irq.o
obj-$(CONFIG_MFD_WM8350)	+= wm8350.o
obj-$(CONFIG_MFD_WM8350_I2C)	+= wm8350-i2c.o

obj-$(CONFIG_TPS65010)		+= tps65010.o
obj-$(CONFIG_MENELAUS)		+= menelaus.o

obj-$(CONFIG_TWL4030_CORE)	+= twl-core.o twl4030-irq.o twl6030-irq.o
obj-$(CONFIG_TWL4030_POWER)    += twl4030-power.o
obj-$(CONFIG_TWL4030_CODEC)	+= twl4030-codec.o

obj-$(CONFIG_MFD_MC13783)	+= mc13783-core.o

obj-$(CONFIG_MFD_CORE)		+= mfd-core.o

obj-$(CONFIG_EZX_PCAP)		+= ezx-pcap.o

obj-$(CONFIG_MCP)		+= mcp-core.o
obj-$(CONFIG_MCP_SA11X0)	+= mcp-sa11x0.o
obj-$(CONFIG_MCP_UCB1200)	+= ucb1x00-core.o
obj-$(CONFIG_MCP_UCB1200_TS)	+= ucb1x00-ts.o

ifeq ($(CONFIG_SA1100_ASSABET),y)
obj-$(CONFIG_MCP_UCB1200)	+= ucb1x00-assabet.o
endif
obj-$(CONFIG_UCB1400_CORE)	+= ucb1400_core.o

obj-$(CONFIG_PMIC_DA903X)	+= da903x.o

obj-$(CONFIG_MFD_PCF50633)	+= pcf50633-core.o
obj-$(CONFIG_PCF50633_ADC)	+= pcf50633-adc.o
obj-$(CONFIG_PCF50633_GPIO)	+= pcf50633-gpio.o
obj-$(CONFIG_AB3100_CORE)	+= ab3100-core.o
obj-$(CONFIG_AB3100_OTP)	+= ab3100-otp.o
obj-$(CONFIG_AB4500_CORE)	+= ab4500-core.o
obj-$(CONFIG_MFD_88PM8607)	+= 88pm8607.o
obj-$(CONFIG_PMIC_ADP5520)	+= adp5520.o