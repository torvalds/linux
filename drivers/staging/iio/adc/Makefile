#
# Makefile for industrial I/O ADC drivers
#

max1363-y := max1363_core.o
max1363-y += max1363_ring.o

obj-$(CONFIG_MAX1363) += max1363.o

ad7606-y := ad7606_core.o
ad7606-$(CONFIG_IIO_BUFFER) += ad7606_ring.o
ad7606-$(CONFIG_AD7606_IFACE_PARALLEL) += ad7606_par.o
ad7606-$(CONFIG_AD7606_IFACE_SPI) += ad7606_spi.o
obj-$(CONFIG_AD7606) += ad7606.o

ad799x-y := ad799x_core.o
ad799x-$(CONFIG_AD799X_RING_BUFFER) += ad799x_ring.o
obj-$(CONFIG_AD799X) += ad799x.o

ad7887-y := ad7887_core.o
ad7887-$(CONFIG_IIO_BUFFER) += ad7887_ring.o
obj-$(CONFIG_AD7887) += ad7887.o

ad7298-y := ad7298_core.o
ad7298-$(CONFIG_IIO_BUFFER) += ad7298_ring.o
obj-$(CONFIG_AD7298) += ad7298.o

obj-$(CONFIG_AD7291) += ad7291.o
obj-$(CONFIG_AD7780) += ad7780.o
obj-$(CONFIG_AD7793) += ad7793.o
obj-$(CONFIG_AD7816) += ad7816.o
obj-$(CONFIG_AD7192) += ad7192.o
obj-$(CONFIG_ADT7410) += adt7410.o
obj-$(CONFIG_AD7280) += ad7280a.o
obj-$(CONFIG_LPC32XX_ADC) += lpc32xx_adc.o
obj-$(CONFIG_MXS_LRADC) += mxs-lradc.o
obj-$(CONFIG_SPEAR_ADC) += spear_adc.o
