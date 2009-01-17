#
# Makefile for Meilhaus linux driver system
#

obj-$(CONFIG_MEILHAUS) += memain.o
obj-$(CONFIG_ME1600) += me1600.o
obj-$(CONFIG_ME1000) += me1000.o
obj-$(CONFIG_ME1400) += me1400.o
obj-$(CONFIG_ME4600) += me4600.o
obj-$(CONFIG_ME6000) += me6000.o
obj-$(CONFIG_ME0600) += me0600.o
obj-$(CONFIG_ME8100) += me8100.o
obj-$(CONFIG_ME8200) += me8200.o
obj-$(CONFIG_ME0900) += me0900.o
obj-$(CONFIG_MEDUMMY) += medummy.o


me1600-objs := medevice.o medlist.o medlock.o me1600_device.o
me1600-objs += mesubdevice.o meslist.o meslock.o me1600_ao.o

me1000-objs := medevice.o medlist.o medlock.o me1000_device.o
me1000-objs += mesubdevice.o meslist.o meslock.o me1000_dio.o

me1400-objs := medevice.o medlist.o medlock.o me1400_device.o
me1400-objs += mesubdevice.o meslist.o meslock.o me8254.o me8255.o me1400_ext_irq.o

me4600-objs := medevice.o medlist.o medlock.o mefirmware.o me4600_device.o
me4600-objs += mesubdevice.o meslist.o meslock.o me4600_do.o me4600_di.o me4600_dio.o me8254.o me4600_ai.o me4600_ao.o me4600_ext_irq.o

me6000-objs := medevice.o medlist.o medlock.o mefirmware.o me6000_device.o
me6000-objs += mesubdevice.o meslist.o meslock.o me6000_dio.o me6000_ao.o

me0600-objs := medevice.o medlist.o medlock.o me0600_device.o
me0600-objs += mesubdevice.o meslist.o meslock.o me0600_relay.o me0600_ttli.o me0600_optoi.o me0600_dio.o me0600_ext_irq.o

me8100-objs := medevice.o medlist.o medlock.o me8100_device.o
me8100-objs += mesubdevice.o meslist.o meslock.o me8100_di.o me8100_do.o me8254.o

me8200-objs := medevice.o medlist.o medlock.o me8200_device.o
me8200-objs += mesubdevice.o meslist.o meslock.o me8200_di.o me8200_do.o me8200_dio.o

me0900-objs := medevice.o medlist.o medlock.o me0900_device.o
me0900-objs += mesubdevice.o meslist.o meslock.o me0900_do.o me0900_di.o
