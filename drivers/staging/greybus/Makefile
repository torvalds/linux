greybus-y :=	core.o		\
		debugfs.o	\
		hd.o		\
		manifest.o	\
		module.o	\
		interface.o	\
		bundle.o	\
		connection.o	\
		protocol.o	\
		control.o	\
		svc.o		\
		svc_watchdog.o	\
		bootrom.o	\
		operation.o	\
		legacy.o

gb-phy-y :=	gpbridge.o	\
		sdio.o	\
		uart.o	\
		spi.o	\
		usb.o

# Prefix all modules with gb-
gb-vibrator-y := vibrator.o
gb-power-supply-y := power_supply.o
gb-loopback-y := loopback.o
gb-light-y := light.o
gb-raw-y := raw.o
gb-hid-y := hid.o
gb-es2-y := es2.o
gb-arche-y := arche-platform.o arche-apb-ctrl.o
gb-audio-module-y := audio_module.o audio_topology.o
gb-audio-codec-y := audio_codec.o
gb-audio-gb-y := audio_gb.o
gb-audio-apbridgea-y := audio_apbridgea.o
gb-audio-manager-y += audio_manager.o
gb-audio-manager-y += audio_manager_module.o
gb-camera-y := camera.o
gb-firmware-y := fw-core.o fw-download.o
gb-pwm-y := pwm.o
gb-gpio-y := gpio.o
gb-i2c-y := i2c.o

obj-m += greybus.o
obj-m += gb-phy.o
obj-m += gb-vibrator.o
obj-m += gb-power-supply.o
obj-m += gb-loopback.o
obj-m += gb-light.o
obj-m += gb-hid.o
obj-m += gb-raw.o
obj-m += gb-es2.o
ifeq ($(CONFIG_USB_HSIC_USB3613),y)
 obj-m += gb-arche.o
endif
ifeq ($(CONFIG_ARCH_MSM8994),y)
 obj-m += gb-audio-codec.o
 obj-m += gb-audio-module.o
 obj-m += gb-camera.o
endif
obj-m += gb-audio-gb.o
obj-m += gb-audio-apbridgea.o
obj-m += gb-audio-manager.o
obj-m += gb-firmware.o
obj-m += gb-pwm.o
obj-m += gb-gpio.o
obj-m += gb-i2c.o

KERNELVER		?= $(shell uname -r)
KERNELDIR 		?= /lib/modules/$(KERNELVER)/build
INSTALL_MOD_PATH	?= /..
PWD			:= $(shell pwd)

# kernel config option that shall be enable
CONFIG_OPTIONS_ENABLE := POWER_SUPPLY PWM SYSFS SPI USB SND_SOC MMC LEDS_CLASS INPUT

# kernel config option that shall be disable
CONFIG_OPTIONS_DISABLE :=

# this only run in kbuild part of the makefile
ifneq ($(KERNELRELEASE),)
# This function returns the argument version if current kernel version is minor
# than the passed version, return 1 if equal or the current kernel version if it
# is greater than argument version.
kvers_cmp=$(shell [ "$(KERNELVERSION)" = "$(1)" ] && echo 1 || printf "$(1)\n$(KERNELVERSION)" | sort -V | tail -1)

ifneq ($(call kvers_cmp,"3.19.0"),3.19.0)
    CONFIG_OPTIONS_ENABLE += LEDS_CLASS_FLASH
endif

ifneq ($(call kvers_cmp,"4.2.0"),4.2.0)
    CONFIG_OPTIONS_ENABLE += V4L2_FLASH_LED_CLASS
endif

$(foreach opt,$(CONFIG_OPTIONS_ENABLE),$(if $(CONFIG_$(opt)),, \
     $(error CONFIG_$(opt) is disabled in the kernel configuration and must be enable \
     to continue compilation)))
$(foreach opt,$(CONFIG_OPTIONS_DISABLE),$(if $(filter m y, $(CONFIG_$(opt))), \
     $(error CONFIG_$(opt) is enabled in the kernel configuration and must be disable \
     to continue compilation),))
endif

# add -Wall to try to catch everything we can.
ccflags-y := -Wall

# needed for trace events
ccflags-y += -I$(src)

GB_AUDIO_MANAGER_SYSFS ?= true
ifeq ($(GB_AUDIO_MANAGER_SYSFS),true)
gb-audio-manager-y += audio_manager_sysfs.o
ccflags-y += -DGB_AUDIO_MANAGER_SYSFS
endif

all: module

tools::
	$(MAKE) -C tools KERNELDIR=$(realpath $(KERNELDIR))

module:
	$(MAKE) -C $(KERNELDIR) M=$(PWD)

check:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) C=2 CF="-D__CHECK_ENDIAN__"

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers
	$(MAKE) -C tools clean

coccicheck:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) coccicheck

install: module
	mkdir -p $(INSTALL_MOD_PATH)/lib/modules/$(KERNELVER)/kernel/drivers/greybus/
	cp -f *.ko $(INSTALL_MOD_PATH)/lib/modules/$(KERNELVER)/kernel/drivers/greybus/
	depmod -b $(INSTALL_MOD_PATH) -a $(KERNELVER)
