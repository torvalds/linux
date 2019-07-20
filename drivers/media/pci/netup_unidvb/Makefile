# SPDX-License-Identifier: GPL-2.0
netup-unidvb-objs += netup_unidvb_core.o
netup-unidvb-objs += netup_unidvb_i2c.o
netup-unidvb-objs += netup_unidvb_ci.o
netup-unidvb-objs += netup_unidvb_spi.o

obj-$(CONFIG_DVB_NETUP_UNIDVB) += netup-unidvb.o

ccflags-y += -I $(srctree)/drivers/media/dvb-frontends
