#
# Makefile for Inertial Measurement Units
#

adis16300-y             := adis16300_core.o
adis16300-$(CONFIG_IIO_RING_BUFFER) += adis16300_ring.o adis16300_trigger.o
obj-$(CONFIG_ADIS16300) += adis16300.o

adis16350-y             := adis16350_core.o
adis16350-$(CONFIG_IIO_RING_BUFFER) += adis16350_ring.o adis16350_trigger.o
obj-$(CONFIG_ADIS16350) += adis16350.o

adis16400-y             := adis16400_core.o
adis16400-$(CONFIG_IIO_RING_BUFFER) += adis16400_ring.o adis16400_trigger.o
obj-$(CONFIG_ADIS16400) += adis16400.o
