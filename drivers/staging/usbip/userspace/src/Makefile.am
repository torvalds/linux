AM_CPPFLAGS := -I$(top_srcdir)/libsrc -DUSBIDS_FILE='"@USBIDS_DIR@/usb.ids"'
AM_CFLAGS   := @EXTRA_CFLAGS@ @PACKAGE_CFLAGS@
LDADD       := $(top_srcdir)/libsrc/libusbip.la @PACKAGE_LIBS@

sbin_PROGRAMS := usbip usbipd usbip_bind_driver

usbip_SOURCES := usbip.c usbip_network.c usbip_network.h
usbipd_SOURCES := usbipd.c usbip_network.c usbip_network.h
usbip_bind_driver_SOURCES := bind-driver.c utils.c utils.h \
			     usbip_network.h usbip_network.c
