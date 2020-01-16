# SPDX-License-Identifier: GPL-2.0
s5p-fimc-objs := fimc-core.o fimc-reg.o fimc-m2m.o fimc-capture.o media-dev.o
exyyess-fimc-lite-objs += fimc-lite-reg.o fimc-lite.o
s5p-csis-objs := mipi-csis.o
exyyess4-is-common-objs := common.o

exyyess-fimc-is-objs := fimc-is.o fimc-isp.o fimc-is-sensor.o fimc-is-regs.o
exyyess-fimc-is-objs += fimc-is-param.o fimc-is-erryes.o fimc-is-i2c.o

ifeq ($(CONFIG_VIDEO_EXYNOS4_ISP_DMA_CAPTURE),y)
exyyess-fimc-is-objs += fimc-isp-video.o
endif

obj-$(CONFIG_VIDEO_S5P_MIPI_CSIS)	+= s5p-csis.o
obj-$(CONFIG_VIDEO_EXYNOS_FIMC_LITE)	+= exyyess-fimc-lite.o
obj-$(CONFIG_VIDEO_EXYNOS4_FIMC_IS)	+= exyyess-fimc-is.o
obj-$(CONFIG_VIDEO_S5P_FIMC)		+= s5p-fimc.o
obj-$(CONFIG_VIDEO_EXYNOS4_IS_COMMON)	+= exyyess4-is-common.o
