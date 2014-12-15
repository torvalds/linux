#
# Makefile for the linux kernel.
#

# Object file lists.

ifeq ($(CONFIG_ARCH_MESON1),y)
obj-y = m1/
endif
ifeq ($(CONFIG_ARCH_MESON2),y)
obj-y = m2/
endif
ifeq ($(CONFIG_ARCH_MESON3),y)
obj-y = m3/
endif
ifeq ($(CONFIG_ARCH_MESON6),y)
obj-y = m6/
endif
ifeq ($(CONFIG_ARCH_MESON6TV),y)
obj-y = m6tv/
endif
ifeq ($(CONFIG_ARCH_MESON6TVD),y)
obj-y = m6tvd/
endif
ifeq ($(CONFIG_ARCH_MESON8),y)
obj-y = m8/
endif
ifeq ($(CONFIG_ARCH_MESON8B),y)
obj-y = m8b/
endif

obj-$(CONFIG_AM_STREAMING) += amports.o

AMPORTS_OBJS = amstream.o streambuf.o vdec.o adec.o tsdemux.o psparser.o esparser.o rmparser.o amvdec.o amstream_profile.o tsync_pcr.o

ifeq ($(CONFIG_ARCH_MESON8B),y)
amports-y    := amstream.o streambuf.o vdec.o adec.o tsdemux.o psparser.o esparser.o rmparser.o amvdec.o amstream_profile.o  tsync_pcr.o
else
ifeq ($(CONFIG_ARCH_MESON8),y)
amports-objs := $(AMPORTS_OBJS)
amports-y    := $(AMPORTS_OBJS)
else
ifeq ($(CONFIG_ARCH_MESON6),y)
amports-objs := $(AMPORTS_OBJS)
amports-y    := $(AMPORTS_OBJS)
else
ifeq ($(CONFIG_ARCH_MESON6TV),y)
amports-objs := $(AMPORTS_OBJS)
amports-y    := $(AMPORTS_OBJS)
else
ifeq ($(CONFIG_ARCH_MESON6TVD),y)
amports-objs := $(AMPORTS_OBJS)
amports-y    := $(AMPORTS_OBJS)
else
amports-objs := $(AMPORTS_OBJS) amve.o amcm.o
amports-y    := $(AMPORTS_OBJS) amve.o amcm.o
endif
endif
endif
endif
endif

obj-$(CONFIG_AM_VDEC_MPEG12) += amvdec_mpeg12.o
amvdec_mpeg12-objs := vmpeg12.o
amvdec_mpeg12-y    := vmpeg12.o

obj-$(CONFIG_AM_VDEC_MPEG4) += amvdec_mpeg4.o
amvdec_mpeg4-objs := vmpeg4.o
amvdec_mpeg4-y    := vmpeg4.o

obj-$(CONFIG_AM_VDEC_VC1) += amvdec_vc1.o
amvdec_vc1-objs := vvc1.o
amvdec_vc1-y    := vvc1.o

obj-$(CONFIG_AM_VDEC_H264) += amvdec_h264.o
amvdec_h264-objs := vh264.o
amvdec_h264-y    := vh264.o

obj-$(CONFIG_AM_VDEC_H264MVC) += amvdec_h264mvc.o
amvdec_h264mvc-objs := vh264mvc.o
amvdec_h264mvc-y    := vh264mvc.o

obj-$(CONFIG_AM_VDEC_H264_4K2K) += amvdec_h264_4k2k.o
amvdec_h264_4k2k-objs := vh264_4k2k.o
amvdec_h264_4k2k-y    := vh264_4k2k.o

obj-$(CONFIG_AM_VDEC_H265) += amvdec_h265.o
amvdec_h265-objs := vh265.o
amvdec_h265-y    := vh265.o

obj-$(CONFIG_AM_VDEC_MJPEG) += amvdec_mjpeg.o
amvdec_mjpeg-objs := vmjpeg.o
amvdec_mjpeg-y    := vmjpeg.o

obj-$(CONFIG_AM_VDEC_REAL) += amvdec_real.o
amvdec_real-objs := vreal.o
amvdec_real-y    := vreal.o

ifeq ($(CONFIG_ARCH_MESON6TV),y)
obj-$(CONFIG_AM_VDEC_AVS) += amvdec_avs.o
amvdec_avs-objs := vavs.o
amvdec_avs-y    := vavs.o
else
ifeq ($(CONFIG_ARCH_MESON6TVD),y)
obj-$(CONFIG_AM_VDEC_AVS) += amvdec_avs.o
amvdec_avs-objs := vavs.o
amvdec_avs-y    := vavs.o
else
ifeq ($(CONFIG_ARCH_MESON8B),y)
obj-$(CONFIG_AM_VDEC_AVS) += amvdec_avs.o
amvdec_avs-objs := vavs.o
amvdec_avs-y    := vavs.o
else
ifeq ($(CONFIG_ARCH_MESON8),y)
obj-$(CONFIG_AM_VDEC_AVS) += amvdec_avs.o
amvdec_avs-objs := vavs.o
amvdec_avs-y    := vavs.o
endif
endif
endif
endif

obj-$(CONFIG_AM_JPEGDEC) += jpegdec.o

obj-$(CONFIG_AM_ENCODER) += encoder.o
obj-$(CONFIG_AM_JPEG_ENCODER) += jpegenc.o
obj-$(CONFIG_AM_PIC_DEC) += picdec.o
obj-$(CONFIG_AM_VIDEO) += amvideo.o
ifeq ($(CONFIG_ARCH_MESON8),y)
amvideo-objs := video.o vpp.o video_prot.o
amvideo-y    := video.o vpp.o video_prot.o
else
amvideo-objs := video.o vpp.o
amvideo-y    := video.o vpp.o
endif

ifeq ($(CONFIG_AM_VIDEOCAPTURE),y)
amports-objs +=  amvideocap.o
amports-y   +=  amvideocap.o
endif

obj-$(CONFIG_AM_VIDEO2) += amvideo2.o
amvideo2-objs := video2.o vpp2.o
amvideo2-y    := video2.o vpp2.o

obj-$(CONFIG_AM_DEINTERLACE) += deinterlace.o

obj-$(CONFIG_AM_TIMESYNC) += amtsync.o
amtsync-objs := tsync.o timestamp.o
amtsync-y    := tsync.o timestamp.o

obj-$(CONFIG_AM_PTSSERVER) += ptsserv.o

obj-$(CONFIG_AM_SUBTITLE) += subtitle.o

obj-$(CONFIG_VSYNC_RDMA) += rdma.o


ifneq ($(CONFIG_AM_DVB),)
EXTRA_CFLAGS = -DENABLE_DEMUX_DRIVER -DMODIFY_TIMESTAMP_INC_WITH_PLL -DCALC_CACHED_TIME #-DUSE_VMASTER_MODE
endif
