#
# Makefile for the Dallas's 1-wire slaves.
#

ifeq ($(CONFIG_W1_SLAVE_DS2433_CRC), y)
EXTRA_CFLAGS += -DCONFIG_W1_F23_CRC
endif

obj-$(CONFIG_W1_SLAVE_THERM)	+= w1_therm.o
obj-$(CONFIG_W1_SLAVE_SMEM)	+= w1_smem.o
obj-$(CONFIG_W1_SLAVE_DS2433)	+= w1_ds2433.o

