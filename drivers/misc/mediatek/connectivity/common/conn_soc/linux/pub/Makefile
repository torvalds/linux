ifeq ($(CONFIG_MTK_COMBO), y)

ccflags-y += \
	-I$(src)/../../linux/include \
	-I$(src)/../../linux/pri/include \
	-I$(src)/../../core/include \
	-I$(src)/../../include \
	-I$(src)/../include \
	-I$(src)/../../../common_detect \
	-I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/mach \
	-DMTK_BT_HCI=1

ccflags-y += -DWMT_CREATE_NODE_DYNAMIC=1

ifeq ($(CONFIG_MTK_TC1_FEATURE), y)
    ccflags-y += -DCFG_TC1_FEATURE=1
else
    ccflags-y += -DCFG_TC1_FEATURE=0
endif

obj-y += osal.o \
         bgw_desense.o \
         wmt_idc.o
obj-$(CONFIG_MTK_COMBO_BT)		+= stp_chrdev_bt.o
obj-$(CONFIG_MTK_COMBO_WIFI)	+= wmt_chrdev_wifi.o

endif
