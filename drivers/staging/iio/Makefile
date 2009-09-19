#
# Makefile for the industrial I/O core.
#

obj-$(CONFIG_IIO) += industrialio.o
industrialio-y := industrialio-core.o
industrialio-$(CONFIG_IIO_RING_BUFFER) += industrialio-ring.o
industrialio-$(CONFIG_IIO_TRIGGER) += industrialio-trigger.o

obj-$(CONFIG_IIO_SW_RING) += ring_sw.o

obj-y += accel/
obj-y += adc/
obj-y += light/

obj-y += trigger/