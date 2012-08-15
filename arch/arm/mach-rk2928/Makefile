obj-y += common.o
obj-y += io.o
obj-y += reset.o
obj-y += timer.o
obj-y += devices.o
obj-y += iomux.o
obj-y += clock.o
obj-y += clock_data.o
obj-$(CONFIG_CPU_FREQ) += cpufreq.o
obj-$(CONFIG_DVFS) += dvfs.o
obj-$(CONFIG_PM) += pm.o
CFLAGS_pm.o += -Os -mthumb

obj-$(CONFIG_MACH_RK2928_FPGA) += board-rk2928-fpga.o
obj-$(CONFIG_MACH_RK2928_SDK) += board-rk2928-sdk.o
