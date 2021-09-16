/* SPDX-License-Identifier: GPL-2.0-only */
/*
################################################################################
#
# r8168 is the Linux device driver released for Realtek Gigabit Ethernet
# controllers with PCI-Express interface.
#
# Copyright(c) 2021 Realtek Semiconductor Corp. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, see <http://www.gnu.org/licenses/>.
#
# Author:
# Realtek NIC software team <nicfae@realtek.com>
# No. 2, Innovation Road II, Hsinchu Science Park, Hsinchu 300, Taiwan
#
################################################################################
*/

/************************************************************************************
 *  This product is covered by one or more of the following patents:
 *  US6,570,884, US6,115,776, and US6,327,625.
 ***********************************************************************************/

#include <linux/ethtool.h>
#include "r8168_dash.h"
#include "r8168_realwow.h"
#include "r8168_fiber.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,32)
typedef int netdev_tx_t;
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
#define skb_transport_offset(skb) (skb->h.raw - skb->data)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
#define device_set_wakeup_enable(dev, val)	do {} while (0)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,14,0)
static inline void ether_addr_copy(u8 *dst, const u8 *src)
{
        u16 *a = (u16 *)dst;
        const u16 *b = (const u16 *)src;

        a[0] = b[0];
        a[1] = b[1];
        a[2] = b[2];
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,15,0)
#define IS_ERR_OR_NULL(ptr)			(!ptr)
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
#define reinit_completion(x)			((x)->done = 0)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
#define pm_runtime_mark_last_busy(x)
#define pm_runtime_put_autosuspend(x)		pm_runtime_put(x)
#define pm_runtime_put_sync_autosuspend(x)	pm_runtime_put_sync(x)

static inline bool pm_runtime_suspended(struct device *dev)
{
        return dev->power.runtime_status == RPM_SUSPENDED
               && !dev->power.disable_depth;
}

