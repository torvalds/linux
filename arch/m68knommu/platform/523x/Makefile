#
# Makefile for the m68knommu linux kernel.
#

#
# If you want to play with the HW breakpoints then you will
# need to add define this,  which will give you a stack backtrace
# on the console port whenever a DBG interrupt occurs.  You have to
# set up you HW breakpoints to trigger a DBG interrupt:
#
# EXTRA_CFLAGS += -DTRAP_DBG_INTERRUPT
# EXTRA_AFLAGS += -DTRAP_DBG_INTERRUPT
#

asflags-$(CONFIG_FULLDEBUG) := -DDEBUGGER_COMPATIBLE_CACHE=1

obj-y := config.o
