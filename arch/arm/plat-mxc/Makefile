#
# Makefile for the linux kernel.
#

# Common support
obj-y := irq.o clock.o gpio.o time.o devices.o cpu.o system.o

obj-$(CONFIG_ARCH_MX1) += iomux-mx1-mx2.o dma-mx1-mx2.o
obj-$(CONFIG_ARCH_MX2) += iomux-mx1-mx2.o dma-mx1-mx2.o
obj-$(CONFIG_ARCH_MXC_IOMUX_V3) += iomux-v3.o
obj-$(CONFIG_MXC_PWM)  += pwm.o