static inline bool pm_runtime_active(struct device *dev)
{
        return dev->power.runtime_status == RPM_ACTIVE
               || dev->power.disable_depth;
}
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
#define queue_delayed_work(long_wq, work, delay)	schedule_delayed_work(work, delay)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34)
#define netif_printk(priv, type, level, netdev, fmt, args...)	\
	do {								\
		if (netif_msg_##type(priv))				\
			printk(level "%s: " fmt,(netdev)->name , ##args); \
	} while (0)

#define netif_emerg(priv, type, netdev, fmt, args...)		\
		netif_printk(priv, type, KERN_EMERG, netdev, fmt, ##args)
#define netif_alert(priv, type, netdev, fmt, args...)		\
		netif_printk(priv, type, KERN_ALERT, netdev, fmt, ##args)
#define netif_crit(priv, type, netdev, fmt, args...)		\
		netif_printk(priv, type, KERN_CRIT, netdev, fmt, ##args)
#define netif_err(priv, type, netdev, fmt, args...)		\
		netif_printk(priv, type, KERN_ERR, netdev, fmt, ##args)
#define netif_warn(priv, type, netdev, fmt, args...)		\
		netif_printk(priv, type, KERN_WARNING, netdev, fmt, ##args)
#define netif_notice(priv, type, netdev, fmt, args...)		\
		netif_printk(priv, type, KERN_NOTICE, netdev, fmt, ##args)
#define netif_info(priv, type, netdev, fmt, args...)		\
		netif_printk(priv, type, KERN_INFO, (netdev), fmt, ##args)
#endif
#endif
#endif
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
#define setup_timer(_timer, _function, _data) \
do { \
	(_timer)->function = _function; \
	(_timer)->data = _data; \
	init_timer(_timer); \
} while (0)
#endif //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0)
#if defined(skb_vlan_tag_present) && !defined(vlan_tx_tag_present)
#define vlan_tx_tag_present skb_vlan_tag_present
#endif
#if defined(skb_vlan_tag_get) && !defined(vlan_tx_tag_get)
#define vlan_tx_tag_get skb_vlan_tag_get
#endif
#endif //LINUX_VERSION_CODE < KERNEL_VERSION(4,0,0)

#define RTL_ALLOC_SKB_INTR(tp, length) dev_alloc_skb(length)
#ifdef CONFIG_R8168_NAPI
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0)
#undef RTL_ALLOC_SKB_INTR
#define RTL_ALLOC_SKB_INTR(tp, length) napi_alloc_skb(&tp->napi, length)
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)
#define netdev_features_t  u32
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,5,0)
#define NETIF_F_ALL_CSUM        NETIF_F_CSUM_MASK
#else
#ifndef NETIF_F_ALL_CSUM
#define NETIF_F_ALL_CSUM        NETIF_F_CSUM_MASK
#endif
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,37)
#define ENABLE_R8168_PROCFS
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
#define NETIF_F_HW_VLAN_RX	NETIF_F_HW_VLAN_CTAG_RX
#define NETIF_F_HW_VLAN_TX	NETIF_F_HW_VLAN_CTAG_TX
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0)
#define __devinit
#define __devexit
#define __devexit_p(func)   func
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
#define CHECKSUM_PARTIAL CHECKSUM_HW
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#define irqreturn_t void
#define IRQ_HANDLED    1
#define IRQ_NONE   0
#define IRQ_RETVAL(x)
#endif

#ifndef NETIF_F_RXALL
#define NETIF_F_RXALL  0
#endif

#ifndef NETIF_F_RXFCS
#define NETIF_F_RXFCS  0
#endif

#ifndef HAVE_FREE_NETDEV
#define free_netdev(x)  kfree(x)
#endif

#ifndef SET_NETDEV_DEV
#define SET_NETDEV_DEV(net, pdev)
#endif

#ifndef SET_MODULE_OWNER
#define SET_MODULE_OWNER(dev)
#endif

#ifndef SA_SHIRQ
#define SA_SHIRQ IRQF_SHARED
#endif

#ifndef NETIF_F_GSO
#define gso_size    tso_size
#define gso_segs    tso_segs
#endif

#ifndef PCI_VENDOR_ID_DLINK
#define PCI_VENDOR_ID_DLINK 0x1186
#endif

#ifndef dma_mapping_error
#define dma_mapping_error(a,b) 0
#endif

#ifndef netif_err
#define netif_err(a,b,c,d)
#endif

#ifndef AUTONEG_DISABLE
#define AUTONEG_DISABLE   0x00
#endif

#ifndef AUTONEG_ENABLE
#define AUTONEG_ENABLE    0x01
#endif

#ifndef BMCR_SPEED1000
#define BMCR_SPEED1000  0x0040
#endif

#ifndef BMCR_SPEED100
#define BMCR_SPEED100   0x2000
#endif

#ifndef BMCR_SPEED10
#define BMCR_SPEED10    0x0000
#endif

#ifndef SPEED_UNKNOWN
#define SPEED_UNKNOWN   -1
#endif

#ifndef DUPLEX_UNKNOWN
#define DUPLEX_UNKNOWN  0xff
#endif

#ifndef SUPPORTED_Pause
#define SUPPORTED_Pause  (1 << 13)
#endif

#ifndef SUPPORTED_Asym_Pause
#define SUPPORTED_Asym_Pause  (1 << 14)
#endif

#ifndef  MDIO_EEE_100TX
#define  MDIO_EEE_100TX  0x0002
#endif

#ifndef  MDIO_EEE_1000T
#define  MDIO_EEE_1000T  0x0004
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
#ifdef CONFIG_NET_POLL_CONTROLLER
#define RTL_NET_POLL_CONTROLLER dev->poll_controller=rtl8168_netpoll
#else
#define RTL_NET_POLL_CONTROLLER
#endif

#ifdef CONFIG_R8168_VLAN
#define RTL_SET_VLAN dev->vlan_rx_register=rtl8168_vlan_rx_register
#else
#define RTL_SET_VLAN
#endif

#define RTL_NET_DEVICE_OPS(ops) dev->open=rtl8168_open; \
                    dev->hard_start_xmit=rtl8168_start_xmit; \
                    dev->get_stats=rtl8168_get_stats; \
                    dev->stop=rtl8168_close; \
                    dev->tx_timeout=rtl8168_tx_timeout; \
                    dev->set_multicast_list=rtl8168_set_rx_mode; \
                    dev->change_mtu=rtl8168_change_mtu; \
                    dev->set_mac_address=rtl8168_set_mac_address; \
                    dev->do_ioctl=rtl8168_do_ioctl; \
                    RTL_NET_POLL_CONTROLLER; \
                    RTL_SET_VLAN;
#else
#define RTL_NET_DEVICE_OPS(ops) dev->netdev_ops=&ops
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE  1
#endif

#ifndef false
#define false 0
#endif

#ifndef true
#define true  1
#endif

//Hardware will continue interrupt 10 times after interrupt finished.
#define RTK_KEEP_INTERRUPT_COUNT (10)

//Due to the hardware design of RTL8111B, the low 32 bit address of receive
//buffer must be 8-byte alignment.
#ifndef NET_IP_ALIGN
#define NET_IP_ALIGN        2
#endif
#define RTK_RX_ALIGN        8

#ifdef CONFIG_R8168_NAPI
#define NAPI_SUFFIX "-NAPI"
#else
#define NAPI_SUFFIX ""
#endif
#ifdef ENABLE_FIBER_SUPPORT
#define FIBER_SUFFIX "-FIBER"
#else
#define FIBER_SUFFIX ""
#endif
#ifdef ENABLE_REALWOW_SUPPORT
#define REALWOW_SUFFIX "-REALWOW"
#else
#define REALWOW_SUFFIX ""
#endif
#if defined(ENABLE_DASH_PRINTER_SUPPORT)
#define DASH_SUFFIX "-PRINTER"
#elif defined(ENABLE_DASH_SUPPORT)
#define DASH_SUFFIX "-DASH"
#else
#define DASH_SUFFIX ""
#endif

#define RTL8168_VERSION "8.049.02" NAPI_SUFFIX FIBER_SUFFIX REALWOW_SUFFIX DASH_SUFFIX
#define MODULENAME "r8168"
#define PFX MODULENAME ": "

#define GPL_CLAIM "\
r8168  Copyright (C) 2021 Realtek NIC software team <nicfae@realtek.com> \n \
This program comes with ABSOLUTELY NO WARRANTY; for details, please see <http://www.gnu.org/licenses/>. \n \
This is free software, and you are welcome to redistribute it under certain conditions; see <http://www.gnu.org/licenses/>. \n"

#ifdef RTL8168_DEBUG
#define assert(expr) \
        if(!(expr)) {                   \
            printk( "Assertion failed! %s,%s,%s,line=%d\n", \
            #expr,__FILE__,__FUNCTION__,__LINE__);      \
        }
#define dprintk(fmt, args...)   do { printk(PFX fmt, ## args); } while (0)
#else
#define assert(expr) do {} while (0)
#define dprintk(fmt, args...)   do {} while (0)
#endif /* RTL8168_DEBUG */

#define R8168_MSG_DEFAULT \
    (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_IFUP | NETIF_MSG_IFDOWN)

#ifdef CONFIG_R8168_NAPI
#define rtl8168_rx_hwaccel_skb      vlan_hwaccel_receive_skb
#define rtl8168_rx_quota(count, quota)  min(count, quota)
#else
#define rtl8168_rx_hwaccel_skb      vlan_hwaccel_rx
#define rtl8168_rx_quota(count, quota)  count
#endif

/* MAC address length */
#ifndef MAC_ADDR_LEN
#define MAC_ADDR_LEN    6
#endif

#ifndef MAC_PROTOCOL_LEN
#define MAC_PROTOCOL_LEN    2
#endif

#ifndef ETH_FCS_LEN
#define ETH_FCS_LEN	  4
#endif

#ifndef NETIF_F_TSO6
#define NETIF_F_TSO6  0
#endif

#define Reserved2_data  7
#define RX_DMA_BURST    7   /* Maximum PCI burst, '6' is 1024 */
#define TX_DMA_BURST_unlimited  7
#define TX_DMA_BURST_1024   6
#define TX_DMA_BURST_512    5
#define TX_DMA_BURST_256    4
#define TX_DMA_BURST_128    3
#define TX_DMA_BURST_64     2
#define TX_DMA_BURST_32     1
#define TX_DMA_BURST_16     0
#define Reserved1_data  0x3F
#define RxPacketMaxSize 0x3FE8  /* 16K - 1 - ETH_HLEN - VLAN - CRC... */
#define Jumbo_Frame_1k  ETH_DATA_LEN
#define Jumbo_Frame_2k  (2*1024 - ETH_HLEN - VLAN_HLEN - ETH_FCS_LEN)
#define Jumbo_Frame_3k  (3*1024 - ETH_HLEN - VLAN_HLEN - ETH_FCS_LEN)
#define Jumbo_Frame_4k  (4*1024 - ETH_HLEN - VLAN_HLEN - ETH_FCS_LEN)
#define Jumbo_Frame_5k  (5*1024 - ETH_HLEN - VLAN_HLEN - ETH_FCS_LEN)
#define Jumbo_Frame_6k  (6*1024 - ETH_HLEN - VLAN_HLEN - ETH_FCS_LEN)
#define Jumbo_Frame_7k  (7*1024 - ETH_HLEN - VLAN_HLEN - ETH_FCS_LEN)
#define Jumbo_Frame_8k  (8*1024 - ETH_HLEN - VLAN_HLEN - ETH_FCS_LEN)
#define Jumbo_Frame_9k  (9*1024 - ETH_HLEN - VLAN_HLEN - ETH_FCS_LEN)
#define InterFrameGap   0x03    /* 3 means InterFrameGap = the shortest one */
#define RxEarly_off_V1 (0x07 << 11)
#define RxEarly_off_V2 (1 << 11)
#define Rx_Single_fetch_V2 (1 << 14)

#define R8168_REGS_SIZE     (256)
#define R8168_MAC_REGS_SIZE     (256)
#define R8168_PHY_REGS_SIZE     (16*2)
#define R8168_EPHY_REGS_SIZE  	(31*2)
#define R8168_ERI_REGS_SIZE  	(0x100)
#define R8168_REGS_DUMP_SIZE     (0x400)
#define R8168_PCI_REGS_SIZE  	(0x100)
#define R8168_NAPI_WEIGHT   64

#define RTL8168_TX_TIMEOUT  (6 * HZ)
#define RTL8168_LINK_TIMEOUT    (1 * HZ)
#define RTL8168_ESD_TIMEOUT (2 * HZ)

#define NUM_TX_DESC 1024    /* Number of Tx descriptor registers */
#define NUM_RX_DESC 1024    /* Number of Rx descriptor registers */

#define RX_BUF_SIZE 0x05F3  /* 0x05F3 = 1522bye + 1 */
#define R8168_TX_RING_BYTES (NUM_TX_DESC * sizeof(struct TxDesc))
#define R8168_RX_RING_BYTES (NUM_RX_DESC * sizeof(struct RxDesc))

#define OCP_STD_PHY_BASE	0xa400

#define NODE_ADDRESS_SIZE 6

#define SHORT_PACKET_PADDING_BUF_SIZE 256

#define RTK_MAGIC_DEBUG_VALUE 0x0badbeef

/* write/read MMIO register */
#define RTL_W8(tp, reg, val8)	writeb((val8), tp->mmio_addr + (reg))
#define RTL_W16(tp, reg, val16)	writew((val16), tp->mmio_addr + (reg))
#define RTL_W32(tp, reg, val32)	writel((val32), tp->mmio_addr + (reg))
#define RTL_R8(tp, reg)		readb(tp->mmio_addr + (reg))
#define RTL_R16(tp, reg)		readw(tp->mmio_addr + (reg))
#define RTL_R32(tp, reg)		((unsigned long) readl(tp->mmio_addr + (reg)))

#ifndef DMA_64BIT_MASK
#define DMA_64BIT_MASK  0xffffffffffffffffULL
#endif

#ifndef DMA_32BIT_MASK
#define DMA_32BIT_MASK  0x00000000ffffffffULL
#endif

#ifndef NETDEV_TX_OK
#define NETDEV_TX_OK 0      /* driver took care of packet */
#endif

#ifndef NETDEV_TX_BUSY
#define NETDEV_TX_BUSY 1    /* driver tx path was busy*/
#endif

#ifndef NETDEV_TX_LOCKED
#define NETDEV_TX_LOCKED -1 /* driver tx lock was already taken */
#endif

#ifndef ADVERTISED_Pause
#define ADVERTISED_Pause    (1 << 13)
#endif

#ifndef ADVERTISED_Asym_Pause
#define ADVERTISED_Asym_Pause   (1 << 14)
#endif

#ifndef ADVERTISE_PAUSE_CAP
#define ADVERTISE_PAUSE_CAP 0x400
#endif

#ifndef ADVERTISE_PAUSE_ASYM
#define ADVERTISE_PAUSE_ASYM    0x800
#endif

#ifndef MII_CTRL1000
#define MII_CTRL1000        0x09
#endif

#ifndef ADVERTISE_1000FULL
#define ADVERTISE_1000FULL  0x200
#endif

#ifndef ADVERTISE_1000HALF
#define ADVERTISE_1000HALF  0x100
#endif

#ifndef ETH_MIN_MTU
#define ETH_MIN_MTU  68
#endif

/*****************************************************************************/

//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,3)
#if (( LINUX_VERSION_CODE < KERNEL_VERSION(2,4,27) ) || \
     (( LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) ) && \
      ( LINUX_VERSION_CODE < KERNEL_VERSION(2,6,3) )))
/* copied from linux kernel 2.6.20 include/linux/netdev.h */
#define NETDEV_ALIGN        32
#define NETDEV_ALIGN_CONST  (NETDEV_ALIGN - 1)

static inline void *netdev_priv(struct net_device *dev)
{
        return (char *)dev + ((sizeof(struct net_device)
                               + NETDEV_ALIGN_CONST)
                              & ~NETDEV_ALIGN_CONST);
}
#endif  //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,3)

/*****************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
#define RTLDEV  tp
#else
#define RTLDEV  dev
#endif  //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
/*****************************************************************************/

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
typedef struct net_device *napi_ptr;
typedef int *napi_budget;

#define napi dev
#define RTL_NAPI_CONFIG(ndev, priv, function, weig) ndev->poll=function;    \
                                ndev->weight=weig;
#define RTL_NAPI_QUOTA(budget, ndev)            min(*budget, ndev->quota)
#define RTL_GET_PRIV(stuct_ptr, priv_struct)        netdev_priv(stuct_ptr)
#define RTL_GET_NETDEV(priv_ptr)
#define RTL_RX_QUOTA(budget)          *budget
#define RTL_NAPI_QUOTA_UPDATE(ndev, work_done, budget)  *budget -= work_done;   \
                                ndev->quota -= work_done;
#define RTL_NETIF_RX_COMPLETE(dev, napi, work_done)        netif_rx_complete(dev)
#define RTL_NETIF_RX_SCHEDULE_PREP(dev, napi)       netif_rx_schedule_prep(dev)
#define __RTL_NETIF_RX_SCHEDULE(dev, napi)      __netif_rx_schedule(dev)
#define RTL_NAPI_RETURN_VALUE               work_done >= work_to_do
#define RTL_NAPI_ENABLE(dev, napi)          netif_poll_enable(dev)
#define RTL_NAPI_DISABLE(dev, napi)         netif_poll_disable(dev)
#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : ((1ULL<<(n))-1))
#else
typedef struct napi_struct *napi_ptr;
typedef int napi_budget;

#define RTL_NAPI_CONFIG(ndev, priv, function, weight)   netif_napi_add(ndev, &priv->napi, function, weight)
#define RTL_NAPI_QUOTA(budget, ndev)            min(budget, budget)
#define RTL_GET_PRIV(stuct_ptr, priv_struct)        container_of(stuct_ptr, priv_struct, stuct_ptr)
#define RTL_GET_NETDEV(priv_ptr)            struct net_device *dev = priv_ptr->dev;
#define RTL_RX_QUOTA(budget)          budget
#define RTL_NAPI_QUOTA_UPDATE(ndev, work_done, budget)
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
#define RTL_NETIF_RX_COMPLETE(dev, napi, work_done)        netif_rx_complete(dev, napi)
#define RTL_NETIF_RX_SCHEDULE_PREP(dev, napi)       netif_rx_schedule_prep(dev, napi)
#define __RTL_NETIF_RX_SCHEDULE(dev, napi)      __netif_rx_schedule(dev, napi)
#endif
#if LINUX_VERSION_CODE == KERNEL_VERSION(2,6,29)
#define RTL_NETIF_RX_COMPLETE(dev, napi, work_done)        netif_rx_complete(napi)
#define RTL_NETIF_RX_SCHEDULE_PREP(dev, napi)       netif_rx_schedule_prep(napi)
#define __RTL_NETIF_RX_SCHEDULE(dev, napi)      __netif_rx_schedule(napi)
#endif
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,29)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,19,0)
#define RTL_NETIF_RX_COMPLETE(dev, napi, work_done)        napi_complete_done(napi, work_done)
#else
#define RTL_NETIF_RX_COMPLETE(dev, napi, work_done)        napi_complete(napi)
#endif
#define RTL_NETIF_RX_SCHEDULE_PREP(dev, napi)       napi_schedule_prep(napi)
#define __RTL_NETIF_RX_SCHEDULE(dev, napi)      __napi_schedule(napi)
#endif
#define RTL_NAPI_RETURN_VALUE work_done
#define RTL_NAPI_ENABLE(dev, napi)          napi_enable(napi)
#define RTL_NAPI_DISABLE(dev, napi)         napi_disable(napi)
#endif  //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)
#define RTL_NAPI_DEL(priv)
#else
#define RTL_NAPI_DEL(priv)   netif_napi_del(&priv->napi)
#endif //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,27)

/*****************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,9)
#ifdef __CHECKER__
#define __iomem __attribute__((noderef, address_space(2)))
extern void __chk_io_ptr(void __iomem *);
#define __bitwise __attribute__((bitwise))
#else
#define __iomem
#define __chk_io_ptr(x) (void)0
#define __bitwise
#endif
#endif  //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,9)

/*****************************************************************************/
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,8)
#ifdef __CHECKER__
#define __force __attribute__((force))
#else
#define __force
#endif
#endif  //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,8)

