CFLAGS_platsmp.o := -march=armv7-a

obj-$(CONFIG_ARCH_ROCKCHIP) += rockchip.o
obj-$(CONFIG_PM_SLEEP) += pm.o rk3288_ddr_suspend.o
obj-$(CONFIG_SMP) += headsmp.o platsmp.o

obj-$(CONFIG_PM_SLEEP) += embedded/
