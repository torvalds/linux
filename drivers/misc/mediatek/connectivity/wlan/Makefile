ifeq ($(CONFIG_MTK_COMBO_WIFI),y)
    subdir-ccflags-y += -D MTK_WCN_BUILT_IN_DRIVER
endif

ifneq ($(filter "CONSYS_%",$(CONFIG_MTK_COMBO_CHIP)),)
#$(warning include gen2)
	obj-y   +=  gen2/
endif
