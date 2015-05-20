greybus-y :=	core.o		\
		debugfs.o	\
		ap.o		\
		manifest.o	\
		endo.o		\
		module.o	\
		interface.o	\
		bundle.o	\
		connection.o	\
		protocol.o	\
		operation.o

gb-phy-y :=	gpbridge.o	\
		sdio.o	\
		uart.o	\
		pwm.o	\
		gpio.o	\
		hid.o	\
		i2c.o	\
		spi.o	\
		usb.o	\
		audio.o		\
		audio-pcm.o	\
		audio-dai.o	\
		audio-gb-cmds.o

# Prefix all modules with gb-
gb-vibrator-y := vibrator.o
gb-battery-y := battery.o
gb-loopback-y := loopback.o
gb-raw-y := raw.o
gb-es1-y := es1.o
gb-es2-y := es2.o

obj-m += greybus.o
obj-m += gb-phy.o
obj-m += gb-vibrator.o
obj-m += gb-battery.o
obj-m += gb-loopback.o
obj-m += gb-raw.o
obj-m += gb-es1.o
obj-m += gb-es2.o

KERNELVER		?= $(shell uname -r)
KERNELDIR 		?= /lib/modules/$(KERNELVER)/build
INSTALL_MOD_PATH	?= /..
PWD			:= $(shell pwd)

# kernel config option that shall be enable
CONFIG_OPTIONS_ENABLE := SYSFS SPI USB SND_SOC

# kernel config option that shall be disable
CONFIG_OPTIONS_DISABLE :=

# this only run in kbuild part of the makefile
ifneq ($(KERNELRELEASE),)
$(foreach opt,$(CONFIG_OPTIONS_ENABLE),$(if $(CONFIG_$(opt)),, \
     $(error CONFIG_$(opt) is disabled in the kernel configuration and must be enable \
     to continue compilation)))
$(foreach opt,$(CONFIG_OPTIONS_DISABLE),$(if $(filter m y, $(CONFIG_$(opt))), \
     $(error CONFIG_$(opt) is enabled in the kernel configuration and must be disable \
     to continue compilation),))
endif

# add -Wall to try to catch everything we can.
ccFlags-y := -Wall

all: module

module:
	$(MAKE) -C $(KERNELDIR) M=$(PWD)

check:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) C=2 CF="-D__CHECK_ENDIAN__"

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers

coccicheck:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) coccicheck

install: module
	mkdir -p $(INSTALL_MOD_PATH)/lib/modules/$(KERNELVER)/kernel/drivers/greybus/
	cp -f *.ko $(INSTALL_MOD_PATH)/lib/modules/$(KERNELVER)/kernel/drivers/greybus/
	depmod -b $(INSTALL_MOD_PATH) -a $(KERNELVER)
