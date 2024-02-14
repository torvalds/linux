# SPDX-License-Identifier: GPL-2.0
vivid-objs := vivid-core.o vivid-ctrls.o vivid-vid-common.o vivid-vbi-gen.o \
		vivid-vid-cap.o vivid-vid-out.o vivid-kthread-cap.o vivid-kthread-out.o \
		vivid-radio-rx.o vivid-radio-tx.o vivid-radio-common.o \
		vivid-rds-gen.o vivid-sdr-cap.o vivid-vbi-cap.o vivid-vbi-out.o \
		vivid-osd.o vivid-meta-cap.o vivid-meta-out.o \
		vivid-kthread-touch.o vivid-touch-cap.o
ifeq ($(CONFIG_VIDEO_VIVID_CEC),y)
  vivid-objs += vivid-cec.o
endif

obj-$(CONFIG_VIDEO_VIVID) += vivid.o
