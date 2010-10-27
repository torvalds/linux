#
# Makefile for the linux kernel, U8500 machine.
#

obj-y				:= clock.o cpu.o devices.o
obj-$(CONFIG_UX500_SOC_DB5500)	+= cpu-db5500.o devices-db5500.o
obj-$(CONFIG_UX500_SOC_DB8500)	+= cpu-db8500.o devices-db8500.o
obj-$(CONFIG_MACH_U8500_MOP)	+= board-mop500.o
obj-$(CONFIG_MACH_U5500)	+= board-u5500.o
obj-$(CONFIG_SMP)		+= platsmp.o headsmp.o
obj-$(CONFIG_LOCAL_TIMERS)	+= localtimer.o
