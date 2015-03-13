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
	-DCPTCFG_VERSION=\"$(BACKPORTS_VERSION)\" \
	-DCPTCFG_KERNEL_VERSION=\"$(BACKPORTED_KERNEL_VERSION)\" \
	-DCPTCFG_KERNEL_NAME=\"$(BACKPORTED_KERNEL_NAME)\" \
	$(BACKPORTS_GIT_TRACKER_DEF) \
	$(CFLAGS)

export backport_srctree = $(M)
else
export BACKPORT_DIR = backports/
export backport_srctree = $(BACKPORT_DIR)
NOSTDINC_FLAGS := \
	-I$(BACKPORT_DIR)/backport-include/ \
	-I$(BACKPORT_DIR)/backport-include/uapi \
	-I$(BACKPORT_DIR)/include/ \
	-I$(BACKPORT_DIR)/include/uapi \
	-include $(BACKPORT_DIR)/backport-include/backport/backport.h \
	$(CFLAGS)
endif


obj-y += compat/

obj-$(CPTCFG_CFG80211) += net/wireless/
obj-$(CPTCFG_MAC80211) += net/mac80211/
obj-$(CPTCFG_WLAN) += drivers/net/wireless/
obj-$(CPTCFG_BT) += net/bluetooth/
obj-$(CPTCFG_BT) += drivers/bluetooth/
obj-$(CPTCFG_SSB) += drivers/ssb/
obj-$(CPTCFG_BCMA) += drivers/bcma/
obj-$(CPTCFG_ETHERNET) += drivers/net/ethernet/
obj-$(CPTCFG_USB_NET_RNDIS_WLAN) += drivers/net/usb/
obj-$(CPTCFG_NFC) += net/nfc/
obj-$(CPTCFG_NFC) += drivers/nfc/
obj-$(CPTCFG_MEDIA_SUPPORT) += drivers/media/

obj-$(CPTCFG_6LOWPAN) += net/6lowpan/
obj-$(CPTCFG_IEEE802154) += net/ieee802154/
obj-$(CPTCFG_BT) += net/ieee802154/
obj-$(CPTCFG_MAC802154) += net/mac802154/
obj-$(CPTCFG_IEEE802154) += drivers/net/ieee802154/

obj-$(CPTCFG_USB_WDM) += drivers/usb/class/
obj-$(CPTCFG_USB_USBNET) += drivers/net/usb/
