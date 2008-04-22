
# link order is important here

obj-$(CONFIG_BLK_DEV_ALI14XX)		+= ali14xx.o
obj-$(CONFIG_BLK_DEV_UMC8672)		+= umc8672.o
obj-$(CONFIG_BLK_DEV_DTC2278)		+= dtc2278.o
obj-$(CONFIG_BLK_DEV_HT6560B)		+= ht6560b.o
obj-$(CONFIG_BLK_DEV_QD65XX)		+= qd65xx.o
obj-$(CONFIG_BLK_DEV_4DRIVES)		+= ide-4drives.o

obj-$(CONFIG_BLK_DEV_GAYLE)		+= gayle.o
obj-$(CONFIG_BLK_DEV_FALCON_IDE)	+= falconide.o
obj-$(CONFIG_BLK_DEV_MAC_IDE)		+= macide.o
obj-$(CONFIG_BLK_DEV_Q40IDE)		+= q40ide.o
obj-$(CONFIG_BLK_DEV_BUDDHA)		+= buddha.o

ifeq ($(CONFIG_BLK_DEV_IDECS), m)
	obj-m += ide-cs.o
endif

ifeq ($(CONFIG_BLK_DEV_PLATFORM), m)
	obj-m += ide_platform.o
endif

EXTRA_CFLAGS	:= -Idrivers/ide
