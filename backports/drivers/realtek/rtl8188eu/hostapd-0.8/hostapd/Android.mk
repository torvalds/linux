LOCAL_PATH := $(call my-dir)

WPA_BUILD_HOSTAPD := false
ifneq ($(TARGET_SIMULATOR),true)
  ifneq ($(BOARD_HOSTAPD_DRIVER),)
    WPA_BUILD_HOSTAPD := true
    CONFIG_DRIVER_$(BOARD_HOSTAPD_DRIVER) := y
  endif
endif

include $(LOCAL_PATH)/.config

# To ignore possible wrong network configurations
L_CFLAGS = -DWPA_IGNORE_CONFIG_ERRORS

# To force sizeof(enum) = 4
ifeq ($(TARGET_ARCH),arm)
L_CFLAGS += -mabi=aapcs-linux
endif

# To allow non-ASCII characters in SSID
L_CFLAGS += -DWPA_UNICODE_SSID

# OpenSSL is configured without engines on Android
L_CFLAGS += -DOPENSSL_NO_ENGINE

INCLUDES = $(LOCAL_PATH)
INCLUDES += $(LOCAL_PATH)/src
INCLUDES += $(LOCAL_PATH)/src/utils
INCLUDES += external/openssl/include
INCLUDES += frameworks/base/cmds/keystore
ifdef CONFIG_DRIVER_NL80211
INCLUDES += external/libnl_2/include
endif


ifndef CONFIG_OS
ifdef CONFIG_NATIVE_WINDOWS
CONFIG_OS=win32
else
CONFIG_OS=unix
endif
endif

ifeq ($(CONFIG_OS), internal)
L_CFLAGS += -DOS_NO_C_LIB_DEFINES
endif

ifdef CONFIG_NATIVE_WINDOWS
L_CFLAGS += -DCONFIG_NATIVE_WINDOWS
LIBS += -lws2_32
endif

OBJS = main.c
OBJS += config_file.c

OBJS += src/ap/hostapd.c
OBJS += src/ap/wpa_auth_glue.c
OBJS += src/ap/drv_callbacks.c
OBJS += src/ap/ap_drv_ops.c
OBJS += src/ap/utils.c
OBJS += src/ap/authsrv.c
OBJS += src/ap/ieee802_1x.c
OBJS += src/ap/ap_config.c
OBJS += src/ap/ieee802_11_auth.c
OBJS += src/ap/sta_info.c
OBJS += src/ap/wpa_auth.c
OBJS += src/ap/tkip_countermeasures.c
OBJS += src/ap/ap_mlme.c
OBJS += src/ap/wpa_auth_ie.c
OBJS += src/ap/preauth_auth.c
OBJS += src/ap/pmksa_cache_auth.c
OBJS_d =
OBJS_p =
LIBS =
LIBS_c =
HOBJS =
LIBS_h =

NEED_RC4=y
NEED_AES=y
NEED_MD5=y
NEED_SHA1=y

OBJS += src/drivers/drivers.c
L_CFLAGS += -DHOSTAPD

ifdef CONFIG_WPA_TRACE
L_CFLAGS += -DWPA_TRACE
OBJS += src/utils/trace.c
HOBJS += src/utils/trace.c
LDFLAGS += -rdynamic
L_CFLAGS += -funwind-tables
ifdef CONFIG_WPA_TRACE_BFD
L_CFLAGS += -DWPA_TRACE_BFD
LIBS += -lbfd
LIBS_c += -lbfd
LIBS_h += -lbfd
endif
endif

OBJS += src/utils/eloop.c
OBJS += src/utils/common.c
OBJS += src/utils/wpa_debug.c
OBJS += src/utils/wpabuf.c
OBJS += src/utils/os_$(CONFIG_OS).c
OBJS += src/utils/ip_addr.c

OBJS += src/common/ieee802_11_common.c
OBJS += src/common/wpa_common.c

OBJS += src/eapol_auth/eapol_auth_sm.c


ifndef CONFIG_NO_DUMP_STATE
# define HOSTAPD_DUMP_STATE to include SIGUSR1 handler for dumping state to
# a file (undefine it, if you want to save in binary size)
L_CFLAGS += -DHOSTAPD_DUMP_STATE
OBJS += dump_state.c
OBJS += src/eapol_auth/eapol_auth_dump.c
endif

