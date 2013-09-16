#ifndef __GMAC_DESC_H__
#define __GMAC_DESC_H__

#include <linux/types.h>
#include "gmac_ethtool.h"

/* GMAC TX FIFO is 8K, Rx FIFO is 16K */
#define BUF_SIZE_16KiB 16384
#define BUF_SIZE_8KiB 8192
#define BUF_SIZE_4KiB 4096
#define BUF_SIZE_2KiB 2048

typedef union {
	struct {
		/* TDES0 */
		u32 deferred:1;		/* Deferred bit (only half-duplex) */
		u32 under_err:1;	/* Underflow error */
		u32 ex_deferral:1;	/* Excessive deferral */
		u32 coll_cnt:4;		/* Collision count */
		u32 vlan_tag:1;		/* VLAN Frame */
		u32 ex_coll:1;		/* Excessive collision */
		u32 late_coll:1;	/* Late collision */
		u32 no_carr:1;		/* No carrier */
		u32 loss_carr:1;	/* Loss of collision */
		u32 ipdat_err:1;	/* IP payload error */
		u32 frm_flu:1;		/* Frame flushed */
		u32 jab_timeout:1;	/* Jabber timeout */
		u32 err_sum:1;		/* Error summary */
		u32 iphead_err:1;	/* IP header error */
		u32 ttss:1;			/* Transmit time stamp status */
		u32 reserved0:13;
		u32 own:1;			/* Own bit. CPU:0, DMA:1 */
	} tx;

	struct {
		/* RDES0 */
		u32 chsum_err:1;	/* Payload checksum error */
		u32 crc_err:1;		/* CRC error */
		u32 dribbling:1;	/* Dribble bit error */
		u32 mii_err:1;		/* Received error (bit3) */
		u32 recv_wt:1;		/* Received watchdog timeout */
		u32 frm_type:1;		/* Frame type */
		u32 late_coll:1;	/* Late Collision */
		u32	ipch_err:1;		/* IPv header checksum error (bit7) */
		u32 last_desc:1;	/* Laset descriptor */
		u32 first_desc:1;	/* First descriptor */
		u32 vlan_tag:1;		/* VLAN Tag */
		u32 over_err:1;		/* Overflow error (bit11) */
		u32 len_err:1;		/* Length error */
		u32 sou_filter:1;	/* Source address filter fail */
		u32 desc_err:1;		/* Descriptor error */
		u32 err_sum:1;		/* Error summary (bit15) */
		u32 frm_len:14;		/* Frame length */
		u32 des_filter:1;	/* Destination address filter fail */
		u32 own:1;			/* Own bit. CPU:0, DMA:1 */
	#define RX_PKT_OK		0x7FFFB77C
	#define RX_LEN			0x3FFF0000
	} rx;

	u32 all;
} desc0_u;

typedef union {
	struct {
		/* TDES1 */
		u32 buf1_size:11;	/* Transmit buffer1 size */
		u32 buf2_size:11;	/* Transmit buffer2 size */
		u32 ttse:1;			/* Transmit time stamp enable */
		u32 dis_pad:1;		/* Disable pad (bit23) */
		u32 adr_chain:1;	/* Second address chained */
		u32 end_ring:1;		/* Transmit end of ring */
		u32 crc_dis:1;		/* Disable CRC */
		u32 cic:2;			/* Checksum insertion control (bit27:28) */
		u32 first_sg:1;		/* First Segment */
		u32 last_seg:1;		/* Last Segment */
		u32 interrupt:1;	/* Interrupt on completion */
	} tx;

	struct {
		/* RDES1 */
		u32 buf1_size:11;	/* Received buffer1 size */
		u32 buf2_size:11;	/* Received buffer2 size */
		u32 reserved1:2;
		u32 adr_chain:1;	/* Second address chained */
		u32 end_ring:1;		/* Received end of ring */
		u32 reserved2:5;
		u32 dis_ic:1;		/* Disable interrupt on completion */
	} rx;

	u32 all;
} desc1_u;

enum csum_insertion{
	cic_dis		= 0, /* Checksum Insertion Control */
	cic_ip		= 1, /* Only IP header */
	cic_no_pse	= 2, /* IP header but not pseudoheader */
	cic_full	= 3, /* IP header and pseudoheader */
};

typedef struct dma_desc {
	desc0_u desc0;
	desc1_u desc1;
	u32 desc2;
	u32	desc3;
} __attribute__((packed)) dma_desc_t;

#ifdef CONFIG_GMAC_RING
static inline void desc_rx_set_on_ring_chain(dma_desc_t *p, int end)
{
	p->desc1.rx.buf2_size = BUF_SIZE_2KiB - 1;
	if (end)
		p->desc1.rx.end_ring = 1;
}

static inline void desc_tx_set_on_ring_chain(dma_desc_t *p, int end)
{
	if (end)
		p->desc1.tx.end_ring = 1;
}

static inline void desc_end_tx_desc(dma_desc_t *p, int ter)
{
	p->desc1.tx.end_ring = ter;
}

static inline void norm_set_tx_desc_len(dma_desc_t *p, int len)
{
		p->desc1.tx.buf1_size = len;
}

#else
static inline void desc_rx_set_on_ring_chain(dma_desc_t *p, int end)
{
	p->desc1.rx.adr_chain = 1;
}

static inline void desc_tx_set_on_ring_chain(dma_desc_t *p, int ring_size)
{
	p->desc1.tx.adr_chain = 1;
}

static inline void desc_end_tx_desc(dma_desc_t *p, int ter)
{
	p->desc1.tx.adr_chain = 1;
}

static inline void norm_set_tx_desc_len(dma_desc_t *p, int len)
{
	p->desc1.tx.buf1_size = len;
}
#endif

int desc_get_tx_status(void *data, struct gmac_extra_stats *x,
						dma_desc_t *p, void __iomem *ioaddr);
int desc_get_tx_len(dma_desc_t *p);
void desc_init_tx(dma_desc_t *p, unsigned int ring_size);
int desc_get_tx_own(dma_desc_t *p);
void desc_set_tx_own(dma_desc_t *p);
int desc_get_tx_ls(dma_desc_t *p);
void desc_release_tx(dma_desc_t *p);
void desc_prepare_tx(dma_desc_t *p, int is_fs, int len, int csum_flag);
void desc_clear_tx_ic(dma_desc_t *p);
void desc_close_tx(dma_desc_t *p);

int desc_get_rx_status(void *data, struct gmac_extra_stats *x, dma_desc_t *p);
void desc_init_rx(dma_desc_t *p, unsigned int ring_size, int disable_rx_ic);
int desc_get_rx_own(dma_desc_t *p);
void desc_set_rx_own(dma_desc_t *p);
int desc_get_rx_frame_len(dma_desc_t *p);

unsigned int gmac_jumbo_frm(void *p, struct sk_buff *skb, int csum);
unsigned int gmac_is_jumbo_frm(int len);
void gmac_refill_desc3(int bfsize, dma_desc_t *p);
void gmac_init_desc3(int desc3_as_data_buf, dma_desc_t *p);
void gmac_init_dma_chain(dma_desc_t *des, dma_addr_t phy_addr, unsigned int size);
void gmac_clean_desc3(dma_desc_t *p);
int gmac_set_16kib_bfsize(int mtu);

#endif //__GMAC_DESC_H__
