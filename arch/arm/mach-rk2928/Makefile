obj-y += common.o
CFLAGS_common.o += -DTEXT_OFFSET=$(TEXT_OFFSET)
obj-y += io.o
obj-y += reset.o
obj-y += timer.o
obj-y += devices.o
obj-y += iomux.o
obj-y += ../plat-rk/clock.o
obj-y += clock_data.o
obj-y += ddr.o
obj-$(CONFIG_CPU_FREQ) += cpufreq.o
obj-$(CONFIG_DVFS) += dvfs.o
obj-$(CONFIG_PM) += pm.o
obj-$(CONFIG_RK30_I2C_INSRAM) += i2c_sram.o

board-y := board-rk2928.o
obj-$(CONFIG_MACH_RK2928) += board.o

obj-$(CONFIG_MACH_RK2928_SDK) += board-rk2928-sdk.o
obj-$(CONFIG_MACH_RK2926_SDK) += board-rk2926-sdk.o
obj-$(CONFIG_MACH_RK2928_PHONEPAD) += board-rk2928-phonepad.o

obj-$(CONFIG_MACH_RK2928_A720) += board-rk2928-a720.o
obj-$(CONFIG_MACH_RK2928_TB) += board-rk2928-tb.o
obj-$(CONFIG_MACH_RK2926_TB) += board-rk2928-tb.o
obj-$(CONFIG_MACH_RK2928_FPGA) += board-rk2928-fpga.o
