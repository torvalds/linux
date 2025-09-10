# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2024-2025 Intel Corporation

obj-$(CONFIG_LIBETH)		+= libeth.o

libeth-y			:= rx.o
libeth-y			+= tx.o

obj-$(CONFIG_LIBETH_XDP)	+= libeth_xdp.o

libeth_xdp-y			+= xdp.o
libeth_xdp-y			+= xsk.o
