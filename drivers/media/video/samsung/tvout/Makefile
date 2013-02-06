# linux/drivers/media/video/samsung/tvout/Makefile
#
# Copyright (c) 2009 Samsung Electronics
#		http://www.samsung.com/
#
# Makefile for Samsung TVOUT driver

ifeq ($(CONFIG_VIDEO_TVOUT_DEBUG), y)
EXTRA_CFLAGS += -DDEBUG
endif
ifeq ($(CONFIG_VIDEO_UMP),y)
    EXTRA_CFLAGS += -Idrivers/media/video/samsung/ump/include
endif

obj-$(CONFIG_VIDEO_TVOUT) :=	s5p_tvout.o		\
				s5p_tvout_v4l2.o	\
				s5p_tvout_fb.o		\
				s5p_tvout_common_lib.o	\
				s5p_mixer_ctrl.o	\
				s5p_vp_ctrl.o		\
				s5p_tvif_ctrl.o

obj-$(CONFIG_HDMI_HPD) +=	s5p_tvout_hpd.o

obj-$(CONFIG_HDMI_CEC) +=	s5p_tvout_cec.o

obj-$(CONFIG_VIDEO_TVOUT) += hw_if/
