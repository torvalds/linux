ifeq ($(CONFIG_ARCH_EXYNOS5),y)
fimc-lite-objs := fimc-lite-core.o fimc-lite-reg.o fimc-lite-vb2.o
else
fimc-lite-objs := fimc-lite-core.o fimc-lite-reg.o
endif
obj-$(CONFIG_VIDEO_EXYNOS_FIMC_LITE)	+= fimc-lite.o
