obj-y += proc_comm.o
obj-y += io.o idle.o timer.o dma.o
obj-y += vreg.o
obj-y += acpuclock-arm11.o
obj-y += clock.o clock-pcom.o
obj-y += gpio.o

ifdef CONFIG_MSM_VIC
obj-y += irq-vic.o
else
obj-y += irq.o
endif

obj-$(CONFIG_ARCH_QSD8X50) += sirc.o
obj-$(CONFIG_MSM_SMD) += smd.o smd_debug.o
obj-$(CONFIG_MSM_SMD) += last_radio_log.o

obj-$(CONFIG_MACH_TROUT) += board-trout.o board-trout-gpio.o board-trout-mmc.o devices-msm7x00.o
obj-$(CONFIG_MACH_HALIBUT) += board-halibut.o devices-msm7x00.o
obj-$(CONFIG_ARCH_MSM7X30) += board-msm7x30.o devices-msm7x30.o
obj-$(CONFIG_ARCH_QSD8X50) += board-qsd8x50.o devices-qsd8x50.o

