#
# Makefile for the linux kernel, U8500 machine.
#

obj-y				:= clock.o cpu.o devices.o devices-common.o \
				   id.o usb.o
obj-$(CONFIG_UX500_SOC_DB5500)	+= cpu-db5500.o dma-db5500.o
obj-$(CONFIG_UX500_SOC_DB8500)	+= cpu-db8500.o devices-db8500.o prcmu.o
obj-$(CONFIG_MACH_U8500)	+= board-mop500.o board-mop500-sdi.o \
				board-mop500-regulators.o \
				board-mop500-uib.o board-mop500-stuib.o \
				board-mop500-u8500uib.o \
				board-mop500-pins.o
obj-$(CONFIG_MACH_U5500)	+= board-u5500.o board-u5500-sdi.o
obj-$(CONFIG_SMP)		+= platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o
obj-$(CONFIG_LOCAL_TIMERS)	+= localtimer.o
obj-$(CONFIG_U5500_MODEM_IRQ)	+= modem-irq-db5500.o
obj-$(CONFIG_U5500_MBOX)	+= mbox-db5500.o
obj-$(CONFIG_CPU_FREQ)		+= cpufreq.o
