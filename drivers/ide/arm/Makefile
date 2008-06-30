
obj-$(CONFIG_BLK_DEV_IDE_ICSIDE)	+= icside.o
obj-$(CONFIG_BLK_DEV_IDE_RAPIDE)	+= rapide.o
obj-$(CONFIG_BLK_DEV_IDE_BAST)		+= bast-ide.o
obj-$(CONFIG_BLK_DEV_PALMCHIP_BK3710)	+= palm_bk3710.o

ifeq ($(CONFIG_IDE_ARM), m)
	obj-m += ide_arm.o
endif

EXTRA_CFLAGS	:= -Idrivers/ide
