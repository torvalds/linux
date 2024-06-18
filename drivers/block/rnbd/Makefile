# SPDX-License-Identifier: GPL-2.0-or-later

ccflags-y := -I$(srctree)/drivers/infiniband/ulp/rtrs

rnbd-client-y := rnbd-clt.o \
		  rnbd-clt-sysfs.o

CFLAGS_rnbd-srv-trace.o = -I$(src)

rnbd-server-y := rnbd-srv.o \
		  rnbd-srv-sysfs.o \
		  rnbd-srv-trace.o

obj-$(CONFIG_BLK_DEV_RNBD_CLIENT) += rnbd-client.o
obj-$(CONFIG_BLK_DEV_RNBD_SERVER) += rnbd-server.o
