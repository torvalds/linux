# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_VIDEO_DM365_VPFE) += davinci-vfpe.o

davinci-vfpe-objs := \
	dm365_isif.o dm365_ipipe_hw.o dm365_ipipe.o \
	dm365_resizer.o dm365_ipipeif.o vpfe_mc_capture.o vpfe_video.o

# Allow building it with COMPILE_TEST on other archs
ifndef CONFIG_ARCH_DAVINCI
ccflags-y += -I $(srctree)/arch/arm/mach-davinci/include/
endif