ifdef CONFIG_NO_RADIUS
L_CFLAGS += -DCONFIG_NO_RADIUS
CONFIG_NO_ACCOUNTING=y
else
OBJS += src/radius/radius.c
OBJS += src/radius/radius_client.c
endif

ifdef CONFIG_NO_ACCOUNTING
L_CFLAGS += -DCONFIG_NO_ACCOUNTING
else
OBJS += src/ap/accounting.c
endif

ifdef CONFIG_NO_VLAN
L_CFLAGS += -DCONFIG_NO_VLAN
else
OBJS += src/ap/vlan_init.c
endif

ifdef CONFIG_NO_CTRL_IFACE
L_CFLAGS += -DCONFIG_NO_CTRL_IFACE
else
OBJS += ctrl_iface.c
OBJS += src/ap/ctrl_iface_ap.c
endif

OBJS += src/crypto/md5.c

L_CFLAGS += -DCONFIG_CTRL_IFACE -DCONFIG_CTRL_IFACE_UNIX

ifdef CONFIG_IAPP
L_CFLAGS += -DCONFIG_IAPP
OBJS += src/ap/iapp.c
endif

ifdef CONFIG_RSN_PREAUTH
L_CFLAGS += -DCONFIG_RSN_PREAUTH
CONFIG_L2_PACKET=y
endif

ifdef CONFIG_PEERKEY
L_CFLAGS += -DCONFIG_PEERKEY
OBJS += src/ap/peerkey_auth.c
endif

ifdef CONFIG_IEEE80211W
L_CFLAGS += -DCONFIG_IEEE80211W
NEED_SHA256=y
NEED_AES_OMAC1=y
endif

ifdef CONFIG_IEEE80211R
L_CFLAGS += -DCONFIG_IEEE80211R
OBJS += src/ap/wpa_auth_ft.c
NEED_SHA256=y
NEED_AES_OMAC1=y
NEED_AES_UNWRAP=y
endif

ifdef CONFIG_IEEE80211N
L_CFLAGS += -DCONFIG_IEEE80211N
endif

include $(LOCAL_PATH)/src/drivers/drivers.mk

OBJS += $(DRV_AP_OBJS)
L_CFLAGS += $(DRV_AP_CFLAGS)
LDFLAGS += $(DRV_AP_LDFLAGS)
LIBS += $(DRV_AP_LIBS)

ifdef CONFIG_L2_PACKET
ifdef CONFIG_DNET_PCAP
ifdef CONFIG_L2_FREEBSD
LIBS += -lpcap
OBJS += src/l2_packet/l2_packet_freebsd.c
else
LIBS += -ldnet -lpcap
OBJS += src/l2_packet/l2_packet_pcap.c
endif
else
OBJS += src/l2_packet/l2_packet_linux.c
endif
else
OBJS += src/l2_packet/l2_packet_none.c
endif


ifdef CONFIG_EAP_MD5
L_CFLAGS += -DEAP_SERVER_MD5
OBJS += src/eap_server/eap_server_md5.c
CHAP=y
endif

ifdef CONFIG_EAP_TLS
L_CFLAGS += -DEAP_SERVER_TLS
OBJS += src/eap_server/eap_server_tls.c
TLS_FUNCS=y
endif

ifdef CONFIG_EAP_PEAP
L_CFLAGS += -DEAP_SERVER_PEAP
OBJS += src/eap_server/eap_server_peap.c
OBJS += src/eap_common/eap_peap_common.c
TLS_FUNCS=y
CONFIG_EAP_MSCHAPV2=y
endif

ifdef CONFIG_EAP_TTLS
L_CFLAGS += -DEAP_SERVER_TTLS
OBJS += src/eap_server/eap_server_ttls.c
TLS_FUNCS=y
CHAP=y
endif

ifdef CONFIG_EAP_MSCHAPV2
L_CFLAGS += -DEAP_SERVER_MSCHAPV2
OBJS += src/eap_server/eap_server_mschapv2.c
MS_FUNCS=y
endif

ifdef CONFIG_EAP_GTC
L_CFLAGS += -DEAP_SERVER_GTC
OBJS += src/eap_server/eap_server_gtc.c
endif

