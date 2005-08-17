#
# Makefile for the Dallas's 1-wire bus.
#

ifneq ($(CONFIG_NET), y)
EXTRA_CFLAGS	+= -DNETLINK_DISABLED
endif

ifeq ($(CONFIG_W1_DS2433_CRC), y)
EXTRA_CFLAGS	+= -DCONFIG_W1_F23_CRC
endif

obj-$(CONFIG_W1)	+= wire.o
wire-objs		:= w1.o w1_int.o w1_family.o w1_netlink.o w1_io.o

obj-$(CONFIG_W1_MATROX)		+= matrox_w1.o
obj-$(CONFIG_W1_THERM)		+= w1_therm.o
obj-$(CONFIG_W1_SMEM)		+= w1_smem.o

obj-$(CONFIG_W1_DS9490)		+= ds9490r.o
ds9490r-objs    := dscore.o

obj-$(CONFIG_W1_DS9490_BRIDGE)	+= ds_w1_bridge.o

obj-$(CONFIG_W1_DS2433)		+= w1_ds2433.o
