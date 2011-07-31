#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm
nouveau-y := nouveau_drv.o nouveau_state.o nouveau_channel.o nouveau_mem.o \
             nouveau_object.o nouveau_irq.o nouveau_notifier.o \
             nouveau_sgdma.o nouveau_dma.o \
             nouveau_bo.o nouveau_fence.o nouveau_gem.o nouveau_ttm.o \
             nouveau_hw.o nouveau_calc.o nouveau_bios.o nouveau_i2c.o \
             nouveau_display.o nouveau_connector.o nouveau_fbcon.o \
             nouveau_dp.o \
             nv04_timer.o \
             nv04_mc.o nv40_mc.o nv50_mc.o \
             nv04_fb.o nv10_fb.o nv30_fb.o nv40_fb.o nv50_fb.o nvc0_fb.o \
             nv04_fifo.o nv10_fifo.o nv40_fifo.o nv50_fifo.o nvc0_fifo.o \
             nv04_graph.o nv10_graph.o nv20_graph.o \
             nv40_graph.o nv50_graph.o nvc0_graph.o \
             nv40_grctx.o nv50_grctx.o \
             nv04_instmem.o nv50_instmem.o nvc0_instmem.o \
             nv50_crtc.o nv50_dac.o nv50_sor.o \
             nv50_cursor.o nv50_display.o nv50_fbcon.o \
             nv04_dac.o nv04_dfp.o nv04_tv.o nv17_tv.o nv17_tv_modes.o \
             nv04_crtc.o nv04_display.o nv04_cursor.o nv04_fbcon.o \
             nv10_gpio.o nv50_gpio.o \
	     nv50_calc.o

nouveau-$(CONFIG_DRM_NOUVEAU_DEBUG) += nouveau_debugfs.o
nouveau-$(CONFIG_COMPAT) += nouveau_ioc32.o
nouveau-$(CONFIG_DRM_NOUVEAU_BACKLIGHT) += nouveau_backlight.o
nouveau-$(CONFIG_ACPI) += nouveau_acpi.o

obj-$(CONFIG_DRM_NOUVEAU)+= nouveau.o
