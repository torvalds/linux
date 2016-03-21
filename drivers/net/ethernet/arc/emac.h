/*
 * Copyright (C) 2004-2013 Synopsys, Inc. (www.synopsys.com)
 *
 * Registers and bits definitions of ARC EMAC
 */

#ifndef ARC_EMAC_H
#define ARC_EMAC_H

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/clk.h>

/* STATUS and ENABLE Register bit masks */
#define TXINT_MASK	(1 << 0)	/* Transmit interrupt */
#define RXINT_MASK	(1 << 1)	/* Receive interrupt */
#define ERR_MASK	(1 << 2)	/* Error interrupt */
#define TXCH_MASK	(1 << 3)	/* Transmit chaining error interrupt */
#define MSER_MASK	(1 << 4)	/* Missed packet counter error */
#define RXCR_MASK	(1 << 8)	/* RXCRCERR counter rolled over  */
#define RXFR_MASK	(1 << 9)	/* RXFRAMEERR counter rolled over */
#define RXFL_MASK	(1 << 10)	/* RXOFLOWERR counter rolled over */
#define MDIO_MASK	(1 << 12)	/* MDIO complete interrupt */
#define TXPL_MASK	(1 << 31)	/* Force polling of BD by EMAC */

/* CONTROL Register bit masks */
#define EN_MASK		(1 << 0)	/* VMAC enable */
#define TXRN_MASK	(1 << 3)	/* TX enable */
#define RXRN_MASK	(1 << 4)	/* RX enable */
#define DSBC_MASK	(1 << 8)	/* Disable receive broadcast */
#define ENFL_MASK	(1 << 10)	/* Enable Full-duplex */
#define PROM_MASK	(1 << 11)	/* Promiscuous mode */

/* Buffer descriptor INFO bit masks */
#define OWN_MASK	(1 << 31)	/* 0-CPU or 1-EMAC owns buffer */
#define FIRST_MASK	(1 << 16)	/* First buffer in chain */
#define LAST_MASK	(1 << 17)	/* Last buffer in chain */
#define LEN_MASK	0x000007FF	/* last 11 bits */
#define CRLS		(1 << 21)
#define DEFR		(1 << 22)
#define DROP		(1 << 23)
#define RTRY		(1 << 24)
#define LTCL		(1 << 28)
#define UFLO		(1 << 29)

#define FOR_EMAC	OWN_MASK
#define FOR_CPU		0

/* ARC EMAC register set combines entries for MAC and MDIO */
enum {
	R_ID = 0,
	R_STATUS,
	R_ENABLE,
	R_CTRL,
	R_POLLRATE,
	R_RXERR,
	R_MISS,
	R_TX_RING,
	R_RX_RING,
	R_ADDRL,
	R_ADDRH,
	R_LAFL,
	R_LAFH,
	R_MDIO,
};

#define TX_TIMEOUT		(400 * HZ / 1000) /* Transmission timeout */

#define ARC_EMAC_NAPI_WEIGHT	40		/* Workload for NAPI */

#define EMAC_BUFFER_SIZE	1536		/* EMAC buffer size */

/**
 * struct arc_emac_bd - EMAC buffer descriptor (BD).
 *
 * @info:	Contains status information on the buffer itself.
 * @data:	32-bit byte addressable pointer to the packet data.
 */
struct arc_emac_bd {
	__le32 info;
	dma_addr_t data;
};

/* Number of Rx/Tx BD's */
#define RX_BD_NUM	128
#define TX_BD_NUM	128

#define RX_RING_SZ	(RX_BD_NUM * sizeof(struct arc_emac_bd))
#define TX_RING_SZ	(TX_BD_NUM * sizeof(struct arc_emac_bd))

/**
 * struct buffer_state - Stores Rx/Tx buffer state.
 * @sk_buff:	Pointer to socket buffer.
 * @addr:	Start address of DMA-mapped memory region.
 * @len:	Length of DMA-mapped memory region.
 */
struct buffer_state {
	struct sk_buff *skb;
	DEFINE_DMA_UNMAP_ADDR(addr);
	DEFINE_DMA_UNMAP_LEN(len);
};

struct arc_emac_mdio_bus_data {
	struct gpio_desc *reset_gpio;
	int msec;
};

