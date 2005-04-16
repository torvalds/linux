#ifndef _IBM_EMAC_MAL_H
#define _IBM_EMAC_MAL_H

#include <linux/list.h>

#define MAL_DT_ALIGN	(4096)	/* Alignment for each channel's descriptor table */

#define MAL_CHAN_MASK(chan)	(0x80000000 >> (chan))

/* MAL Buffer Descriptor structure */
struct mal_descriptor {
	unsigned short ctrl;	/* MAL / Commac status control bits */
	short data_len;		/* Max length is 4K-1 (12 bits)     */
	unsigned char *data_ptr;	/* pointer to actual data buffer    */
} __attribute__ ((packed));

/* the following defines are for the MadMAL status and control registers. */
/* MADMAL transmit and receive status/control bits  */
#define MAL_RX_CTRL_EMPTY		0x8000
#define MAL_RX_CTRL_WRAP		0x4000
#define MAL_RX_CTRL_CM			0x2000
#define MAL_RX_CTRL_LAST		0x1000
#define MAL_RX_CTRL_FIRST		0x0800
#define MAL_RX_CTRL_INTR		0x0400

#define MAL_TX_CTRL_READY		0x8000
#define MAL_TX_CTRL_WRAP		0x4000
#define MAL_TX_CTRL_CM			0x2000
#define MAL_TX_CTRL_LAST		0x1000
#define MAL_TX_CTRL_INTR		0x0400

struct mal_commac_ops {
	void (*txeob) (void *dev, u32 chanmask);
	void (*txde) (void *dev, u32 chanmask);
	void (*rxeob) (void *dev, u32 chanmask);
	void (*rxde) (void *dev, u32 chanmask);
};

struct mal_commac {
	struct mal_commac_ops *ops;
	void *dev;
	u32 tx_chan_mask, rx_chan_mask;
	struct list_head list;
};

struct ibm_ocp_mal {
	int dcrbase;

	struct list_head commac;
	u32 tx_chan_mask, rx_chan_mask;

	dma_addr_t tx_phys_addr;
	struct mal_descriptor *tx_virt_addr;

	dma_addr_t rx_phys_addr;
	struct mal_descriptor *rx_virt_addr;
};

#define GET_MAL_STANZA(base,dcrn) \
	case base: \
		x = mfdcr(dcrn(base)); \
		break;

#define SET_MAL_STANZA(base,dcrn, val) \
	case base: \
		mtdcr(dcrn(base), (val)); \
		break;

#define GET_MAL0_STANZA(dcrn) GET_MAL_STANZA(DCRN_MAL_BASE,dcrn)
#define SET_MAL0_STANZA(dcrn,val) SET_MAL_STANZA(DCRN_MAL_BASE,dcrn,val)

#ifdef DCRN_MAL1_BASE
#define GET_MAL1_STANZA(dcrn) GET_MAL_STANZA(DCRN_MAL1_BASE,dcrn)
#define SET_MAL1_STANZA(dcrn,val) SET_MAL_STANZA(DCRN_MAL1_BASE,dcrn,val)
#else				/* ! DCRN_MAL1_BASE */
#define GET_MAL1_STANZA(dcrn)
#define SET_MAL1_STANZA(dcrn,val)
#endif

#define get_mal_dcrn(mal, dcrn) ({ \
	u32 x; \
	switch ((mal)->dcrbase) { \
		GET_MAL0_STANZA(dcrn) \
		GET_MAL1_STANZA(dcrn) \
	default: \
		x = 0; \
		BUG(); \
	} \
x; })

#define set_mal_dcrn(mal, dcrn, val) do { \
	switch ((mal)->dcrbase) { \
		SET_MAL0_STANZA(dcrn,val) \
		SET_MAL1_STANZA(dcrn,val) \
	default: \
		BUG(); \
	} } while (0)

static inline void mal_enable_tx_channels(struct ibm_ocp_mal *mal, u32 chanmask)
{
	set_mal_dcrn(mal, DCRN_MALTXCASR,
		     get_mal_dcrn(mal, DCRN_MALTXCASR) | chanmask);
}

static inline void mal_disable_tx_channels(struct ibm_ocp_mal *mal,
					   u32 chanmask)
{
	set_mal_dcrn(mal, DCRN_MALTXCARR, chanmask);
}

static inline void mal_enable_rx_channels(struct ibm_ocp_mal *mal, u32 chanmask)
{
	set_mal_dcrn(mal, DCRN_MALRXCASR,
		     get_mal_dcrn(mal, DCRN_MALRXCASR) | chanmask);
}

static inline void mal_disable_rx_channels(struct ibm_ocp_mal *mal,
					   u32 chanmask)
{
	set_mal_dcrn(mal, DCRN_MALRXCARR, chanmask);
}

extern int mal_register_commac(struct ibm_ocp_mal *mal,
			       struct mal_commac *commac);
extern int mal_unregister_commac(struct ibm_ocp_mal *mal,
				 struct mal_commac *commac);

extern int mal_set_rcbs(struct ibm_ocp_mal *mal, int channel,
			unsigned long size);

#endif				/* _IBM_EMAC_MAL_H */
