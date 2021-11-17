# SPDX-License-Identifier: GPL-2.0-or-later

ccflags-y := -I$(srctree)/drivers/infiniband/ulp/rtrs

rnbd-client-y := rnbd-clt.o \
		  rnbd-clt-sysfs.o \
		  rnbd-common.o

rnbd-server-y := rnbd-common.o \
		  rnbd-srv.o \
		  rnbd-srv-dev.o \
		  rnbd-srv-sysfs.o

obj-$(CONFIG_BLK_DEV_RNBD_CLIENT) += rnbd-client.o
obj-$(CONFIG_BLK_DEV_RNBD_SERVER) += rnbd-server.o
