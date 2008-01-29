/*
 *  Copyright (C) 2002 Intersil Americas Inc.
 *  Copyright (C) 2003 Herbert Valerio Riedel <hvr@gnu.org>
 *  Copyright (C) 2003 Luis R. Rodriguez <mcgrof@ruslug.rutgers.edu>
 *  Copyright (C) 2003 Aurelien Alleaume <slts@free.fr>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef _ISLPCI_DEV_H
#define _ISLPCI_DEV_H

#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <linux/list.h>
#include <linux/mutex.h>

#include "isl_38xx.h"
#include "isl_oid.h"
#include "islpci_mgt.h"

/* some states might not be superflous and may be removed when
   design is finalized (hvr) */
typedef enum {
	PRV_STATE_OFF = 0,	/* this means hw_unavailable is != 0 */
	PRV_STATE_PREBOOT,	/* we are in a pre-boot state (empty RAM) */
	PRV_STATE_BOOT,		/* boot state (fw upload, run fw) */
	PRV_STATE_POSTBOOT,	/* after boot state, need reset now */
	PRV_STATE_PREINIT,	/* pre-init state */
	PRV_STATE_INIT,		/* init state (restore MIB backup to device) */
	PRV_STATE_READY,	/* driver&device are in operational state */
	PRV_STATE_SLEEP		/* device in sleep mode */
} islpci_state_t;

/* ACL using MAC address */
struct mac_entry {
   struct list_head _list;
   char addr[ETH_ALEN];
};

struct islpci_acl {
   enum { MAC_POLICY_OPEN=0, MAC_POLICY_ACCEPT=1, MAC_POLICY_REJECT=2 } policy;
   struct list_head mac_list;  /* a list of mac_entry */
   int size;   /* size of queue */
   struct semaphore sem;   /* accessed in ioctls and trap_work */
};

struct islpci_membuf {
	int size;                   /* size of memory */
	void *mem;                  /* address of memory as seen by CPU */
	dma_addr_t pci_addr;        /* address of memory as seen by device */
};

#define MAX_BSS_WPA_IE_COUNT 64
#define MAX_WPA_IE_LEN 64
struct islpci_bss_wpa_ie {
	struct list_head list;
	unsigned long last_update;
	u8 bssid[ETH_ALEN];
	u8 wpa_ie[MAX_WPA_IE_LEN];
	size_t wpa_ie_len;

};

