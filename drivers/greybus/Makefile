# SPDX-License-Identifier: GPL-2.0
# Greybus core
greybus-y :=	core.o		\
		debugfs.o	\
		hd.o		\
		manifest.o	\
		module.o	\
		interface.o	\
		bundle.o	\
		connection.o	\
		control.o	\
		svc.o		\
		svc_watchdog.o	\
		operation.o

obj-$(CONFIG_GREYBUS)		+= greybus.o

# needed for trace events
ccflags-y += -I$(src)

# Greybus Host controller drivers
gb-es2-y := es2.o

obj-$(CONFIG_GREYBUS_ES2)	+= gb-es2.o


