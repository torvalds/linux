# SPDX-License-Identifier: GPL-2.0
# Makefile for Amlogic meson video decoder driver

meson-vdec-objs = esparser.o vdec.o vdec_helpers.o vdec_platform.o
meson-vdec-objs += vdec_1.o vdec_hevc.o
meson-vdec-objs += codec_mpeg12.o codec_h264.o codec_hevc_common.o codec_vp9.o

obj-$(CONFIG_VIDEO_MESON_VDEC) += meson-vdec.o
