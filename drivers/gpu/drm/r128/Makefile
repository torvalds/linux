# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

r128-y   := r128_drv.o r128_cce.o r128_state.o r128_irq.o ati_pcigart.o

r128-$(CONFIG_COMPAT)   += r128_ioc32.o

obj-$(CONFIG_DRM_R128)	+= r128.o
