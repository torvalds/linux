EXTRA_CFLAGS += -Os
ifneq ($(CONFIG_RK_FPGA),y)
obj-y += ../plat-rk/clock.o
obj-y += clock_data.o
obj-y += ../mach-rk30/ddr.o
obj-y += ../mach-rk30/pmu.o
obj-y += ../mach-rk30/reset.o
obj-$(CONFIG_PM) += ../mach-rk30/pm.o
obj-y += ../mach-rk30/common.o
CFLAGS_common.o += -DTEXT_OFFSET=$(TEXT_OFFSET)
endif
obj-y += ../mach-rk30/devices.o
obj-y += io.o
obj-y += rk_timer.o
obj-$(CONFIG_SMP) += ../mach-rk30/platsmp.o ../mach-rk30/headsmp.o
obj-$(CONFIG_HOTPLUG_CPU) += ../mach-rk30/hotplug.o
obj-$(CONFIG_CPU_IDLE) += ../mach-rk30/cpuidle.o
obj-$(CONFIG_CPU_FREQ) += cpufreq.o
obj-$(CONFIG_DVFS) += dvfs.o
obj-y += board.o

board-$(CONFIG_MACH_RK3190_FPGA) += board-rk3190-fpga.o
board-$(CONFIG_MACH_RK3190_TB) += board-rk3190-tb.o
