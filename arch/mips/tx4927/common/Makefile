#
# Makefile for common code for Toshiba TX4927 based systems
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

obj-y	+= tx4927_prom.o tx4927_setup.o tx4927_irq.o

obj-$(CONFIG_TOSHIBA_FPCIB0)	   += smsc_fdc37m81x.o
obj-$(CONFIG_KGDB)                 += tx4927_dbgio.o