typedef struct {
	spinlock_t slock;	/* generic spinlock; */

	u32 priv_oid;

	/* our mib cache */
	u32 iw_mode;
        struct rw_semaphore mib_sem;
	void **mib;
	char nickname[IW_ESSID_MAX_SIZE+1];

	/* Take care of the wireless stats */
	struct work_struct stats_work;
	struct semaphore stats_sem;
	/* remember when we last updated the stats */
	unsigned long stats_timestamp;
	/* The first is accessed under semaphore locking.
	 * The second is the clean one we return to iwconfig.
	 */
	struct iw_statistics local_iwstatistics;
	struct iw_statistics iwstatistics;

	struct iw_spy_data spy_data; /* iwspy support */

	struct iw_public_data wireless_data;

	int monitor_type; /* ARPHRD_IEEE80211 or ARPHRD_IEEE80211_PRISM */

	struct islpci_acl acl;

	/* PCI bus allocation & configuration members */
	struct pci_dev *pdev;	/* PCI structure information */
	char firmware[33];

	void __iomem *device_base;	/* ioremapped device base address */

	/* consistent DMA region */
	void *driver_mem_address;	/* base DMA address */
	dma_addr_t device_host_address;	/* base DMA address (bus address) */
	dma_addr_t device_psm_buffer;	/* host memory for PSM buffering (bus address) */

	/* our network_device structure  */
	struct net_device *ndev;

	/* device queue interface members */
	struct isl38xx_cb *control_block;	/* device control block
							   (== driver_mem_address!) */

	/* Each queue has three indexes:
	 *   free/index_mgmt/data_rx/tx (called index, see below),
	 *   driver_curr_frag, and device_curr_frag (in the control block)
	 * All indexes are ever-increasing, but interpreted modulo the
	 * device queue size when used.
	 *   index <= device_curr_frag <= driver_curr_frag  at all times
	 * For rx queues, [index, device_curr_frag) contains fragments
	 * that the interrupt processing needs to handle (owned by driver).
	 * [device_curr_frag, driver_curr_frag) is the free space in the
	 * rx queue, waiting for data (owned by device).  The driver
	 * increments driver_curr_frag to indicate to the device that more
	 * buffers are available.
	 * If device_curr_frag == driver_curr_frag, no more rx buffers are
	 * available, and the rx DMA engine of the device is halted.
	 * For tx queues, [index, device_curr_frag) contains fragments
	 * where tx is done; they need to be freed (owned by driver).
	 * [device_curr_frag, driver_curr_frag) contains the frames
	 * that are being transferred (owned by device).  The driver
	 * increments driver_curr_frag to indicate that more tx work
	 * needs to be done.
	 */
	u32 index_mgmt_rx;              /* real index mgmt rx queue */
	u32 index_mgmt_tx;              /* read index mgmt tx queue */
	u32 free_data_rx;	/* free pointer data rx queue */
	u32 free_data_tx;	/* free pointer data tx queue */
	u32 data_low_tx_full;	/* full detected flag */

	/* frame memory buffers for the device queues */
	struct islpci_membuf mgmt_tx[ISL38XX_CB_MGMT_QSIZE];
	struct islpci_membuf mgmt_rx[ISL38XX_CB_MGMT_QSIZE];
	struct sk_buff *data_low_tx[ISL38XX_CB_TX_QSIZE];
	struct sk_buff *data_low_rx[ISL38XX_CB_RX_QSIZE];
	dma_addr_t pci_map_tx_address[ISL38XX_CB_TX_QSIZE];
	dma_addr_t pci_map_rx_address[ISL38XX_CB_RX_QSIZE];

	/* driver network interface members */
	struct net_device_stats statistics;

	/* wait for a reset interrupt */
	wait_queue_head_t reset_done;

	/* used by islpci_mgt_transaction */
	struct mutex mgmt_lock; /* serialize access to mailbox and wqueue */
	struct islpci_mgmtframe *mgmt_received;	  /* mbox for incoming frame */
	wait_queue_head_t mgmt_wqueue;            /* waitqueue for mbox */

	/* state machine */
	islpci_state_t state;
	int state_off;		/* enumeration of off-state, if 0 then
				 * we're not in any off-state */

	/* WPA stuff */
	int wpa; /* WPA mode enabled */
	struct list_head bss_wpa_list;
	int num_bss_wpa;
	struct semaphore wpa_sem;
	u8 wpa_ie[MAX_WPA_IE_LEN];
	size_t wpa_ie_len;

	struct work_struct reset_task;
	int reset_task_pending;
} islpci_private;

static inline islpci_state_t
islpci_get_state(islpci_private *priv)
{
	/* lock */
	return priv->state;
	/* unlock */
}

islpci_state_t islpci_set_state(islpci_private *priv, islpci_state_t new_state);

#define ISLPCI_TX_TIMEOUT               (2*HZ)

irqreturn_t islpci_interrupt(int, void *);

int prism54_post_setup(islpci_private *, int);
int islpci_reset(islpci_private *, int);

static inline void
islpci_trigger(islpci_private *priv)
{
	isl38xx_trigger_device(islpci_get_state(priv) == PRV_STATE_SLEEP,
			       priv->device_base);
}

int islpci_free_memory(islpci_private *);
struct net_device *islpci_setup(struct pci_dev *);

#define DRV_NAME	"prism54"
#define DRV_VERSION	"1.2"

#endif				/* _ISLPCI_DEV_H */