/**
 * struct arc_emac_priv - Storage of EMAC's private information.
 * @dev:	Pointer to the current device.
 * @phy_dev:	Pointer to attached PHY device.
 * @bus:	Pointer to the current MII bus.
 * @regs:	Base address of EMAC memory-mapped control registers.
 * @napi:	Structure for NAPI.
 * @rxbd:	Pointer to Rx BD ring.
 * @txbd:	Pointer to Tx BD ring.
 * @rxbd_dma:	DMA handle for Rx BD ring.
 * @txbd_dma:	DMA handle for Tx BD ring.
 * @rx_buff:	Storage for Rx buffers states.
 * @tx_buff:	Storage for Tx buffers states.
 * @txbd_curr:	Index of Tx BD to use on the next "ndo_start_xmit".
 * @txbd_dirty:	Index of Tx BD to free on the next Tx interrupt.
 * @last_rx_bd:	Index of the last Rx BD we've got from EMAC.
 * @link:	PHY's last seen link state.
 * @duplex:	PHY's last set duplex mode.
 * @speed:	PHY's last set speed.
 */
struct arc_emac_priv {
	const char *drv_name;
	const char *drv_version;
	void (*set_mac_speed)(void *priv, unsigned int speed);

	/* Devices */
	struct device *dev;
	struct phy_device *phy_dev;
	struct mii_bus *bus;
	struct arc_emac_mdio_bus_data bus_data;

	void __iomem *regs;
	struct clk *clk;

	struct napi_struct napi;

	struct arc_emac_bd *rxbd;
	struct arc_emac_bd *txbd;

	dma_addr_t rxbd_dma;
	dma_addr_t txbd_dma;

	struct buffer_state rx_buff[RX_BD_NUM];
	struct buffer_state tx_buff[TX_BD_NUM];
	unsigned int txbd_curr;
	unsigned int txbd_dirty;

	unsigned int last_rx_bd;

	unsigned int link;
	unsigned int duplex;
	unsigned int speed;
};

/**
 * arc_reg_set - Sets EMAC register with provided value.
 * @priv:	Pointer to ARC EMAC private data structure.
 * @reg:	Register offset from base address.
 * @value:	Value to set in register.
 */
static inline void arc_reg_set(struct arc_emac_priv *priv, int reg, int value)
{
	iowrite32(value, priv->regs + reg * sizeof(int));
}

/**
 * arc_reg_get - Gets value of specified EMAC register.
 * @priv:	Pointer to ARC EMAC private data structure.
 * @reg:	Register offset from base address.
 *
 * returns:	Value of requested register.
 */
static inline unsigned int arc_reg_get(struct arc_emac_priv *priv, int reg)
{
	return ioread32(priv->regs + reg * sizeof(int));
}

/**
 * arc_reg_or - Applies mask to specified EMAC register - ("reg" | "mask").
 * @priv:	Pointer to ARC EMAC private data structure.
 * @reg:	Register offset from base address.
 * @mask:	Mask to apply to specified register.
 *
 * This function reads initial register value, then applies provided mask
 * to it and then writes register back.
 */
static inline void arc_reg_or(struct arc_emac_priv *priv, int reg, int mask)
{
	unsigned int value = arc_reg_get(priv, reg);

	arc_reg_set(priv, reg, value | mask);
}

/**
 * arc_reg_clr - Applies mask to specified EMAC register - ("reg" & ~"mask").
 * @priv:	Pointer to ARC EMAC private data structure.
 * @reg:	Register offset from base address.
 * @mask:	Mask to apply to specified register.
 *
 * This function reads initial register value, then applies provided mask
 * to it and then writes register back.
 */
static inline void arc_reg_clr(struct arc_emac_priv *priv, int reg, int mask)
{
	unsigned int value = arc_reg_get(priv, reg);

	arc_reg_set(priv, reg, value & ~mask);
}

int arc_mdio_probe(struct arc_emac_priv *priv);
int arc_mdio_remove(struct arc_emac_priv *priv);
int arc_emac_probe(struct net_device *ndev, int interface);
int arc_emac_remove(struct net_device *ndev);

#endif /* ARC_EMAC_H */
