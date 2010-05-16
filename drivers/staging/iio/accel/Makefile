#
# Makefile for industrial I/O accelerometer drivers
#
adis16209-y             := adis16209_core.o
adis16209-$(CONFIG_IIO_RING_BUFFER) += adis16209_ring.o adis16209_trigger.o
obj-$(CONFIG_ADIS16209) += adis16209.o

adis16220-y             := adis16220_core.o
obj-$(CONFIG_ADIS16220) += adis16220.o

adis16240-y             := adis16240_core.o
adis16240-$(CONFIG_IIO_RING_BUFFER) += adis16240_ring.o adis16240_trigger.o
obj-$(CONFIG_ADIS16240) += adis16240.o

obj-$(CONFIG_KXSD9)	+= kxsd9.o

lis3l02dq-y		:= lis3l02dq_core.o
lis3l02dq-$(CONFIG_IIO_RING_BUFFER) += lis3l02dq_ring.o
obj-$(CONFIG_LIS3L02DQ)	+= lis3l02dq.o

sca3000-y		:= sca3000_core.o sca3000_ring.o
obj-$(CONFIG_SCA3000)	+= sca3000.o