#
# Makefile for RTC class/drivers.
#

obj-$(CONFIG_RTC_LIB)		+= rtc-lib.o
obj-$(CONFIG_RTC_HCTOSYS)	+= hctosys.o
obj-$(CONFIG_RTC_CLASS)		+= rtc-core.o
rtc-core-y			:= class.o interface.o

obj-$(CONFIG_RTC_INTF_SYSFS)	+= rtc-sysfs.o
obj-$(CONFIG_RTC_INTF_PROC)	+= rtc-proc.o
obj-$(CONFIG_RTC_INTF_DEV)	+= rtc-dev.o

obj-$(CONFIG_RTC_DRV_X1205)	+= rtc-x1205.o
obj-$(CONFIG_RTC_DRV_TEST)	+= rtc-test.o
obj-$(CONFIG_RTC_DRV_DS1672)	+= rtc-ds1672.o
obj-$(CONFIG_RTC_DRV_PCF8563)	+= rtc-pcf8563.o
obj-$(CONFIG_RTC_DRV_RS5C372)	+= rtc-rs5c372.o
obj-$(CONFIG_RTC_DRV_M48T86)	+= rtc-m48t86.o
obj-$(CONFIG_RTC_DRV_EP93XX)	+= rtc-ep93xx.o
obj-$(CONFIG_RTC_DRV_SA1100)	+= rtc-sa1100.o
