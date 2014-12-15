#
# Makefile for the linux kernel.
#

# Common support


obj-y := cpu.o clock.o

# Board support
obj-$(CONFIG_MACH_MESONG9TV_COMMON_BOARD) += board-mesong9tv-common.o

# Cache config
obj-$(CONFIG_CACHE_L2X0)	+= cache.o

# Power Management
obj-$(CONFIG_SUSPEND)		+= pm.o

obj-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o hotplug-asm.o
obj-$(CONFIG_SMP)		+= headsmp.o smp.o

obj-$(CONFIG_AMLOGIC_USB)	+= usbclock.o

# gpio support
obj-$(CONFIG_GPIO_AMLOGIC)	+= gpio.o

#pinctrl support
obj-$(CONFIG_PINCTRL_AMLOGIC)	+= pinctrl.o

obj-$(CONFIG_CARDREADER)	+= sd_pad.o

obj-$(CONFIG_MESON_SUSPEND)	+= power_suspend.o
#ifneq ($(CONFIG_MESON_SUSPEND),y)
#obj-$(CONFIG_SUSPEND) += sleep.o
#endif

obj-$(CONFIG_AML_HDMI_TX)	+=  hdmi_tx_hw/

# ARM Trustzone
obj-$(CONFIG_MESON_TRUSTZONE)	+= meson-smc.o meson-secure.o
ifeq ($(CONFIG_MESON_TRUSTZONE),y)
plus_sec			:= $(call as-instr,.arch_extension sec,+sec)
AFLAGS_meson-smc.o		:= -Wa,-march=armv7-a$(plus_sec)
endif

