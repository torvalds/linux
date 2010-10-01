#
# Makefile for industrial I/O ADC drivers
#

max1363-y := max1363_core.o
max1363-y += max1363_ring.o

obj-$(CONFIG_MAX1363) += max1363.o

ad799x-y := ad799x_core.o
ad799x-$(CONFIG_AD799X_RING_BUFFER) += ad799x_ring.o
obj-$(CONFIG_AD799X) += ad799x.o
