# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

qxl-y := qxl_drv.o qxl_kms.o qxl_display.o qxl_ttm.o qxl_object.o qxl_gem.o qxl_cmd.o qxl_image.o qxl_draw.o qxl_debugfs.o qxl_irq.o qxl_dumb.o qxl_ioctl.o qxl_release.o qxl_prime.o

obj-$(CONFIG_DRM_QXL)+= qxl.o
