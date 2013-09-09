ifneq ($(CONFIG_RK_FPGA),y)
obj-y += ../plat-rk/clock.o
obj-y += clock_data.o
obj-y += ../mach-rk2928/ddr.o
obj-y += reset.o
obj-$(CONFIG_PM) += ../mach-rk2928/pm.o
endif
obj-y += common.o
CFLAGS_common.o += -DTEXT_OFFSET=$(TEXT_OFFSET)
obj-y += ../mach-rk2928/devices.o
obj-y += ../mach-rk2928/io.o
obj-y += rk_timer.o
obj-$(CONFIG_SMP) += ../mach-rk30/platsmp.o ../mach-rk30/headsmp.o
obj-$(CONFIG_HOTPLUG_CPU) += ../mach-rk30/hotplug.o
obj-$(CONFIG_CPU_IDLE) += ../mach-rk30/cpuidle.o
obj-$(CONFIG_CPU_FREQ) += ../mach-rk3188/cpufreq.o
obj-$(CONFIG_DVFS) += dvfs.o
obj-$(CONFIG_RK30_I2C_INSRAM) += ../mach-rk2928/i2c_sram.o

obj-y += board.o

board-$(CONFIG_MACH_RK3026_FPGA) += board-rk3026-fpga.o
board-$(CONFIG_MACH_RK3026_TB) += board-rk3026-tb.o
board-$(CONFIG_MACH_RK3026_86V) += board-rk3026-86v.o
board-$(CONFIG_MACH_RK3028A_TB) += board-rk3028a-tb.o
board-$(CONFIG_MACH_RK3028A_86V) += board-rk3028a-86v.o
board-$(CONFIG_MACH_RK3028A_FAC) += board-rk3028a-86v-fac.o
board-$(CONFIG_MACH_RK3026_86V_FAC) += board-rk3026-86v-fac.o
