obj-$(CONFIG_POWERVR_ADF_FBDEV) += powervr_adf_fbdev.o
powervr_adf_fbdev-y += adf_common.o adf_fbdev.o
ccflags-y += \
	-include $(srctree)/drivers/staging/imgtec/config_kernel.h \
	-I$(srctree)/$(src) \
	-I$(srctree)/drivers/staging/android

obj-y += rogue/
obj-y += apollo/
