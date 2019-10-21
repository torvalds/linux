# SPDX-License-Identifier: GPL-2.0-only
obj-y := dma-buf.o dma-fence.o dma-fence-array.o dma-fence-chain.o \
	 dma-resv.o seqno-fence.o
obj-$(CONFIG_DMABUF_HEAPS)	+= dma-heap.o
obj-$(CONFIG_SYNC_FILE)		+= sync_file.o
obj-$(CONFIG_SW_SYNC)		+= sw_sync.o sync_debug.o
obj-$(CONFIG_UDMABUF)		+= udmabuf.o

dmabuf_selftests-y := \
	selftest.o \
	st-dma-fence.o

obj-$(CONFIG_DMABUF_SELFTESTS)	+= dmabuf_selftests.o
