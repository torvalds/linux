#ifndef HOSTAP_CONFIG_H
#define HOSTAP_CONFIG_H

#define PRISM2_VERSION "0.4.4-kernel"

/* In the previous versions of Host AP driver, support for user space version
 * of IEEE 802.11 management (hostapd) used to be disabled in the default
 * configuration. From now on, support for hostapd is always included and it is
 * possible to disable kernel driver version of IEEE 802.11 management with a
 * separate define, PRISM2_NO_KERNEL_IEEE80211_MGMT. */
/* #define PRISM2_NO_KERNEL_IEEE80211_MGMT */

/* Maximum number of events handler per one interrupt */
#define PRISM2_MAX_INTERRUPT_EVENTS 20

/* Include code for downloading firmware images into volatile RAM. */
#define PRISM2_DOWNLOAD_SUPPORT

/* Allow kernel configuration to enable download support. */
#if !defined(PRISM2_DOWNLOAD_SUPPORT) && defined(CONFIG_HOSTAP_FIRMWARE)
#define PRISM2_DOWNLOAD_SUPPORT
#endif

/* Allow kernel configuration to enable non-volatile download support. */
#ifdef CONFIG_HOSTAP_FIRMWARE_NVRAM
#define PRISM2_NON_VOLATILE_DOWNLOAD
#endif

/* Save low-level I/O for debugging. This should not be enabled in normal use.
 */
/* #define PRISM2_IO_DEBUG */

/* Following defines can be used to remove unneeded parts of the driver, e.g.,
 * to limit the size of the kernel module. Definitions can be added here in
 * hostap_config.h or they can be added to make command with EXTRA_CFLAGS,
 * e.g.,
 * 'make pccard EXTRA_CFLAGS="-DPRISM2_NO_DEBUG -DPRISM2_NO_PROCFS_DEBUG"'
 */

/* Do not include debug messages into the driver */
/* #define PRISM2_NO_DEBUG */

/* Do not include /proc/net/prism2/wlan#/{registers,debug} */
/* #define PRISM2_NO_PROCFS_DEBUG */

/* Do not include station functionality (i.e., allow only Master (Host AP) mode
 */
/* #define PRISM2_NO_STATION_MODES */

#endif /* HOSTAP_CONFIG_H */
