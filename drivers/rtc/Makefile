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
