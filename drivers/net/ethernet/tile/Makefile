#
# Makefile for the TILE on-chip networking support.
#

obj-$(CONFIG_TILE_NET) += tile_net.o
ifdef CONFIG_TILEGX
tile_net-y := tilegx.o
else
tile_net-y := tilepro.o
endif
