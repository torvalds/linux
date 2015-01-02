##########################################
########## Amlogic Drivers ###############
##########################################

##########################################
## The order of directories matter.
## Do not change.
##########################################


obj-y +=early_init/
obj-$(CONFIG_EFUSE)+=efuse/

###For Display, this is for quick logo show##
obj-$(CONFIG_AM_DISPLAY_MODULE) += display/

obj-$(CONFIG_POST_PROCESS_MANAGER) += ppmgr/

obj-$(CONFIG_V4L_AMLOGIC_VIDEO) += video_dev/
obj-$(CONFIG_V4L_AMLOGIC_VIDEO2) += video_dev/
obj-$(CONFIG_AMLOGIC_IONVIDEO) += ionvideo/

obj-$(CONFIG_DEINTERLACE) += deinterlace/

##### Basic drivers#####

obj-y += watchdog/
obj-y += secure/
obj-y += securitykeys/
obj-y += keymanage/

obj-y += uart/
obj-y += of/
obj-y += usb/
obj-y += cpufreq/
obj-$(CONFIG_GPIO_AMLOGIC) += gpio/
obj-y += pinctrl/
obj-y += ethernet/

obj-$(CONFIG_AM_CANVAS) += canvas/

obj-y += amports/

# ve&cm
obj-y += amvecm/

obj-$(CONFIG_I2C) += i2c/

#obj-y += extern_io/

obj-$(CONFIG_AM_INPUT)		+= input/

ifneq ($(wildcard $(srctree)/../hardware/amlogic/touch),)
obj-$(CONFIG_AML_TOUCH_ALGORITHM_SUPPORT) += ../../../hardware/amlogic/touch/
endif
obj-$(CONFIG_AMLOGIC_SPI_NOR) += spi_nor/

#obj-$(CONFIG_CARDREADER) += cardreader/
obj-$(CONFIG_MMC_AML) +=mmc/

obj-$(CONFIG_AML_VFM) += vfm/

obj-$(CONFIG_AML_AUDIO_DSP) += audiodsp/

obj-$(CONFIG_AML_RTC)+= rtc/

obj-y += power/

ifneq ($(wildcard $(srctree)/../hardware/amlogic/pmu),)
    obj-$(CONFIG_AML_PMU_ALGORITHM_SUPPORT) += ../../../hardware/amlogic/pmu/
endif

obj-y += tvin_v4l2/
ifneq ($(wildcard $(srctree)/../hardware/tvin),)
	obj-$(CONFIG_TVIN) += ../../../hardware/tvin/
endif

obj-$(CONFIG_AML_EXT_HDMIIN) += ext_hdmiin/

obj-y += thermal/


#####Sound system is later init in kernel default..######################
obj-$(CONFIG_AMAUDIO) += amaudio/
obj-$(CONFIG_AMAUDIO2) += amaudio2/
obj-y += mhl/
obj-y += hdmi/

obj-$(CONFIG_AM_DVB) += dvb_tv/
obj-$(CONFIG_AM_SMARTCARD) += smartcard/

obj-$(CONFIG_AM_WIFI) += wifi/
ifneq ($(wildcard $(srctree)/../hardware/wifi),)
	obj-$(CONFIG_SDIO_DHD_CDC_WIFI_40181_MODULE) += ../../../hardware/wifi/broadcom/drivers/ap6xxx/broadcm_40181/
	obj-$(CONFIG_BCMDHD) += ../../../hardware/wifi/broadcom/drivers/usi/bcmdhd/
	obj-$(CONFIG_RTL8188EU) += ../../../hardware/wifi/realtek/drivers/8188eu/rtl8xxx_EU/
	obj-$(CONFIG_RTL8188EU_MP) += ../../../hardware/wifi/realtek/drivers/8188eu/rtl8xxx_EU_MP/
	obj-$(CONFIG_RTL8192CU) += ../../../hardware/wifi/realtek/drivers/8192cu/rtl8xxx_CU/
	obj-$(CONFIG_RTL8192DU) += ../../../hardware/wifi/realtek/drivers/8192du/rtl8xxx_DU/
	obj-$(CONFIG_RTL8192EU) += ../../../hardware/wifi/realtek/drivers/8192eu/rtl8192EU/
	obj-$(CONFIG_RTL8189ES) += ../../../hardware/wifi/realtek/drivers/8189es/rtl8189ES/	
	obj-$(CONFIG_RTL8723BS) += ../../../hardware/wifi/realtek/drivers/8723bs/rtl8723BS/	
	obj-$(CONFIG_RTL8723AU) += ../../../hardware/wifi/realtek/drivers/8723au/rtl8723AU/
	obj-$(CONFIG_RTL8821AU) += ../../../hardware/wifi/realtek/drivers/8811au/rtl8811AU/
	obj-$(CONFIG_RTL8812AU) += ../../../hardware/wifi/realtek/drivers/8812au/rtl8812AU/
endif
obj-y += bluetooth/
obj-$(CONFIG_MESON_NFC) += nfc/

obj-$(CONFIG_VIDEO_AMLOGIC_CAPTURE) += camera/

obj-$(CONFIG_AMLOGIC_MIPI) += mipi/

obj-$(CONFIG_D2D3_PROCESS) += d2d3/

obj-$(CONFIG_AML_REG_DEBUG) += regd/

obj-$(CONFIG_AML_WATCHDOG) += watchdog/

ifneq ($(wildcard $(srctree)/../hardware/arm/gpu),)
	obj-$(CONFIG_UMP) += ../../../hardware/arm/gpu/ump/
	obj-$(CONFIG_MALI400) += ../../../hardware/arm/gpu/mali/
endif

obj-$(CONFIG_AMLOGIC_ION) += ion_dev/

obj-y += crypto/

#obj-$(CONFIG_MESON_TRUSTZONE) += trustzone/
obj-$(CONFIG_MESON_TRUSTZONE) += secure_monitor/
obj-y += spi/

obj-$(CONFIG_MESON_PWM) += pwm/