ifdef CONFIG_EAP_SIM
L_CFLAGS += -DEAP_SERVER_SIM
OBJS += src/eap_server/eap_server_sim.c
CONFIG_EAP_SIM_COMMON=y
NEED_AES_CBC=y
endif

ifdef CONFIG_EAP_AKA
L_CFLAGS += -DEAP_SERVER_AKA
OBJS += src/eap_server/eap_server_aka.c
CONFIG_EAP_SIM_COMMON=y
NEED_SHA256=y
NEED_AES_CBC=y
endif

ifdef CONFIG_EAP_AKA_PRIME
L_CFLAGS += -DEAP_SERVER_AKA_PRIME
endif

ifdef CONFIG_EAP_SIM_COMMON
OBJS += src/eap_common/eap_sim_common.c
# Example EAP-SIM/AKA interface for GSM/UMTS authentication. This can be
# replaced with another file implementating the interface specified in
# eap_sim_db.h.
OBJS += src/eap_server/eap_sim_db.c
NEED_FIPS186_2_PRF=y
endif

ifdef CONFIG_EAP_PAX
L_CFLAGS += -DEAP_SERVER_PAX
OBJS += src/eap_server/eap_server_pax.c src/eap_common/eap_pax_common.c
endif

ifdef CONFIG_EAP_PSK
L_CFLAGS += -DEAP_SERVER_PSK
OBJS += src/eap_server/eap_server_psk.c src/eap_common/eap_psk_common.c
NEED_AES_OMAC1=y
NEED_AES_ENCBLOCK=y
NEED_AES_EAX=y
endif

ifdef CONFIG_EAP_SAKE
L_CFLAGS += -DEAP_SERVER_SAKE
OBJS += src/eap_server/eap_server_sake.c src/eap_common/eap_sake_common.c
endif

ifdef CONFIG_EAP_GPSK
L_CFLAGS += -DEAP_SERVER_GPSK
OBJS += src/eap_server/eap_server_gpsk.c src/eap_common/eap_gpsk_common.c
ifdef CONFIG_EAP_GPSK_SHA256
L_CFLAGS += -DEAP_SERVER_GPSK_SHA256
endif
NEED_SHA256=y
NEED_AES_OMAC1=y
endif

ifdef CONFIG_EAP_PWD
L_CFLAGS += -DEAP_SERVER_PWD
OBJS += src/eap_server/eap_server_pwd.c src/eap_common/eap_pwd_common.c
NEED_SHA256=y
endif

ifdef CONFIG_EAP_VENDOR_TEST
L_CFLAGS += -DEAP_SERVER_VENDOR_TEST
OBJS += src/eap_server/eap_server_vendor_test.c
endif

ifdef CONFIG_EAP_FAST
L_CFLAGS += -DEAP_SERVER_FAST
OBJS += src/eap_server/eap_server_fast.c
OBJS += src/eap_common/eap_fast_common.c
TLS_FUNCS=y
NEED_T_PRF=y
NEED_AES_UNWRAP=y
endif

ifdef CONFIG_WPS
ifdef CONFIG_WPS2
L_CFLAGS += -DCONFIG_WPS2
endif

L_CFLAGS += -DCONFIG_WPS -DEAP_SERVER_WSC
OBJS += src/utils/uuid.c
OBJS += src/ap/wps_hostapd.c
OBJS += src/eap_server/eap_server_wsc.c src/eap_common/eap_wsc_common.c
OBJS += src/wps/wps.c
OBJS += src/wps/wps_common.c
OBJS += src/wps/wps_attr_parse.c
OBJS += src/wps/wps_attr_build.c
OBJS += src/wps/wps_attr_process.c
OBJS += src/wps/wps_dev_attr.c
OBJS += src/wps/wps_enrollee.c
OBJS += src/wps/wps_registrar.c
NEED_DH_GROUPS=y
NEED_SHA256=y
NEED_BASE64=y
NEED_AES_CBC=y
NEED_MODEXP=y
CONFIG_EAP=y

ifdef CONFIG_WPS_UFD
L_CFLAGS += -DCONFIG_WPS_UFD
OBJS += src/wps/wps_ufd.c
NEED_WPS_OOB=y
endif

