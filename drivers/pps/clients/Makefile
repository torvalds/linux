#
# Makefile for PPS clients.
#

obj-$(CONFIG_PPS_CLIENT_KTIMER)	+= pps-ktimer.o
obj-$(CONFIG_PPS_CLIENT_LDISC)	+= pps-ldisc.o
obj-$(CONFIG_PPS_CLIENT_PARPORT) += pps_parport.o

ccflags-$(CONFIG_PPS_DEBUG) := -DDEBUG
