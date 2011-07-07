libusbip_la_CPPFLAGS = -DUSBIDS_FILE='"@USBIDS_DIR@/usb.ids"'
libusbip_la_CFLAGS   = @EXTRA_CFLAGS@
libusbip_la_LDFLAGS  = -version-info @LIBUSBIP_VERSION@

lib_LTLIBRARIES := libusbip.la
libusbip_la_SOURCES := names.c names.h usbip_host_driver.c usbip_host_driver.h \
		       usbip_common.c usbip_common.h vhci_driver.c vhci_driver.h
