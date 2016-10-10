# Greybus core
greybus-y :=	core.o		\
		debugfs.o	\
		hd.o		\
		manifest.o	\
		module.o	\
		interface.o	\
		bundle.o	\
		connection.o	\
		control.o	\
		svc.o		\
		svc_watchdog.o	\
		operation.o	\
		timesync.o	\
		timesync_platform.o

obj-$(CONFIG_GREYBUS)		+= greybus.o

# needed for trace events
ccflags-y += -I$(src)


# Greybus Host controller drivers
gb-es2-y := es2.o

obj-$(CONFIG_GREYBUS_ES2)	+= gb-es2.o

# Greybus class drivers
gb-bootrom-y		:= bootrom.o
gb-camera-y		:= camera.o
gb-firmware-y		:= fw-core.o fw-download.o fw-management.o authentication.o
gb-spilib-y		:= spilib.o
gb-hid-y		:= hid.o
gb-light-y		:= light.o
gb-log-y		:= log.o
gb-loopback-y		:= loopback.o
gb-power-supply-y	:= power_supply.o
gb-raw-y		:= raw.o
gb-vibrator-y		:= vibrator.o

obj-$(CONFIG_GREYBUS_BOOTROM)	+= gb-bootrom.o
obj-$(CONFIG_GREYBUS_CAMERA)	+= gb-camera.o
obj-$(CONFIG_GREYBUS_FIRMWARE)	+= gb-firmware.o gb-spilib.o
obj-$(CONFIG_GREYBUS_HID)	+= gb-hid.o
obj-$(CONFIG_GREYBUS_LIGHT)	+= gb-light.o
obj-$(CONFIG_GREYBUS_LOG)	+= gb-log.o
obj-$(CONFIG_GREYBUS_LOOPBACK)	+= gb-loopback.o
obj-$(CONFIG_GREYBUS_POWER)	+= gb-power-supply.o
obj-$(CONFIG_GREYBUS_RAW)	+= gb-raw.o
obj-$(CONFIG_GREYBUS_VIBRATOR)	+= gb-vibrator.o

# Greybus Audio is a bunch of modules
gb-audio-module-y	:= audio_module.o audio_topology.o
gb-audio-codec-y	:= audio_codec.o
gb-audio-gb-y		:= audio_gb.o
gb-audio-apbridgea-y	:= audio_apbridgea.o
gb-audio-manager-y	:= audio_manager.o audio_manager_module.o

# Greybus Audio sysfs helpers can be useful when debugging
#GB_AUDIO_MANAGER_SYSFS ?= true
#ifeq ($(GB_AUDIO_MANAGER_SYSFS),true)
#gb-audio-manager-y += audio_manager_sysfs.o
#ccflags-y += -DGB_AUDIO_MANAGER_SYSFS
#endif

obj-$(CONFIG_GREYBUS_AUDIO_MSM8994)	+= gb-audio-codec.o
obj-$(CONFIG_GREYBUS_AUDIO_MSM8994)	+= gb-audio-module.o
obj-$(CONFIG_GREYBUS_AUDIO)		+= gb-audio-gb.o
obj-$(CONFIG_GREYBUS_AUDIO)		+= gb-audio-apbridgea.o
obj-$(CONFIG_GREYBUS_AUDIO)		+= gb-audio-manager.o


# Greybus Bridged PHY drivers
gb-gbphy-y := gbphy.o
gb-gpio-y := gpio.o
gb-i2c-y := i2c.o
gb-pwm-y := pwm.o
gb-sdio-y := sdio.o
gb-spi-y := spi.o
gb-uart-y := uart.o
gb-usb-y := usb.o

obj-$(CONFIG_GREYBUS_BRIDGED_PHY)	+= gb-gbphy.o
obj-$(CONFIG_GREYBUS_GPIO)		+= gb-gpio.o
obj-$(CONFIG_GREYBUS_I2C)		+= gb-i2c.o
obj-$(CONFIG_GREYBUS_PWM)		+= gb-pwm.o
obj-$(CONFIG_GREYBUS_SDIO)		+= gb-sdio.o
obj-$(CONFIG_GREYBUS_SPI)		+= gb-spi.o gb-spilib.o
obj-$(CONFIG_GREYBUS_UART)		+= gb-uart.o
obj-$(CONFIG_GREYBUS_USB)		+= gb-usb.o


# Greybus Platform driver
gb-arche-y	:= arche-platform.o arche-apb-ctrl.o

obj-$(CONFIG_USB_HSIC_USB3613)	+= gb-arche.o
