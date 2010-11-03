#
# Makefile for the linux kernel, U8500 machine.
#

obj-y				:= clock.o cpu.o devices.o
obj-$(CONFIG_UX500_SOC_DB5500)	+= cpu-db5500.o devices-db5500.o
obj-$(CONFIG_UX500_SOC_DB8500)	+= cpu-db8500.o devices-db8500.o prcmu.o
obj-$(CONFIG_MACH_U8500_MOP)	+= board-mop500.o board-mop500-sdi.o
obj-$(CONFIG_MACH_U5500)	+= board-u5500.o
obj-$(CONFIG_SMP)		+= platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o
obj-$(CONFIG_LOCAL_TIMERS)	+= localtimer.o
obj-$(CONFIG_REGULATOR_AB8500)	+= board-mop500-regulators.o
obj-$(CONFIG_U5500_MODEM_IRQ)	+= modem_irq.o
obj-$(CONFIG_U5500_MBOX)	+= mbox.o
