# SPDX-License-Identifier: GPL-2.0
#
# Makefile for at91 specific clk
#

obj-y += pmc.o sckc.o dt-compat.o
obj-y += clk-slow.o clk-main.o clk-pll.o clk-plldiv.o clk-master.o
obj-y += clk-system.o clk-peripheral.o clk-programmable.o

obj-$(CONFIG_HAVE_AT91_AUDIO_PLL)	+= clk-audio-pll.o
obj-$(CONFIG_HAVE_AT91_UTMI)		+= clk-utmi.o
obj-$(CONFIG_HAVE_AT91_USB_CLK)		+= clk-usb.o
obj-$(CONFIG_HAVE_AT91_SMD)		+= clk-smd.o
obj-$(CONFIG_HAVE_AT91_H32MX)		+= clk-h32mx.o
obj-$(CONFIG_HAVE_AT91_GENERATED_CLK)	+= clk-generated.o
obj-$(CONFIG_HAVE_AT91_I2S_MUX_CLK)	+= clk-i2s-mux.o
obj-$(CONFIG_HAVE_AT91_SAM9X60_PLL)	+= clk-sam9x60-pll.o
obj-$(CONFIG_SOC_AT91RM9200) += at91rm9200.o
obj-$(CONFIG_SOC_AT91SAM9) += at91sam9260.o at91sam9rl.o at91sam9x5.o
obj-$(CONFIG_SOC_AT91SAM9) += at91sam9g45.o
obj-$(CONFIG_SOC_AT91SAM9) += at91sam9n12.o at91sam9x5.o
obj-$(CONFIG_SOC_SAM9X60) += sam9x60.o
obj-$(CONFIG_SOC_SAMA5D3) += sama5d3.o
obj-$(CONFIG_SOC_SAMA5D4) += sama5d4.o
obj-$(CONFIG_SOC_SAMA5D2) += sama5d2.o
obj-$(CONFIG_SOC_SAMA7G5) += sama7g5.o
