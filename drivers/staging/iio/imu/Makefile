#
# Makefile for Inertial Measurement Units
#

adis16400-y             := adis16400_core.o
adis16400-$(CONFIG_IIO_RING_BUFFER) += adis16400_ring.o adis16400_trigger.o
obj-$(CONFIG_ADIS16400) += adis16400.o
