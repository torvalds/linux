#
# Makefile for the m68knommu kernel.
#

#
# If you want to play with the HW breakpoints then you will
# need to add define this,  which will give you a stack backtrace
# on the console port whenever a DBG interrupt occurs. You have to
# set up you HW breakpoints to trigger a DBG interrupt:
#
# EXTRA_CFLAGS += -DTRAP_DBG_INTERRUPT
# EXTRA_AFLAGS += -DTRAP_DBG_INTERRUPT
#

ifdef CONFIG_FULLDEBUG
AFLAGS += -DDEBUGGER_COMPATIBLE_CACHE=1
endif

obj-$(CONFIG_COLDFIRE)	+= dma.o entry.o vectors.o
obj-$(CONFIG_M5206)	+= timers.o
obj-$(CONFIG_M5206e)	+= timers.o
obj-$(CONFIG_M520x)	+= pit.o
obj-$(CONFIG_M523x)	+= pit.o
obj-$(CONFIG_M5249)	+= timers.o
obj-$(CONFIG_M527x)	+= pit.o
obj-$(CONFIG_M5272)	+= timers.o
obj-$(CONFIG_M528x)	+= pit.o
obj-$(CONFIG_M5307)	+= timers.o
obj-$(CONFIG_M532x)	+= timers.o
obj-$(CONFIG_M5407)	+= timers.o

extra-y := head.o
