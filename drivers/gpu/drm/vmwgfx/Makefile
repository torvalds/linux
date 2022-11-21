# SPDX-License-Identifier: GPL-2.0
vmwgfx-y := vmwgfx_execbuf.o vmwgfx_gmr.o vmwgfx_hashtab.o vmwgfx_kms.o vmwgfx_drv.o \
	    vmwgfx_ioctl.o vmwgfx_resource.o vmwgfx_ttm_buffer.o \
	    vmwgfx_cmd.o vmwgfx_irq.o vmwgfx_ldu.o vmwgfx_ttm_glue.o \
	    vmwgfx_overlay.o vmwgfx_gmrid_manager.o vmwgfx_fence.o \
	    vmwgfx_bo.o vmwgfx_scrn.o vmwgfx_context.o \
	    vmwgfx_surface.o vmwgfx_prime.o vmwgfx_mob.o vmwgfx_shader.o \
	    vmwgfx_cmdbuf_res.o vmwgfx_cmdbuf.o vmwgfx_stdu.o \
	    vmwgfx_cotable.o vmwgfx_so.o vmwgfx_binding.o vmwgfx_msg.o \
	    vmwgfx_simple_resource.o vmwgfx_va.o vmwgfx_blit.o \
	    vmwgfx_validation.o vmwgfx_page_dirty.o vmwgfx_streamoutput.o \
	    vmwgfx_devcaps.o ttm_object.o vmwgfx_system_manager.o \
	    vmwgfx_gem.o

vmwgfx-$(CONFIG_DRM_FBDEV_EMULATION) += vmwgfx_fb.o

obj-$(CONFIG_DRM_VMWGFX) := vmwgfx.o
