obj-y += clock.o
obj-y += common.o
obj-y += devices.o
obj-y += io.o
obj-y += iomux.o
obj-y += reset.o
obj-y += timer.o
obj-y += ../mach-rk29/early_printk.o
ifndef CONFIG_DEBUG_LL
obj-y += ../kernel/debug.o
endif
obj-$(CONFIG_LOCAL_TIMERS) += localtimer.o
obj-$(CONFIG_MACH_RK30_SDK) += board-rk30-sdk.o board-rk30sdk-key.o
