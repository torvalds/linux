# SPDX-License-Identifier: GPL-2.0
# Family-Level Interface Shim (FLIS)
obj-$(subst m,y,$(CONFIG_PINCTRL_MERRIFIELD)) += platform_mrfld_pinctrl.o
# SDHCI Devices
obj-$(subst m,y,$(CONFIG_MMC_SDHCI_PCI)) += platform_mrfld_sd.o
# WiFi + BT
obj-$(subst m,y,$(CONFIG_BRCMFMAC_SDIO)) += platform_bcm43xx.o
obj-$(subst m,y,$(CONFIG_BT_HCIUART_BCM)) += platform_bt.o
# SPI Devices
obj-$(subst m,y,$(CONFIG_SPI_SPIDEV)) += platform_mrfld_spidev.o
# I2C Devices
obj-$(subst m,y,$(CONFIG_SENSORS_EMC1403)) += platform_emc1403.o
obj-$(subst m,y,$(CONFIG_SENSORS_LIS3LV02D)) += platform_lis331.o
obj-$(subst m,y,$(CONFIG_MPU3050_I2C)) += platform_mpu3050.o
obj-$(subst m,y,$(CONFIG_INPUT_BMA150)) += platform_bma023.o
obj-$(subst m,y,$(CONFIG_DRM_MEDFIELD)) += platform_tc35876x.o
# I2C GPIO Expanders
obj-$(subst m,y,$(CONFIG_GPIO_PCA953X)) += platform_max7315.o
obj-$(subst m,y,$(CONFIG_GPIO_PCA953X)) += platform_pcal9555a.o
obj-$(subst m,y,$(CONFIG_GPIO_PCA953X)) += platform_tca6416.o
# MISC Devices
obj-$(subst m,y,$(CONFIG_KEYBOARD_GPIO)) += platform_gpio_keys.o
obj-$(subst m,y,$(CONFIG_RTC_DRV_CMOS)) += platform_mrfld_rtc.o
