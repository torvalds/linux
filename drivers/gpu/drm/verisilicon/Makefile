# SPDX-License-Identifier: GPL-2.0

vs_drm-objs := vs_dc_hw.o \
		vs_dc.o \
		vs_crtc.o \
		vs_drv.o \
		vs_fb.o \
		vs_gem.o \
		vs_plane.o \
		vs_simple_enc.o

vs_drm-$(CONFIG_VERISILICON_VIRTUAL_DISPLAY) += vs_virtual.o
vs_drm-$(CONFIG_VERISILICON_DW_MIPI_DSI) += dw_mipi_dsi.o
vs_drm-$(CONFIG_VERISILICON_MMU) += vs_dc_mmu.o
vs_drm-$(CONFIG_VERISILICON_DEC) += vs_dc_dec.o
vs_drm-$(CONFIG_STARFIVE_INNO_HDMI) += inno_hdmi.o \
					starfive_hdmi_audio.o
vs_drm-$(CONFIG_STARFIVE_DSI) += starfive_drm_dsi.o starfive_drm_seeedpanel.o
obj-$(CONFIG_DRM_VERISILICON) += vs_drm.o

