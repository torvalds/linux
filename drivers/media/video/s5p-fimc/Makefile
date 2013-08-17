s5p-fimc-objs := fimc-core.o fimc-reg.o fimc-m2m.o fimc-capture.o fimc-mdevice.o fimc-vb.o
s5p-fimc-lite-objs += fimc-lite-reg.o fimc-lite.o
s5p-fimc-is-objs += fimc-is.o fimc-isp.o fimc-is-sensor.o fimc-is-regs.o
s5p-fimc-is-objs += fimc-is-config.o fimc-is-errno.o
s5p-csis-objs := mipi-csis.o

obj-$(CONFIG_VIDEO_S5P_MIPI_CSIS)	+= s5p-csis.o
obj-$(CONFIG_VIDEO_S5P_FIMC_LITE)	+= s5p-fimc-lite.o
obj-$(CONFIG_VIDEO_EXYNOS4_FIMC_IS)	+= s5p-fimc-is.o
obj-$(CONFIG_VIDEO_S5P_FIMC)		+= s5p-fimc.o
