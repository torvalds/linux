#
# Makefile for metering ic drivers
#

obj-$(CONFIG_ADE7753) += ade7753.o
obj-$(CONFIG_ADE7754) += ade7754.o

ade7758-y             := ade7758_core.o
ade7758-$(CONFIG_IIO_RING_BUFFER) += ade7758_ring.o ade7758_trigger.o
obj-$(CONFIG_ADE7758) += ade7758.o
