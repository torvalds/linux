#
# Makefile for the V4L2 core
#

tuner-objs	:=	tuner-core.o

videodev-objs	:=	v4l2-dev.o v4l2-ioctl.o v4l2-device.o v4l2-fh.o \
			v4l2-event.o v4l2-ctrls.o v4l2-subdev.o v4l2-clk.o \
			v4l2-async.o
ifeq ($(CONFIG_COMPAT),y)
  videodev-objs += v4l2-compat-ioctl32.o
endif
ifeq ($(CONFIG_OF),y)
  videodev-objs += v4l2-of.o
endif
obj-$(CONFIG_V4L2_FWNODE) += v4l2-fwnode.o
ifeq ($(CONFIG_TRACEPOINTS),y)
  videodev-objs += vb2-trace.o v4l2-trace.o
endif
videodev-$(CONFIG_MEDIA_CONTROLLER) += v4l2-mc.o

obj-$(CONFIG_VIDEO_V4L2) += videodev.o
obj-$(CONFIG_VIDEO_V4L2) += v4l2-common.o
obj-$(CONFIG_VIDEO_V4L2) += v4l2-dv-timings.o

obj-$(CONFIG_VIDEO_TUNER) += tuner.o

obj-$(CONFIG_V4L2_MEM2MEM_DEV) += v4l2-mem2mem.o

obj-$(CONFIG_V4L2_FLASH_LED_CLASS) += v4l2-flash-led-class.o

obj-$(CONFIG_VIDEOBUF_GEN) += videobuf-core.o
obj-$(CONFIG_VIDEOBUF_DMA_SG) += videobuf-dma-sg.o
obj-$(CONFIG_VIDEOBUF_DMA_CONTIG) += videobuf-dma-contig.o
obj-$(CONFIG_VIDEOBUF_VMALLOC) += videobuf-vmalloc.o
obj-$(CONFIG_VIDEOBUF_DVB) += videobuf-dvb.o

obj-$(CONFIG_VIDEOBUF2_CORE) += videobuf2-core.o videobuf2-v4l2.o
obj-$(CONFIG_VIDEOBUF2_MEMOPS) += videobuf2-memops.o
obj-$(CONFIG_VIDEOBUF2_VMALLOC) += videobuf2-vmalloc.o
obj-$(CONFIG_VIDEOBUF2_DMA_CONTIG) += videobuf2-dma-contig.o
obj-$(CONFIG_VIDEOBUF2_DMA_SG) += videobuf2-dma-sg.o
obj-$(CONFIG_VIDEOBUF2_DVB) += videobuf2-dvb.o

ccflags-y += -I$(srctree)/drivers/media/dvb-core
ccflags-y += -I$(srctree)/drivers/media/dvb-frontends
ccflags-y += -I$(srctree)/drivers/media/tuners

