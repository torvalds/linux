EXTRA_CFLAGS += -DCONFIG_RTL8821C

ifeq ($(CONFIG_USB_HCI), y)
FILE_NAME = 8821cu
endif
ifeq ($(CONFIG_PCI_HCI), y)
FILE_NAME = 8821ce
endif
ifeq ($(CONFIG_SDIO_HCI), y)
FILE_NAME = 8821cs
endif

_HAL_INTFS_FILES +=	hal/rtl8821c/rtl8821c_halinit.o \
			hal/rtl8821c/rtl8821c_mac.o \
			hal/rtl8821c/rtl8821c_cmd.o \
			hal/rtl8821c/rtl8821c_phy.o \
			hal/rtl8821c/rtl8821c_dm.o \
			hal/rtl8821c/rtl8821c_ops.o \
			hal/rtl8821c/hal8821c_fw.o

_HAL_INTFS_FILES +=	hal/rtl8821c/$(HCI_NAME)/rtl$(FILE_NAME)_halinit.o \
			hal/rtl8821c/$(HCI_NAME)/rtl$(FILE_NAME)_halmac.o \
			hal/rtl8821c/$(HCI_NAME)/rtl$(FILE_NAME)_io.o \
			hal/rtl8821c/$(HCI_NAME)/rtl$(FILE_NAME)_xmit.o \
			hal/rtl8821c/$(HCI_NAME)/rtl$(FILE_NAME)_recv.o \
			hal/rtl8821c/$(HCI_NAME)/rtl$(FILE_NAME)_led.o \
			hal/rtl8821c/$(HCI_NAME)/rtl$(FILE_NAME)_ops.o

ifeq ($(CONFIG_SDIO_HCI), y)
_HAL_INTFS_FILES +=hal/efuse/$(RTL871X)/HalEfuseMask8821C_SDIO.o
endif
ifeq ($(CONFIG_USB_HCI), y)
_HAL_INTFS_FILES +=hal/efuse/$(RTL871X)/HalEfuseMask8821C_USB.o
endif
ifeq ($(CONFIG_PCI_HCI), y)
_HAL_INTFS_FILES +=hal/efuse/$(RTL871X)/HalEfuseMask8821C_PCIE.o
endif

include $(src)/halmac.mk

_BTC_FILES += hal/btc/halbtc8821cwifionly.o
ifeq ($(CONFIG_BT_COEXIST), y)
_BTC_FILES += hal/btc/halbtc8821c1ant.o \
				hal/btc/halbtc8821c2ant.o
endif