#ifndef module_param
#define module_param(v,t,p) MODULE_PARM(v, "i");
#endif

#ifndef PCI_DEVICE
#define PCI_DEVICE(vend,dev) \
    .vendor = (vend), .device = (dev), \
    .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID
#endif

/*****************************************************************************/
/* 2.5.28 => 2.4.23 */
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,5,28) )

static inline void _kc_synchronize_irq(void)
{
        synchronize_irq();
}
#undef synchronize_irq
#define synchronize_irq(X) _kc_synchronize_irq()

#include <linux/tqueue.h>
#define work_struct tq_struct
#undef INIT_WORK
#define INIT_WORK(a,b,c) INIT_TQUEUE(a,(void (*)(void *))b,c)
#undef container_of
#define container_of list_entry
#define schedule_work schedule_task
#define flush_scheduled_work flush_scheduled_tasks
#endif /* 2.5.28 => 2.4.17 */

/*****************************************************************************/
/* 2.6.4 => 2.6.0 */
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,6,4) )
#define MODULE_VERSION(_version) MODULE_INFO(version, _version)
#endif /* 2.6.4 => 2.6.0 */
/*****************************************************************************/
/* 2.6.0 => 2.5.28 */
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0) )
#define MODULE_INFO(version, _version)
#ifndef CONFIG_E1000_DISABLE_PACKET_SPLIT
#define CONFIG_E1000_DISABLE_PACKET_SPLIT 1
#endif

#define pci_set_consistent_dma_mask(dev,mask) 1

#undef dev_put
#define dev_put(dev) __dev_put(dev)

#ifndef skb_fill_page_desc
#define skb_fill_page_desc _kc_skb_fill_page_desc
extern void _kc_skb_fill_page_desc(struct sk_buff *skb, int i, struct page *page, int off, int size);
#endif

#ifndef pci_dma_mapping_error
#define pci_dma_mapping_error _kc_pci_dma_mapping_error
static inline int _kc_pci_dma_mapping_error(dma_addr_t dma_addr)
{
        return dma_addr == 0;
}
#endif

#undef ALIGN
#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))

#endif /* 2.6.0 => 2.5.28 */

/*****************************************************************************/
/* 2.4.22 => 2.4.17 */
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,4,22) )
#define pci_name(x) ((x)->slot_name)
#endif /* 2.4.22 => 2.4.17 */

/*****************************************************************************/
/* 2.6.5 => 2.6.0 */
#if ( LINUX_VERSION_CODE < KERNEL_VERSION(2,6,5) )
#define pci_dma_sync_single_for_cpu pci_dma_sync_single
#define pci_dma_sync_single_for_device  pci_dma_sync_single_for_cpu
#endif /* 2.6.5 => 2.6.0 */

/*****************************************************************************/

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
/*
 * initialize a work-struct's func and data pointers:
 */
#define PREPARE_WORK(_work, _func, _data)           \
    do {                            \
        (_work)->func = _func;              \
        (_work)->data = _data;              \
    } while (0)

#endif
/*****************************************************************************/
/* 2.6.4 => 2.6.0 */
#if ((LINUX_VERSION_CODE < KERNEL_VERSION(2,4,25) && \
     LINUX_VERSION_CODE > KERNEL_VERSION(2,4,22)) || \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) && \
      LINUX_VERSION_CODE < KERNEL_VERSION(2,6,4)))
#define ETHTOOL_OPS_COMPAT
#endif /* 2.6.4 => 2.6.0 */

/*****************************************************************************/
/* Installations with ethtool version without eeprom, adapter id, or statistics
 * support */

#ifndef ETH_GSTRING_LEN
#define ETH_GSTRING_LEN 32
#endif

#ifndef ETHTOOL_GSTATS
#define ETHTOOL_GSTATS 0x1d
#undef ethtool_drvinfo
#define ethtool_drvinfo k_ethtool_drvinfo
struct k_ethtool_drvinfo {
        u32 cmd;
        char driver[32];
        char version[32];
        char fw_version[32];
        char bus_info[32];
        char reserved1[32];
        char reserved2[16];
        u32 n_stats;
        u32 testinfo_len;
        u32 eedump_len;
        u32 regdump_len;
};

struct ethtool_stats {
        u32 cmd;
        u32 n_stats;
        u64 data[0];
};
#endif /* ETHTOOL_GSTATS */

#ifndef ETHTOOL_PHYS_ID
#define ETHTOOL_PHYS_ID 0x1c
#endif /* ETHTOOL_PHYS_ID */

