#
# Makefile for the davinci video device drivers.
#

# VPIF
obj-$(CONFIG_VIDEO_DAVINCI_VPIF) += vpif.o

#DM646x EVM Display driver
obj-$(CONFIG_DISPLAY_DAVINCI_DM646X_EVM) += vpif_display.o
#DM646x EVM Capture driver
obj-$(CONFIG_CAPTURE_DAVINCI_DM646X_EVM) += vpif_capture.o

# Capture: DM6446 and DM355
obj-$(CONFIG_VIDEO_VPSS_SYSTEM) += vpss.o
obj-$(CONFIG_VIDEO_VPFE_CAPTURE) += vpfe_capture.o
obj-$(CONFIG_VIDEO_DM6446_CCDC) += dm644x_ccdc.o
obj-$(CONFIG_VIDEO_DM355_CCDC) += dm355_ccdc.o
obj-$(CONFIG_VIDEO_ISIF) += isif.o
