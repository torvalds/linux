#
# Makefile for RTC class/drivers.
#

ifeq ($(CONFIG_RTC_DEBUG),y)
	EXTRA_CFLAGS		+= -DDEBUG
endif

obj-$(CONFIG_RTC_LIB)		+= rtc-lib.o
obj-$(CONFIG_RTC_HCTOSYS)	+= hctosys.o
obj-$(CONFIG_RTC_CLASS)		+= rtc-core.o
rtc-core-y			:= class.o interface.o

rtc-core-$(CONFIG_RTC_INTF_DEV)	+= rtc-dev.o
rtc-core-$(CONFIG_RTC_INTF_PROC) += rtc-proc.o
rtc-core-$(CONFIG_RTC_INTF_SYSFS) += rtc-sysfs.o

obj-$(CONFIG_RTC_DRV_CMOS)	+= rtc-cmos.o
obj-$(CONFIG_RTC_DRV_X1205)	+= rtc-x1205.o
obj-$(CONFIG_RTC_DRV_ISL1208)	+= rtc-isl1208.o
obj-$(CONFIG_RTC_DRV_TEST)	+= rtc-test.o
obj-$(CONFIG_RTC_DRV_AT32AP700X)	+= rtc-at32ap700x.o
obj-$(CONFIG_RTC_DRV_DS1307)	+= rtc-ds1307.o
obj-$(CONFIG_RTC_DRV_DS1672)	+= rtc-ds1672.o
obj-$(CONFIG_RTC_DRV_DS1742)	+= rtc-ds1742.o
obj-$(CONFIG_RTC_DRV_OMAP)	+= rtc-omap.o
obj-$(CONFIG_RTC_DRV_PCF8563)	+= rtc-pcf8563.o
obj-$(CONFIG_RTC_DRV_PCF8583)	+= rtc-pcf8583.o
obj-$(CONFIG_RTC_DRV_RS5C372)	+= rtc-rs5c372.o
obj-$(CONFIG_RTC_DRV_S3C)	+= rtc-s3c.o
obj-$(CONFIG_RTC_DRV_RS5C348)	+= rtc-rs5c348.o
obj-$(CONFIG_RTC_DRV_M41T80)	+= rtc-m41t80.o
obj-$(CONFIG_RTC_DRV_M48T86)	+= rtc-m48t86.o
obj-$(CONFIG_RTC_DRV_DS1553)	+= rtc-ds1553.o
obj-$(CONFIG_RTC_DRV_RS5C313)	+= rtc-rs5c313.o
obj-$(CONFIG_RTC_DRV_EP93XX)	+= rtc-ep93xx.o
obj-$(CONFIG_RTC_DRV_SA1100)	+= rtc-sa1100.o
obj-$(CONFIG_RTC_DRV_VR41XX)	+= rtc-vr41xx.o
obj-$(CONFIG_RTC_DRV_PL031)	+= rtc-pl031.o
obj-$(CONFIG_RTC_DRV_MAX6900)	+= rtc-max6900.o
obj-$(CONFIG_RTC_DRV_MAX6902)	+= rtc-max6902.o
obj-$(CONFIG_RTC_DRV_V3020)	+= rtc-v3020.o
obj-$(CONFIG_RTC_DRV_AT91RM9200)+= rtc-at91rm9200.o
obj-$(CONFIG_RTC_DRV_SH)	+= rtc-sh.o
obj-$(CONFIG_RTC_DRV_BFIN)	+= rtc-bfin.o
obj-$(CONFIG_RTC_DRV_M48T59)	+= rtc-m48t59.o
