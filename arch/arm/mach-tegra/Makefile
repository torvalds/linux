obj-y                                   += common.o
obj-y                                   += io.o
obj-y                                   += irq.o legacy_irq.o
obj-y                                   += clock.o
obj-y                                   += timer.o
obj-y                                   += gpio.o
obj-y                                   += pinmux.o
obj-y                                   += devices.o
obj-y                                   += delay.o
obj-y                                   += powergate.o
obj-y					+= suspend.o
obj-y					+= fuse.o
obj-y					+= tegra_i2s_audio.o
obj-y					+= tegra_spdif_audio.o
obj-$(CONFIG_USB_SUPPORT)               += usb_phy.o
obj-$(CONFIG_FIQ)                       += fiq.o
obj-$(CONFIG_TEGRA_PWM)                 += pwm.o

obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += clock.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += tegra2_clocks.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += tegra2_dvfs.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += suspend-t2.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= tegra2_save.o
obj-$(CONFIG_CPU_V7)			+= cortex-a9.o

obj-$(CONFIG_ARCH_TEGRA_2x_SOC)		+= pinmux-t2-tables.o
ifeq ($(CONFIG_SMP),y)
obj-y                                   += platsmp.o localtimer.o
obj-$(CONFIG_ARCH_TEGRA_2x_SOC)         += headsmp-t2.o
endif
obj-$(CONFIG_TEGRA_SYSTEM_DMA)		+= dma.o
obj-$(CONFIG_CPU_FREQ)                  += cpu-tegra.o
obj-$(CONFIG_CPU_IDLE)			+= cpuidle.o
obj-$(CONFIG_TEGRA_IOVMM)               += iovmm.o
obj-$(CONFIG_TEGRA_IOVMM_GART)          += iovmm-gart.o

obj-${CONFIG_MACH_HARMONY}              += board-harmony.o
obj-${CONFIG_MACH_HARMONY}              += board-harmony-pinmux.o
obj-${CONFIG_MACH_HARMONY}              += board-harmony-panel.o
obj-${CONFIG_MACH_HARMONY}              += board-harmony-sdhci.o

obj-${CONFIG_MACH_VENTANA}              += board-ventana.o
obj-${CONFIG_MACH_VENTANA}              += board-ventana-pinmux.o
obj-${CONFIG_MACH_VENTANA}              += board-ventana-sdhci.o
obj-${CONFIG_MACH_VENTANA}              += board-ventana-power.o

obj-${CONFIG_MACH_OLYMPUS}              += board-olympus.o
obj-${CONFIG_MACH_OLYMPUS}              += board-olympus-pinmux.o
obj-${CONFIG_MACH_OLYMPUS}              += board-olympus-panel.o
obj-${CONFIG_MACH_OLYMPUS}              += board-olympus-i2c.o
obj-${CONFIG_MACH_OLYMPUS}              += board-olympus-keypad.o
obj-${CONFIG_MACH_OLYMPUS}              += board-olympus-wifi.o

obj-${CONFIG_MACH_STINGRAY}             += board-stingray.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-pinmux.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-panel.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-i2c.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-keypad.o
obj-${CONFIG_MACH_STINGRAY}             += board-stingray-wifi.o
