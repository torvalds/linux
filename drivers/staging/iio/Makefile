#
# Makefile for the industrial I/O core.
#

obj-$(CONFIG_IIO) += industrialio.o
industrialio-y := industrialio-core.o
industrialio-$(CONFIG_IIO_RING_BUFFER) += industrialio-ring.o
industrialio-$(CONFIG_IIO_TRIGGER) += industrialio-trigger.o

obj-$(CONFIG_IIO_SW_RING) += ring_sw.o
obj-$(CONFIG_IIO_KFIFO_BUF) += kfifo_buf.o

obj-y += accel/
obj-y += adc/
obj-y += addac/
obj-y += dac/
obj-y += dds/
obj-y += gyro/
obj-y += imu/
obj-y += light/
obj-y += magnetometer/
obj-y += meter/
obj-y += resolver/
obj-y += trigger/