ifdef CONFIG_WPS_NFC
L_CFLAGS += -DCONFIG_WPS_NFC
OBJS += src/wps/ndef.c
OBJS += src/wps/wps_nfc.c
NEED_WPS_OOB=y
ifdef CONFIG_WPS_NFC_PN531
PN531_PATH ?= /usr/local/src/nfc
L_CFLAGS += -DCONFIG_WPS_NFC_PN531
L_CFLAGS += -I${PN531_PATH}/inc
OBJS += src/wps/wps_nfc_pn531.c
LIBS += ${PN531_PATH}/lib/wpsnfc.dll
LIBS += ${PN531_PATH}/lib/libnfc_mapping_pn53x.dll
endif
endif

ifdef NEED_WPS_OOB
L_CFLAGS += -DCONFIG_WPS_OOB
endif

ifdef CONFIG_WPS_UPNP
L_CFLAGS += -DCONFIG_WPS_UPNP
OBJS += src/wps/wps_upnp.c
OBJS += src/wps/wps_upnp_ssdp.c
OBJS += src/wps/wps_upnp_web.c
OBJS += src/wps/wps_upnp_event.c
OBJS += src/wps/wps_upnp_ap.c
OBJS += src/wps/upnp_xml.c
OBJS += src/wps/httpread.c
OBJS += src/wps/http_client.c
OBJS += src/wps/http_server.c
endif

ifdef CONFIG_WPS_STRICT
L_CFLAGS += -DCONFIG_WPS_STRICT
OBJS += src/wps/wps_validate.c
endif

ifdef CONFIG_WPS_TESTING
L_CFLAGS += -DCONFIG_WPS_TESTING
endif

endif

ifdef CONFIG_EAP_IKEV2
L_CFLAGS += -DEAP_SERVER_IKEV2
OBJS += src/eap_server/eap_server_ikev2.c src/eap_server/ikev2.c
OBJS += src/eap_common/eap_ikev2_common.c src/eap_common/ikev2_common.c
NEED_DH_GROUPS=y
NEED_DH_GROUPS_ALL=y
NEED_MODEXP=y
NEED_CIPHER=y
endif

ifdef CONFIG_EAP_TNC
L_CFLAGS += -DEAP_SERVER_TNC
OBJS += src/eap_server/eap_server_tnc.c
OBJS += src/eap_server/tncs.c
NEED_BASE64=y
ifndef CONFIG_DRIVER_BSD
LIBS += -ldl
endif
endif

# Basic EAP functionality is needed for EAPOL
OBJS += eap_register.c
OBJS += src/eap_server/eap_server.c
OBJS += src/eap_common/eap_common.c
OBJS += src/eap_server/eap_server_methods.c
OBJS += src/eap_server/eap_server_identity.c
L_CFLAGS += -DEAP_SERVER_IDENTITY

ifdef CONFIG_EAP
L_CFLAGS += -DEAP_SERVER
endif

ifdef CONFIG_PKCS12
L_CFLAGS += -DPKCS12_FUNCS
endif

ifdef MS_FUNCS
OBJS += src/crypto/ms_funcs.c
NEED_DES=y
NEED_MD4=y
endif

ifdef CHAP
OBJS += src/eap_common/chap.c
endif

ifdef TLS_FUNCS
NEED_DES=y
# Shared TLS functions (needed for EAP_TLS, EAP_PEAP, and EAP_TTLS)
L_CFLAGS += -DEAP_TLS_FUNCS
OBJS += src/eap_server/eap_server_tls_common.c
NEED_TLS_PRF=y
endif

ifndef CONFIG_TLS
CONFIG_TLS=openssl
endif

ifeq ($(CONFIG_TLS), openssl)
ifdef TLS_FUNCS
OBJS += src/crypto/tls_openssl.c
LIBS += -lssl
endif
OBJS += src/crypto/crypto_openssl.c
HOBJS += src/crypto/crypto_openssl.c
ifdef NEED_FIPS186_2_PRF
OBJS += src/crypto/fips_prf_openssl.c
endif
LIBS += -lcrypto
LIBS_h += -lcrypto
endif