#ifndef ETHTOOL_GSTRINGS
#define ETHTOOL_GSTRINGS 0x1b
enum ethtool_stringset {
        ETH_SS_TEST             = 0,
        ETH_SS_STATS,
};
struct ethtool_gstrings {
        u32 cmd;            /* ETHTOOL_GSTRINGS */
        u32 string_set;     /* string set id e.c. ETH_SS_TEST, etc*/
        u32 len;            /* number of strings in the string set */
        u8 data[0];
};
#endif /* ETHTOOL_GSTRINGS */

#ifndef ETHTOOL_TEST
#define ETHTOOL_TEST 0x1a
enum ethtool_test_flags {
        ETH_TEST_FL_OFFLINE = (1 << 0),
        ETH_TEST_FL_FAILED  = (1 << 1),
};
struct ethtool_test {
        u32 cmd;
        u32 flags;
        u32 reserved;
        u32 len;
        u64 data[0];
};
#endif /* ETHTOOL_TEST */

#ifndef ETHTOOL_GEEPROM
#define ETHTOOL_GEEPROM 0xb
#undef ETHTOOL_GREGS
struct ethtool_eeprom {
        u32 cmd;
        u32 magic;
        u32 offset;
        u32 len;
        u8 data[0];
};

struct ethtool_value {
        u32 cmd;
        u32 data;
};
#endif /* ETHTOOL_GEEPROM */

#ifndef ETHTOOL_GLINK
#define ETHTOOL_GLINK 0xa
#endif /* ETHTOOL_GLINK */

#ifndef ETHTOOL_GREGS
#define ETHTOOL_GREGS       0x00000004 /* Get NIC registers */
#define ethtool_regs _kc_ethtool_regs
/* for passing big chunks of data */
struct _kc_ethtool_regs {
        u32 cmd;
        u32 version; /* driver-specific, indicates different chips/revs */
        u32 len; /* bytes */
        u8 data[0];
};
#endif /* ETHTOOL_GREGS */

#ifndef ETHTOOL_GMSGLVL
#define ETHTOOL_GMSGLVL     0x00000007 /* Get driver message level */
#endif
#ifndef ETHTOOL_SMSGLVL
#define ETHTOOL_SMSGLVL     0x00000008 /* Set driver msg level, priv. */
#endif
#ifndef ETHTOOL_NWAY_RST
#define ETHTOOL_NWAY_RST    0x00000009 /* Restart autonegotiation, priv */
#endif
#ifndef ETHTOOL_GLINK
#define ETHTOOL_GLINK       0x0000000a /* Get link status */
#endif
#ifndef ETHTOOL_GEEPROM
#define ETHTOOL_GEEPROM     0x0000000b /* Get EEPROM data */
#endif
#ifndef ETHTOOL_SEEPROM
#define ETHTOOL_SEEPROM     0x0000000c /* Set EEPROM data */
#endif
#ifndef ETHTOOL_GCOALESCE
#define ETHTOOL_GCOALESCE   0x0000000e /* Get coalesce config */
/* for configuring coalescing parameters of chip */
#define ethtool_coalesce _kc_ethtool_coalesce
struct _kc_ethtool_coalesce {
        u32 cmd;    /* ETHTOOL_{G,S}COALESCE */

        /* How many usecs to delay an RX interrupt after
         * a packet arrives.  If 0, only rx_max_coalesced_frames
         * is used.
         */
        u32 rx_coalesce_usecs;

        /* How many packets to delay an RX interrupt after
         * a packet arrives.  If 0, only rx_coalesce_usecs is
         * used.  It is illegal to set both usecs and max frames
         * to zero as this would cause RX interrupts to never be
         * generated.
         */
        u32 rx_max_coalesced_frames;

        /* Same as above two parameters, except that these values
         * apply while an IRQ is being serviced by the host.  Not
         * all cards support this feature and the values are ignored
         * in that case.
         */
        u32 rx_coalesce_usecs_irq;
        u32 rx_max_coalesced_frames_irq;

        /* How many usecs to delay a TX interrupt after
         * a packet is sent.  If 0, only tx_max_coalesced_frames
         * is used.
         */
        u32 tx_coalesce_usecs;

        /* How many packets to delay a TX interrupt after
         * a packet is sent.  If 0, only tx_coalesce_usecs is
         * used.  It is illegal to set both usecs and max frames
         * to zero as this would cause TX interrupts to never be
         * generated.
         */
        u32 tx_max_coalesced_frames;

        /* Same as above two parameters, except that these values
         * apply while an IRQ is being serviced by the host.  Not
         * all cards support this feature and the values are ignored
         * in that case.
         */
        u32 tx_coalesce_usecs_irq;
        u32 tx_max_coalesced_frames_irq;

        /* How many usecs to delay in-memory statistics
         * block updates.  Some drivers do not have an in-memory
         * statistic block, and in such cases this value is ignored.
         * This value must not be zero.
         */
        u32 stats_block_coalesce_usecs;

        /* Adaptive RX/TX coalescing is an algorithm implemented by
         * some drivers to improve latency under low packet rates and
         * improve throughput under high packet rates.  Some drivers
         * only implement one of RX or TX adaptive coalescing.  Anything
         * not implemented by the driver causes these values to be
         * silently ignored.
         */
        u32 use_adaptive_rx_coalesce;
        u32 use_adaptive_tx_coalesce;

        /* When the packet rate (measured in packets per second)
         * is below pkt_rate_low, the {rx,tx}_*_low parameters are
         * used.
         */
        u32 pkt_rate_low;
        u32 rx_coalesce_usecs_low;
        u32 rx_max_coalesced_frames_low;
        u32 tx_coalesce_usecs_low;
        u32 tx_max_coalesced_frames_low;

        /* When the packet rate is below pkt_rate_high but above
         * pkt_rate_low (both measured in packets per second) the
         * normal {rx,tx}_* coalescing parameters are used.
         */

        /* When the packet rate is (measured in packets per second)
         * is above pkt_rate_high, the {rx,tx}_*_high parameters are
         * used.
         */
        u32 pkt_rate_high;
        u32 rx_coalesce_usecs_high;
        u32 rx_max_coalesced_frames_high;
        u32 tx_coalesce_usecs_high;
        u32 tx_max_coalesced_frames_high;

        /* How often to do adaptive coalescing packet rate sampling,
         * measured in seconds.  Must not be zero.
         */
        u32 rate_sample_interval;
};
#endif /* ETHTOOL_GCOALESCE */

#ifndef ETHTOOL_SCOALESCE
#define ETHTOOL_SCOALESCE   0x0000000f /* Set coalesce config. */
#endif
#ifndef ETHTOOL_GRINGPARAM
#define ETHTOOL_GRINGPARAM  0x00000010 /* Get ring parameters */
/* for configuring RX/TX ring parameters */
#define ethtool_ringparam _kc_ethtool_ringparam
struct _kc_ethtool_ringparam {
        u32 cmd;    /* ETHTOOL_{G,S}RINGPARAM */

        /* Read only attributes.  These indicate the maximum number
         * of pending RX/TX ring entries the driver will allow the
         * user to set.
         */
        u32 rx_max_pending;
        u32 rx_mini_max_pending;
        u32 rx_jumbo_max_pending;
        u32 tx_max_pending;

        /* Values changeable by the user.  The valid values are
         * in the range 1 to the "*_max_pending" counterpart above.
         */
        u32 rx_pending;
        u32 rx_mini_pending;
        u32 rx_jumbo_pending;
        u32 tx_pending;
};
#endif /* ETHTOOL_GRINGPARAM */

#ifndef ETHTOOL_SRINGPARAM
#define ETHTOOL_SRINGPARAM  0x00000011 /* Set ring parameters, priv. */
#endif
#ifndef ETHTOOL_GPAUSEPARAM
#define ETHTOOL_GPAUSEPARAM 0x00000012 /* Get pause parameters */
/* for configuring link flow control parameters */
#define ethtool_pauseparam _kc_ethtool_pauseparam
struct _kc_ethtool_pauseparam {
        u32 cmd;    /* ETHTOOL_{G,S}PAUSEPARAM */

        /* If the link is being auto-negotiated (via ethtool_cmd.autoneg
         * being true) the user may set 'autonet' here non-zero to have the
         * pause parameters be auto-negotiated too.  In such a case, the
         * {rx,tx}_pause values below determine what capabilities are
         * advertised.
         *
         * If 'autoneg' is zero or the link is not being auto-negotiated,
         * then {rx,tx}_pause force the driver to use/not-use pause
         * flow control.
         */
        u32 autoneg;
        u32 rx_pause;
        u32 tx_pause;
};
#endif /* ETHTOOL_GPAUSEPARAM */

