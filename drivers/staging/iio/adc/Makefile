#
# Makefile for industrial I/O ADC drivers
#

ad7606-y := ad7606_core.o ad7606_ring.o
obj-$(CONFIG_AD7606_IFACE_PARALLEL) += ad7606_par.o
obj-$(CONFIG_AD7606_IFACE_SPI) += ad7606_spi.o
obj-$(CONFIG_AD7606) += ad7606.o

obj-$(CONFIG_AD7780) += ad7780.o
obj-$(CONFIG_AD7816) += ad7816.o
obj-$(CONFIG_AD7192) += ad7192.o
obj-$(CONFIG_AD7280) += ad7280a.o
obj-$(CONFIG_LPC32XX_ADC) += lpc32xx_adc.o
obj-$(CONFIG_MXS_LRADC) += mxs-lradc.o
obj-$(CONFIG_SPEAR_ADC) += spear_adc.o
