
# Makefile for industrial I/O ADC drivers
#

max1363-y := max1363_core.o
max1363-$(CONFIG_MAX1363_RING_BUFFER) += max1363_ring.o

obj-$(CONFIG_MAX1363) += max1363.o
