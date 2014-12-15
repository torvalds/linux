#
# Makefile for the linux kernel.
#

# Common support
obj-y := cpu.o clock.o time.o iomapping.o gpio.o pinctrl.o mod_gate.o power_gate.o

obj-m :=
obj-n :=
obj-  :=

# Board support
obj-$(CONFIG_MACH_MESON6TVD_COMMON_BOARD)	+= board-meson6tvd-common.o

# Cache config
obj-$(CONFIG_CACHE_L2X0)			+= cache.o

# Power Management
obj-$(CONFIG_SUSPEND)				+= pm.o

obj-$(CONFIG_AMLOGIC_USB) += usbclock.o

obj-$(CONFIG_HOTPLUG_CPU)			+= hotplug.o
obj-$(CONFIG_SMP)				+= headsmp.o smp.o

obj-$(CONFIG_MESON_SUSPEND)			+= power_suspend.o

obj-$(CONFIG_CPU_IDLE)				+= cpuidle.o

