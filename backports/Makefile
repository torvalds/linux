ifeq ($(CONFIG_BACKPORT_INTEGRATE),)
# Since 2.6.21, try-run is available, but cc-disable-warning
# was only added later, so we add it here ourselves:
backport-cc-disable-warning = $(call try-run,\
	$(CC) $(KBUILD_CPPFLAGS) $(KBUILD_CFLAGS) -W$(strip $(1)) -c -x c /dev/null -o "$$TMP",-Wno-$(strip $(1)))

NOSTDINC_FLAGS := \
	-I$(M)/backport-include/ \
	-I$(M)/backport-include/uapi \
	-I$(M)/include/ \
	-I$(M)/include/uapi \
	-include $(M)/backport-include/backport/backport.h \
	$(call backport-cc-disable-warning, unused-but-set-variable) \
	-DCONFIG_BACKPORT_VERSION=\"$(BACKPORTS_VERSION)\" \
	-DCONFIG_BACKPORT_KERNEL_VERSION=\"$(BACKPORTED_KERNEL_VERSION)\" \
	-DCONFIG_BACKPORT_KERNEL_NAME=\"$(BACKPORTED_KERNEL_NAME)\" \
	$(BACKPORTS_GIT_TRACKER_DEF)

export backport_srctree = $(M)
else
export BACKPORT_DIR = $(srctree)/backports
export backport_srctree = $(BACKPORT_DIR)
NOSTDINC_FLAGS := \
	-I$(BACKPORT_DIR)/backport-include/ \
	-I$(BACKPORT_DIR)/backport-include/uapi \
	-I$(BACKPORT_DIR)/include/ \
	-I$(BACKPORT_DIR)/include/uapi \
	-include $(BACKPORT_DIR)/backport-include/backport/backport.h
endif


obj-y += compat/

obj-$(CONFIG_BACKPORT_CFG80211) += net/wireless/
obj-$(CONFIG_BACKPORT_MAC80211) += net/mac80211/
obj-$(CONFIG_BACKPORT_WLAN) += drivers/net/wireless/
obj-$(CONFIG_BACKPORT_BT) += net/bluetooth/
obj-$(CONFIG_BACKPORT_BT) += drivers/bluetooth/
obj-$(CONFIG_BACKPORT_SSB) += drivers/ssb/
obj-$(CONFIG_BACKPORT_BCMA) += drivers/bcma/
obj-$(CONFIG_BACKPORT_ETHERNET) += drivers/net/ethernet/
obj-$(CONFIG_BACKPORT_USB_NET_RNDIS_WLAN) += drivers/net/usb/
obj-$(CONFIG_BACKPORT_NFC) += net/nfc/
obj-$(CONFIG_BACKPORT_NFC) += drivers/nfc/
obj-$(CONFIG_BACKPORT_MEDIA_SUPPORT) += drivers/media/

obj-$(CONFIG_BACKPORT_6LOWPAN) += net/6lowpan/
obj-$(CONFIG_BACKPORT_IEEE802154) += net/ieee802154/
obj-$(CONFIG_BACKPORT_BT) += net/ieee802154/
obj-$(CONFIG_BACKPORT_MAC802154) += net/mac802154/
obj-$(CONFIG_BACKPORT_IEEE802154) += drivers/net/ieee802154/

obj-$(CONFIG_BACKPORT_USB_WDM) += drivers/usb/class/
obj-$(CONFIG_BACKPORT_USB_USBNET) += drivers/net/usb/
obj-y += drivers/realtek/