#ifndef ETHTOOL_SPAUSEPARAM
#define ETHTOOL_SPAUSEPARAM 0x00000013 /* Set pause parameters. */
#endif
#ifndef ETHTOOL_GRXCSUM
#define ETHTOOL_GRXCSUM     0x00000014 /* Get RX hw csum enable (ethtool_value) */
#endif
#ifndef ETHTOOL_SRXCSUM
#define ETHTOOL_SRXCSUM     0x00000015 /* Set RX hw csum enable (ethtool_value) */
#endif
#ifndef ETHTOOL_GTXCSUM
#define ETHTOOL_GTXCSUM     0x00000016 /* Get TX hw csum enable (ethtool_value) */
#endif
#ifndef ETHTOOL_STXCSUM
#define ETHTOOL_STXCSUM     0x00000017 /* Set TX hw csum enable (ethtool_value) */
#endif
#ifndef ETHTOOL_GSG
#define ETHTOOL_GSG     0x00000018 /* Get scatter-gather enable
* (ethtool_value) */
#endif
#ifndef ETHTOOL_SSG
#define ETHTOOL_SSG     0x00000019 /* Set scatter-gather enable
* (ethtool_value). */
#endif
#ifndef ETHTOOL_TEST
#define ETHTOOL_TEST        0x0000001a /* execute NIC self-test, priv. */
#endif
#ifndef ETHTOOL_GSTRINGS
#define ETHTOOL_GSTRINGS    0x0000001b /* get specified string set */
#endif
#ifndef ETHTOOL_PHYS_ID
#define ETHTOOL_PHYS_ID     0x0000001c /* identify the NIC */
#endif
#ifndef ETHTOOL_GSTATS
#define ETHTOOL_GSTATS      0x0000001d /* get NIC-specific statistics */
#endif
#ifndef ETHTOOL_GTSO
#define ETHTOOL_GTSO        0x0000001e /* Get TSO enable (ethtool_value) */
#endif
#ifndef ETHTOOL_STSO
#define ETHTOOL_STSO        0x0000001f /* Set TSO enable (ethtool_value) */
#endif

#ifndef ETHTOOL_BUSINFO_LEN
#define ETHTOOL_BUSINFO_LEN 32
#endif

/*****************************************************************************/

enum RTL8168_DSM_STATE {
        DSM_MAC_INIT = 1,
        DSM_NIC_GOTO_D3 = 2,
        DSM_IF_DOWN = 3,
        DSM_NIC_RESUME_D3 = 4,
        DSM_IF_UP = 5,
};

enum RTL8168_registers {
        MAC0            = 0x00,     /* Ethernet hardware address. */
        MAC4            = 0x04,
        MAR0            = 0x08,     /* Multicast filter. */
        CounterAddrLow      = 0x10,
        CounterAddrHigh     = 0x14,
        CustomLED       = 0x18,
        TxDescStartAddrLow  = 0x20,
        TxDescStartAddrHigh = 0x24,
        TxHDescStartAddrLow = 0x28,
        TxHDescStartAddrHigh    = 0x2c,
        FLASH           = 0x30,
        ERSR            = 0x36,
        ChipCmd         = 0x37,
        TxPoll          = 0x38,
        IntrMask        = 0x3C,
        IntrStatus      = 0x3E,
        TxConfig        = 0x40,
        RxConfig        = 0x44,
        TCTR            = 0x48,
        Cfg9346         = 0x50,
        Config0         = 0x51,
        Config1         = 0x52,
        Config2         = 0x53,
        Config3         = 0x54,
        Config4         = 0x55,
        Config5         = 0x56,
        TDFNR           = 0x57,
        TimeInt0        = 0x58,
        TimeInt1        = 0x5C,
        PHYAR           = 0x60,
        CSIDR           = 0x64,
        CSIAR           = 0x68,
        PHYstatus       = 0x6C,
        MACDBG          = 0x6D,
        GPIO            = 0x6E,
        PMCH            = 0x6F,
        ERIDR           = 0x70,
        ERIAR           = 0x74,
        EPHY_RXER_NUM   = 0x7C,
        EPHYAR          = 0x80,
        TimeInt2        = 0x8C,
        OCPDR           = 0xB0,
        MACOCP          = 0xB0,
        OCPAR           = 0xB4,
        SecMAC0         = 0xB4,
        SecMAC4         = 0xB8,
        PHYOCP          = 0xB8,
        DBG_reg         = 0xD1,
        TwiCmdReg       = 0xD2,
        MCUCmd_reg      = 0xD3,
        RxMaxSize       = 0xDA,
        EFUSEAR         = 0xDC,
        CPlusCmd        = 0xE0,
        IntrMitigate    = 0xE2,
        RxDescAddrLow   = 0xE4,
        RxDescAddrHigh  = 0xE8,
        MTPS            = 0xEC,
        FuncEvent       = 0xF0,
        PPSW            = 0xF2,
        FuncEventMask   = 0xF4,
        TimeInt3        = 0xF4,
        FuncPresetState = 0xF8,
        CMAC_IBCR0      = 0xF8,
        CMAC_IBCR2      = 0xF9,
        CMAC_IBIMR0     = 0xFA,
        CMAC_IBISR0     = 0xFB,
        FuncForceEvent  = 0xFC,
};

enum RTL8168_register_content {
        /* InterruptStatusBits */
        SYSErr      = 0x8000,
        PCSTimeout  = 0x4000,
        SWInt       = 0x0100,
        TxDescUnavail   = 0x0080,
        RxFIFOOver  = 0x0040,
        LinkChg     = 0x0020,
        RxDescUnavail   = 0x0010,
        TxErr       = 0x0008,
        TxOK        = 0x0004,
        RxErr       = 0x0002,
        RxOK        = 0x0001,

        /* RxStatusDesc */
        RxRWT = (1 << 22),
        RxRES = (1 << 21),
        RxRUNT = (1 << 20),
        RxCRC = (1 << 19),

        /* ChipCmdBits */
        StopReq  = 0x80,
        CmdReset = 0x10,
        CmdRxEnb = 0x08,
        CmdTxEnb = 0x04,
        RxBufEmpty = 0x01,

        /* Cfg9346Bits */
        Cfg9346_Lock = 0x00,
        Cfg9346_Unlock = 0xC0,
        Cfg9346_EEDO = (1 << 0),
        Cfg9346_EEDI = (1 << 1),
        Cfg9346_EESK = (1 << 2),
        Cfg9346_EECS = (1 << 3),
        Cfg9346_EEM0 = (1 << 6),
        Cfg9346_EEM1 = (1 << 7),

        /* rx_mode_bits */
        AcceptErr = 0x20,
        AcceptRunt = 0x10,
        AcceptBroadcast = 0x08,
        AcceptMulticast = 0x04,
        AcceptMyPhys = 0x02,
        AcceptAllPhys = 0x01,

        /* Transmit Priority Polling*/
        HPQ = 0x80,
        NPQ = 0x40,
        FSWInt = 0x01,

        /* RxConfigBits */
        Reserved2_shift = 13,
        RxCfgDMAShift = 8,
        RxCfg_128_int_en = (1 << 15),
        RxCfg_fet_multi_en = (1 << 14),
        RxCfg_half_refetch = (1 << 13),
        RxCfg_9356SEL = (1 << 6),

        /* TxConfigBits */
        TxInterFrameGapShift = 24,
        TxDMAShift = 8, /* DMA burst value (0-7) is shift this many bits */
        TxMACLoopBack = (1 << 17),  /* MAC loopback */

        /* Config1 register */
        LEDS1       = (1 << 7),
        LEDS0       = (1 << 6),
        Speed_down  = (1 << 4),
        MEMMAP      = (1 << 3),
        IOMAP       = (1 << 2),
        VPD         = (1 << 1),
        PMEnable    = (1 << 0), /* Power Management Enable */

        /* Config2 register */
        ClkReqEn	= (1 << 7),	/* Clock Request Enable */
        PMSTS_En    = (1 << 5),

        /* Config3 register */
        Isolate_en  = (1 << 12), /* Isolate enable */
        MagicPacket = (1 << 5), /* Wake up when receives a Magic Packet */
        LinkUp      = (1 << 4), /* This bit is reserved in RTL8168B.*/
        /* Wake up when the cable connection is re-established */
        ECRCEN      = (1 << 3), /* This bit is reserved in RTL8168B*/
        Jumbo_En0   = (1 << 2), /* This bit is reserved in RTL8168B*/
        RDY_TO_L23  = (1 << 1), /* This bit is reserved in RTL8168B*/
        Beacon_en   = (1 << 0), /* This bit is reserved in RTL8168B*/

        /* Config4 register */
        Jumbo_En1   = (1 << 1), /* This bit is reserved in RTL8168B*/

        /* Config5 register */
        BWF     = (1 << 6), /* Accept Broadcast wakeup frame */
        MWF     = (1 << 5), /* Accept Multicast wakeup frame */
        UWF     = (1 << 4), /* Accept Unicast wakeup frame */
        LanWake     = (1 << 1), /* LanWake enable/disable */
        PMEStatus   = (1 << 0), /* PME status can be reset by PCI RST# */
        ASPM_en	    = (1 << 0),	/* ASPM enable */

