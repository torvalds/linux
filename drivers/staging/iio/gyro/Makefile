#
# Makefile for digital gyroscope sensor drivers
#

adis16260-y             := adis16260_core.o
adis16260-$(CONFIG_IIO_RING_BUFFER) += adis16260_ring.o adis16260_trigger.o
obj-$(CONFIG_ADIS16260) += adis16260.o

adis16251-y             := adis16251_core.o
obj-$(CONFIG_ADIS16251) += adis16251.o
