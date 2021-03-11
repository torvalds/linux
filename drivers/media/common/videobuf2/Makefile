# SPDX-License-Identifier: GPL-2.0
videobuf2-common-objs := videobuf2-core.o
videobuf2-common-objs += frame_vector.o

ifeq ($(CONFIG_TRACEPOINTS),y)
  videobuf2-common-objs += vb2-trace.o
endif

obj-$(CONFIG_VIDEOBUF2_CORE) += videobuf2-common.o
obj-$(CONFIG_VIDEOBUF2_V4L2) += videobuf2-v4l2.o
obj-$(CONFIG_VIDEOBUF2_MEMOPS) += videobuf2-memops.o
obj-$(CONFIG_VIDEOBUF2_VMALLOC) += videobuf2-vmalloc.o
obj-$(CONFIG_VIDEOBUF2_DMA_CONTIG) += videobuf2-dma-contig.o
obj-$(CONFIG_VIDEOBUF2_DMA_SG) += videobuf2-dma-sg.o
obj-$(CONFIG_VIDEOBUF2_DVB) += videobuf2-dvb.o