        /* CPlusCmd */
        EnableBist  = (1 << 15),
        Macdbgo_oe  = (1 << 14),
        Normal_mode = (1 << 13),
        Force_halfdup   = (1 << 12),
        Force_rxflow_en = (1 << 11),
        Force_txflow_en = (1 << 10),
        Cxpl_dbg_sel    = (1 << 9),//This bit is reserved in RTL8168B
        ASF     = (1 << 8),//This bit is reserved in RTL8168C
        PktCntrDisable  = (1 << 7),
        RxVlan      = (1 << 6),
        RxChkSum    = (1 << 5),
        Macdbgo_sel = 0x001C,
        INTT_0      = 0x0000,
        INTT_1      = 0x0001,
        INTT_2      = 0x0002,
        INTT_3      = 0x0003,

        /* rtl8168_PHYstatus */
        PowerSaveStatus = 0x80,
        TxFlowCtrl = 0x40,
        RxFlowCtrl = 0x20,
        _1000bpsF = 0x10,
        _100bps = 0x08,
        _10bps = 0x04,
        LinkStatus = 0x02,
        FullDup = 0x01,

        /* DBG_reg */
        Fix_Nak_1 = (1 << 4),
        Fix_Nak_2 = (1 << 3),
        DBGPIN_E2 = (1 << 0),

        /* ResetCounterCommand */
        CounterReset = 0x1,
        /* DumpCounterCommand */
        CounterDump = 0x8,

        /* PHY access */
        PHYAR_Flag = 0x80000000,
        PHYAR_Write = 0x80000000,
        PHYAR_Read = 0x00000000,
        PHYAR_Reg_Mask = 0x1f,
        PHYAR_Reg_shift = 16,
        PHYAR_Data_Mask = 0xffff,

        /* EPHY access */
        EPHYAR_Flag = 0x80000000,
        EPHYAR_Write = 0x80000000,
        EPHYAR_Read = 0x00000000,
        EPHYAR_Reg_Mask = 0x3f,
        EPHYAR_Reg_shift = 16,
        EPHYAR_Data_Mask = 0xffff,

        /* CSI access */
        CSIAR_Flag = 0x80000000,
        CSIAR_Write = 0x80000000,
        CSIAR_Read = 0x00000000,
        CSIAR_ByteEn = 0x0f,
        CSIAR_ByteEn_shift = 12,
        CSIAR_Addr_Mask = 0x0fff,

        /* ERI access */
        ERIAR_Flag = 0x80000000,
        ERIAR_Write = 0x80000000,
        ERIAR_Read = 0x00000000,
        ERIAR_Addr_Align = 4, /* ERI access register address must be 4 byte alignment */
        ERIAR_ExGMAC = 0,
        ERIAR_MSIX = 1,
        ERIAR_ASF = 2,
        ERIAR_OOB = 2,
        ERIAR_Type_shift = 16,
        ERIAR_ByteEn = 0x0f,
        ERIAR_ByteEn_shift = 12,

        /* OCP GPHY access */
        OCPDR_Write = 0x80000000,
        OCPDR_Read = 0x00000000,
        OCPDR_Reg_Mask = 0xFF,
        OCPDR_Data_Mask = 0xFFFF,
        OCPDR_GPHY_Reg_shift = 16,
        OCPAR_Flag = 0x80000000,
        OCPAR_GPHY_Write = 0x8000F060,
        OCPAR_GPHY_Read = 0x0000F060,
        OCPR_Write = 0x80000000,
        OCPR_Read = 0x00000000,
        OCPR_Addr_Reg_shift = 16,
        OCPR_Flag = 0x80000000,
        OCP_STD_PHY_BASE_PAGE = 0x0A40,

        /* MCU Command */
        Now_is_oob = (1 << 7),
        Txfifo_empty = (1 << 5),
        Rxfifo_empty = (1 << 4),

        /* E-FUSE access */
        EFUSE_WRITE = 0x80000000,
        EFUSE_WRITE_OK  = 0x00000000,
        EFUSE_READ  = 0x00000000,
        EFUSE_READ_OK   = 0x80000000,
        EFUSE_WRITE_V3 = 0x40000000,
        EFUSE_WRITE_OK_V3  = 0x00000000,
        EFUSE_READ_V3  = 0x80000000,
        EFUSE_READ_OK_V3   = 0x00000000,
        EFUSE_Reg_Mask  = 0x03FF,
        EFUSE_Reg_Shift = 8,
        EFUSE_Check_Cnt = 300,
        EFUSE_READ_FAIL = 0xFF,
        EFUSE_Data_Mask = 0x000000FF,

        /* GPIO */
        GPIO_en = (1 << 0),

};

enum _DescStatusBit {
        DescOwn     = (1 << 31), /* Descriptor is owned by NIC */
        RingEnd     = (1 << 30), /* End of descriptor ring */
        FirstFrag   = (1 << 29), /* First segment of a packet */
        LastFrag    = (1 << 28), /* Final segment of a packet */

        /* Tx private */
        /*------ offset 0 of tx descriptor ------*/
        LargeSend   = (1 << 27), /* TCP Large Send Offload (TSO) */
        GiantSendv4 = (1 << 26), /* TCP Giant Send Offload V4 (GSOv4) */
        GiantSendv6 = (1 << 25), /* TCP Giant Send Offload V6 (GSOv6) */
        LargeSend_DP = (1 << 16), /* TCP Large Send Offload (TSO) */
        MSSShift    = 16,        /* MSS value position */
        MSSMask     = 0x7FFU,    /* MSS value 11 bits */
        TxIPCS      = (1 << 18), /* Calculate IP checksum */
        TxUDPCS     = (1 << 17), /* Calculate UDP/IP checksum */
        TxTCPCS     = (1 << 16), /* Calculate TCP/IP checksum */
        TxVlanTag   = (1 << 17), /* Add VLAN tag */

        /*@@@@@@ offset 4 of tx descriptor => bits for RTL8168C/CP only     begin @@@@@@*/
        TxUDPCS_C   = (1 << 31), /* Calculate UDP/IP checksum */
        TxTCPCS_C   = (1 << 30), /* Calculate TCP/IP checksum */
        TxIPCS_C    = (1 << 29), /* Calculate IP checksum */
        TxIPV6F_C   = (1 << 28), /* Indicate it is an IPv6 packet */
        /*@@@@@@ offset 4 of tx descriptor => bits for RTL8168C/CP only     end @@@@@@*/


        /* Rx private */
        /*------ offset 0 of rx descriptor ------*/
        PID1        = (1 << 18), /* Protocol ID bit 1/2 */
        PID0        = (1 << 17), /* Protocol ID bit 2/2 */

#define RxProtoUDP  (PID1)
#define RxProtoTCP  (PID0)
#define RxProtoIP   (PID1 | PID0)
#define RxProtoMask RxProtoIP

        RxIPF       = (1 << 16), /* IP checksum failed */
        RxUDPF      = (1 << 15), /* UDP/IP checksum failed */
        RxTCPF      = (1 << 14), /* TCP/IP checksum failed */
        RxVlanTag   = (1 << 16), /* VLAN tag available */

        /*@@@@@@ offset 0 of rx descriptor => bits for RTL8168C/CP only     begin @@@@@@*/
        RxUDPT      = (1 << 18),
        RxTCPT      = (1 << 17),
        /*@@@@@@ offset 0 of rx descriptor => bits for RTL8168C/CP only     end @@@@@@*/

        /*@@@@@@ offset 4 of rx descriptor => bits for RTL8168C/CP only     begin @@@@@@*/
        RxV6F       = (1 << 31),
        RxV4F       = (1 << 30),
        /*@@@@@@ offset 4 of rx descriptor => bits for RTL8168C/CP only     end @@@@@@*/
};

enum features {
//  RTL_FEATURE_WOL = (1 << 0),
        RTL_FEATURE_MSI = (1 << 1),
};

enum wol_capability {
        WOL_DISABLED = 0,
        WOL_ENABLED = 1
};

enum bits {
        BIT_0 = (1 << 0),
        BIT_1 = (1 << 1),
        BIT_2 = (1 << 2),
        BIT_3 = (1 << 3),
        BIT_4 = (1 << 4),
        BIT_5 = (1 << 5),
        BIT_6 = (1 << 6),
        BIT_7 = (1 << 7),
        BIT_8 = (1 << 8),
        BIT_9 = (1 << 9),
        BIT_10 = (1 << 10),
        BIT_11 = (1 << 11),
        BIT_12 = (1 << 12),
        BIT_13 = (1 << 13),
        BIT_14 = (1 << 14),
        BIT_15 = (1 << 15),
        BIT_16 = (1 << 16),
        BIT_17 = (1 << 17),
        BIT_18 = (1 << 18),
        BIT_19 = (1 << 19),
        BIT_20 = (1 << 20),
        BIT_21 = (1 << 21),
        BIT_22 = (1 << 22),
        BIT_23 = (1 << 23),
        BIT_24 = (1 << 24),
        BIT_25 = (1 << 25),
        BIT_26 = (1 << 26),
        BIT_27 = (1 << 27),
        BIT_28 = (1 << 28),
        BIT_29 = (1 << 29),
        BIT_30 = (1 << 30),
        BIT_31 = (1 << 31)
};

