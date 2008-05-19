#
# Makefile for the kernel PNPACPI driver.
#

obj-y := core.o rsparser.o

ifeq ($(CONFIG_PNP_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
