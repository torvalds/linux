#
# Makefile for industrial I/O ADC drivers
#

max1363-y := max1363_core.o
max1363-y += max1363_ring.o

obj-$(CONFIG_MAX1363) += max1363.o

ad799x-y := ad799x_core.o
ad799x-$(CONFIG_AD799X_RING_BUFFER) += ad799x_ring.o
obj-$(CONFIG_AD799X) += ad799x.o

ad7476-y := ad7476_core.o
ad7476-$(CONFIG_IIO_RING_BUFFER) += ad7476_ring.o
obj-$(CONFIG_AD7476) += ad7476.o

ad7887-y := ad7887_core.o
ad7887-$(CONFIG_IIO_RING_BUFFER) += ad7887_ring.o
obj-$(CONFIG_AD7887) += ad7887.o

obj-$(CONFIG_AD7150) += ad7150.o
obj-$(CONFIG_AD7152) += ad7152.o
obj-$(CONFIG_AD7291) += ad7291.o
obj-$(CONFIG_AD7298) += ad7298.o
obj-$(CONFIG_AD7314) += ad7314.o
obj-$(CONFIG_AD7745) += ad7745.o
obj-$(CONFIG_AD7816) += ad7816.o
obj-$(CONFIG_ADT75) += adt75.o
obj-$(CONFIG_ADT7310) += adt7310.o
obj-$(CONFIG_ADT7410) += adt7410.o
