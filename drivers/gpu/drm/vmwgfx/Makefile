
ccflags-y := -Iinclude/drm

vmwgfx-y := vmwgfx_execbuf.o vmwgfx_gmr.o vmwgfx_kms.o vmwgfx_drv.o \
	    vmwgfx_fb.o vmwgfx_ioctl.o vmwgfx_resource.o vmwgfx_buffer.o \
	    vmwgfx_fifo.o vmwgfx_irq.o vmwgfx_ldu.o vmwgfx_ttm_glue.o \
	    vmwgfx_overlay.o vmwgfx_fence.o

obj-$(CONFIG_DRM_VMWGFX) := vmwgfx.o
