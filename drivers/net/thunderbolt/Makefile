# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_USB4_NET) := thunderbolt_net.o
thunderbolt_net-objs := main.o trace.o

# Tracepoints need to know where to find trace.h
CFLAGS_trace.o := -I$(src)
