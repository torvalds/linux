# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_FB_NOTIFY)           += fb_notify.o
obj-$(CONFIG_FB)                  += fb.o
fb-y                              := fbmem.o fbmon.o fbcmap.o fbsysfs.o \
                                     modedb.o fbcvt.o fb_cmdline.o
fb-$(CONFIG_FB_DEFERRED_IO)       += fb_defio.o

ifeq ($(CONFIG_FRAMEBUFFER_CONSOLE),y)
fb-y				  += fbcon.o bitblit.o softcursor.o
ifeq ($(CONFIG_FB_TILEBLITTING),y)
fb-y				  += tileblit.o
endif
ifeq ($(CONFIG_FRAMEBUFFER_CONSOLE_ROTATION),y)
fb-y				  += fbcon_rotate.o fbcon_cw.o fbcon_ud.o \
				     fbcon_ccw.o
endif
endif

obj-$(CONFIG_FB_CFB_FILLRECT)  += cfbfillrect.o
obj-$(CONFIG_FB_CFB_COPYAREA)  += cfbcopyarea.o
obj-$(CONFIG_FB_CFB_IMAGEBLIT) += cfbimgblt.o
obj-$(CONFIG_FB_SYS_FILLRECT)  += sysfillrect.o
obj-$(CONFIG_FB_SYS_COPYAREA)  += syscopyarea.o
obj-$(CONFIG_FB_SYS_IMAGEBLIT) += sysimgblt.o
obj-$(CONFIG_FB_SYS_FOPS)      += fb_sys_fops.o
obj-$(CONFIG_FB_SVGALIB)       += svgalib.o
obj-$(CONFIG_FB_DDC)           += fb_ddc.o
