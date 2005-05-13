#ifndef HOSTAP_CONFIG_H
#define HOSTAP_CONFIG_H

#define PRISM2_VERSION "CVS"

/* In the previous versions of Host AP driver, support for user space version
 * of IEEE 802.11 management (hostapd) used to be disabled in the default
 * configuration. From now on, support for hostapd is always included and it is
 * possible to disable kernel driver version of IEEE 802.11 management with a
 * separate define, PRISM2_NO_KERNEL_IEEE80211_MGMT. */
/* #define PRISM2_NO_KERNEL_IEEE80211_MGMT */

/* Maximum number of events handler per one interrupt */
#define PRISM2_MAX_INTERRUPT_EVENTS 20

/* Use PCI bus master to copy data to/from BAP (only available for
 * hostap_pci.o).
 *
 * Note! This is extremely experimental. PCI bus master is not supported by
 * Intersil and it seems to have some problems at least on TX path (see below).
 * The driver code for implementing bus master support is based on guessing
 * and experimenting suitable control bits and these might not be correct.
 * This code is included because using bus master makes a huge difference in
 * host CPU load (something like 40% host CPU usage to 5-10% when sending or
 * receiving at maximum throughput).
 *
 * Note2! Station firmware version 1.3.5 and primary firmware version 1.0.7
 * have some fixes for PCI corruption and these (or newer) versions are
 * recommended especially when using bus mastering.
 *
 * NOTE: PCI bus mastering code has not been updated for long time and it is
 * not likely to compile and it will _not_ work as is. Only enable this if you
 * are prepared to first fix the implementation..
 */
/* #define PRISM2_BUS_MASTER */

#ifdef PRISM2_BUS_MASTER

/* PCI bus master implementation seems to be broken in current
 * hardware/firmware versions. Enable this to use enable command to fix
 * something before starting bus master operation on TX path. This will add
 * some latency and an extra interrupt to each TX packet. */
#define PRISM2_ENABLE_BEFORE_TX_BUS_MASTER

#endif /* PRISM2_BUS_MASTER */

/* Include code for downloading firmware images into volatile RAM. */
#define PRISM2_DOWNLOAD_SUPPORT

/* Allow kernel configuration to enable download support. */
#if !defined(PRISM2_DOWNLOAD_SUPPORT) && defined(CONFIG_HOSTAP_FIRMWARE)
#define PRISM2_DOWNLOAD_SUPPORT
#endif

#ifdef PRISM2_DOWNLOAD_SUPPORT
/* Allow writing firmware images into flash, i.e., to non-volatile storage.
 * Before you enable this option, you should make absolutely sure that you are
 * using prism2_srec utility that comes with THIS version of the driver!
 * In addition, please note that it is possible to kill your card with
 * non-volatile download if you are using incorrect image. This feature has not
 * been fully tested, so please be careful with it. */
/* #define PRISM2_NON_VOLATILE_DOWNLOAD */
#endif /* PRISM2_DOWNLOAD_SUPPORT */

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
