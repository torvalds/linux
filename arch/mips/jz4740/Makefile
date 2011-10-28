#
# Makefile for the Ingenic JZ4740.
#

# Object file lists.

obj-y += prom.o irq.o time.o reset.o setup.o dma.o \
	gpio.o clock.o platform.o timer.o pwm.o serial.o

obj-$(CONFIG_DEBUG_FS) += clock-debugfs.o

# board specific support

obj-$(CONFIG_JZ4740_QI_LB60)	+= board-qi_lb60.o

# PM support

obj-$(CONFIG_PM) += pm.o

ccflags-y := -Werror -Wall
