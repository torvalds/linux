ifeq ($(CONFIG_MTK_COMBO),y)
	ccflags-y += -I$(src)/inc/
	ccflags-y += -I$(src)/../

ifneq ($(filter "MT6630",$(CONFIG_MTK_COMBO_CHIP)),)
	ccflags-y += -D MTK_WCN_WLAN_GEN3
endif
ifneq ($(filter "CONSYS_6797",$(CONFIG_MTK_COMBO_CHIP)),)
	ccflags-y += -D MTK_WCN_WLAN_GEN3
else ifneq ($(filter "CONSYS_%",$(CONFIG_MTK_COMBO_CHIP)),)
	ccflags-y += -D MTK_WCN_WLAN_GEN2
endif

	obj-y += conn_drv_init.o
	obj-y += common_drv_init.o
	obj-y += bluetooth_drv_init.o
	obj-y += gps_drv_init.o
	obj-y += fm_drv_init.o
	obj-y += wlan_drv_init.o
	obj-($(CONFIG_MTK_COMBO_ANT)) += ant_drv_init.o

endif
