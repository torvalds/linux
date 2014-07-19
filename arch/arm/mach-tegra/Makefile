asflags-y				+= -march=armv7-a

obj-y                                   += io.o
obj-y                                   += irq.o
obj-y					+= flowctrl.o
obj-y					+= pm.o
obj-y					+= reset.o
obj-y					+= reset-handler.o
obj-y					+= sleep.o
obj-y					+= tegra.o
obj-$(CONFIG_CPU_IDLE)			+= cpuidle.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= sleep-tegra20.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= pm-tegra20.o
ifeq ($(CONFIG_CPU_IDLE),y)
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= cpuidle-tegra20.o
endif
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)		+= sleep-tegra30.o
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)		+= pm-tegra30.o
ifeq ($(CONFIG_CPU_IDLE),y)
obj-$(CONFIG_ARCH_TEGRA_3x_SOC)		+= cpuidle-tegra30.o
endif
obj-$(CONFIG_SMP)			+= platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)               += hotplug.o

obj-$(CONFIG_ARCH_TEGRA_114_SOC)	+= sleep-tegra30.o
obj-$(CONFIG_ARCH_TEGRA_114_SOC)	+= pm-tegra30.o
ifeq ($(CONFIG_CPU_IDLE),y)
obj-$(CONFIG_ARCH_TEGRA_114_SOC)	+= cpuidle-tegra114.o
endif
obj-$(CONFIG_ARCH_TEGRA_124_SOC)	+= sleep-tegra30.o
obj-$(CONFIG_ARCH_TEGRA_124_SOC)	+= pm-tegra30.o
ifeq ($(CONFIG_CPU_IDLE),y)
obj-$(CONFIG_ARCH_TEGRA_124_SOC)	+= cpuidle-tegra114.o
endif

obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= board-paz00.o
