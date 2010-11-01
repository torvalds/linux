#
# Makefile for the TILE on-chip networking support.
#

obj-$(CONFIG_TILE_NET) += tile_net.o
ifdef CONFIG_TILEGX
tile_net-objs := tilegx.o mpipe.o iorpc_mpipe.o dma_queue.o
else
tile_net-objs := tilepro.o
endif
