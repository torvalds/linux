# SPDX-License-Identifier: GPL-2.0+
# Copyright (C) 2019-2021 Maximilian Luz <luzmaximilian@gmail.com>

# For include/trace/define_trace.h to include trace.h
CFLAGS_core.o = -I$(src)

obj-$(CONFIG_SURFACE_AGGREGATOR) += surface_aggregator.o

surface_aggregator-y := core.o
surface_aggregator-y += ssh_parser.o
surface_aggregator-y += ssh_packet_layer.o
surface_aggregator-y += ssh_request_layer.o
surface_aggregator-$(CONFIG_SURFACE_AGGREGATOR_BUS) += bus.o
surface_aggregator-y += controller.o
