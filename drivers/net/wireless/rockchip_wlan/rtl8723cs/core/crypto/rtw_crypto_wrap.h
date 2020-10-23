#ifndef RTW_CRYTO_WRAP_H
#define RTW_CRYTO_WRAP_H

#include <drv_types.h>

#define TEST_FAIL() 0

#define os_memset _rtw_memset
#define os_memcpy _rtw_memcpy
#define os_malloc rtw_malloc

#define le_to_host16 le16_to_cpu
#define host_to_le16 cpu_to_le16

#define WPA_PUT_LE16 RTW_PUT_LE16
#define WPA_GET_LE16 RTW_GET_LE16
#define WPA_PUT_LE32 RTW_PUT_LE32
#define WPA_GET_LE32 RTW_GET_LE32
#define WPA_PUT_LE64 RTW_PUT_LE64
#define WPA_GET_LE64 RTW_GET_LE64
#define WPA_PUT_BE16 RTW_PUT_BE16
#define WPA_GET_BE16 RTW_GET_BE16
#define WPA_PUT_BE32 RTW_PUT_BE32
#define WPA_GET_BE32 RTW_GET_BE32
#define WPA_PUT_BE64 RTW_PUT_BE64
#define WPA_GET_BE64 RTW_GET_BE64

#ifndef MAC2STR
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif

#define WLAN_FC_PVER		0x0003
#define WLAN_FC_TODS		0x0100
#define WLAN_FC_FROMDS		0x0200
#define WLAN_FC_MOREFRAG	0x0400
#define WLAN_FC_RETRY		0x0800
#define WLAN_FC_PWRMGT		0x1000
#define WLAN_FC_MOREDATA	0x2000
#define WLAN_FC_ISWEP		0x4000
#define WLAN_FC_ORDER		0x8000

#define WLAN_FC_TYPE_DATA RTW_IEEE80211_FTYPE_DATA
#define WLAN_FC_TYPE_MGMT RTW_IEEE80211_FTYPE_MGMT

#define WLAN_FC_STYPE_QOS_DATA RTW_IEEE80211_STYPE_QOS_DATA

enum {
	_MSG_EXCESSIVE_, _MSG_MSGDUMP_, _MSG_DEBUG_, _MSG_INFO_, _MSG_WARNING_, _MSG_ERROR_
};

int os_memcmp(const void *s1, const void *s2, size_t n);
int os_memcmp_const(const void *a, const void *b, size_t len);
void* os_memdup(const void *src, u32 sz);
size_t os_strlen(const char *s);

void forced_memzero(void *ptr, size_t len);
void bin_clear_free(void *bin, size_t len);

void wpa_printf(int level, const char *fmt, ...);
void wpa_hexdump(int level, const char *title, const void *buf, size_t len);
void wpa_hexdump_key(int level, const char *title, const void *buf, size_t len);

#endif /* RTW_CRYTO_WRAP_H */
