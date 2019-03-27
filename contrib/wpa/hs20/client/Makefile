all: hs20-osu-client

ifndef CC
CC=gcc
endif

ifndef LDO
LDO=$(CC)
endif

Q=@
E=echo
ifeq ($(V), 1)
Q=
E=true
endif

ifndef CFLAGS
CFLAGS = -MMD -O2 -Wall -g
endif

CFLAGS += -I../../src/utils
CFLAGS += -I../../src/common
CFLAGS += -I../../src

ifndef CONFIG_NO_BROWSER
ifndef CONFIG_BROWSER_SYSTEM
GTKCFLAGS := $(shell pkg-config --cflags gtk+-3.0 webkitgtk-3.0)
GTKLIBS := $(shell pkg-config --libs gtk+-3.0 webkitgtk-3.0)
CFLAGS += $(GTKCFLAGS)
LIBS += $(GTKLIBS)
endif
endif

OBJS=spp_client.o
OBJS += oma_dm_client.o
OBJS += osu_client.o
OBJS += est.o
OBJS += ../../src/utils/xml-utils.o
CFLAGS += -DCONFIG_CTRL_IFACE
CFLAGS += -DCONFIG_CTRL_IFACE_UNIX
OBJS += ../../src/common/wpa_ctrl.o ../../src/common/wpa_helpers.o
ifdef CONFIG_NO_BROWSER
CFLAGS += -DCONFIG_NO_BROWSER
else
ifdef CONFIG_BROWSER_SYSTEM
OBJS += ../../src/utils/eloop.o
OBJS += ../../src/utils/wpabuf.o
OBJS += ../../src/wps/httpread.o
OBJS += ../../src/wps/http_server.o
OBJS += ../../src/utils/browser-system.o
else
OBJS += ../../src/utils/browser.o
endif
endif
OBJS += ../../src/utils/xml_libxml2.o
OBJS += ../../src/utils/http_curl.o
OBJS += ../../src/utils/base64.o
OBJS += ../../src/utils/os_unix.o
CFLAGS += -DCONFIG_DEBUG_FILE
OBJS += ../../src/utils/wpa_debug.o
OBJS += ../../src/utils/common.o
OBJS += ../../src/crypto/crypto_internal.o
OBJS += ../../src/crypto/md5-internal.o
OBJS += ../../src/crypto/sha1-internal.o
OBJS += ../../src/crypto/sha256-internal.o

CFLAGS += $(shell xml2-config --cflags)
LIBS += $(shell xml2-config --libs)

# Allow static/custom linking of libcurl.
ifdef CUST_CURL_LINKAGE
LIBS += ${CUST_CURL_LINKAGE}
else
LIBS += -lcurl
endif

CFLAGS += -DEAP_TLS_OPENSSL
OBJS += ../../src/crypto/tls_openssl_ocsp.o
LIBS += -lssl -lcrypto

hs20-osu-client: $(OBJS)
	$(Q)$(LDO) $(LDFLAGS) -o hs20-osu-client $(OBJS) $(LIBS)
	@$(E) "  LD " $@

%.o: %.c
	$(Q)$(CC) -c -o $@ $(CFLAGS) $<
	@$(E) "  CC " $<

clean:
	rm -f core *~ *.o *.d hs20-osu-client
	rm -f ../../src/utils/*.o
	rm -f ../../src/utils/*.d
	rm -f ../../src/common/*.o
	rm -f ../../src/common/*.d
	rm -f ../../src/crypto/*.o
	rm -f ../../src/crypto/*.d
	rm -f ../../src/wps/*.o
	rm -f ../../src/wps/*.d

-include $(OBJS:%.o=%.d)
