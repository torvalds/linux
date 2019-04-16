#
# Makefile for the IPU3 ImgU drivers
#

ipu3-imgu-objs += \
		ipu3-mmu.o ipu3-dmamap.o \
		ipu3-tables.o ipu3-css-pool.o \
		ipu3-css-fw.o ipu3-css-params.o \
		ipu3-css.o ipu3-v4l2.o ipu3.o

obj-$(CONFIG_VIDEO_IPU3_IMGU) += ipu3-imgu.o

# HACK! While this driver is in bad shape, don't enable several warnings
#       that would be otherwise enabled with W=1
ccflags-y += $(call cc-disable-warning, packed-not-aligned)
ccflags-y += $(call cc-disable-warning, type-limits)
ccflags-y += $(call cc-disable-warning, unused-const-variable)
