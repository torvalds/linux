obj-y += clock.o
obj-y += clock_data.o
obj-y += common.o
CFLAGS_common.o += -DTEXT_OFFSET=$(TEXT_OFFSET)
obj-y += ddr.o
CFLAGS_ddr.o += -Os
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
obj-$(CONFIG_CPU_IDLE) += cpuidle.o
obj-$(CONFIG_CPU_FREQ) += cpufreq.o
obj-$(CONFIG_DVFS) += dvfs.o
obj-$(CONFIG_DDR_FREQ) += ddr_freq.o

obj-$(CONFIG_MACH_RK3066_SDK) += board-rk30-sdk.o board-rk30-sdk-key.o
obj-$(CONFIG_MACH_RK30_SDK) += board-rk30-sdk.o board-rk30-sdk-key.o
obj-$(CONFIG_MACH_RK30_PHONE) += board-rk30-phone.o board-rk30-phone-key.o
obj-$(CONFIG_MACH_RK30_PHONE_PAD) += board-rk30-phonepad.o board-rk30-phonepad-key.o board-rk30-phonepad-rfkill.o
obj-$(CONFIG_MACH_RK30_PHONE_LOQUAT) += board-rk30-phone-loquat.o board-rk30-phone-loquat-key.o
obj-$(CONFIG_MACH_RK30_DS1001B) += board-rk30-ds1001b.o board-rk30-ds1001b-key.o board-rk30-ds1001b-rfkill.o
obj-$(CONFIG_MACH_RK30_PHONE_A22) += board-rk30-phone-a22.o board-rk30-phone-a22-key.o 

