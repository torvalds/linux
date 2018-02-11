/* SPDX-License-Identifier: GPL-2.0 */
/* drivers/atm/zatm.h - ZeitNet ZN122x device driver declarations */

/* Written 1995-1998 by Werner Almesberger, EPFL LRC/ICA */


#ifndef DRIVER_ATM_ZATM_H
#define DRIVER_ATM_ZATM_H

#include <linux/skbuff.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/sonet.h>
#include <linux/pci.h>


#define DEV_LABEL	"zatm"

#define MAX_AAL5_PDU	10240	/* allocate for AAL5 PDUs of this size */
#define MAX_RX_SIZE_LD	14	/* ceil(log2((MAX_AAL5_PDU+47)/48)) */

#define LOW_MARK	12	/* start adding new buffers if less than 12 */
#define HIGH_MARK	30	/* stop adding buffers after reaching 30 */
#define OFF_CNG_THRES	5	/* threshold for offset changes */

#define RX_SIZE		2	/* RX lookup entry size (in bytes) */
#define NR_POOLS	32	/* number of free buffer pointers */
#define POOL_SIZE	8	/* buffer entry size (in bytes) */
#define NR_SHAPERS	16	/* number of shapers */
#define SHAPER_SIZE	4	/* shaper entry size (in bytes) */
#define VC_SIZE		32	/* VC dsc (TX or RX) size (in bytes) */

#define RING_ENTRIES	32	/* ring entries (without back pointer) */
#define RING_WORDS	4	/* ring element size */
#define RING_SIZE	(sizeof(unsigned long)*(RING_ENTRIES+1)*RING_WORDS)

#define NR_MBX		4	/* four mailboxes */
#define MBX_RX_0	0	/* mailbox indices */
#define MBX_RX_1	1
#define MBX_TX_0	2
#define MBX_TX_1	3

struct zatm_vcc {
	/*-------------------------------- RX part */
	int rx_chan;			/* RX channel, 0 if none */
	int pool;			/* free buffer pool */
	/*-------------------------------- TX part */
	int tx_chan;			/* TX channel, 0 if none */
	int shaper;			/* shaper, <0 if none */
	struct sk_buff_head tx_queue;	/* list of buffers in transit */
	wait_queue_head_t tx_wait;	/* for close */
	u32 *ring;			/* transmit ring */
	int ring_curr;			/* current write position */
	int txing;			/* number of transmits in progress */
	struct sk_buff_head backlog;	/* list of buffers waiting for ring */
};

struct zatm_dev {
	/*-------------------------------- TX part */
	int tx_bw;			/* remaining bandwidth */
	u32 free_shapers;		/* bit set */
	int ubr;			/* UBR shaper; -1 if none */
	int ubr_ref_cnt;		/* number of VCs using UBR shaper */
	/*-------------------------------- RX part */
	int pool_ref[NR_POOLS];		/* free buffer pool usage counters */
	volatile struct sk_buff *last_free[NR_POOLS];
					/* last entry in respective pool */
	struct sk_buff_head pool[NR_POOLS];/* free buffer pools */
	struct zatm_pool_info pool_info[NR_POOLS]; /* pool information */
	/*-------------------------------- maps */
	struct atm_vcc **tx_map;	/* TX VCCs */
	struct atm_vcc **rx_map;	/* RX VCCs */
	int chans;			/* map size, must be 2^n */
	/*-------------------------------- mailboxes */
	unsigned long mbx_start[NR_MBX];/* start addresses */
	dma_addr_t mbx_dma[NR_MBX];
	u16 mbx_end[NR_MBX];		/* end offset (in bytes) */
	/*-------------------------------- other pointers */
	u32 pool_base;			/* Free buffer pool dsc (word addr) */
	/*-------------------------------- ZATM links */
	struct atm_dev *more;		/* other ZATM devices */
	/*-------------------------------- general information */
	int mem;			/* RAM on board (in bytes) */
	int khz;			/* timer clock */
	int copper;			/* PHY type */
	unsigned char irq;		/* IRQ */
	unsigned int base;		/* IO base address */
	struct pci_dev *pci_dev;	/* PCI stuff */
	spinlock_t lock;
};


#define ZATM_DEV(d) ((struct zatm_dev *) (d)->dev_data)
#define ZATM_VCC(d) ((struct zatm_vcc *) (d)->dev_data)


struct zatm_skb_prv {
	struct atm_skb_data _;		/* reserved */
	u32 *dsc;			/* pointer to skb's descriptor */
};

#define ZATM_PRV_DSC(skb) (((struct zatm_skb_prv *) (skb)->cb)->dsc)

#endif
