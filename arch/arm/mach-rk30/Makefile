EXTRA_CFLAGS += -Os
ifneq ($(CONFIG_RK_FPGA),y)
obj-y += ../plat-rk/clock.o
obj-$(CONFIG_ARCH_RK30XX) += clock_data.o
obj-$(CONFIG_ARCH_RK3066B) += clock_data-rk3066b.o
endif
obj-y += common.o
CFLAGS_common.o += -DTEXT_OFFSET=$(TEXT_OFFSET)
obj-y += ddr.o
obj-y += devices.o
obj-y += io.o
ifeq ($(CONFIG_ARCH_RK30XX),y)
obj-y += iomux.o
obj-$(CONFIG_CPU_FREQ) += cpufreq.o
else
obj-$(CONFIG_CPU_FREQ) += ../mach-rk3188/cpufreq.o
endif
obj-y += pmu.o
obj-y += reset.o
obj-y += timer.o
obj-y += tsadc.o
obj-$(CONFIG_SMP) += platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU) += hotplug.o
obj-$(CONFIG_LOCAL_TIMERS) += localtimer.o
obj-$(CONFIG_PM) += pm.o
obj-$(CONFIG_CPU_IDLE) += cpuidle.o
ifeq ($(CONFIG_DVFS_WITH_UOC),y)
obj-$(CONFIG_DVFS) += dvfs-rk3066b.o
else
obj-$(CONFIG_DVFS) += dvfs.o
endif
obj-$(CONFIG_RK30_I2C_INSRAM) += i2c_sram.o

obj-$(CONFIG_ARCH_RK30XX) += board.o
board-$(CONFIG_MACH_RK3066_SDK) += board-rk30-sdk.o
board-$(CONFIG_MACH_RK30_SDK) += board-rk30-sdk.o
board-$(CONFIG_MACH_RK30_DS975) += board-rk30-ds975.o
board-$(CONFIG_MACH_RK30_Z600T) += board-rk30-phonepad-z600t.o 
board-$(CONFIG_MACH_RK30_PHONE) += board-rk30-phone.o board-rk30-phone-key.o
board-$(CONFIG_MACH_RK30_PHONE_PAD) += board-rk30-phonepad.o board-rk30-phonepad-key.o
board-$(CONFIG_MACH_RK30_PHONE_LOQUAT) += board-rk30-phone-loquat.o board-rk30-phone-loquat-key.o
board-$(CONFIG_MACH_RK30_DS1001B) += board-rk30-ds1001b.o board-rk30-ds1001b-key.o board-rk30-ds1001b-rfkill.o
board-$(CONFIG_MACH_RK30_PHONE_A22) += board-rk30-phone-a22.o board-rk30-phone-a22-key.o

obj-$(CONFIG_SOC_RK3028) += board.o
board-$(CONFIG_MACH_RK3028_TB) += board-rk3028-tb.o

obj-$(CONFIG_SOC_RK3066B) += board.o
board-$(CONFIG_MACH_RK3066B_FPGA) += board-rk3066b-fpga.o
board-$(CONFIG_MACH_RK3066B_SDK) += board-rk3066b-sdk.o
board-$(CONFIG_MACH_RK3066B_M701) += board-rk3066b-m701.o

obj-$(CONFIG_SOC_RK3108) += board.o
board-$(CONFIG_MACH_RK3108_TB) += board-rk3108-tb.o

obj-$(CONFIG_SOC_RK3168) += board.o
board-$(CONFIG_MACH_RK3168_TB) += board-rk3168-tb.o
board-$(CONFIG_MACH_RK3168_FT) += ../mach-rk3188/board-rk3188-ft.o
board-$(CONFIG_MACH_RK3168_LR097) += board-rk3168-LR097.o
board-$(CONFIG_MACH_RK3168_DS1006H) += board-rk3168-ds1006h.o
board-$(CONFIG_MACH_RK3168_86V) += board-rk3168-86v.o
board-$(CONFIG_MACH_RK3168_86V_OLD) += board-rk3168-86v-old.o

obj-$(CONFIG_SOC_RK3168M) += board.o
board-$(CONFIG_MACH_RK3168M_TB) += board-rk3168m-tb.o
