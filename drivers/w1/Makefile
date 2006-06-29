#
# Makefile for the Dallas's 1-wire bus.
#

ifeq ($(CONFIG_W1_DS2433_CRC), y)
EXTRA_CFLAGS	+= -DCONFIG_W1_F23_CRC
endif

obj-$(CONFIG_W1)	+= wire.o
wire-objs		:= w1.o w1_int.o w1_family.o w1_netlink.o w1_io.o

obj-y			+= masters/ slaves/

