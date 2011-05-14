libusbip_la_CPPFLAGS := -DUSBIDS_FILE='"@USBIDS_DIR@/usb.ids"'
libusbip_la_CFLAGS   := @EXTRA_CFLAGS@ -Wall -W -Wstrict-prototypes -std=gnu99
libusbip_la_LDFLAGS  := -version-info 0:1:0

lib_LTLIBRARIES := libusbip.la
libusbip_la_SOURCES := names.c names.h stub_driver.c stub_driver.h usbip.h \
		       usbip_common.c usbip_common.h vhci_driver.c vhci_driver.h
