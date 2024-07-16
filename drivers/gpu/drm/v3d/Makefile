# SPDX-License-Identifier: GPL-2.0-only
# Please keep these build lists sorted!

# core driver code
v3d-y := \
	v3d_bo.o \
	v3d_drv.o \
	v3d_fence.o \
	v3d_gem.o \
	v3d_irq.o \
	v3d_mmu.o \
	v3d_perfmon.o \
	v3d_trace_points.o \
	v3d_sched.o

v3d-$(CONFIG_DEBUG_FS) += v3d_debugfs.o

obj-$(CONFIG_DRM_V3D)  += v3d.o

CFLAGS_v3d_trace_points.o := -I$(src)
