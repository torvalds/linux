EXTRA_CFLAGS += -Os
ifneq ($(CONFIG_RK_FPGA),y)
obj-y += ../plat-rk/clock.o
obj-$(CONFIG_ARCH_RK30XX) += clock_data.o
obj-$(CONFIG_ARCH_RK3066B) += clock_data-rk3066b.o
endif
obj-y += common.o
CFLAGS_common.o += -DTEXT_OFFSET=$(TEXT_OFFSET)
obj-y += ddr.o
CFLAGS_ddr.o += -mthumb
obj-y += devices.o
obj-y += io.o
obj-y += iomux.o
obj-y += pmu.o
obj-y += reset.o
obj-y += timer.o
obj-y += tsadc.o
obj-$(CONFIG_SMP) += platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU) += hotplug.o
obj-$(CONFIG_LOCAL_TIMERS) += localtimer.o
obj-$(CONFIG_PM) += pm.o
CFLAGS_pm.o += -mthumb
obj-$(CONFIG_CPU_IDLE) += cpuidle.o
obj-$(CONFIG_CPU_FREQ) += cpufreq.o
obj-$(CONFIG_DVFS) += dvfs.o
obj-$(CONFIG_RK30_I2C_INSRAM) += i2c_sram.o
CFLAGS_i2c_sram.o += -mthumb

obj-$(CONFIG_MACH_RK3066_SDK) += board-rk30-sdk.o
obj-$(CONFIG_MACH_RK30_SDK) += board-rk30-sdk.o
obj-$(CONFIG_MACH_RK30_DS975) += board-rk30-ds975.o
obj-$(CONFIG_MACH_RK30_PHONE) += board-rk30-phone.o board-rk30-phone-key.o
obj-$(CONFIG_MACH_RK30_PHONE_PAD) += board-rk30-phonepad.o board-rk30-phonepad-key.o
obj-$(CONFIG_MACH_RK30_PHONE_LOQUAT) += board-rk30-phone-loquat.o board-rk30-phone-loquat-key.o
obj-$(CONFIG_MACH_RK30_DS1001B) += board-rk30-ds1001b.o board-rk30-ds1001b-key.o board-rk30-ds1001b-rfkill.o
obj-$(CONFIG_MACH_RK30_PHONE_A22) += board-rk30-phone-a22.o board-rk30-phone-a22-key.o 

obj-$(CONFIG_MACH_RK3066B_FPGA) += board-rk3066b-fpga.o
obj-$(CONFIG_MACH_RK3066B_SDK) += board-rk3066b-sdk.o
obj-$(CONFIG_MACH_RK3066B_M701) += board-rk3066b-m701.o
obj-$(CONFIG_MACH_RK3108_TB) += board-rk3108-tb.o
obj-$(CONFIG_MACH_RK3168_TB) += board-rk3168-tb.o
obj-$(CONFIG_MACH_RK3168_LR097) += board-rk3168-LR097.o
obj-$(CONFIG_MACH_RK3168_DS1006H) += board-rk3168-ds1006h.o
