#
# Makefile for the IP27 specific kernel interface routines under Linux.
#

obj-y	:= ip27-berr.o ip27-console.o ip27-irq.o ip27-init.o \
	   ip27-klconfig.o ip27-klnuma.o ip27-memory.o ip27-nmi.o ip27-reset.o \
	   ip27-timer.o ip27-hubio.o ip27-xtalk.o

obj-$(CONFIG_KGDB)	+= ip27-dbgio.o
obj-$(CONFIG_SMP)	+= ip27-smp.o

EXTRA_AFLAGS := $(CFLAGS)
