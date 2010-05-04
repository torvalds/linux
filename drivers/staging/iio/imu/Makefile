#
# Makefile for Inertial Measurement Units
#
adis16300-y             := adis16300_core.o
adis16300-$(CONFIG_IIO_RING_BUFFER) += adis16300_ring.o adis16300_trigger.o
obj-$(CONFIG_ADIS16300) += adis16300.o