enum effuse {
        EFUSE_NOT_SUPPORT = 0,
        EFUSE_SUPPORT_V1,
        EFUSE_SUPPORT_V2,
        EFUSE_SUPPORT_V3,
};
#define RsvdMask    0x3fffc000

struct TxDesc {
        u32 opts1;
        u32 opts2;
        u64 addr;
};

struct RxDesc {
        u32 opts1;
        u32 opts2;
        u64 addr;
};

struct ring_info {
        struct sk_buff  *skb;
        u32     len;
        u8      __pad[sizeof(void *) - sizeof(u32)];
};

struct pci_resource {
        u8  cmd;
        u8  cls;
        u16 io_base_h;
        u16 io_base_l;
        u16 mem_base_h;
        u16 mem_base_l;
        u8  ilr;
        u16 resv_0x1c_h;
        u16 resv_0x1c_l;
        u16 resv_0x20_h;
        u16 resv_0x20_l;
        u16 resv_0x24_h;
        u16 resv_0x24_l;
        u16 resv_0x2c_h;
        u16 resv_0x2c_l;
        u32 pci_sn_l;
        u32 pci_sn_h;
};

struct rtl8168_private {
        void __iomem *mmio_addr;    /* memory map physical address */
        struct pci_dev *pci_dev;    /* Index of PCI device */
        struct net_device *dev;
#ifdef CONFIG_R8168_NAPI
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
        struct napi_struct napi;
#endif
#endif
        struct net_device_stats stats;  /* statistics of net device */
        spinlock_t lock;        /* spin lock flag */
        u32 msg_enable;
        u32 tx_tcp_csum_cmd;
        u32 tx_udp_csum_cmd;
        u32 tx_ip_csum_cmd;
        u32 tx_ipv6_csum_cmd;
        int max_jumbo_frame_size;
        int chipset;
        u32 mcfg;
        u32 cur_rx; /* Index into the Rx descriptor buffer of next Rx pkt. */
        u32 cur_tx; /* Index into the Tx descriptor buffer of next Rx pkt. */
        u32 dirty_rx;
        u32 dirty_tx;
        struct TxDesc *TxDescArray; /* 256-aligned Tx descriptor ring */
        struct RxDesc *RxDescArray; /* 256-aligned Rx descriptor ring */
        dma_addr_t TxPhyAddr;
        dma_addr_t RxPhyAddr;
        struct sk_buff *Rx_skbuff[NUM_RX_DESC]; /* Rx data buffers */
        struct ring_info tx_skb[NUM_TX_DESC];   /* Tx data buffers */
        unsigned rx_buf_sz;
        struct timer_list esd_timer;
        struct timer_list link_timer;
        struct pci_resource pci_cfg_space;
        unsigned int esd_flag;
        unsigned int pci_cfg_is_read;
        unsigned int rtl8168_rx_config;
        u16 cp_cmd;
        u16 intr_mask;
        u16 timer_intr_mask;
        int phy_auto_nego_reg;
        int phy_1000_ctrl_reg;
        u8 org_mac_addr[NODE_ADDRESS_SIZE];
        struct rtl8168_counters *tally_vaddr;
        dma_addr_t tally_paddr;

#ifdef CONFIG_R8168_VLAN
        struct vlan_group *vlgrp;
#endif
        u8  wol_enabled;
        u32 wol_opts;
        u8  efuse_ver;
        u8  eeprom_type;
        u8  autoneg;
        u8  duplex;
        u32 speed;
        u32 advertising;
        u16 eeprom_len;
        u16 cur_page;
        u32 bios_setting;

        int (*set_speed)(struct net_device *, u8 autoneg, u32 speed, u8 duplex, u32 adv);
#if LINUX_VERSION_CODE < KERNEL_VERSION(4,6,0)
        void (*get_settings)(struct net_device *, struct ethtool_cmd *);
#else
        void (*get_settings)(struct net_device *, struct ethtool_link_ksettings *);
#endif
        void (*phy_reset_enable)(struct net_device *);
        unsigned int (*phy_reset_pending)(struct net_device *);
        unsigned int (*link_ok)(struct net_device *);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
        struct work_struct task;
#else
        struct delayed_work task;
#endif
        unsigned features;

        u8 org_pci_offset_99;
        u8 org_pci_offset_180;
        u8 issue_offset_99_event;

        u8 org_pci_offset_80;
        u8 org_pci_offset_81;
        u8 use_timer_interrrupt;

        u32 keep_intr_cnt;

        u8  HwIcVerUnknown;
        u8  NotWrRamCodeToMicroP;
        u8  NotWrMcuPatchCode;
        u8  HwHasWrRamCodeToMicroP;

        u16 sw_ram_code_ver;
        u16 hw_ram_code_ver;

        u8 rtk_enable_diag;

        u8 ShortPacketSwChecksum;

        u8 UseSwPaddingShortPkt;

        u8 RequireAdcBiasPatch;
        u16 AdcBiasPatchIoffset;

        u8 RequireAdjustUpsTxLinkPulseTiming;
        u16 SwrCnt1msIni;

        u8 HwSuppNowIsOobVer;

        u8 RequiredSecLanDonglePatch;

        u32 HwFiberModeVer;
        u32 HwFiberStat;
        u8 HwSwitchMdiToFiber;

        u8 HwSuppSerDesPhyVer;

        u8 HwSuppPhyOcpVer;

        u8 HwSuppAspmClkIntrLock;

        u16 NicCustLedValue;

        u8 HwSuppUpsVer;

        u8 HwSuppMagicPktVer;

        u8 HwSuppCheckPhyDisableModeVer;

        u8 random_mac;

        u16 phy_reg_aner;
        u16 phy_reg_anlpar;
        u16 phy_reg_gbsr;

        u32 HwPcieSNOffset;

        u8 HwSuppEsdVer;
        u8 TestPhyOcpReg;
        u16 BackupPhyFuseDout_15_0;
        u16 BackupPhyFuseDout_47_32;
        u16 BackupPhyFuseDout_63_48;

        const char *fw_name;
        struct rtl8168_fw *rtl_fw;
        u32 ocp_base;

        //Dash+++++++++++++++++
        u8 HwSuppDashVer;
        u8 DASH;
        u8 dash_printer_enabled;
        u8 HwPkgDet;
        void __iomem *mapped_cmac_ioaddr; /* mapped cmac memory map physical address */
        void __iomem *cmac_ioaddr; /* cmac memory map physical address */

#ifdef ENABLE_DASH_SUPPORT
        u16 AfterRecvFromFwBufLen;
        u8 AfterRecvFromFwBuf[RECV_FROM_FW_BUF_SIZE];
        u16 AfterSendToFwBufLen;
        u8 AfterSendToFwBuf[SEND_TO_FW_BUF_SIZE];
        u16 SendToFwBufferLen;
        u32 SizeOfSendToFwBuffer ;
        u32 SizeOfSendToFwBufferMemAlloc ;
        u32 NumOfSendToFwBuffer ;

        u8 OobReq;
        u8 OobAck;
        u32 OobReqComplete;
        u32 OobAckComplete;

        u8 RcvFwReqSysOkEvt;
        u8 RcvFwDashOkEvt;
        u8 SendFwHostOkEvt;

        u8 DashFwDisableRx;

        void *SendToFwBuffer ;
        dma_addr_t SendToFwBufferPhy ;
        u8 SendingToFw;
        PTX_DASH_SEND_FW_DESC TxDashSendFwDesc;
        dma_addr_t TxDashSendFwDescPhy;
        u32 SizeOfTxDashSendFwDescMemAlloc;
        u32 SizeOfTxDashSendFwDesc ;
        u32 NumTxDashSendFwDesc ;
        u32 CurrNumTxDashSendFwDesc ;
        u32 LastSendNumTxDashSendFwDesc ;

        u32 NumRecvFromFwBuffer ;
        u32 SizeOfRecvFromFwBuffer ;
        u32 SizeOfRecvFromFwBufferMemAlloc ;
        void *RecvFromFwBuffer ;
        dma_addr_t RecvFromFwBufferPhy ;

        PRX_DASH_FROM_FW_DESC RxDashRecvFwDesc;
        dma_addr_t RxDashRecvFwDescPhy;
        u32 SizeOfRxDashRecvFwDescMemAlloc;
        u32 SizeOfRxDashRecvFwDesc ;
        u32 NumRxDashRecvFwDesc ;
        u32 CurrNumRxDashRecvFwDesc ;
        u8 DashReqRegValue;
        u16 HostReqValue;

