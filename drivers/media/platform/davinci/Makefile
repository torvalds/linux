#
# Makefile for the davinci video device drivers.
#

#VPIF Display driver
obj-$(CONFIG_VIDEO_DAVINCI_VPIF_DISPLAY) += vpif.o vpif_display.o
#VPIF Capture driver
obj-$(CONFIG_VIDEO_DAVINCI_VPIF_CAPTURE) += vpif.o vpif_capture.o

# Capture: DM6446 and DM355
obj-$(CONFIG_VIDEO_DM6446_CCDC) += vpfe_capture.o vpss.o dm644x_ccdc.o
obj-$(CONFIG_VIDEO_DM355_CCDC) += vpfe_capture.o vpss.o dm355_ccdc.o
obj-$(CONFIG_VIDEO_DM365_ISIF) += vpfe_capture.o vpss.o isif.o
obj-$(CONFIG_VIDEO_DAVINCI_VPBE_DISPLAY) += vpss.o vpbe.o vpbe_osd.o \
	vpbe_venc.o vpbe_display.o
