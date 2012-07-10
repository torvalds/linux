#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm -DCONFIG_NOUVEAU_DEBUG=7 -DCONFIG_NOUVEAU_DEBUG_DEFAULT=3
ccflags-y += -I$(src)/core/include
ccflags-y += -I$(src)

nouveau-y := core/core/client.o
nouveau-y += core/core/engine.o
nouveau-y += core/core/handle.o
nouveau-y += core/core/mm.o
nouveau-y += core/core/namedb.o
nouveau-y += core/core/object.o
nouveau-y += core/core/option.o
nouveau-y += core/core/parent.o
nouveau-y += core/core/printk.o
nouveau-y += core/core/ramht.o
nouveau-y += core/core/subdev.o

nouveau-y += core/subdev/bios/base.o
nouveau-y += core/subdev/bios/bit.o
nouveau-y += core/subdev/device/base.o
nouveau-y += core/subdev/device/nv04.o
nouveau-y += core/subdev/device/nv10.o
nouveau-y += core/subdev/device/nv20.o
nouveau-y += core/subdev/device/nv30.o
nouveau-y += core/subdev/device/nv40.o
nouveau-y += core/subdev/device/nv50.o
nouveau-y += core/subdev/device/nvc0.o
nouveau-y += core/subdev/device/nve0.o
nouveau-y += core/subdev/fb/nv04.o
nouveau-y += core/subdev/fb/nv10.o
nouveau-y += core/subdev/fb/nv20.o
nouveau-y += core/subdev/fb/nv30.o
nouveau-y += core/subdev/fb/nv40.o
nouveau-y += core/subdev/fb/nv50.o
nouveau-y += core/subdev/fb/nvc0.o
nouveau-y += core/subdev/fb/nv50_vram.o
nouveau-y += core/subdev/fb/nvc0_vram.o
nouveau-y += core/subdev/gpio/base.o
nouveau-y += core/subdev/gpio/nv10.o
nouveau-y += core/subdev/gpio/nv50.o
nouveau-y += core/subdev/i2c/base.o
nouveau-y += core/subdev/instmem/nv04.o
nouveau-y += core/subdev/instmem/nv50.o
nouveau-y += core/subdev/instmem/nvc0.o
nouveau-y += core/subdev/mc/nv04.o
nouveau-y += core/subdev/mc/nv40.o
nouveau-y += core/subdev/mc/nv50.o
nouveau-y += core/subdev/timer/nv04.o
nouveau-y += core/subdev/vm/base.o
nouveau-y += core/subdev/vm/nv50.o
nouveau-y += core/subdev/vm/nvc0.o

nouveau-y += core/engine/bsp/nv84.o
nouveau-y += core/engine/copy/nva3.o
nouveau-y += core/engine/copy/nvc0.o
nouveau-y += core/engine/crypt/nv84.o
nouveau-y += core/engine/crypt/nv98.o
nouveau-y += core/engine/fifo/nv04.o
nouveau-y += core/engine/fifo/nv10.o
nouveau-y += core/engine/fifo/nv17.o
nouveau-y += core/engine/fifo/nv40.o
nouveau-y += core/engine/fifo/nv50.o
nouveau-y += core/engine/fifo/nv84.o
nouveau-y += core/engine/fifo/nvc0.o
nouveau-y += core/engine/fifo/nve0.o
nouveau-y += core/engine/graph/ctxnv40.o
nouveau-y += core/engine/graph/ctxnv50.o
nouveau-y += core/engine/graph/ctxnvc0.o
nouveau-y += core/engine/graph/ctxnve0.o
nouveau-y += core/engine/graph/nv04.o
nouveau-y += core/engine/graph/nv10.o
nouveau-y += core/engine/graph/nv20.o
nouveau-y += core/engine/graph/nv40.o
nouveau-y += core/engine/graph/nv50.o
nouveau-y += core/engine/graph/nvc0.o
nouveau-y += core/engine/graph/nve0.o
nouveau-y += core/engine/mpeg/nv31.o
nouveau-y += core/engine/mpeg/nv50.o
nouveau-y += core/engine/ppp/nv98.o
nouveau-y += core/engine/vp/nv84.o

nouveau-y += nouveau_drm.o nouveau_compat.o \
	     nouveau_drv.o nouveau_state.o nouveau_channel.o nouveau_mem.o \
             nouveau_gpuobj.o nouveau_irq.o nouveau_notifier.o \
             nouveau_sgdma.o nouveau_dma.o nouveau_util.o \
             nouveau_bo.o nouveau_fence.o nouveau_gem.o nouveau_ttm.o \
             nouveau_hw.o nouveau_calc.o \
             nouveau_display.o nouveau_connector.o nouveau_fbcon.o \
             nouveau_hdmi.o nouveau_dp.o \
	     nouveau_pm.o nouveau_volt.o nouveau_perf.o nouveau_temp.o \
	     nouveau_mxm.o nouveau_agp.o \
	     nouveau_abi16.o \
	     nouveau_bios.o \
             nv04_fence.o nv10_fence.o nv84_fence.o nvc0_fence.o \
             nv04_software.o nv50_software.o nvc0_software.o \
             nv04_dac.o nv04_dfp.o nv04_tv.o nv17_tv.o nv17_tv_modes.o \
             nv04_crtc.o nv04_display.o nv04_cursor.o \
             nv50_evo.o nv50_crtc.o nv50_dac.o nv50_sor.o \
             nv50_cursor.o nv50_display.o \
             nvd0_display.o \
             nv04_fbcon.o nv50_fbcon.o nvc0_fbcon.o \
	     nv50_calc.o \
	     nv04_pm.o nv40_pm.o nv50_pm.o nva3_pm.o nvc0_pm.o \
	     nouveau_prime.o

nouveau-$(CONFIG_DRM_NOUVEAU_DEBUG) += nouveau_debugfs.o
nouveau-$(CONFIG_COMPAT) += nouveau_ioc32.o
nouveau-$(CONFIG_DRM_NOUVEAU_BACKLIGHT) += nouveau_backlight.o
nouveau-$(CONFIG_ACPI) += nouveau_acpi.o

obj-$(CONFIG_DRM_NOUVEAU)+= nouveau.o
