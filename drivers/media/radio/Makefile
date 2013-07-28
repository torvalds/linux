#
# Makefile for the kernel character device drivers.
#

obj-$(CONFIG_RADIO_ISA) += radio-isa.o
obj-$(CONFIG_RADIO_AZTECH) += radio-aztech.o
obj-$(CONFIG_RADIO_RTRACK2) += radio-rtrack2.o
obj-$(CONFIG_RADIO_SF16FMI) += radio-sf16fmi.o
obj-$(CONFIG_RADIO_SF16FMR2) += radio-sf16fmr2.o
obj-$(CONFIG_RADIO_CADET) += radio-cadet.o
obj-$(CONFIG_RADIO_TYPHOON) += radio-typhoon.o
obj-$(CONFIG_RADIO_TERRATEC) += radio-terratec.o
obj-$(CONFIG_RADIO_MAXIRADIO) += radio-maxiradio.o
obj-$(CONFIG_RADIO_SHARK) += radio-shark.o
obj-$(CONFIG_RADIO_SHARK2) += shark2.o
obj-$(CONFIG_RADIO_RTRACK) += radio-aimslab.o
obj-$(CONFIG_RADIO_ZOLTRIX) += radio-zoltrix.o
obj-$(CONFIG_RADIO_GEMTEK) += radio-gemtek.o
obj-$(CONFIG_RADIO_TRUST) += radio-trust.o
obj-$(CONFIG_I2C_SI4713) += si4713-i2c.o
obj-$(CONFIG_RADIO_SI4713) += radio-si4713.o
obj-$(CONFIG_RADIO_SI476X) += radio-si476x.o
obj-$(CONFIG_RADIO_MIROPCM20) += radio-miropcm20.o
obj-$(CONFIG_USB_DSBR) += dsbr100.o
obj-$(CONFIG_RADIO_SI470X) += si470x/
obj-$(CONFIG_USB_MR800) += radio-mr800.o
obj-$(CONFIG_USB_KEENE) += radio-keene.o
obj-$(CONFIG_USB_MA901) += radio-ma901.o
obj-$(CONFIG_RADIO_TEA5764) += radio-tea5764.o
obj-$(CONFIG_RADIO_SAA7706H) += saa7706h.o
obj-$(CONFIG_RADIO_TEF6862) += tef6862.o
obj-$(CONFIG_RADIO_TIMBERDALE) += radio-timb.o
obj-$(CONFIG_RADIO_WL1273) += radio-wl1273.o
obj-$(CONFIG_RADIO_WL128X) += wl128x/
obj-$(CONFIG_RADIO_TEA575X) += tea575x.o

shark2-objs := radio-shark2.o radio-tea5777.o

ccflags-y += -Isound
