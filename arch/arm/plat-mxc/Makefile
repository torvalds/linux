#
# Makefile for the linux kernel.
#

# Common support
obj-y := clock.o gpio.o time.o devices.o cpu.o system.o irq-common.o

# MX51 uses the TZIC interrupt controller, older platforms use AVIC
obj-$(CONFIG_MXC_TZIC) += tzic.o
obj-$(CONFIG_MXC_AVIC) += avic.o

obj-$(CONFIG_IMX_HAVE_IOMUX_V1) += iomux-v1.o
obj-$(CONFIG_ARCH_MXC_IOMUX_V3) += iomux-v3.o
obj-$(CONFIG_IRAM_ALLOC) += iram_alloc.o
obj-$(CONFIG_MXC_PWM)  += pwm.o
obj-$(CONFIG_USB_EHCI_MXC) += ehci.o
obj-$(CONFIG_MXC_ULPI) += ulpi.o
obj-$(CONFIG_MXC_USE_EPIT) += epit.o
obj-$(CONFIG_ARCH_MXC_AUDMUX_V1) += audmux-v1.o
obj-$(CONFIG_ARCH_MXC_AUDMUX_V2) += audmux-v2.o
obj-$(CONFIG_MXC_DEBUG_BOARD) += 3ds_debugboard.o
obj-$(CONFIG_CPU_FREQ_IMX)    += cpufreq.o
ifdef CONFIG_SND_IMX_SOC
obj-y += ssi-fiq.o
obj-y += ssi-fiq-ksym.o
endif

obj-y += devices/
