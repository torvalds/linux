#ifndef __BACKPORT_NETDEV_FEATURES_H
#define __BACKPORT_NETDEV_FEATURES_H

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
#include <linux/netdevice.h>
#include <linux/types.h>

/* added via 9356b8fc */
#define NETIF_F_HW_VLAN_CTAG_RX			NETIF_F_HW_VLAN_RX
#define NETIF_F_HW_VLAN_CTAG_TX			NETIF_F_HW_VLAN_TX

/* added via d314774c */
#define NETIF_F_HW_VLAN_CTAG_FILTER		NETIF_F_HW_VLAN_FILTER

/* c8f44aff made this u32 but later a861a8b2 changed it to u64 both on v3.3 */
typedef u32 netdev_features_t;

#else
#include_next <linux/netdev_features.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,10,0)
/* See commit f646968f8f on next-20130423 */
#define NETIF_F_HW_VLAN_CTAG_TX_BIT		NETIF_F_HW_VLAN_TX_BIT
#define NETIF_F_HW_VLAN_CTAG_RX_BIT		NETIF_F_HW_VLAN_RX_BIT
#define NETIF_F_HW_VLAN_CTAG_FILTER_BIT		NETIF_F_HW_VLAN_FILTER_BIT

#define NETIF_F_HW_VLAN_CTAG_FILTER		NETIF_F_HW_VLAN_FILTER
#define NETIF_F_HW_VLAN_CTAG_RX			NETIF_F_HW_VLAN_RX
#define NETIF_F_HW_VLAN_CTAG_TX			NETIF_F_HW_VLAN_TX
#endif

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0) */

#if !defined(NETIF_F_RXCSUM)
#define NETIF_F_RXCSUM 0
#endif

#if !defined(NETIF_F_RXALL)
#define NETIF_F_RXALL 0
#endif

#if !defined(NETIF_F_RXFCS)
#define NETIF_F_RXFCS 0
#endif

#endif /* __BACKPORT_NETDEV_FEATURES_H */
