# $Id$

sbin_PROGRAMS = usbip usbipd usbip_bind_driver

usbip_SOURCES = usbip.c usbip_network.c usbip_network.h
usbipd_SOURCES = usbipd.c usbip_network.c usbip_network.h
usbip_bind_driver_SOURCES = bind-driver.c \
		utils.c utils.h \
		usbip_network.h usbip_network.c

#usbip_bind_driver_CFLAGS = -Wall -W -Wstrict-prototypes @PACKAGE_CFLAGS@
#usbip_bind_driver_LDADD = @PACKAGE_LIBS@

#AM_CPPFLAGS = -I../include -Wall -std=gnu99
INCLUDES = -I$(top_srcdir)/lib
LDADD = ../lib/libusbip.la @PACKAGE_LIBS@
EXTRA_CFLAGS = @EXTRA_CFLAGS@
AM_CFLAGS = -Wall -W -Wstrict-prototypes -std=gnu99 $(EXTRA_CFLAGS) @PACKAGE_CFLAGS@ -DUSBIDS_FILE='"@USBIDS_DIR@/usb.ids"'