ifeq ($(CONFIG_TLS), gnutls)
ifdef TLS_FUNCS
OBJS += src/crypto/tls_gnutls.c
LIBS += -lgnutls -lgpg-error
ifdef CONFIG_GNUTLS_EXTRA
L_CFLAGS += -DCONFIG_GNUTLS_EXTRA
LIBS += -lgnutls-extra
endif
endif
OBJS += src/crypto/crypto_gnutls.c
HOBJS += src/crypto/crypto_gnutls.c
ifdef NEED_FIPS186_2_PRF
OBJS += src/crypto/fips_prf_gnutls.c
endif
LIBS += -lgcrypt
LIBS_h += -lgcrypt
CONFIG_INTERNAL_SHA256=y
CONFIG_INTERNAL_RC4=y
CONFIG_INTERNAL_DH_GROUP5=y
endif

ifeq ($(CONFIG_TLS), schannel)
ifdef TLS_FUNCS
OBJS += src/crypto/tls_schannel.c
endif
OBJS += src/crypto/crypto_cryptoapi.c
OBJS_p += src/crypto/crypto_cryptoapi.c
CONFIG_INTERNAL_SHA256=y
CONFIG_INTERNAL_RC4=y
CONFIG_INTERNAL_DH_GROUP5=y
endif

ifeq ($(CONFIG_TLS), nss)
ifdef TLS_FUNCS
OBJS += src/crypto/tls_nss.c
LIBS += -lssl3
endif
OBJS += src/crypto/crypto_nss.c
ifdef NEED_FIPS186_2_PRF
OBJS += src/crypto/fips_prf_nss.c
endif
LIBS += -lnss3
LIBS_h += -lnss3
CONFIG_INTERNAL_MD4=y
CONFIG_INTERNAL_DH_GROUP5=y
endif

ifeq ($(CONFIG_TLS), internal)
ifndef CONFIG_CRYPTO
CONFIG_CRYPTO=internal
endif
ifdef TLS_FUNCS
OBJS += src/crypto/crypto_internal-rsa.c
OBJS += src/crypto/tls_internal.c
OBJS += src/tls/tlsv1_common.c
OBJS += src/tls/tlsv1_record.c
OBJS += src/tls/tlsv1_cred.c
OBJS += src/tls/tlsv1_server.c
OBJS += src/tls/tlsv1_server_write.c
OBJS += src/tls/tlsv1_server_read.c
OBJS += src/tls/asn1.c
OBJS += src/tls/rsa.c
OBJS += src/tls/x509v3.c
OBJS += src/tls/pkcs1.c
OBJS += src/tls/pkcs5.c
OBJS += src/tls/pkcs8.c
NEED_SHA256=y
NEED_BASE64=y
NEED_TLS_PRF=y
NEED_MODEXP=y
NEED_CIPHER=y
L_CFLAGS += -DCONFIG_TLS_INTERNAL
L_CFLAGS += -DCONFIG_TLS_INTERNAL_SERVER
endif
ifdef NEED_CIPHER
NEED_DES=y
OBJS += src/crypto/crypto_internal-cipher.c
endif
ifdef NEED_MODEXP
OBJS += src/crypto/crypto_internal-modexp.c
OBJS += src/tls/bignum.c
endif
ifeq ($(CONFIG_CRYPTO), libtomcrypt)
OBJS += src/crypto/crypto_libtomcrypt.c
LIBS += -ltomcrypt -ltfm
LIBS_h += -ltomcrypt -ltfm
CONFIG_INTERNAL_SHA256=y
CONFIG_INTERNAL_RC4=y
CONFIG_INTERNAL_DH_GROUP5=y
endif
ifeq ($(CONFIG_CRYPTO), internal)
OBJS += src/crypto/crypto_internal.c
NEED_AES_DEC=y
L_CFLAGS += -DCONFIG_CRYPTO_INTERNAL
ifdef CONFIG_INTERNAL_LIBTOMMATH
L_CFLAGS += -DCONFIG_INTERNAL_LIBTOMMATH
ifdef CONFIG_INTERNAL_LIBTOMMATH_FAST
L_CFLAGS += -DLTM_FAST
endif
else
LIBS += -ltommath
LIBS_h += -ltommath
endif
CONFIG_INTERNAL_AES=y
CONFIG_INTERNAL_DES=y
CONFIG_INTERNAL_SHA1=y
CONFIG_INTERNAL_MD4=y
CONFIG_INTERNAL_MD5=y
CONFIG_INTERNAL_SHA256=y
CONFIG_INTERNAL_RC4=y
CONFIG_INTERNAL_DH_GROUP5=y
endif
ifeq ($(CONFIG_CRYPTO), cryptoapi)
OBJS += src/crypto/crypto_cryptoapi.c
OBJS_p += src/crypto/crypto_cryptoapi.c
L_CFLAGS += -DCONFIG_CRYPTO_CRYPTOAPI
CONFIG_INTERNAL_SHA256=y
CONFIG_INTERNAL_RC4=y
endif
endif

