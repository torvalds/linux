obj-$(CONFIG_FB_SUNXI) += disp.o

disp-objs :=  dev_disp.o dev_fb.o
disp-objs += OSAL/OSAL_Cache.o OSAL/OSAL_Clock.o OSAL/OSAL_Dma.o OSAL/OSAL_Int.o OSAL/OSAL_IrqLock.o\
	OSAL/OSAL_Lib_C.o OSAL/OSAL_Pin.o OSAL/OSAL_Semi.o OSAL/OSAL_Thread.o OSAL/OSAL_Time.o OSAL/OSAL_Parser.o

ifeq ($(CONFIG_ARCH_SUN4I),y)
disp-objs += de_bsp_sun4i/de/ebios/de_be.o de_bsp_sun4i/de/ebios/de_fe.o de_bsp_sun4i/de/ebios/de_hwc.o\
	de_bsp_sun4i/de/ebios/de_layer.o de_bsp_sun4i/de/ebios/de_lcdc.o de_bsp_sun4i/de/ebios/de_tvec.o\
	de_bsp_sun4i/de/disp_clk.o de_bsp_sun4i/de/disp_combined.o de_bsp_sun4i/de/disp_de.o de_bsp_sun4i/de/disp_display.o\
	de_bsp_sun4i/de/disp_event.o de_bsp_sun4i/de/disp_hdmi.o de_bsp_sun4i/de/disp_hwc.o de_bsp_sun4i/de/disp_layer.o\
	de_bsp_sun4i/de/disp_lcd.o de_bsp_sun4i/de/disp_scaler.o de_bsp_sun4i/de/disp_sprite.o de_bsp_sun4i/de/disp_tv.o\
	de_bsp_sun4i/de/disp_vga.o de_bsp_sun4i/de/disp_video.o
else
disp-objs += de_bsp_sun5i/de/ebios/de_be.o de_bsp_sun5i/de/ebios/de_fe.o de_bsp_sun5i/de/ebios/de_hwc.o\
	de_bsp_sun5i/de/ebios/de_layer.o de_bsp_sun5i/de/ebios/de_lcdc.o de_bsp_sun5i/de/ebios/de_tvec.o\
	de_bsp_sun5i/de/disp_clk.o de_bsp_sun5i/de/disp_combined.o de_bsp_sun5i/de/disp_de.o de_bsp_sun5i/de/disp_display.o\
	de_bsp_sun5i/de/disp_event.o de_bsp_sun5i/de/disp_hdmi.o de_bsp_sun5i/de/disp_hwc.o de_bsp_sun5i/de/disp_layer.o\
	de_bsp_sun5i/de/disp_lcd.o de_bsp_sun5i/de/disp_scaler.o de_bsp_sun5i/de/disp_sprite.o de_bsp_sun5i/de/disp_tv.o\
	de_bsp_sun5i/de/disp_vga.o de_bsp_sun5i/de/disp_video.o \
	de_bsp_sun5i/iep/de_iep.o de_bsp_sun5i/iep/disp_iep.o
endif

ifeq ($(CONFIG_FB_SUNXI_UMP),y)
obj-m += disp_ump.o
EXTRA_CFLAGS += -Idrivers/gpu/mali/include
endif
