subdir-ccflags-y += \
	-I$(src)/linux/include \
	-I$(src)/linux/pri/include \
	-I$(src)/core/include \
	-I$(src)/include \
	-I$(src)/../common_detect \
	-I$(srctree)/drivers/misc/mediatek/btif/common/inc

subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/eccci/
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/eccci/$(MTK_PLATFORM)
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/eemcs/
subdir-ccflags-y += -I$(srctree)/drivers/misc/mediatek/conn_md/include

EXT_FLAG=_soc
COMMON_SRC_PATH := $(src)
COMMON_OBJ_PATH := $(src)

ifeq ($(CONFIG_ARCH_MT6580), y)
subdir-ccflags-y += -D CFG_WMT_READ_EFUSE_VCN33
endif

ifeq ($(CONFIG_MTK_COMBO), m)
# WMT DRIVER
obj-$(CONFIG_MTK_COMBO) += mtk_stp_wmt$(EXT_FLAG).o
# WMT DRIVER-core part
mtk_stp_wmt$(EXT_FLAG)-objs	:= core/wmt_core.o core/wmt_ctrl.o core/wmt_func.o core/wmt_ic_soc.o core/wmt_lib.o core/wmt_conf.o


# WMT DRIVER-linux private part
mtk_stp_wmt$(EXT_FLAG)-objs	+= linux/pri/wmt_dev.o linux/pri/wmt_exp.o
mtk_stp_wmt$(EXT_FLAG)-objs	+= linux/pri/stp_btif.o


# WMT DRIVER-OSAL
mtk_stp_wmt$(EXT_FLAG)-objs	+= linux/pub/osal.o linux/pub/bgw_desense.o
# WMT DRIVER-platform implementation
# ccflags-y += -D WMT_PLAT_ALPS
# mtk_stp_wmt$(EXT_FLAG)-objs += platform/alps/wmt_plat_alps.o

# mtk_stp_wmt$(EXT_FLAG)-objs += platform/alps/mtk_wcn_consys_hw.o


mtk_stp_wmt$(EXT_FLAG)-objs	+= linux/pri/stp_exp.o core/stp_core.o core/psm_core.o core/btm_core.o linux/pri/stp_dbg.o

# WMT stub part (built-in kernel image)
# obj-y			+= platform/alps/mtk_wcn_consys_stub_alps.o



obj-$(CONFIG_MTK_COMBO_BT) 	+= mtk_stp_bt$(EXT_FLAG).o
mtk_stp_bt$(EXT_FLAG)-objs	:= linux/pub/stp_chrdev_bt.o


obj-$(CONFIG_MTK_COMBO_WIFI) += mtk_wmt_wifi$(EXT_FLAG).o
mtk_wmt_wifi$(EXT_FLAG)-objs := linux/pub/wmt_chrdev_wifi.o

endif

ifeq ($(CONFIG_MTK_COMBO), y)
# subdir-ccflags-y += -D WMT_PLAT_ALPS
obj-y += core/
obj-y += linux/
#obj-y += $(subst ",,$(CONFIG_MTK_PLATFORM))/
obj-y += mt7623/
endif
