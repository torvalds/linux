/*
 * drivers/net/ibm_newemac/mal.h
 *
 * Memory Access Layer (MAL) support
 *
 * Copyright (c) 2004, 2005 Zultys Technologies.
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 *
 * Based on original work by
 *      Armin Kuster <akuster@mvista.com>
 *      Copyright 2002 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#ifndef __IBM_NEWEMAC_MAL_H
#define __IBM_NEWEMAC_MAL_H

/*
 * There are some variations on the MAL, we express them in this driver as
 * MAL Version 1 and 2 though that doesn't match any IBM terminology.
 *
 * We call MAL 1 the version in 405GP, 405GPR, 405EP, 440EP, 440GR and
 * NP405H.
 *
 * We call MAL 2 the version in 440GP, 440GX, 440SP, 440SPE and Axon
 *
 * The driver expects a "version" property in the emac node containing
 * a number 1 or 2. New device-trees for EMAC capable platforms are thus
 * required to include that when porting to arch/powerpc.
 */

/* MALx DCR registers */
#define	MAL_CFG			0x00
#define	  MAL_CFG_SR		0x80000000
#define   MAL_CFG_PLBB		0x00004000
#define   MAL_CFG_OPBBL		0x00000080
#define   MAL_CFG_EOPIE		0x00000004
#define   MAL_CFG_LEA		0x00000002
#define   MAL_CFG_SD		0x00000001

/* MAL V1 CFG bits */
#define   MAL1_CFG_PLBP_MASK	0x00c00000
#define   MAL1_CFG_PLBP_10	0x00800000
#define   MAL1_CFG_GA		0x00200000
#define   MAL1_CFG_OA		0x00100000
#define   MAL1_CFG_PLBLE	0x00080000
#define   MAL1_CFG_PLBT_MASK	0x00078000
#define   MAL1_CFG_DEFAULT	(MAL1_CFG_PLBP_10 | MAL1_CFG_PLBT_MASK)

/* MAL V2 CFG bits */
#define   MAL2_CFG_RPP_MASK	0x00c00000
#define   MAL2_CFG_RPP_10	0x00800000
#define   MAL2_CFG_RMBS_MASK	0x00300000
#define   MAL2_CFG_WPP_MASK	0x000c0000
#define   MAL2_CFG_WPP_10	0x00080000
#define   MAL2_CFG_WMBS_MASK	0x00030000
#define   MAL2_CFG_PLBLE	0x00008000
#define   MAL2_CFG_DEFAULT	(MAL2_CFG_RMBS_MASK | MAL2_CFG_WMBS_MASK | \
				 MAL2_CFG_RPP_10 | MAL2_CFG_WPP_10)

#define MAL_ESR			0x01
#define   MAL_ESR_EVB		0x80000000
#define   MAL_ESR_CIDT		0x40000000
#define   MAL_ESR_CID_MASK	0x3e000000
#define   MAL_ESR_CID_SHIFT	25
#define   MAL_ESR_DE		0x00100000
#define   MAL_ESR_OTE		0x00040000
#define   MAL_ESR_OSE		0x00020000
#define   MAL_ESR_PEIN		0x00010000
#define   MAL_ESR_DEI		0x00000010
#define   MAL_ESR_OTEI		0x00000004
#define   MAL_ESR_OSEI		0x00000002
#define   MAL_ESR_PBEI		0x00000001

/* MAL V1 ESR bits */
#define   MAL1_ESR_ONE		0x00080000
#define   MAL1_ESR_ONEI		0x00000008

/* MAL V2 ESR bits */
#define   MAL2_ESR_PTE		0x00800000
#define   MAL2_ESR_PRE		0x00400000
#define   MAL2_ESR_PWE		0x00200000
#define   MAL2_ESR_PTEI		0x00000080
#define   MAL2_ESR_PREI		0x00000040
#define   MAL2_ESR_PWEI		0x00000020


#define MAL_IER			0x02
#define   MAL_IER_DE		0x00000010
#define   MAL_IER_OTE		0x00000004
#define   MAL_IER_OE		0x00000002
#define   MAL_IER_PE		0x00000001
/* MAL V1 IER bits */
#define   MAL1_IER_NWE		0x00000008
#define   MAL1_IER_SOC_EVENTS	MAL1_IER_NWE
#define   MAL1_IER_EVENTS	(MAL1_IER_SOC_EVENTS | MAL_IER_OTE | \
				 MAL_IER_OTE | MAL_IER_OE | MAL_IER_PE)

/* MAL V2 IER bits */
#define   MAL2_IER_PT		0x00000080
#define   MAL2_IER_PRE		0x00000040
#define   MAL2_IER_PWE		0x00000020
#define   MAL2_IER_SOC_EVENTS	(MAL2_IER_PT | MAL2_IER_PRE | MAL2_IER_PWE)
#define   MAL2_IER_EVENTS	(MAL2_IER_SOC_EVENTS | MAL_IER_OTE | \
				 MAL_IER_OTE | MAL_IER_OE | MAL_IER_PE)


#define MAL_TXCASR		0x04
#define MAL_TXCARR		0x05
#define MAL_TXEOBISR		0x06
#define MAL_TXDEIR		0x07
#define MAL_RXCASR		0x10
#define MAL_RXCARR		0x11
#define MAL_RXEOBISR		0x12
#define MAL_RXDEIR		0x13
#define MAL_TXCTPR(n)		((n) + 0x20)
#define MAL_RXCTPR(n)		((n) + 0x40)
#define MAL_RCBS(n)		((n) + 0x60)

/* In reality MAL can handle TX buffers up to 4095 bytes long,
 * but this isn't a good round number :) 		 --ebs
 */
