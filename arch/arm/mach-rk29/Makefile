obj-y += timer.o io.o devices.o iomux.o clock.o rk29-pl330.o dma.o ddr.o memcpy_dma.o reset.o
obj-y += tests.o memtester.o
obj-y += early_printk.o
ifndef CONFIG_DEBUG_LL
obj-y += ../kernel/debug.o
endif
obj-$(CONFIG_PM) += pm.o
obj-$(CONFIG_CPU_FREQ) += cpufreq.o
obj-$(CONFIG_DDR_FREQ) += ddrfreq.o
obj-$(CONFIG_RK29_VPU) += vpu_mem.o
obj-$(CONFIG_RK29_I2C_INSRAM) += i2c_sram.o
obj-y += spi_sram.o
obj-$(CONFIG_RK29_PWM_INSRAM) += pwm_sram.o
obj-$(CONFIG_MACH_RK29SDK) += board-rk29sdk.o board-rk29sdk-key.o board-rk29sdk-rfkill.o board-rk29sdk-power.o
obj-$(CONFIG_MACH_RK29SDK_DDR3) += board-rk29-ddr3sdk.o board-rk29sdk-key.o board-rk29sdk-rfkill.o board-rk29sdk-power.o
obj-$(CONFIG_MACH_RK29_PHONESDK) += board-rk29-phonesdk.o board-rk29-phonesdk-key.o board-rk29-phonesdk-rfkill.o
obj-$(CONFIG_MACH_RK29FIH) += board-rk29-fih.o board-rk29-fih-key.o board-rk29sdk-rfkill.o board-rk29sdk-power.o
obj-$(CONFIG_MACH_RK29_A22) += board-rk29-a22.o board-rk29-a22-key.o board-rk29-a22-rfkill.o
obj-$(CONFIG_MACH_RK29_TD8801_V2) += board-rk29-td8801_v2.o board-rk29-td8801_v2-key.o board-rk29-td8801_v2-rfkill.o
obj-$(CONFIG_MACH_RK29_PHONEPADSDK) += board-rk29phonepadsdk.o board-rk29phonepadsdk-key.o board-rk29phonepadsdk-rfkill.o board-rk29phonepadsdk-power.o
obj-$(CONFIG_MACH_RK29_newton) += board-rk29-newton.o board-rk29-newton-key.o board-newton-rfkill.o board-rk29sdk-power.o
obj-$(CONFIG_MACH_RK29_K97) += board-rk29-k97.o board-rk29k97-key.o board-rk29sdk-rfkill.o board-rk29sdk-power.o
obj-$(CONFIG_MACH_RK29_PHONE_Z5) += board-rk29-z5.o board-rk29-z5-key.o board-rk29-z5-rfkill.o
