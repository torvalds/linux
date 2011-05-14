SUBDIRS = lib cmd
includedir=@includedir@/usbip
include_HEADERS = lib/usbip.h lib/usbip_common.h lib/vhci_driver.h lib/stub_driver.h

if INSTALL_USBIDS
pkgdata_DATA = usb.ids
EXTRA_DIST = $(pkgdata_DATA)
endif