ifeq ($(CONFIG_TLS), none)
ifdef TLS_FUNCS
OBJS += src/crypto/tls_none.c
L_CFLAGS += -DEAP_TLS_NONE
CONFIG_INTERNAL_AES=y
CONFIG_INTERNAL_SHA1=y
CONFIG_INTERNAL_MD5=y
endif
OBJS += src/crypto/crypto_none.c
OBJS_p += src/crypto/crypto_none.c
CONFIG_INTERNAL_SHA256=y
CONFIG_INTERNAL_RC4=y
endif

ifndef TLS_FUNCS
OBJS += src/crypto/tls_none.c
ifeq ($(CONFIG_TLS), internal)
CONFIG_INTERNAL_AES=y
CONFIG_INTERNAL_SHA1=y
CONFIG_INTERNAL_MD5=y
CONFIG_INTERNAL_RC4=y
endif
endif

AESOBJS = # none so far
ifdef CONFIG_INTERNAL_AES
AESOBJS += src/crypto/aes-internal.c src/crypto/aes-internal-enc.c
endif

AESOBJS += src/crypto/aes-wrap.c
ifdef NEED_AES_EAX
AESOBJS += src/crypto/aes-eax.c
NEED_AES_CTR=y
endif
ifdef NEED_AES_CTR
AESOBJS += src/crypto/aes-ctr.c
endif
ifdef NEED_AES_ENCBLOCK
AESOBJS += src/crypto/aes-encblock.c
endif
ifdef NEED_AES_OMAC1
AESOBJS += src/crypto/aes-omac1.c
endif
ifdef NEED_AES_UNWRAP
NEED_AES_DEC=y
AESOBJS += src/crypto/aes-unwrap.c
endif
ifdef NEED_AES_CBC
NEED_AES_DEC=y
AESOBJS += src/crypto/aes-cbc.c
endif
ifdef NEED_AES_DEC
ifdef CONFIG_INTERNAL_AES
AESOBJS += src/crypto/aes-internal-dec.c
endif
endif
ifdef NEED_AES
OBJS += $(AESOBJS)
endif

SHA1OBJS =
ifdef NEED_SHA1
SHA1OBJS += src/crypto/sha1.c
ifdef CONFIG_INTERNAL_SHA1
SHA1OBJS += src/crypto/sha1-internal.c
ifdef NEED_FIPS186_2_PRF
SHA1OBJS += src/crypto/fips_prf_internal.c
endif
endif
SHA1OBJS += src/crypto/sha1-pbkdf2.c
ifdef NEED_T_PRF
SHA1OBJS += src/crypto/sha1-tprf.c
endif
ifdef NEED_TLS_PRF
SHA1OBJS += src/crypto/sha1-tlsprf.c
endif
endif

ifdef NEED_SHA1
OBJS += $(SHA1OBJS)
endif

ifdef NEED_MD5
ifdef CONFIG_INTERNAL_MD5
OBJS += src/crypto/md5-internal.c
HOBJS += src/crypto/md5-internal.c
endif
endif

ifdef NEED_MD4
ifdef CONFIG_INTERNAL_MD4
OBJS += src/crypto/md4-internal.c
endif
endif

ifdef NEED_DES
ifdef CONFIG_INTERNAL_DES
OBJS += src/crypto/des-internal.c
endif
endif

ifdef NEED_RC4
ifdef CONFIG_INTERNAL_RC4
OBJS += src/crypto/rc4.c
endif
endif

ifdef NEED_SHA256
OBJS += src/crypto/sha256.c
ifdef CONFIG_INTERNAL_SHA256
OBJS += src/crypto/sha256-internal.c
endif
endif