#define MAL_MAX_TX_SIZE		4080
#define MAL_MAX_RX_SIZE		4080

static inline int mal_rx_size(int len)
{
	len = (len + 0xf) & ~0xf;
	return len > MAL_MAX_RX_SIZE ? MAL_MAX_RX_SIZE : len;
}

static inline int mal_tx_chunks(int len)
{
	return (len + MAL_MAX_TX_SIZE - 1) / MAL_MAX_TX_SIZE;
}

#define MAL_CHAN_MASK(n)	(0x80000000 >> (n))

/* MAL Buffer Descriptor structure */
struct mal_descriptor {
	u16 ctrl;		/* MAL / Commac status control bits */
	u16 data_len;		/* Max length is 4K-1 (12 bits)     */
	u32 data_ptr;		/* pointer to actual data buffer    */
};

/* the following defines are for the MadMAL status and control registers. */
/* MADMAL transmit and receive status/control bits  */
#define MAL_RX_CTRL_EMPTY	0x8000
#define MAL_RX_CTRL_WRAP	0x4000
#define MAL_RX_CTRL_CM		0x2000
#define MAL_RX_CTRL_LAST	0x1000
#define MAL_RX_CTRL_FIRST	0x0800
#define MAL_RX_CTRL_INTR	0x0400
#define MAL_RX_CTRL_SINGLE	(MAL_RX_CTRL_LAST | MAL_RX_CTRL_FIRST)
#define MAL_IS_SINGLE_RX(ctrl)	(((ctrl) & MAL_RX_CTRL_SINGLE) == MAL_RX_CTRL_SINGLE)

#define MAL_TX_CTRL_READY	0x8000
#define MAL_TX_CTRL_WRAP	0x4000
#define MAL_TX_CTRL_CM		0x2000
#define MAL_TX_CTRL_LAST	0x1000
#define MAL_TX_CTRL_INTR	0x0400

struct mal_commac_ops {
	void	(*poll_tx) (void *dev);
	int	(*poll_rx) (void *dev, int budget);
	int	(*peek_rx) (void *dev);
	void	(*rxde) (void *dev);
};

struct mal_commac {
	struct mal_commac_ops	*ops;
	void			*dev;
	struct list_head	poll_list;
	long       		flags;
#define MAL_COMMAC_RX_STOPPED		0
#define MAL_COMMAC_POLL_DISABLED	1
	u32			tx_chan_mask;
	u32			rx_chan_mask;
	struct list_head	list;
};

struct mal_instance {
	int			version;
	dcr_host_t		dcr_host;

	int			num_tx_chans;	/* Number of TX channels */
	int			num_rx_chans;	/* Number of RX channels */
	int 			txeob_irq;	/* TX End Of Buffer IRQ  */
	int 			rxeob_irq;	/* RX End Of Buffer IRQ  */
	int			txde_irq;	/* TX Descriptor Error IRQ */
	int			rxde_irq;	/* RX Descriptor Error IRQ */
	int			serr_irq;	/* MAL System Error IRQ    */

	struct list_head	poll_list;
	struct napi_struct	napi;

	struct list_head	list;
	u32			tx_chan_mask;
	u32			rx_chan_mask;

	dma_addr_t		bd_dma;
	struct mal_descriptor	*bd_virt;

	struct of_device	*ofdev;
	int			index;
	spinlock_t		lock;
};

static inline u32 get_mal_dcrn(struct mal_instance *mal, int reg)
{
	return dcr_read(mal->dcr_host, reg);
}

static inline void set_mal_dcrn(struct mal_instance *mal, int reg, u32 val)
{
	dcr_write(mal->dcr_host, reg, val);
}

/* Register MAL devices */
int mal_init(void);
void mal_exit(void);

int mal_register_commac(struct mal_instance *mal,
			struct mal_commac *commac);
void mal_unregister_commac(struct mal_instance *mal,
			   struct mal_commac *commac);
int mal_set_rcbs(struct mal_instance *mal, int channel, unsigned long size);

/* Returns BD ring offset for a particular channel
   (in 'struct mal_descriptor' elements)
*/
int mal_tx_bd_offset(struct mal_instance *mal, int channel);
int mal_rx_bd_offset(struct mal_instance *mal, int channel);

void mal_enable_tx_channel(struct mal_instance *mal, int channel);
void mal_disable_tx_channel(struct mal_instance *mal, int channel);
void mal_enable_rx_channel(struct mal_instance *mal, int channel);
void mal_disable_rx_channel(struct mal_instance *mal, int channel);

void mal_poll_disable(struct mal_instance *mal, struct mal_commac *commac);
void mal_poll_enable(struct mal_instance *mal, struct mal_commac *commac);

/* Add/remove EMAC to/from MAL polling list */
void mal_poll_add(struct mal_instance *mal, struct mal_commac *commac);
void mal_poll_del(struct mal_instance *mal, struct mal_commac *commac);

/* Ethtool MAL registers */
struct mal_regs {
	u32 tx_count;
	u32 rx_count;

	u32 cfg;
	u32 esr;
	u32 ier;
	u32 tx_casr;
	u32 tx_carr;
	u32 tx_eobisr;
	u32 tx_deir;
	u32 rx_casr;
	u32 rx_carr;
	u32 rx_eobisr;
	u32 rx_deir;
	u32 tx_ctpr[32];
	u32 rx_ctpr[32];
	u32 rcbs[32];
};

int mal_get_regs_len(struct mal_instance *mal);
void *mal_dump_regs(struct mal_instance *mal, void *buf);

#endif /* __IBM_NEWEMAC_MAL_H */
