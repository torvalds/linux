# SPDX-License-Identifier: GPL-2.0+
# Copyright (C) 2019-2021 Maximilian Luz <luzmaximilian@gmail.com>

# For include/trace/define_trace.h to include trace.h
CFLAGS_core.o = -I$(src)

obj-$(CONFIG_SURFACE_AGGREGATOR) += surface_aggregator.o

surface_aggregator-objs := core.o
surface_aggregator-objs += ssh_parser.o
surface_aggregator-objs += ssh_packet_layer.o
surface_aggregator-objs += ssh_request_layer.o
surface_aggregator-objs += controller.o

ifeq ($(CONFIG_SURFACE_AGGREGATOR_BUS),y)
surface_aggregator-objs += bus.o
endif
