#
# Makefile for the linux kernel.
#

# Common support
#obj-y := clock.o time.o iomapping.o usbclock.o gpio.o mod_gate.o power_gate.o

obj-y := cpu.o clock.o time.o iomapping.o usbclock.o  mod_gate.o power_gate.o gpio.o pinctrl.o
#obj-y +=  usbclock.o usbsetting.o 
#obj-y += power_gate.o mod_gate.o watchdog.o
obj-m :=
obj-n :=
obj-  :=

# Board support
obj-$(CONFIG_MACH_MESON6_COMMON_BOARD)	+= board-meson6-common.o

# Cache config
obj-$(CONFIG_CACHE_L2X0) += cache.o

# Power Management
obj-$(CONFIG_SUSPEND)		+= pm.o

obj-$(CONFIG_HOTPLUG_CPU) += hotplug.o
obj-$(CONFIG_SMP) += headsmp.o smp.o

# gpio support
#obj-$(CONFIG_GPIO_AMLOGIC) += gpio-m6.o

#pinctrl support
#obj-$(CONFIG_PINCTRL_AMLOGIC) += pinctrl-m6.o

obj-$(CONFIG_CARDREADER)	+= sd_pad.o
#obj-$(CONFIG_CARDREADER)	+= gpio.o

obj-$(CONFIG_MESON_SUSPEND) += power_suspend.o
#ifneq ($(CONFIG_MESON_SUSPEND),y)
#obj-$(CONFIG_SUSPEND) += sleep.o
#endif

obj-$(CONFIG_CPU_IDLE) += cpuidle.o

obj-$(CONFIG_AML_HDMI_TX) +=  hdmi_tx_hw/

obj-$(CONFIG_AM_LCD_OUTPUT) +=  lcd/

# ARM Trustzone
obj-$(CONFIG_MESON_TRUSTZONE) += meson-smc.o meson-secure.o
ifeq ($(CONFIG_MESON_TRUSTZONE),y)
plus_sec                      := $(call as-instr,.arch_extension sec,+sec)
AFLAGS_meson-smc.o            := -Wa,-march=armv7-a$(plus_sec)
endif
