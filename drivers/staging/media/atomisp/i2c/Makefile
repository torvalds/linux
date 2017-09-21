#
# Makefile for sensor drivers
#

obj-$(CONFIG_VIDEO_IMX)        += imx/
obj-$(CONFIG_VIDEO_OV5693)     += ov5693/
obj-$(CONFIG_VIDEO_MT9M114)    += mt9m114.o
obj-$(CONFIG_VIDEO_GC2235)     += gc2235.o
obj-$(CONFIG_VIDEO_OV2722)     += ov2722.o
obj-$(CONFIG_VIDEO_OV2680)     += ov2680.o
obj-$(CONFIG_VIDEO_GC0310)     += gc0310.o

obj-$(CONFIG_VIDEO_MSRLIST_HELPER) += libmsrlisthelper.o

obj-$(CONFIG_VIDEO_AP1302)     += ap1302.o

# Makefile for flash drivers
#

obj-$(CONFIG_VIDEO_LM3554) += lm3554.o

# HACK! While this driver is in bad shape, don't enable several warnings
#       that would be otherwise enabled with W=1
ccflags-y += $(call cc-disable-warning, unused-but-set-variable)
ccflags-y += $(call cc-disable-warning, unused-const-variable)
ccflags-y += $(call cc-disable-warning, missing-prototypes)
ccflags-y += $(call cc-disable-warning, missing-declarations)
