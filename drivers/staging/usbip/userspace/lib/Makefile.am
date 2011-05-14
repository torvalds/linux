# AM_CFLAGS = -Wall -std=gnu99
lib_LTLIBRARIES = libusbip.la
libusbip_la_SOURCES = names.c names.h stub_driver.c stub_driver.h usbip.h usbip_common.c usbip_common.h vhci_driver.c vhci_driver.h
#libusbip_la_SOURCES = names.c names.h stub_driver.c usbip_common.c vhci_driver.c
#INCLUDES = -I../include
#AM_CPPFLAGS = -I../include -Wall -std=gnu99
libusbip_la_LDFLAGS = -version-info 0:1:0
EXTRA_CFLAGS = @EXTRA_CFLAGS@
libusbip_la_CFLAGS = -Wall -W -Wstrict-prototypes -std=gnu99 $(EXTRA_CFLAGS) -DUSBIDS_FILE='"@USBIDS_DIR@/usb.ids"'

