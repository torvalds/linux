obj-y                                   += board-pinmux.o
obj-y                                   += common.o
obj-y                                   += devices.o
obj-y                                   += io.o
obj-y                                   += irq.o
obj-y                                   += clock.o
obj-y                                   += timer.o
obj-y					+= fuse.o
obj-y					+= pmc.o
obj-y					+= flowctrl.o
obj-y					+= powergate.o
obj-y					+= apbio.o
obj-$(CONFIG_CPU_IDLE)			+= cpuidle.o
obj-$(CONFIG_CPU_IDLE)			+= sleep.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += tegra2_clocks.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= tegra2_emc.o
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)		+= tegra30_clocks.o
obj-$(CONFIG_SMP)			+= platsmp.o headsmp.o
obj-$(CONFIG_SMP)                       += reset.o
obj-$(CONFIG_HOTPLUG_CPU)               += hotplug.o
obj-$(CONFIG_TEGRA_SYSTEM_DMA)		+= dma.o
obj-$(CONFIG_CPU_FREQ)                  += cpu-tegra.o
obj-$(CONFIG_TEGRA_PCI)			+= pcie.o
obj-$(CONFIG_USB_SUPPORT)		+= usb_phy.o

obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= board-dt-tegra20.o
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)		+= board-dt-tegra30.o

obj-$(CONFIG_MACH_HARMONY)              += board-harmony.o
obj-$(CONFIG_MACH_HARMONY)              += board-harmony-pinmux.o
obj-$(CONFIG_MACH_HARMONY)              += board-harmony-pcie.o
obj-$(CONFIG_MACH_HARMONY)              += board-harmony-power.o

obj-$(CONFIG_MACH_PAZ00)		+= board-paz00.o
obj-$(CONFIG_MACH_PAZ00)		+= board-paz00-pinmux.o
