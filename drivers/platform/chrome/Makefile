# SPDX-License-Identifier: GPL-2.0

# tell define_trace.h where to find the cros ec trace header
CFLAGS_cros_ec_trace.o:=		-I$(src)

obj-$(CONFIG_CHROMEOS_LAPTOP)		+= chromeos_laptop.o
obj-$(CONFIG_CHROMEOS_PSTORE)		+= chromeos_pstore.o
obj-$(CONFIG_CHROMEOS_TBMC)		+= chromeos_tbmc.o
obj-$(CONFIG_CROS_EC)			+= cros_ec.o
obj-$(CONFIG_CROS_EC_I2C)		+= cros_ec_i2c.o
obj-$(CONFIG_CROS_EC_ISHTP)		+= cros_ec_ishtp.o
obj-$(CONFIG_CROS_EC_RPMSG)		+= cros_ec_rpmsg.o
obj-$(CONFIG_CROS_EC_SPI)		+= cros_ec_spi.o
cros_ec_lpcs-objs			:= cros_ec_lpc.o cros_ec_lpc_mec.o
obj-$(CONFIG_CROS_EC_TYPEC)		+= cros_ec_typec.o
obj-$(CONFIG_CROS_EC_LPC)		+= cros_ec_lpcs.o
obj-$(CONFIG_CROS_EC_PROTO)		+= cros_ec_proto.o cros_ec_trace.o
obj-$(CONFIG_CROS_KBD_LED_BACKLIGHT)	+= cros_kbd_led_backlight.o
obj-$(CONFIG_CROS_EC_CHARDEV)		+= cros_ec_chardev.o
obj-$(CONFIG_CROS_EC_LIGHTBAR)		+= cros_ec_lightbar.o
obj-$(CONFIG_CROS_EC_VBC)		+= cros_ec_vbc.o
obj-$(CONFIG_CROS_EC_DEBUGFS)		+= cros_ec_debugfs.o
cros-ec-sensorhub-objs			:= cros_ec_sensorhub.o cros_ec_sensorhub_ring.o cros_ec_trace.o
obj-$(CONFIG_CROS_EC_SENSORHUB)		+= cros-ec-sensorhub.o
obj-$(CONFIG_CROS_EC_SYSFS)		+= cros_ec_sysfs.o
obj-$(CONFIG_CROS_USBPD_LOGGER)		+= cros_usbpd_logger.o
obj-$(CONFIG_CROS_USBPD_NOTIFY)		+= cros_usbpd_notify.o

obj-$(CONFIG_WILCO_EC)			+= wilco_ec/
