#ccflags-y += -I$(src)/core/include
ccflags-y += -I$(src)/linux/include -I$(src)/core/include -I$(src)/include -I$(src)/
ccflags-y += -DWMT_PLAT_APEX=1
ccflags-y += -DWMT_PLAT_ALPS=0
ccflags-y += -DWMT_UART_RX_MODE_WORK=1 # 1. work thread 0. tasklet
#ifeq ($(MTK_COMBO_CHIP), MT6628)
#EXT_FLAG=
#else
#EXT_FLAG=_mt6628
#endif
EXT_FLAG=
COMMON_SRC_PATH := $(src)
COMMON_OBJ_PATH := $(src)

# WMT DRIVER
obj-$(CONFIG_MTK_COMBO_COMM) += mtk_stp_wmt$(EXT_FLAG).o
# WMT DRIVER-core part
mtk_stp_wmt$(EXT_FLAG)-objs	:= core/wmt_core.o core/wmt_ctrl.o  core/wmt_func.o core/wmt_ic_6620.o core/wmt_lib.o core/wmt_conf.o core/wmt_dbg.o core/wmt_exp.o

#ifeq ($(MTK_COMBO_CHIP), MT6628)
mtk_stp_wmt$(EXT_FLAG)-objs	+= core/wmt_ic_6628.o
#endif
# WMT DRIVER-linux private part
mtk_stp_wmt$(EXT_FLAG)-objs	+= linux/wmt_dev.o
mtk_stp_wmt$(EXT_FLAG)-objs	+= linux/wmt_tm.o

# WMT DRIVER-OSAL
mtk_stp_wmt$(EXT_FLAG)-objs	+= linux/osal.o
# WMT DRIVER-platform implementation
#ccflags-y += -D WMT_PLAT_ALPS #Jake modify

# WMT DRIVER-platform part, specified by string CONFIG_MTK_COMBO_PLAT_PATH
PLAT := $(shell echo $(CONFIG_MTK_COMBO_PLAT_PATH))



#obj-y += platform/$(PLAT)/mtk_wcn_cmb_stub_$(PLAT).o
mtk_stp_wmt$(EXT_FLAG)-objs	+= platform/$(PLAT)/wmt_plat_$(PLAT).o
mtk_stp_wmt$(EXT_FLAG)-objs	+= platform/$(PLAT)/wmt_plat_stub.o

# FIXME: select desired hw according to project configuration
#ifeq ($(MTK_COMBO_CHIP), MT6628)
#mtk_stp_wmt$(EXT_FLAG)-objs	+= platform/alps/mtk_wcn_cmb_hw_6628.o 
#else
mtk_stp_wmt$(EXT_FLAG)-objs	+= platform/$(PLAT)/mtk_wcn_cmb_hw.o
#endif

mtk_stp_wmt$(EXT_FLAG)-objs	+= core/stp_exp.o core/stp_core.o core/psm_core.o core/btm_core.o linux/stp_dbg.o
#ifeq ($(MTK_COMBO_CHIP), MT6628)
# WMT stub part (built-in kernel image)
obj-y			+= platform/$(PLAT)/mtk_wcn_cmb_stub_$(PLAT).o
#endif

obj-$(CONFIG_MTK_COMBO_COMM_UART) += mtk_stp_uart$(EXT_FLAG).o
mtk_stp_uart$(EXT_FLAG)-objs	:= linux/stp_uart.o


obj-$(CONFIG_MTK_COMBO_COMM) += mtk_hif_sdio$(EXT_FLAG).o
mtk_hif_sdio$(EXT_FLAG)-objs	:= linux/hif_sdio.o
mtk_hif_sdio$(EXT_FLAG)-objs	+= linux/hif_sdio_chrdev.o
mtk_hif_sdio$(EXT_FLAG)-objs	+= platform/$(PLAT)/mtk_wcn_cmb_hw.o
mtk_hif_sdio$(EXT_FLAG)-objs	+= platform/$(PLAT)/wmt_plat_$(PLAT).o
mtk_hif_sdio$(EXT_FLAG)-objs	+= linux/osal.o


# obj-$(CONFIG_MTK_COMBO) += mtk_stp_sdio$(EXT_FLAG).o
# mtk_stp_sdio$(EXT_FLAG)-objs	:= linux/stp_sdio.o
# mtk_stp_sdio$(EXT_FLAG)-objs	+= linux/osal.o


obj-$(CONFIG_MTK_COMBO_BT) += mtk_stp_bt$(EXT_FLAG).o
mtk_stp_bt$(EXT_FLAG)-objs		:= linux/stp_chrdev_bt.o

obj-$(CONFIG_MTK_COMBO_GPS) += mtk_stp_gps$(EXT_FLAG).o
mtk_stp_gps$(EXT_FLAG)-objs	:= linux/stp_chrdev_gps.o


obj-$(CONFIG_MTK_COMBO_WIFI) += mtk_wmt_wifi$(EXT_FLAG).o
mtk_wmt_wifi$(EXT_FLAG)-objs       := linux/wmt_chrdev_wifi.o 
