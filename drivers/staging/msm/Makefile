obj-y := msm_fb.o staging-devices.o memory.o

obj-$(CONFIG_FB_MSM_LOGO) += logo.o
obj-$(CONFIG_FB_BACKLIGHT) += msm_fb_bl.o

# MDP
obj-y += mdp.o

ifeq ($(CONFIG_FB_MSM_MDP40),y)
obj-y += mdp4_util.o
obj-$(CONFIG_DEBUG_FS) += mdp4_debugfs.o
else
obj-y += mdp_hw_init.o
obj-y += mdp_ppp.o
ifeq ($(CONFIG_FB_MSM_MDP31),y)
obj-y += mdp_ppp_v31.o
obj-$(CONFIG_MDP_PPP_ASYNC_OP) += mdp_ppp_dq.o
else
obj-y += mdp_ppp_v20.o
endif
endif

ifeq ($(CONFIG_FB_MSM_OVERLAY),y)
obj-y += mdp4_overlay.o
obj-y += mdp4_overlay_lcdc.o
obj-y += mdp4_overlay_mddi.o
else
obj-y += mdp_dma_lcdc.o
endif

obj-y += mdp_dma.o
obj-y += mdp_dma_s.o
obj-y += mdp_vsync.o
obj-y += mdp_cursor.o
obj-y += mdp_dma_tv.o

# EBI2
obj-$(CONFIG_FB_MSM_EBI2) += ebi2_lcd.o

# LCDC
obj-$(CONFIG_FB_MSM_LCDC) += lcdc.o

# MDDI
msm_mddi-objs := mddi.o mddihost.o mddihosti.o
obj-$(CONFIG_FB_MSM_MDDI) += msm_mddi.o

# External MDDI
msm_mddi_ext-objs := mddihost_e.o mddi_ext.o
obj-$(CONFIG_FB_MSM_EXTMDDI) += msm_mddi_ext.o

# TVEnc
obj-$(CONFIG_FB_MSM_TVOUT) += tvenc.o

# MSM FB Panel
obj-y += msm_fb_panel.o
obj-$(CONFIG_FB_MSM_EBI2_TMD_QVGA_EPSON_QCIF) += ebi2_tmd20.o
obj-$(CONFIG_FB_MSM_EBI2_TMD_QVGA_EPSON_QCIF) += ebi2_l2f.o

ifeq ($(CONFIG_FB_MSM_MDDI_AUTO_DETECT),y)
obj-y += mddi_prism.o
obj-y += mddi_toshiba.o
obj-y += mddi_toshiba_vga.o
obj-y += mddi_toshiba_wvga_pt.o
obj-y += mddi_sharp.o
else
obj-$(CONFIG_FB_MSM_MDDI_PRISM_WVGA) += mddi_prism.o
obj-$(CONFIG_FB_MSM_MDDI_TOSHIBA_COMMON) += mddi_toshiba.o
obj-$(CONFIG_FB_MSM_MDDI_TOSHIBA_COMMON_VGA) += mddi_toshiba_vga.o
obj-$(CONFIG_FB_MSM_MDDI_TOSHIBA_WVGA_PORTRAIT) += mddi_toshiba_wvga_pt.o
obj-$(CONFIG_FB_MSM_MDDI_SHARP_QVGA_128x128) += mddi_sharp.o
endif

obj-$(CONFIG_FB_MSM_LCDC_PANEL) += lcdc_panel.o
obj-$(CONFIG_FB_MSM_LCDC_PRISM_WVGA) += lcdc_prism.o
obj-$(CONFIG_FB_MSM_LCDC_EXTERNAL_WXGA) += lcdc_external.o
obj-$(CONFIG_FB_MSM_LCDC_GORDON_VGA) += lcdc_gordon.o
obj-$(CONFIG_FB_MSM_LCDC_TOSHIBA_WVGA_PT) += lcdc_toshiba_wvga_pt.o
obj-$(CONFIG_FB_MSM_LCDC_SHARP_WVGA_PT) += lcdc_sharp_wvga_pt.o
obj-$(CONFIG_FB_MSM_LCDC_ST15_WXGA) += lcdc_st15.o
obj-$(CONFIG_FB_MSM_HDMI_SII_EXTERNAL_720P) += hdmi_sii9022.o

obj-$(CONFIG_FB_MSM_TVOUT_NTSC) += tv_ntsc.o
obj-$(CONFIG_FB_MSM_TVOUT_PAL) += tv_pal.o

obj-$(CONFIG_FB_MSM_EXTMDDI_SVGA) += mddi_ext_lcd.o

clean:
	rm *.o .*cmd
