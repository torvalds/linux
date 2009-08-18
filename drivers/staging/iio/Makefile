#
# Makefile for the industrial I/O core.
#

obj-$(CONFIG_IIO) += industrialio.o
industrialio-y := industrialio-core.o
industrialio-$(CONFIG_IIO_RING_BUFFER) += industrialio-ring.o
industrialio-$(CONFIG_IIO_TRIGGER) += industrialio-trigger.o

obj-y += accel/
obj-y += adc/
obj-y += light/