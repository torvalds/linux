#
# Makefile for the s3c framebuffer driver
#
ifeq ($(CONFIG_FB_S5P),y)
obj-$(CONFIG_FB_S5P_DUMMY_MIPI_LCD)	+= s3cfb_dummymipilcd.o
obj-$(CONFIG_FB_S5P_LG4591)	    += s3cfb_lg4591.o
obj-$(CONFIG_FB_S5P_S6E8AA1)	+= s3cfb_s6e8aa1.o
obj-$(CONFIG_FB_S5P_MIPI_DSIM)	+= s5p_dsim_lowlevel.o
obj-$(CONFIG_FB_S5P_MIPI_DSIM)	+= s5p-dsim.o

obj-y				+= s3cfb.o
s3cfb-y				:= s3cfb_main.o s3cfb_ops.o
obj-$(CONFIG_ARCH_S5PV210)	+= s3cfb_fimd6x.o
obj-$(CONFIG_ARCH_EXYNOS4)	+= s3cfb_fimd6x.o

obj-$(CONFIG_FB_S5P_LTE480WV)	+= s3cfb_lte480wv.o
obj-$(CONFIG_FB_S5P_HT101HD1)	+= s3cfb_ht101hd1.o
obj-$(CONFIG_FB_S5P_WA101S)	+= s3cfb_wa101s.o
obj-$(CONFIG_FB_S5P_AMS369FG06)	+= s3cfb_ams369fg06.o
obj-$(CONFIG_FB_S5P_LMS501KF03)	+= s3cfb_lms501kf03.o
obj-$(CONFIG_FB_S5P_LTN101AL03) += ltn101al03.o
obj-$(CONFIG_FB_S5P_LP101WH1) 	+= lp101wh1.o
obj-$(CONFIG_FB_S5P_U133WA01) 	+= u133wa01.o
obj-$(CONFIG_FB_S5P_FAKE_FB) 	+= fake_fb.o
endif

ifeq ($(CONFIG_FB_S5P_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif

# version 2 rules
ifeq ($(CONFIG_FB_S5P_V2),y)
obj-y					+= s3cfb2.o
obj-$(CONFIG_PLAT_S3C64XX)		+= s3cfb2_fimd4x.o
obj-$(CONFIG_PLAT_S5P64XX)		+= s3cfb2_fimd5x.o
obj-$(CONFIG_CPU_S5P6450)              += s3cfb2_fimd5x.o
obj-$(CONFIG_CPU_S5PC100)		+= s3cfb2_fimd5x.o
obj-$(CONFIG_CPU_S5PC110)		+= s3cfb2_fimd6x.o
obj-$(CONFIG_FB_S5P_V2_LTE480WV)	+= s3cfb2_lte480wv.o
obj-$(CONFIG_FB_S5P_V2_TL2796)		+= s3cfb2_tl2796.o
endif

ifeq ($(CONFIG_FB_S5P_V2_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
