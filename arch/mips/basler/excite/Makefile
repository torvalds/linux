#
# Makefile for Basler eXcite
#

obj-$(CONFIG_BASLER_EXCITE)	+= excite_irq.o excite_prom.o excite_setup.o \
				   excite_device.o excite_procfs.o

obj-$(CONFIG_KGDB)		+= excite_dbg_io.o
obj-m				+= excite_iodev.o
