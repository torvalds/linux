# SPDX-License-Identifier: GPL-2.0
# All needed files would be added to _HAL_INTFS_FILES, and it would include
# hal/hal_halmac.c and all related files in directory hal/halmac/.
# Before include this makefile, be sure interface (CONFIG_*_HCI) and IC
# (CONFIG_RTL*) setting are all ready!

# Base directory
path_hm := hal/halmac

ifeq ($(CONFIG_PCI_HCI), y)
pci := y
endif
ifeq ($(CONFIG_SDIO_HCI), y)
sdio := y
endif
ifeq ($(CONFIG_USB_HCI), y)
usb := y
endif

ifeq ($(CONFIG_RTL8822B), y)
series := 88xx
ic := 8822b
endif

ifeq ($(CONFIG_RTL8822C), y)
series := 88xx
ic := 8822c
endif

ifeq ($(CONFIG_RTL8821C), y)
series := 88xx
ic := 8821c
endif

ifeq ($(CONFIG_RTL8814B), y)
series := 88xx_v1
ic := 8814b
endif

ifeq ($(CONFIG_RTL8723F), y)
series := 87xx
ic := 8723f
endif
ifeq ($(series), 88xx_v1)
d2all :=
else
d2all := y
endif

halmac-y +=		$(path_hm)/halmac_api.o
halmac-y +=		$(path_hm)/halmac_dbg.o

# Level 1 directory
path_hm_d1 := $(path_hm)/halmac_$(series)
halmac-y +=		$(path_hm_d1)/halmac_bb_rf_$(series).o \
			$(path_hm_d1)/halmac_cfg_wmac_$(series).o \
			$(path_hm_d1)/halmac_common_$(series).o \
			$(path_hm_d1)/halmac_efuse_$(series).o \
			$(path_hm_d1)/halmac_flash_$(series).o \
			$(path_hm_d1)/halmac_fw_$(series).o \
			$(path_hm_d1)/halmac_gpio_$(series).o \
			$(path_hm_d1)/halmac_init_$(series).o \
			$(path_hm_d1)/halmac_mimo_$(series).o
halmac-$(pci) += 	$(path_hm_d1)/halmac_pcie_$(series).o
halmac-$(sdio) +=	$(path_hm_d1)/halmac_sdio_$(series).o
halmac-$(usb) += 	$(path_hm_d1)/halmac_usb_$(series).o

# Level 2 directory
path_hm_d2 := $(path_hm_d1)/halmac_$(ic)
halmac-$(d2all) +=	$(path_hm_d2)/halmac_cfg_wmac_$(ic).o \
			$(path_hm_d2)/halmac_common_$(ic).o

halmac-y +=		$(path_hm_d2)/halmac_gpio_$(ic).o \
			$(path_hm_d2)/halmac_init_$(ic).o \
			$(path_hm_d2)/halmac_phy_$(ic).o \
			$(path_hm_d2)/halmac_pwr_seq_$(ic).o
halmac-$(pci) += 	$(path_hm_d2)/halmac_pcie_$(ic).o
halmac-$(sdio) +=	$(path_hm_d2)/halmac_sdio_$(ic).o
halmac-$(usb) += 	$(path_hm_d2)/halmac_usb_$(ic).o

_HAL_INTFS_FILES +=	hal/hal_halmac.o
_HAL_INTFS_FILES +=	$(halmac-y)
