#
# Makefile for the linux kernel.
#

# Common support
obj-y := irq.o clock.o gpio.o time.o devices.o cpu.o system.o

# MX51 uses the TZIC interrupt controller, older platforms use AVIC (irq.o)
obj-$(CONFIG_MXC_TZIC) += tzic.o

obj-$(CONFIG_ARCH_MX1) += dma-mx1-mx2.o
obj-$(CONFIG_ARCH_MX2) += dma-mx1-mx2.o
obj-$(CONFIG_IMX_HAVE_IOMUX_V1) += iomux-v1.o
obj-$(CONFIG_ARCH_MXC_IOMUX_V3) += iomux-v3.o
obj-$(CONFIG_MXC_PWM)  += pwm.o
obj-$(CONFIG_USB_EHCI_MXC) += ehci.o
obj-$(CONFIG_MXC_ULPI) += ulpi.o
obj-$(CONFIG_ARCH_MXC_AUDMUX_V1) += audmux-v1.o
obj-$(CONFIG_ARCH_MXC_AUDMUX_V2) += audmux-v2.o