        u32 CmacResetIsrCounter;
        u8 CmacResetIntr ;
        u8 CmacResetting ;
        u8 CmacOobIssueCmacReset ;
        u32 CmacResetbyFwCnt;

#if defined(ENABLE_DASH_PRINTER_SUPPORT)
        struct completion fw_ack;
        struct completion fw_req;
        struct completion fw_host_ok;
#endif
        //Dash-----------------
#endif //ENABLE_DASH_SUPPORT

        //Realwow++++++++++++++
        u8 HwSuppKCPOffloadVer;

        u8 EnableDhcpTimeoutWake;
        u8 EnableTeredoOffload;
        u8 EnableKCPOffload;
#ifdef ENABLE_REALWOW_SUPPORT
        u32 DhcpTimeout;
        MP_KCP_INFO MpKCPInfo;
        //Realwow--------------
#endif //ENABLE_REALWOW_SUPPORT

        u32 eee_adv_t;
        u8 eee_enabled;

        u32 dynamic_aspm_packet_count;

#ifdef ENABLE_R8168_PROCFS
        //Procfs support
        struct proc_dir_entry *proc_dir;
#endif
};

enum eetype {
        EEPROM_TYPE_NONE=0,
        EEPROM_TYPE_93C46,
        EEPROM_TYPE_93C56,
        EEPROM_TWSI
};

enum mcfg {
        CFG_METHOD_1=0,
        CFG_METHOD_2,
        CFG_METHOD_3,
        CFG_METHOD_4,
        CFG_METHOD_5,
        CFG_METHOD_6,
        CFG_METHOD_7,
        CFG_METHOD_8,
        CFG_METHOD_9 ,
        CFG_METHOD_10,
        CFG_METHOD_11,
        CFG_METHOD_12,
        CFG_METHOD_13,
        CFG_METHOD_14,
        CFG_METHOD_15,
        CFG_METHOD_16,
        CFG_METHOD_17,
        CFG_METHOD_18,
        CFG_METHOD_19,
        CFG_METHOD_20,
        CFG_METHOD_21,
        CFG_METHOD_22,
        CFG_METHOD_23,
        CFG_METHOD_24,
        CFG_METHOD_25,
        CFG_METHOD_26,
        CFG_METHOD_27,
        CFG_METHOD_28,
        CFG_METHOD_29,
        CFG_METHOD_30,
        CFG_METHOD_31,
        CFG_METHOD_32,
        CFG_METHOD_33,
        CFG_METHOD_MAX,
        CFG_METHOD_DEFAULT = 0xFF
};

#define LSO_32K 32000
#define LSO_64K 64000

#define NIC_MIN_PHYS_BUF_COUNT          (2)
#define NIC_MAX_PHYS_BUF_COUNT_LSO_64K  (24)
#define NIC_MAX_PHYS_BUF_COUNT_LSO2     (16*4)

#define GTTCPHO_SHIFT                   18
#define GTTCPHO_MAX                     0x7fU
#define GTPKTSIZE_MAX                   0x3ffffU
#define TCPHO_SHIFT                     18
#define TCPHO_MAX                       0x3ffU
#define LSOPKTSIZE_MAX                  0xffffU
#define MSS_MAX                         0x07ffu /* MSS value */

#define OOB_CMD_RESET       0x00
#define OOB_CMD_DRIVER_START    0x05
#define OOB_CMD_DRIVER_STOP 0x06
#define OOB_CMD_SET_IPMAC   0x41

#define WAKEUP_MAGIC_PACKET_NOT_SUPPORT (0)
#define WAKEUP_MAGIC_PACKET_V1 (1)
#define WAKEUP_MAGIC_PACKET_V2 (2)

//Ram Code Version
#define NIC_RAMCODE_VERSION_CFG_METHOD_14 (0x0057)
#define NIC_RAMCODE_VERSION_CFG_METHOD_16 (0x0055)
#define NIC_RAMCODE_VERSION_CFG_METHOD_18 (0x0052)
#define NIC_RAMCODE_VERSION_CFG_METHOD_20 (0x0044)
#define NIC_RAMCODE_VERSION_CFG_METHOD_21 (0x0042)
#define NIC_RAMCODE_VERSION_CFG_METHOD_24 (0x0001)
#define NIC_RAMCODE_VERSION_CFG_METHOD_23 (0x0015)
#define NIC_RAMCODE_VERSION_CFG_METHOD_26 (0x0012)
#define NIC_RAMCODE_VERSION_CFG_METHOD_28 (0x0019)
#define NIC_RAMCODE_VERSION_CFG_METHOD_29 (0x0055)
#define NIC_RAMCODE_VERSION_CFG_METHOD_31 (0x0003)

//hwoptimize
#define HW_PATCH_SOC_LAN (BIT_0)
#define HW_PATCH_SAMSUNG_LAN_DONGLE (BIT_2)

#define HW_PHY_STATUS_INI       1
#define HW_PHY_STATUS_EXT_INI   2
#define HW_PHY_STATUS_LAN_ON    3

void rtl8168_mdio_write(struct rtl8168_private *tp, u16 RegAddr, u16 value);
void rtl8168_mdio_prot_write(struct rtl8168_private *tp, u32 RegAddr, u32 value);
void rtl8168_mdio_prot_direct_write_phy_ocp(struct rtl8168_private *tp, u32 RegAddr, u32 value);
u32 rtl8168_mdio_read(struct rtl8168_private *tp, u16 RegAddr);
u32 rtl8168_mdio_prot_read(struct rtl8168_private *tp, u32 RegAddr);
u32 rtl8168_mdio_prot_direct_read_phy_ocp(struct rtl8168_private *tp, u32 RegAddr);
void rtl8168_ephy_write(struct rtl8168_private *tp, int RegAddr, int value);
void rtl8168_mac_ocp_write(struct rtl8168_private *tp, u16 reg_addr, u16 value);
u16 rtl8168_mac_ocp_read(struct rtl8168_private *tp, u16 reg_addr);
void rtl8168_clear_eth_phy_bit(struct rtl8168_private *tp, u8 addr, u16 mask);
void rtl8168_set_eth_phy_bit(struct rtl8168_private *tp,  u8  addr, u16  mask);
void rtl8168_ocp_write(struct rtl8168_private *tp, u16 addr, u8 len, u32 data);
void rtl8168_oob_notify(struct rtl8168_private *tp, u8 cmd);
void rtl8168_init_ring_indexes(struct rtl8168_private *tp);
int rtl8168_eri_write(struct rtl8168_private *tp, int addr, int len, u32 value, int type);
void rtl8168_oob_mutex_lock(struct rtl8168_private *tp);
u32 rtl8168_ocp_read(struct rtl8168_private *tp, u16 addr, u8 len);
u32 rtl8168_ocp_read_with_oob_base_address(struct rtl8168_private *tp, u16 addr, u8 len, u32 base_address);
u32 rtl8168_ocp_write_with_oob_base_address(struct rtl8168_private *tp, u16 addr, u8 len, u32 value, u32 base_address);
u32 rtl8168_eri_read(struct rtl8168_private *tp, int addr, int len, int type);
u32 rtl8168_eri_read_with_oob_base_address(struct rtl8168_private *tp, int addr, int len, int type, u32 base_address);
int rtl8168_eri_write_with_oob_base_address(struct rtl8168_private *tp, int addr, int len, u32 value, int type, u32 base_address);
u16 rtl8168_ephy_read(struct rtl8168_private *tp, int RegAddr);
void rtl8168_wait_txrx_fifo_empty(struct net_device *dev);
void rtl8168_wait_ll_share_fifo_ready(struct net_device *dev);
void rtl8168_enable_now_is_oob(struct rtl8168_private *tp);
void rtl8168_disable_now_is_oob(struct rtl8168_private *tp);
void rtl8168_oob_mutex_unlock(struct rtl8168_private *tp);
void rtl8168_dash2_disable_tx(struct rtl8168_private *tp);
void rtl8168_dash2_enable_tx(struct rtl8168_private *tp);
void rtl8168_dash2_disable_rx(struct rtl8168_private *tp);
void rtl8168_dash2_enable_rx(struct rtl8168_private *tp);
void rtl8168_hw_disable_mac_mcu_bps(struct net_device *dev);

#define HW_SUPPORT_CHECK_PHY_DISABLE_MODE(_M)        ((_M)->HwSuppCheckPhyDisableModeVer > 0 )
#define HW_SUPP_SERDES_PHY(_M)        ((_M)->HwSuppSerDesPhyVer > 0)
#define HW_HAS_WRITE_PHY_MCU_RAM_CODE(_M)        (((_M)->HwHasWrRamCodeToMicroP == TRUE) ? 1 : 0)
#define HW_SUPPORT_UPS_MODE(_M)        ((_M)->HwSuppUpsVer > 0)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,34)
#define netdev_mc_count(dev) ((dev)->mc_count)
#define netdev_mc_empty(dev) (netdev_mc_count(dev) == 0)
#define netdev_for_each_mc_addr(mclist, dev) \
    for (mclist = dev->mc_list; mclist; mclist = mclist->next)
#endif