ifdef NEED_DH_GROUPS
OBJS += src/crypto/dh_groups.c
endif
ifdef NEED_DH_GROUPS_ALL
L_CFLAGS += -DALL_DH_GROUPS
endif
ifdef CONFIG_INTERNAL_DH_GROUP5
ifdef NEED_DH_GROUPS
OBJS += src/crypto/dh_group5.c
endif
endif

ifdef CONFIG_NO_RANDOM_POOL
L_CFLAGS += -DCONFIG_NO_RANDOM_POOL
else
OBJS += src/crypto/random.c
HOBJS += src/crypto/random.c
HOBJS += $(SHA1OBJS)
HOBJS += src/crypto/md5.c
endif

ifdef CONFIG_RADIUS_SERVER
L_CFLAGS += -DRADIUS_SERVER
OBJS += src/radius/radius_server.c
endif

ifdef CONFIG_IPV6
L_CFLAGS += -DCONFIG_IPV6
endif

ifdef CONFIG_DRIVER_RADIUS_ACL
L_CFLAGS += -DCONFIG_DRIVER_RADIUS_ACL
endif

ifdef CONFIG_FULL_DYNAMIC_VLAN
# define CONFIG_FULL_DYNAMIC_VLAN to have hostapd manipulate bridges
# and vlan interfaces for the vlan feature.
L_CFLAGS += -DCONFIG_FULL_DYNAMIC_VLAN
endif

ifdef NEED_BASE64
OBJS += src/utils/base64.c
endif

ifdef NEED_AP_MLME
OBJS += src/ap/beacon.c
OBJS += src/ap/wmm.c
OBJS += src/ap/ap_list.c
OBJS += src/ap/ieee802_11.c
OBJS += src/ap/hw_features.c
L_CFLAGS += -DNEED_AP_MLME
endif
ifdef CONFIG_IEEE80211N
OBJS += src/ap/ieee802_11_ht.c
endif

ifdef CONFIG_P2P_MANAGER
L_CFLAGS += -DCONFIG_P2P_MANAGER
OBJS += src/ap/p2p_hostapd.c
endif

ifdef CONFIG_NO_STDOUT_DEBUG
L_CFLAGS += -DCONFIG_NO_STDOUT_DEBUG
endif

ifdef CONFIG_DEBUG_FILE
L_CFLAGS += -DCONFIG_DEBUG_FILE
endif

ifdef CONFIG_ANDROID_LOG
L_CFLAGS += -DCONFIG_ANDROID_LOG
endif

OBJS_c = hostapd_cli.c src/common/wpa_ctrl.c src/utils/os_$(CONFIG_OS).c
ifdef CONFIG_WPA_TRACE
OBJS_c += src/utils/trace.c
OBJS_c += src/utils/wpa_debug.c
endif

ifeq ($(WPA_BUILD_HOSTAPD),true)

########################

include $(CLEAR_VARS)
LOCAL_MODULE := hostapd_cli
LOCAL_MODULE_TAGS := debug
LOCAL_SHARED_LIBRARIES := libc libcutils
LOCAL_CFLAGS := $(L_CFLAGS)
LOCAL_SRC_FILES := $(OBJS_c)
LOCAL_C_INCLUDES := $(INCLUDES)
include $(BUILD_EXECUTABLE)

########################
include $(CLEAR_VARS)
LOCAL_MODULE := hostapd
LOCAL_MODULE_TAGS := optional
ifdef CONFIG_DRIVER_CUSTOM
LOCAL_STATIC_LIBRARIES := libCustomWifi
endif
ifneq ($(BOARD_HOSTAPD_PRIVATE_LIB),)
LOCAL_STATIC_LIBRARIES += $(BOARD_HOSTAPD_PRIVATE_LIB)
endif
LOCAL_SHARED_LIBRARIES := libc libcutils libcrypto libssl
ifdef CONFIG_DRIVER_NL80211
LOCAL_SHARED_LIBRARIES += libnl_2
endif
LOCAL_CFLAGS := $(L_CFLAGS)
LOCAL_SRC_FILES := $(OBJS)
LOCAL_C_INCLUDES := $(INCLUDES)
include $(BUILD_EXECUTABLE)

endif # ifeq ($(WPA_BUILD_HOSTAPD),true)
