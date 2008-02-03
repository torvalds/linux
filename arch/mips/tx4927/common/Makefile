#
# Makefile for common code for Toshiba TX4927 based systems
#

obj-y	+= tx4927_prom.o tx4927_irq.o

obj-$(CONFIG_TOSHIBA_FPCIB0)	   += smsc_fdc37m81x.o
obj-$(CONFIG_KGDB)                 += tx4927_dbgio.o

EXTRA_CFLAGS += -Werror
