#
# Makefile for the davinci video device drivers.
#

# VPIF
obj-$(CONFIG_VIDEO_DAVINCI_VPIF) += vpif.o

#VPIF Display driver
obj-$(CONFIG_VIDEO_DAVINCI_VPIF_DISPLAY) += vpif_display.o
#VPIF Capture driver
obj-$(CONFIG_VIDEO_DAVINCI_VPIF_CAPTURE) += vpif_capture.o

# Capture: DM6446 and DM355
obj-$(CONFIG_VIDEO_VPSS_SYSTEM) += vpss.o
obj-$(CONFIG_VIDEO_VPFE_CAPTURE) += vpfe_capture.o
obj-$(CONFIG_VIDEO_DM6446_CCDC) += dm644x_ccdc.o
obj-$(CONFIG_VIDEO_DM355_CCDC) += dm355_ccdc.o
obj-$(CONFIG_VIDEO_ISIF) += isif.o
obj-$(CONFIG_VIDEO_DM644X_VPBE) += vpbe.o vpbe_osd.o vpbe_venc.o
obj-$(CONFIG_VIDEO_VPBE_DISPLAY) += vpbe_display.o
