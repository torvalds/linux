# SPDX-License-Identifier: GPL-2.0
b2c2-flexcop-objs += flexcop.o flexcop-fe-tuner.o flexcop-i2c.o
b2c2-flexcop-objs += flexcop-sram.o flexcop-eeprom.o flexcop-misc.o
b2c2-flexcop-objs += flexcop-hw-filter.o
obj-$(CONFIG_DVB_B2C2_FLEXCOP) += b2c2-flexcop.o

ccflags-y += -I $(srctree)/drivers/media/dvb-frontends/
ccflags-y += -I $(srctree)/drivers/media/tuners/
