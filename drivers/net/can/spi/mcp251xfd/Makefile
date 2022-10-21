# SPDX-License-Identifier: GPL-2.0-only

obj-$(CONFIG_CAN_MCP251XFD) += mcp251xfd.o

mcp251xfd-objs :=
mcp251xfd-objs += mcp251xfd-chip-fifo.o
mcp251xfd-objs += mcp251xfd-core.o
mcp251xfd-objs += mcp251xfd-crc16.o
mcp251xfd-objs += mcp251xfd-ethtool.o
mcp251xfd-objs += mcp251xfd-ram.o
mcp251xfd-objs += mcp251xfd-regmap.o
mcp251xfd-objs += mcp251xfd-ring.o
mcp251xfd-objs += mcp251xfd-rx.o
mcp251xfd-objs += mcp251xfd-tef.o
mcp251xfd-objs += mcp251xfd-timestamp.o
mcp251xfd-objs += mcp251xfd-tx.o

mcp251xfd-$(CONFIG_DEV_COREDUMP) += mcp251xfd-dump.o
