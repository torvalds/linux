#
# Makefile for PPS generators.
#

obj-$(CONFIG_PPS_GENERATOR_PARPORT) += pps_gen_parport.o

ifeq ($(CONFIG_PPS_DE),y)
EXTRA_CFLAGS += -DDE
endif
