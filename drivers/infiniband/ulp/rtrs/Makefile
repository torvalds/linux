# SPDX-License-Identifier: GPL-2.0-or-later

CFLAGS_rtrs-clt-trace.o = -I$(src)

rtrs-client-y := rtrs-clt.o \
		  rtrs-clt-stats.o \
		  rtrs-clt-sysfs.o \
		  rtrs-clt-trace.o

CFLAGS_rtrs-srv-trace.o = -I$(src)

rtrs-server-y := rtrs-srv.o \
		  rtrs-srv-stats.o \
		  rtrs-srv-sysfs.o \
		  rtrs-srv-trace.o

rtrs-core-y := rtrs.o

obj-$(CONFIG_INFINIBAND_RTRS)        += rtrs-core.o
obj-$(CONFIG_INFINIBAND_RTRS_CLIENT) += rtrs-client.o
obj-$(CONFIG_INFINIBAND_RTRS_SERVER) += rtrs-server.o
