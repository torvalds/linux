#
# Makefile for the amlogic video device interface device drivers.
#
EXTRA_AFLAGS=-mfloat-abi=softfp -mfpu=neon
amlvideodri-objs := amlvideo.o
amlcm-objs := common/vfutil.o
amlvideo2dri-objs := amlvideo2.o

obj-$(CONFIG_V4L_AMLOGIC_VIDEO) +=amlcm.o
obj-$(CONFIG_V4L_AMLOGIC_VIDEO) +=amlvideodri.o 
obj-$(CONFIG_V4L_AMLOGIC_VIDEO2) +=amlvideo2dri.o 
