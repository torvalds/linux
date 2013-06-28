obj-$(CONFIG_FB_SUNXI) += disp.o

disp-objs :=  dev_disp.o dev_fb.o

disp-objs += OSAL_Clock.o OSAL_Pin.o

disp-objs += de_be.o de_fe.o de_hwc.o\
	de_layer.o de_lcdc.o de_tvec.o\
	disp_clk.o disp_combined.o disp_de.o disp_display.o\
	disp_event.o disp_hdmi.o disp_hwc.o disp_layer.o\
	disp_lcd.o disp_scaler.o disp_sprite.o disp_tv.o\
	disp_vga.o disp_video.o de_iep.o disp_iep.o

ifeq ($(CONFIG_FB_SUNXI_UMP),y)
obj-m += disp_ump.o
ccflags-y += -Idrivers/gpu/mali/ump/include
endif
