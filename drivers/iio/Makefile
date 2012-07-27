#
# Makefile for the industrial I/O core.
#

obj-$(CONFIG_IIO) += industrialio.o
industrialio-y := industrialio-core.o industrialio-event.o inkern.o
industrialio-$(CONFIG_IIO_BUFFER) += industrialio-buffer.o
industrialio-$(CONFIG_IIO_TRIGGER) += industrialio-trigger.o

obj-$(CONFIG_IIO_KFIFO_BUF) += kfifo_buf.o

obj-y += adc/
obj-y += amplifiers/
