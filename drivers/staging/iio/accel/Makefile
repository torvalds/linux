#
# Makefile for industrial I/O accelerometer drivers
#
obj-$(CONFIG_KXSD9)	+= kxsd9.o

lis3l02dq-y		:= lis3l02dq_core.o
lis3l02dq-$(CONFIG_IIO_RING_BUFFER) += lis3l02dq_ring.o
obj-$(CONFIG_LIS3L02DQ)	+= lis3l02dq.o

sca3000-y		:= sca3000_core.o sca3000_ring.o
obj-$(CONFIG_SCA3000)	+= sca3000.o