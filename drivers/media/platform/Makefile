#
# Makefile for the video capture/playback device drivers.
#

omap2cam-objs	:=	omap24xxcam.o omap24xxcam-dma.o

obj-$(CONFIG_VIDEO_VINO) += indycam.o

obj-$(CONFIG_VIDEO_VINO) += vino.o
obj-$(CONFIG_VIDEO_TIMBERDALE)	+= timblogiw.o


obj-$(CONFIG_VIDEO_M32R_AR_M64278) += arv.o


obj-$(CONFIG_VIDEO_CAFE_CCIC) += marvell-ccic/
obj-$(CONFIG_VIDEO_MMP_CAMERA) += marvell-ccic/

obj-$(CONFIG_VIDEO_VIA_CAMERA) += via-camera.o

obj-$(CONFIG_VIDEO_OMAP3)	+= omap3isp/

obj-$(CONFIG_VIDEO_VIU) += fsl-viu.o
obj-$(CONFIG_VIDEO_VIVI) += vivi.o
obj-$(CONFIG_VIDEO_MEM2MEM_TESTDEV) += mem2mem_testdev.o


obj-$(CONFIG_VIDEO_OMAP2)		+= omap2cam.o
obj-$(CONFIG_SOC_CAMERA)		+= soc_camera.o soc_mediabus.o
obj-$(CONFIG_SOC_CAMERA_PLATFORM)	+= soc_camera_platform.o
# soc-camera host drivers have to be linked after camera drivers
obj-$(CONFIG_VIDEO_MX1)			+= mx1_camera.o
obj-$(CONFIG_VIDEO_MX2)			+= mx2_camera.o
obj-$(CONFIG_VIDEO_MX3)			+= mx3_camera.o
obj-$(CONFIG_VIDEO_PXA27x)		+= pxa_camera.o
obj-$(CONFIG_VIDEO_SH_MOBILE_CSI2)	+= sh_mobile_csi2.o
obj-$(CONFIG_VIDEO_SH_MOBILE_CEU)	+= sh_mobile_ceu_camera.o
obj-$(CONFIG_VIDEO_OMAP1)		+= omap1_camera.o
obj-$(CONFIG_VIDEO_ATMEL_ISI)		+= atmel-isi.o

obj-$(CONFIG_VIDEO_MX2_EMMAPRP)		+= mx2_emmaprp.o
obj-$(CONFIG_VIDEO_CODA) 			+= coda.o

obj-$(CONFIG_VIDEO_MEM2MEM_DEINTERLACE)	+= m2m-deinterlace.o

obj-$(CONFIG_VIDEO_SAMSUNG_S5P_FIMC) 	+= s5p-fimc/
obj-$(CONFIG_VIDEO_SAMSUNG_S5P_JPEG)	+= s5p-jpeg/
obj-$(CONFIG_VIDEO_SAMSUNG_S5P_MFC)	+= s5p-mfc/
obj-$(CONFIG_VIDEO_SAMSUNG_S5P_TV)	+= s5p-tv/

obj-$(CONFIG_VIDEO_SAMSUNG_S5P_G2D)	+= s5p-g2d/

obj-$(CONFIG_BLACKFIN)                  += blackfin/

obj-$(CONFIG_ARCH_DAVINCI)		+= davinci/

obj-$(CONFIG_VIDEO_SH_VOU)		+= sh_vou.o

obj-y	+= davinci/

obj-$(CONFIG_ARCH_OMAP)	+= omap/

ccflags-y += -I$(srctree)/drivers/media/dvb-core
ccflags-y += -I$(srctree)/drivers/media/dvb-frontends
ccflags-y += -I$(srctree)/drivers/media/tuners
ccflags-y += -I$(srctree)/drivers/media/i2c/soc_camera
