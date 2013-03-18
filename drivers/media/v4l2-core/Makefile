#
# Makefile for the V4L2 core
#

tuner-objs	:=	tuner-core.o

videodev-objs	:=	v4l2-dev.o v4l2-ioctl.o v4l2-device.o v4l2-fh.o \
			v4l2-event.o v4l2-ctrls.o v4l2-subdev.o
ifeq ($(CONFIG_COMPAT),y)
  videodev-objs += v4l2-compat-ioctl32.o
endif

obj-$(CONFIG_VIDEO_DEV) += videodev.o
obj-$(CONFIG_VIDEO_V4L2_INT_DEVICE) += v4l2-int-device.o
obj-$(CONFIG_VIDEO_V4L2) += v4l2-common.o

obj-$(CONFIG_VIDEO_TUNER) += tuner.o

obj-$(CONFIG_V4L2_MEM2MEM_DEV) += v4l2-mem2mem.o

obj-$(CONFIG_VIDEOBUF_GEN) += videobuf-core.o
obj-$(CONFIG_VIDEOBUF_DMA_SG) += videobuf-dma-sg.o
obj-$(CONFIG_VIDEOBUF_DMA_CONTIG) += videobuf-dma-contig.o
obj-$(CONFIG_VIDEOBUF_VMALLOC) += videobuf-vmalloc.o
obj-$(CONFIG_VIDEOBUF_DVB) += videobuf-dvb.o

obj-$(CONFIG_VIDEOBUF2_CORE) += videobuf2-core.o
obj-$(CONFIG_VIDEOBUF2_MEMOPS) += videobuf2-memops.o
obj-$(CONFIG_VIDEOBUF2_VMALLOC) += videobuf2-vmalloc.o
obj-$(CONFIG_VIDEOBUF2_DMA_CONTIG) += videobuf2-dma-contig.o
obj-$(CONFIG_VIDEOBUF2_DMA_SG) += videobuf2-dma-sg.o

ccflags-y += -I$(srctree)/drivers/media/dvb-core
ccflags-y += -I$(srctree)/drivers/media/dvb-frontends
ccflags-y += -I$(srctree)/drivers/media/tuners

