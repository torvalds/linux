#
# Makefile for PPS clients.
#

obj-$(CONFIG_PPS_CLIENT_KTIMER)	+= pps-ktimer.o

ifeq ($(CONFIG_PPS_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
