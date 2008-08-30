#
# Makefile for the kernel miscellaneous SPARC device drivers.
#
# Dave Redman Frame Buffer tuning support.
#
# 7 October 2000, Bartlomiej Zolnierkiewicz <bkz@linux-ide.org>
# Rewritten to use lists instead of if-statements.
#

bbc-objs := bbc_i2c.o bbc_envctrl.o

obj-$(CONFIG_ENVCTRL)			+= envctrl.o
obj-$(CONFIG_DISPLAY7SEG)		+= display7seg.o
obj-$(CONFIG_OBP_FLASH)			+= flash.o
obj-$(CONFIG_SUN_OPENPROMIO)		+= openprom.o
obj-$(CONFIG_SUN_MOSTEK_RTC)		+= rtc.o
obj-$(CONFIG_TADPOLE_TS102_UCTRL)	+= uctrl.o
obj-$(CONFIG_SUN_JSFLASH)		+= jsflash.o
obj-$(CONFIG_BBC_I2C)			+= bbc.o
