/*
 *  SuperH Ethernet device driver
 *
 *  Copyright (C) 2006-2008 Nobuhiro Iwamatsu
 *  Copyright (C) 2008-2009 Renesas Solutions Corp.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 */

#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mdio-bitbang.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/cache.h>
#include <linux/io.h>

#include "sh_eth.h"

/* There is CPU dependent code */
#if defined(CONFIG_CPU_SUBTYPE_SH7724)
#define SH_ETH_RESET_DEFAULT	1
static void sh_eth_set_duplex(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 ioaddr = ndev->base_addr;

	if (mdp->duplex) /* Full */
		ctrl_outl(ctrl_inl(ioaddr + ECMR) | ECMR_DM, ioaddr + ECMR);
	else		/* Half */
		ctrl_outl(ctrl_inl(ioaddr + ECMR) & ~ECMR_DM, ioaddr + ECMR);
}

static void sh_eth_set_rate(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 ioaddr = ndev->base_addr;

	switch (mdp->speed) {
	case 10: /* 10BASE */
		ctrl_outl(ctrl_inl(ioaddr + ECMR) & ~ECMR_RTM, ioaddr + ECMR);
		break;
	case 100:/* 100BASE */
		ctrl_outl(ctrl_inl(ioaddr + ECMR) | ECMR_RTM, ioaddr + ECMR);
		break;
	default:
		break;
	}
}

/* SH7724 */
static struct sh_eth_cpu_data sh_eth_my_cpu_data = {
	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate,

	.ecsr_value	= ECSR_PSRTO | ECSR_LCHNG | ECSR_ICD,
	.ecsipr_value	= ECSIPR_PSRTOIP | ECSIPR_LCHNGIP | ECSIPR_ICDIP,
	.eesipr_value	= DMAC_M_RFRMER | DMAC_M_ECI | 0x01ff009f,

	.tx_check	= EESR_FTC | EESR_CND | EESR_DLC | EESR_CD | EESR_RTO,
	.eesr_err_check	= EESR_TWB | EESR_TABT | EESR_RABT | EESR_RDE |
			  EESR_RFRMER | EESR_TFE | EESR_TDE | EESR_ECI,
	.tx_error_check	= EESR_TWB | EESR_TABT | EESR_TDE | EESR_TFE,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.hw_swap	= 1,
};

#elif defined(CONFIG_CPU_SUBTYPE_SH7763)
#define SH_ETH_HAS_TSU	1
static void sh_eth_chip_reset(struct net_device *ndev)
{
	/* reset device */
	ctrl_outl(ARSTR_ARSTR, ARSTR);
	mdelay(1);
}

static void sh_eth_reset(struct net_device *ndev)
{
	u32 ioaddr = ndev->base_addr;
	int cnt = 100;

	ctrl_outl(EDSR_ENALL, ioaddr + EDSR);
	ctrl_outl(ctrl_inl(ioaddr + EDMR) | EDMR_SRST, ioaddr + EDMR);
	while (cnt > 0) {
		if (!(ctrl_inl(ioaddr + EDMR) & 0x3))
			break;
		mdelay(1);
		cnt--;
	}
	if (cnt < 0)
		printk(KERN_ERR "Device reset fail\n");

	/* Table Init */
	ctrl_outl(0x0, ioaddr + TDLAR);
	ctrl_outl(0x0, ioaddr + TDFAR);
	ctrl_outl(0x0, ioaddr + TDFXR);
	ctrl_outl(0x0, ioaddr + TDFFR);
	ctrl_outl(0x0, ioaddr + RDLAR);
	ctrl_outl(0x0, ioaddr + RDFAR);
	ctrl_outl(0x0, ioaddr + RDFXR);
	ctrl_outl(0x0, ioaddr + RDFFR);
}

static void sh_eth_set_duplex(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 ioaddr = ndev->base_addr;

	if (mdp->duplex) /* Full */
		ctrl_outl(ctrl_inl(ioaddr + ECMR) | ECMR_DM, ioaddr + ECMR);
	else		/* Half */
		ctrl_outl(ctrl_inl(ioaddr + ECMR) & ~ECMR_DM, ioaddr + ECMR);
}

static void sh_eth_set_rate(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 ioaddr = ndev->base_addr;

	switch (mdp->speed) {
	case 10: /* 10BASE */
		ctrl_outl(GECMR_10, ioaddr + GECMR);
		break;
	case 100:/* 100BASE */
		ctrl_outl(GECMR_100, ioaddr + GECMR);
		break;
	case 1000: /* 1000BASE */
		ctrl_outl(GECMR_1000, ioaddr + GECMR);
		break;
	default:
		break;
	}
}

/* sh7763 */
static struct sh_eth_cpu_data sh_eth_my_cpu_data = {
	.chip_reset	= sh_eth_chip_reset,
	.set_duplex	= sh_eth_set_duplex,
	.set_rate	= sh_eth_set_rate,

	.ecsr_value	= ECSR_ICD | ECSR_MPD,
	.ecsipr_value	= ECSIPR_LCHNGIP | ECSIPR_ICDIP | ECSIPR_MPDIP,
	.eesipr_value	= DMAC_M_RFRMER | DMAC_M_ECI | 0x003fffff,

	.tx_check	= EESR_TC1 | EESR_FTC,
	.eesr_err_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_RABT | \
			  EESR_RDE | EESR_RFRMER | EESR_TFE | EESR_TDE | \
			  EESR_ECI,
	.tx_error_check	= EESR_TWB1 | EESR_TWB | EESR_TABT | EESR_TDE | \
			  EESR_TFE,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.bculr		= 1,
	.hw_swap	= 1,
	.rpadir		= 1,
	.no_trimd	= 1,
	.no_ade		= 1,
};

#elif defined(CONFIG_CPU_SUBTYPE_SH7619)
#define SH_ETH_RESET_DEFAULT	1
static struct sh_eth_cpu_data sh_eth_my_cpu_data = {
	.eesipr_value	= DMAC_M_RFRMER | DMAC_M_ECI | 0x003fffff,

	.apr		= 1,
	.mpr		= 1,
	.tpauser	= 1,
	.hw_swap	= 1,
};
#elif defined(CONFIG_CPU_SUBTYPE_SH7710) || defined(CONFIG_CPU_SUBTYPE_SH7712)
#define SH_ETH_RESET_DEFAULT	1
#define SH_ETH_HAS_TSU	1
static struct sh_eth_cpu_data sh_eth_my_cpu_data = {
	.eesipr_value	= DMAC_M_RFRMER | DMAC_M_ECI | 0x003fffff,
};
#endif

static void sh_eth_set_default_cpu_data(struct sh_eth_cpu_data *cd)
{
	if (!cd->ecsr_value)
		cd->ecsr_value = DEFAULT_ECSR_INIT;

	if (!cd->ecsipr_value)
		cd->ecsipr_value = DEFAULT_ECSIPR_INIT;

	if (!cd->fcftr_value)
		cd->fcftr_value = DEFAULT_FIFO_F_D_RFF | \
				  DEFAULT_FIFO_F_D_RFD;

	if (!cd->fdr_value)
		cd->fdr_value = DEFAULT_FDR_INIT;

	if (!cd->rmcr_value)
		cd->rmcr_value = DEFAULT_RMCR_VALUE;

	if (!cd->tx_check)
		cd->tx_check = DEFAULT_TX_CHECK;

	if (!cd->eesr_err_check)
		cd->eesr_err_check = DEFAULT_EESR_ERR_CHECK;

	if (!cd->tx_error_check)
		cd->tx_error_check = DEFAULT_TX_ERROR_CHECK;
}

#if defined(SH_ETH_RESET_DEFAULT)
/* Chip Reset */
static void sh_eth_reset(struct net_device *ndev)
{
	u32 ioaddr = ndev->base_addr;

	ctrl_outl(ctrl_inl(ioaddr + EDMR) | EDMR_SRST, ioaddr + EDMR);
	mdelay(3);
	ctrl_outl(ctrl_inl(ioaddr + EDMR) & ~EDMR_SRST, ioaddr + EDMR);
}
#endif

#if defined(CONFIG_CPU_SH4)
static void sh_eth_set_receive_align(struct sk_buff *skb)
{
	int reserve;

	reserve = SH4_SKB_RX_ALIGN - ((u32)skb->data & (SH4_SKB_RX_ALIGN - 1));
	if (reserve)
		skb_reserve(skb, reserve);
}
#else
static void sh_eth_set_receive_align(struct sk_buff *skb)
{
	skb_reserve(skb, SH2_SH3_SKB_RX_ALIGN);
}
#endif


/* CPU <-> EDMAC endian convert */
static inline __u32 cpu_to_edmac(struct sh_eth_private *mdp, u32 x)
{
	switch (mdp->edmac_endian) {
	case EDMAC_LITTLE_ENDIAN:
		return cpu_to_le32(x);
	case EDMAC_BIG_ENDIAN:
		return cpu_to_be32(x);
	}
	return x;
}

static inline __u32 edmac_to_cpu(struct sh_eth_private *mdp, u32 x)
{
	switch (mdp->edmac_endian) {
	case EDMAC_LITTLE_ENDIAN:
		return le32_to_cpu(x);
	case EDMAC_BIG_ENDIAN:
		return be32_to_cpu(x);
	}
	return x;
}

/*
 * Program the hardware MAC address from dev->dev_addr.
 */
static void update_mac_address(struct net_device *ndev)
{
	u32 ioaddr = ndev->base_addr;

	ctrl_outl((ndev->dev_addr[0] << 24) | (ndev->dev_addr[1] << 16) |
		  (ndev->dev_addr[2] << 8) | (ndev->dev_addr[3]),
		  ioaddr + MAHR);
	ctrl_outl((ndev->dev_addr[4] << 8) | (ndev->dev_addr[5]),
		  ioaddr + MALR);
}

/*
 * Get MAC address from SuperH MAC address register
 *
 * SuperH's Ethernet device doesn't have 'ROM' to MAC address.
 * This driver get MAC address that use by bootloader(U-boot or sh-ipl+g).
 * When you want use this device, you must set MAC address in bootloader.
 *
 */
static void read_mac_address(struct net_device *ndev)
{
	u32 ioaddr = ndev->base_addr;

	ndev->dev_addr[0] = (ctrl_inl(ioaddr + MAHR) >> 24);
	ndev->dev_addr[1] = (ctrl_inl(ioaddr + MAHR) >> 16) & 0xFF;
	ndev->dev_addr[2] = (ctrl_inl(ioaddr + MAHR) >> 8) & 0xFF;
	ndev->dev_addr[3] = (ctrl_inl(ioaddr + MAHR) & 0xFF);
	ndev->dev_addr[4] = (ctrl_inl(ioaddr + MALR) >> 8) & 0xFF;
	ndev->dev_addr[5] = (ctrl_inl(ioaddr + MALR) & 0xFF);
}

struct bb_info {
	struct mdiobb_ctrl ctrl;
	u32 addr;
	u32 mmd_msk;/* MMD */
	u32 mdo_msk;
	u32 mdi_msk;
	u32 mdc_msk;
};

/* PHY bit set */
static void bb_set(u32 addr, u32 msk)
{
	ctrl_outl(ctrl_inl(addr) | msk, addr);
}

/* PHY bit clear */
static void bb_clr(u32 addr, u32 msk)
{
	ctrl_outl((ctrl_inl(addr) & ~msk), addr);
}

/* PHY bit read */
static int bb_read(u32 addr, u32 msk)
{
	return (ctrl_inl(addr) & msk) != 0;
}

/* Data I/O pin control */
static void sh_mmd_ctrl(struct mdiobb_ctrl *ctrl, int bit)
{
	struct bb_info *bitbang = container_of(ctrl, struct bb_info, ctrl);
	if (bit)
		bb_set(bitbang->addr, bitbang->mmd_msk);
	else
		bb_clr(bitbang->addr, bitbang->mmd_msk);
}

/* Set bit data*/
static void sh_set_mdio(struct mdiobb_ctrl *ctrl, int bit)
{
	struct bb_info *bitbang = container_of(ctrl, struct bb_info, ctrl);

	if (bit)
		bb_set(bitbang->addr, bitbang->mdo_msk);
	else
		bb_clr(bitbang->addr, bitbang->mdo_msk);
}

/* Get bit data*/
static int sh_get_mdio(struct mdiobb_ctrl *ctrl)
{
	struct bb_info *bitbang = container_of(ctrl, struct bb_info, ctrl);
	return bb_read(bitbang->addr, bitbang->mdi_msk);
}

/* MDC pin control */
static void sh_mdc_ctrl(struct mdiobb_ctrl *ctrl, int bit)
{
	struct bb_info *bitbang = container_of(ctrl, struct bb_info, ctrl);

	if (bit)
		bb_set(bitbang->addr, bitbang->mdc_msk);
	else
		bb_clr(bitbang->addr, bitbang->mdc_msk);
}

/* mdio bus control struct */
static struct mdiobb_ops bb_ops = {
	.owner = THIS_MODULE,
	.set_mdc = sh_mdc_ctrl,
	.set_mdio_dir = sh_mmd_ctrl,
	.set_mdio_data = sh_set_mdio,
	.get_mdio_data = sh_get_mdio,
};

/* free skb and descriptor buffer */
static void sh_eth_ring_free(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int i;

	/* Free Rx skb ringbuffer */
	if (mdp->rx_skbuff) {
		for (i = 0; i < RX_RING_SIZE; i++) {
			if (mdp->rx_skbuff[i])
				dev_kfree_skb(mdp->rx_skbuff[i]);
		}
	}
	kfree(mdp->rx_skbuff);

	/* Free Tx skb ringbuffer */
	if (mdp->tx_skbuff) {
		for (i = 0; i < TX_RING_SIZE; i++) {
			if (mdp->tx_skbuff[i])
				dev_kfree_skb(mdp->tx_skbuff[i]);
		}
	}
	kfree(mdp->tx_skbuff);
}

/* format skb and descriptor buffer */
static void sh_eth_ring_format(struct net_device *ndev)
{
	u32 ioaddr = ndev->base_addr;
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int i;
	struct sk_buff *skb;
	struct sh_eth_rxdesc *rxdesc = NULL;
	struct sh_eth_txdesc *txdesc = NULL;
	int rx_ringsize = sizeof(*rxdesc) * RX_RING_SIZE;
	int tx_ringsize = sizeof(*txdesc) * TX_RING_SIZE;

	mdp->cur_rx = mdp->cur_tx = 0;
	mdp->dirty_rx = mdp->dirty_tx = 0;

	memset(mdp->rx_ring, 0, rx_ringsize);

	/* build Rx ring buffer */
	for (i = 0; i < RX_RING_SIZE; i++) {
		/* skb */
		mdp->rx_skbuff[i] = NULL;
		skb = dev_alloc_skb(mdp->rx_buf_sz);
		mdp->rx_skbuff[i] = skb;
		if (skb == NULL)
			break;
		dma_map_single(&ndev->dev, skb->tail, mdp->rx_buf_sz,
				DMA_FROM_DEVICE);
		skb->dev = ndev; /* Mark as being used by this device. */
		sh_eth_set_receive_align(skb);

		/* RX descriptor */
		rxdesc = &mdp->rx_ring[i];
		rxdesc->addr = virt_to_phys(PTR_ALIGN(skb->data, 4));
		rxdesc->status = cpu_to_edmac(mdp, RD_RACT | RD_RFP);

		/* The size of the buffer is 16 byte boundary. */
		rxdesc->buffer_length = ALIGN(mdp->rx_buf_sz, 16);
		/* Rx descriptor address set */
		if (i == 0) {
			ctrl_outl(mdp->rx_desc_dma, ioaddr + RDLAR);
#if defined(CONFIG_CPU_SUBTYPE_SH7763)
			ctrl_outl(mdp->rx_desc_dma, ioaddr + RDFAR);
#endif
		}
	}

	mdp->dirty_rx = (u32) (i - RX_RING_SIZE);

	/* Mark the last entry as wrapping the ring. */
	rxdesc->status |= cpu_to_edmac(mdp, RD_RDEL);

	memset(mdp->tx_ring, 0, tx_ringsize);

	/* build Tx ring buffer */
	for (i = 0; i < TX_RING_SIZE; i++) {
		mdp->tx_skbuff[i] = NULL;
		txdesc = &mdp->tx_ring[i];
		txdesc->status = cpu_to_edmac(mdp, TD_TFP);
		txdesc->buffer_length = 0;
		if (i == 0) {
			/* Tx descriptor address set */
			ctrl_outl(mdp->tx_desc_dma, ioaddr + TDLAR);
#if defined(CONFIG_CPU_SUBTYPE_SH7763)
			ctrl_outl(mdp->tx_desc_dma, ioaddr + TDFAR);
#endif
		}
	}

	txdesc->status |= cpu_to_edmac(mdp, TD_TDLE);
}

/* Get skb and descriptor buffer */
static int sh_eth_ring_init(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int rx_ringsize, tx_ringsize, ret = 0;

	/*
	 * +26 gets the maximum ethernet encapsulation, +7 & ~7 because the
	 * card needs room to do 8 byte alignment, +2 so we can reserve
	 * the first 2 bytes, and +16 gets room for the status word from the
	 * card.
	 */
	mdp->rx_buf_sz = (ndev->mtu <= 1492 ? PKT_BUF_SZ :
			  (((ndev->mtu + 26 + 7) & ~7) + 2 + 16));

	/* Allocate RX and TX skb rings */
	mdp->rx_skbuff = kmalloc(sizeof(*mdp->rx_skbuff) * RX_RING_SIZE,
				GFP_KERNEL);
	if (!mdp->rx_skbuff) {
		dev_err(&ndev->dev, "Cannot allocate Rx skb\n");
		ret = -ENOMEM;
		return ret;
	}

	mdp->tx_skbuff = kmalloc(sizeof(*mdp->tx_skbuff) * TX_RING_SIZE,
				GFP_KERNEL);
	if (!mdp->tx_skbuff) {
		dev_err(&ndev->dev, "Cannot allocate Tx skb\n");
		ret = -ENOMEM;
		goto skb_ring_free;
	}

	/* Allocate all Rx descriptors. */
	rx_ringsize = sizeof(struct sh_eth_rxdesc) * RX_RING_SIZE;
	mdp->rx_ring = dma_alloc_coherent(NULL, rx_ringsize, &mdp->rx_desc_dma,
			GFP_KERNEL);

	if (!mdp->rx_ring) {
		dev_err(&ndev->dev, "Cannot allocate Rx Ring (size %d bytes)\n",
			rx_ringsize);
		ret = -ENOMEM;
		goto desc_ring_free;
	}

	mdp->dirty_rx = 0;

	/* Allocate all Tx descriptors. */
	tx_ringsize = sizeof(struct sh_eth_txdesc) * TX_RING_SIZE;
	mdp->tx_ring = dma_alloc_coherent(NULL, tx_ringsize, &mdp->tx_desc_dma,
			GFP_KERNEL);
	if (!mdp->tx_ring) {
		dev_err(&ndev->dev, "Cannot allocate Tx Ring (size %d bytes)\n",
			tx_ringsize);
		ret = -ENOMEM;
		goto desc_ring_free;
	}
	return ret;

desc_ring_free:
	/* free DMA buffer */
	dma_free_coherent(NULL, rx_ringsize, mdp->rx_ring, mdp->rx_desc_dma);

skb_ring_free:
	/* Free Rx and Tx skb ring buffer */
	sh_eth_ring_free(ndev);

	return ret;
}

static int sh_eth_dev_init(struct net_device *ndev)
{
	int ret = 0;
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 ioaddr = ndev->base_addr;
	u_int32_t rx_int_var, tx_int_var;
	u32 val;

	/* Soft Reset */
	sh_eth_reset(ndev);

	/* Descriptor format */
	sh_eth_ring_format(ndev);
	if (mdp->cd->rpadir)
		ctrl_outl(mdp->cd->rpadir_value, ioaddr + RPADIR);

	/* all sh_eth int mask */
	ctrl_outl(0, ioaddr + EESIPR);

#if defined(__LITTLE_ENDIAN__)
	if (mdp->cd->hw_swap)
		ctrl_outl(EDMR_EL, ioaddr + EDMR);
	else
#endif
		ctrl_outl(0, ioaddr + EDMR);

	/* FIFO size set */
	ctrl_outl(mdp->cd->fdr_value, ioaddr + FDR);
	ctrl_outl(0, ioaddr + TFTR);

	/* Frame recv control */
	ctrl_outl(mdp->cd->rmcr_value, ioaddr + RMCR);

	rx_int_var = mdp->rx_int_var = DESC_I_RINT8 | DESC_I_RINT5;
	tx_int_var = mdp->tx_int_var = DESC_I_TINT2;
	ctrl_outl(rx_int_var | tx_int_var, ioaddr + TRSCER);

	if (mdp->cd->bculr)
		ctrl_outl(0x800, ioaddr + BCULR);	/* Burst sycle set */

	ctrl_outl(mdp->cd->fcftr_value, ioaddr + FCFTR);

	if (!mdp->cd->no_trimd)
		ctrl_outl(0, ioaddr + TRIMD);

	/* Recv frame limit set register */
	ctrl_outl(RFLR_VALUE, ioaddr + RFLR);

	ctrl_outl(ctrl_inl(ioaddr + EESR), ioaddr + EESR);
	ctrl_outl(mdp->cd->eesipr_value, ioaddr + EESIPR);

	/* PAUSE Prohibition */
	val = (ctrl_inl(ioaddr + ECMR) & ECMR_DM) |
		ECMR_ZPF | (mdp->duplex ? ECMR_DM : 0) | ECMR_TE | ECMR_RE;

	ctrl_outl(val, ioaddr + ECMR);

	if (mdp->cd->set_rate)
		mdp->cd->set_rate(ndev);

	/* E-MAC Status Register clear */
	ctrl_outl(mdp->cd->ecsr_value, ioaddr + ECSR);

	/* E-MAC Interrupt Enable register */
	ctrl_outl(mdp->cd->ecsipr_value, ioaddr + ECSIPR);

	/* Set MAC address */
	update_mac_address(ndev);

	/* mask reset */
	if (mdp->cd->apr)
		ctrl_outl(APR_AP, ioaddr + APR);
	if (mdp->cd->mpr)
		ctrl_outl(MPR_MP, ioaddr + MPR);
	if (mdp->cd->tpauser)
		ctrl_outl(TPAUSER_UNLIMITED, ioaddr + TPAUSER);

	/* Setting the Rx mode will start the Rx process. */
	ctrl_outl(EDRRR_R, ioaddr + EDRRR);

	netif_start_queue(ndev);

	return ret;
}

/* free Tx skb function */
static int sh_eth_txfree(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct sh_eth_txdesc *txdesc;
	int freeNum = 0;
	int entry = 0;

	for (; mdp->cur_tx - mdp->dirty_tx > 0; mdp->dirty_tx++) {
		entry = mdp->dirty_tx % TX_RING_SIZE;
		txdesc = &mdp->tx_ring[entry];
		if (txdesc->status & cpu_to_edmac(mdp, TD_TACT))
			break;
		/* Free the original skb. */
		if (mdp->tx_skbuff[entry]) {
			dev_kfree_skb_irq(mdp->tx_skbuff[entry]);
			mdp->tx_skbuff[entry] = NULL;
			freeNum++;
		}
		txdesc->status = cpu_to_edmac(mdp, TD_TFP);
		if (entry >= TX_RING_SIZE - 1)
			txdesc->status |= cpu_to_edmac(mdp, TD_TDLE);

		mdp->stats.tx_packets++;
		mdp->stats.tx_bytes += txdesc->buffer_length;
	}
	return freeNum;
}

/* Packet receive function */
static int sh_eth_rx(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct sh_eth_rxdesc *rxdesc;

	int entry = mdp->cur_rx % RX_RING_SIZE;
	int boguscnt = (mdp->dirty_rx + RX_RING_SIZE) - mdp->cur_rx;
	struct sk_buff *skb;
	u16 pkt_len = 0;
	u32 desc_status;

	rxdesc = &mdp->rx_ring[entry];
	while (!(rxdesc->status & cpu_to_edmac(mdp, RD_RACT))) {
		desc_status = edmac_to_cpu(mdp, rxdesc->status);
		pkt_len = rxdesc->frame_length;

		if (--boguscnt < 0)
			break;

		if (!(desc_status & RDFEND))
			mdp->stats.rx_length_errors++;

		if (desc_status & (RD_RFS1 | RD_RFS2 | RD_RFS3 | RD_RFS4 |
				   RD_RFS5 | RD_RFS6 | RD_RFS10)) {
			mdp->stats.rx_errors++;
			if (desc_status & RD_RFS1)
				mdp->stats.rx_crc_errors++;
			if (desc_status & RD_RFS2)
				mdp->stats.rx_frame_errors++;
			if (desc_status & RD_RFS3)
				mdp->stats.rx_length_errors++;
			if (desc_status & RD_RFS4)
				mdp->stats.rx_length_errors++;
			if (desc_status & RD_RFS6)
				mdp->stats.rx_missed_errors++;
			if (desc_status & RD_RFS10)
				mdp->stats.rx_over_errors++;
		} else {
			if (!mdp->cd->hw_swap)
				sh_eth_soft_swap(
					phys_to_virt(ALIGN(rxdesc->addr, 4)),
					pkt_len + 2);
			skb = mdp->rx_skbuff[entry];
			mdp->rx_skbuff[entry] = NULL;
			skb_put(skb, pkt_len);
			skb->protocol = eth_type_trans(skb, ndev);
			netif_rx(skb);
			mdp->stats.rx_packets++;
			mdp->stats.rx_bytes += pkt_len;
		}
		rxdesc->status |= cpu_to_edmac(mdp, RD_RACT);
		entry = (++mdp->cur_rx) % RX_RING_SIZE;
		rxdesc = &mdp->rx_ring[entry];
	}

	/* Refill the Rx ring buffers. */
	for (; mdp->cur_rx - mdp->dirty_rx > 0; mdp->dirty_rx++) {
		entry = mdp->dirty_rx % RX_RING_SIZE;
		rxdesc = &mdp->rx_ring[entry];
		/* The size of the buffer is 16 byte boundary. */
		rxdesc->buffer_length = ALIGN(mdp->rx_buf_sz, 16);

		if (mdp->rx_skbuff[entry] == NULL) {
			skb = dev_alloc_skb(mdp->rx_buf_sz);
			mdp->rx_skbuff[entry] = skb;
			if (skb == NULL)
				break;	/* Better luck next round. */
			dma_map_single(&ndev->dev, skb->tail, mdp->rx_buf_sz,
					DMA_FROM_DEVICE);
			skb->dev = ndev;
			sh_eth_set_receive_align(skb);

			skb->ip_summed = CHECKSUM_NONE;
			rxdesc->addr = virt_to_phys(PTR_ALIGN(skb->data, 4));
		}
		if (entry >= RX_RING_SIZE - 1)
			rxdesc->status |=
				cpu_to_edmac(mdp, RD_RACT | RD_RFP | RD_RDEL);
		else
			rxdesc->status |=
				cpu_to_edmac(mdp, RD_RACT | RD_RFP);
	}

	/* Restart Rx engine if stopped. */
	/* If we don't need to check status, don't. -KDU */
	if (!(ctrl_inl(ndev->base_addr + EDRRR) & EDRRR_R))
		ctrl_outl(EDRRR_R, ndev->base_addr + EDRRR);

	return 0;
}

/* error control function */
static void sh_eth_error(struct net_device *ndev, int intr_status)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 ioaddr = ndev->base_addr;
	u32 felic_stat;
	u32 link_stat;
	u32 mask;

	if (intr_status & EESR_ECI) {
		felic_stat = ctrl_inl(ioaddr + ECSR);
		ctrl_outl(felic_stat, ioaddr + ECSR);	/* clear int */
		if (felic_stat & ECSR_ICD)
			mdp->stats.tx_carrier_errors++;
		if (felic_stat & ECSR_LCHNG) {
			/* Link Changed */
			if (mdp->cd->no_psr) {
				if (mdp->link == PHY_DOWN)
					link_stat = 0;
				else
					link_stat = PHY_ST_LINK;
			} else {
				link_stat = (ctrl_inl(ioaddr + PSR));
			}
			if (!(link_stat & PHY_ST_LINK)) {
				/* Link Down : disable tx and rx */
				ctrl_outl(ctrl_inl(ioaddr + ECMR) &
					  ~(ECMR_RE | ECMR_TE), ioaddr + ECMR);
			} else {
				/* Link Up */
				ctrl_outl(ctrl_inl(ioaddr + EESIPR) &
					  ~DMAC_M_ECI, ioaddr + EESIPR);
				/*clear int */
				ctrl_outl(ctrl_inl(ioaddr + ECSR),
					  ioaddr + ECSR);
				ctrl_outl(ctrl_inl(ioaddr + EESIPR) |
					  DMAC_M_ECI, ioaddr + EESIPR);
				/* enable tx and rx */
				ctrl_outl(ctrl_inl(ioaddr + ECMR) |
					  (ECMR_RE | ECMR_TE), ioaddr + ECMR);
			}
		}
	}

	if (intr_status & EESR_TWB) {
		/* Write buck end. unused write back interrupt */
		if (intr_status & EESR_TABT)	/* Transmit Abort int */
			mdp->stats.tx_aborted_errors++;
	}

	if (intr_status & EESR_RABT) {
		/* Receive Abort int */
		if (intr_status & EESR_RFRMER) {
			/* Receive Frame Overflow int */
			mdp->stats.rx_frame_errors++;
			dev_err(&ndev->dev, "Receive Frame Overflow\n");
		}
	}

	if (!mdp->cd->no_ade) {
		if (intr_status & EESR_ADE && intr_status & EESR_TDE &&
		    intr_status & EESR_TFE)
			mdp->stats.tx_fifo_errors++;
	}

	if (intr_status & EESR_RDE) {
		/* Receive Descriptor Empty int */
		mdp->stats.rx_over_errors++;

		if (ctrl_inl(ioaddr + EDRRR) ^ EDRRR_R)
			ctrl_outl(EDRRR_R, ioaddr + EDRRR);
		dev_err(&ndev->dev, "Receive Descriptor Empty\n");
	}
	if (intr_status & EESR_RFE) {
		/* Receive FIFO Overflow int */
		mdp->stats.rx_fifo_errors++;
		dev_err(&ndev->dev, "Receive FIFO Overflow\n");
	}

	mask = EESR_TWB | EESR_TABT | EESR_ADE | EESR_TDE | EESR_TFE;
	if (mdp->cd->no_ade)
		mask &= ~EESR_ADE;
	if (intr_status & mask) {
		/* Tx error */
		u32 edtrr = ctrl_inl(ndev->base_addr + EDTRR);
		/* dmesg */
		dev_err(&ndev->dev, "TX error. status=%8.8x cur_tx=%8.8x ",
				intr_status, mdp->cur_tx);
		dev_err(&ndev->dev, "dirty_tx=%8.8x state=%8.8x EDTRR=%8.8x.\n",
				mdp->dirty_tx, (u32) ndev->state, edtrr);
		/* dirty buffer free */
		sh_eth_txfree(ndev);

		/* SH7712 BUG */
		if (edtrr ^ EDTRR_TRNS) {
			/* tx dma start */
			ctrl_outl(EDTRR_TRNS, ndev->base_addr + EDTRR);
		}
		/* wakeup */
		netif_wake_queue(ndev);
	}
}

static irqreturn_t sh_eth_interrupt(int irq, void *netdev)
{
	struct net_device *ndev = netdev;
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct sh_eth_cpu_data *cd = mdp->cd;
	irqreturn_t ret = IRQ_NONE;
	u32 ioaddr, boguscnt = RX_RING_SIZE;
	u32 intr_status = 0;

	ioaddr = ndev->base_addr;
	spin_lock(&mdp->lock);

	/* Get interrpt stat */
	intr_status = ctrl_inl(ioaddr + EESR);
	/* Clear interrupt */
	if (intr_status & (EESR_FRC | EESR_RMAF | EESR_RRF |
			EESR_RTLF | EESR_RTSF | EESR_PRE | EESR_CERF |
			cd->tx_check | cd->eesr_err_check)) {
		ctrl_outl(intr_status, ioaddr + EESR);
		ret = IRQ_HANDLED;
	} else
		goto other_irq;

	if (intr_status & (EESR_FRC | /* Frame recv*/
			EESR_RMAF | /* Multi cast address recv*/
			EESR_RRF  | /* Bit frame recv */
			EESR_RTLF | /* Long frame recv*/
			EESR_RTSF | /* short frame recv */
			EESR_PRE  | /* PHY-LSI recv error */
			EESR_CERF)){ /* recv frame CRC error */
		sh_eth_rx(ndev);
	}

	/* Tx Check */
	if (intr_status & cd->tx_check) {
		sh_eth_txfree(ndev);
		netif_wake_queue(ndev);
	}

	if (intr_status & cd->eesr_err_check)
		sh_eth_error(ndev, intr_status);

	if (--boguscnt < 0) {
		printk(KERN_WARNING
		       "%s: Too much work at interrupt, status=0x%4.4x.\n",
		       ndev->name, intr_status);
	}

other_irq:
	spin_unlock(&mdp->lock);

	return ret;
}

static void sh_eth_timer(unsigned long data)
{
	struct net_device *ndev = (struct net_device *)data;
	struct sh_eth_private *mdp = netdev_priv(ndev);

	mod_timer(&mdp->timer, jiffies + (10 * HZ));
}

/* PHY state control function */
static void sh_eth_adjust_link(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct phy_device *phydev = mdp->phydev;
	u32 ioaddr = ndev->base_addr;
	int new_state = 0;

	if (phydev->link != PHY_DOWN) {
		if (phydev->duplex != mdp->duplex) {
			new_state = 1;
			mdp->duplex = phydev->duplex;
			if (mdp->cd->set_duplex)
				mdp->cd->set_duplex(ndev);
		}

		if (phydev->speed != mdp->speed) {
			new_state = 1;
			mdp->speed = phydev->speed;
			if (mdp->cd->set_rate)
				mdp->cd->set_rate(ndev);
		}
		if (mdp->link == PHY_DOWN) {
			ctrl_outl((ctrl_inl(ioaddr + ECMR) & ~ECMR_TXF)
					| ECMR_DM, ioaddr + ECMR);
			new_state = 1;
			mdp->link = phydev->link;
		}
	} else if (mdp->link) {
		new_state = 1;
		mdp->link = PHY_DOWN;
		mdp->speed = 0;
		mdp->duplex = -1;
	}

	if (new_state)
		phy_print_status(phydev);
}

/* PHY init function */
static int sh_eth_phy_init(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	char phy_id[MII_BUS_ID_SIZE + 3];
	struct phy_device *phydev = NULL;

	snprintf(phy_id, sizeof(phy_id), PHY_ID_FMT,
		mdp->mii_bus->id , mdp->phy_id);

	mdp->link = PHY_DOWN;
	mdp->speed = 0;
	mdp->duplex = -1;

	/* Try connect to PHY */
	phydev = phy_connect(ndev, phy_id, &sh_eth_adjust_link,
				0, PHY_INTERFACE_MODE_MII);
	if (IS_ERR(phydev)) {
		dev_err(&ndev->dev, "phy_connect failed\n");
		return PTR_ERR(phydev);
	}

	dev_info(&ndev->dev, "attached phy %i to driver %s\n",
		phydev->addr, phydev->drv->name);

	mdp->phydev = phydev;

	return 0;
}

/* PHY control start function */
static int sh_eth_phy_start(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	int ret;

	ret = sh_eth_phy_init(ndev);
	if (ret)
		return ret;

	/* reset phy - this also wakes it from PDOWN */
	phy_write(mdp->phydev, MII_BMCR, BMCR_RESET);
	phy_start(mdp->phydev);

	return 0;
}

/* network device open function */
static int sh_eth_open(struct net_device *ndev)
{
	int ret = 0;
	struct sh_eth_private *mdp = netdev_priv(ndev);

	ret = request_irq(ndev->irq, &sh_eth_interrupt,
#if defined(CONFIG_CPU_SUBTYPE_SH7763) || defined(CONFIG_CPU_SUBTYPE_SH7764)
				IRQF_SHARED,
#else
				0,
#endif
				ndev->name, ndev);
	if (ret) {
		dev_err(&ndev->dev, "Can not assign IRQ number\n");
		return ret;
	}

	/* Descriptor set */
	ret = sh_eth_ring_init(ndev);
	if (ret)
		goto out_free_irq;

	/* device init */
	ret = sh_eth_dev_init(ndev);
	if (ret)
		goto out_free_irq;

	/* PHY control start*/
	ret = sh_eth_phy_start(ndev);
	if (ret)
		goto out_free_irq;

	/* Set the timer to check for link beat. */
	init_timer(&mdp->timer);
	mdp->timer.expires = (jiffies + (24 * HZ)) / 10;/* 2.4 sec. */
	setup_timer(&mdp->timer, sh_eth_timer, (unsigned long)ndev);

	return ret;

out_free_irq:
	free_irq(ndev->irq, ndev);
	return ret;
}

/* Timeout function */
static void sh_eth_tx_timeout(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 ioaddr = ndev->base_addr;
	struct sh_eth_rxdesc *rxdesc;
	int i;

	netif_stop_queue(ndev);

	/* worning message out. */
	printk(KERN_WARNING "%s: transmit timed out, status %8.8x,"
	       " resetting...\n", ndev->name, (int)ctrl_inl(ioaddr + EESR));

	/* tx_errors count up */
	mdp->stats.tx_errors++;

	/* timer off */
	del_timer_sync(&mdp->timer);

	/* Free all the skbuffs in the Rx queue. */
	for (i = 0; i < RX_RING_SIZE; i++) {
		rxdesc = &mdp->rx_ring[i];
		rxdesc->status = 0;
		rxdesc->addr = 0xBADF00D0;
		if (mdp->rx_skbuff[i])
			dev_kfree_skb(mdp->rx_skbuff[i]);
		mdp->rx_skbuff[i] = NULL;
	}
	for (i = 0; i < TX_RING_SIZE; i++) {
		if (mdp->tx_skbuff[i])
			dev_kfree_skb(mdp->tx_skbuff[i]);
		mdp->tx_skbuff[i] = NULL;
	}

	/* device init */
	sh_eth_dev_init(ndev);

	/* timer on */
	mdp->timer.expires = (jiffies + (24 * HZ)) / 10;/* 2.4 sec. */
	add_timer(&mdp->timer);
}

/* Packet transmit function */
static int sh_eth_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct sh_eth_txdesc *txdesc;
	u32 entry;
	unsigned long flags;

	spin_lock_irqsave(&mdp->lock, flags);
	if ((mdp->cur_tx - mdp->dirty_tx) >= (TX_RING_SIZE - 4)) {
		if (!sh_eth_txfree(ndev)) {
			netif_stop_queue(ndev);
			spin_unlock_irqrestore(&mdp->lock, flags);
			return 1;
		}
	}
	spin_unlock_irqrestore(&mdp->lock, flags);

	entry = mdp->cur_tx % TX_RING_SIZE;
	mdp->tx_skbuff[entry] = skb;
	txdesc = &mdp->tx_ring[entry];
	txdesc->addr = virt_to_phys(skb->data);
	/* soft swap. */
	if (!mdp->cd->hw_swap)
		sh_eth_soft_swap(phys_to_virt(ALIGN(txdesc->addr, 4)),
				 skb->len + 2);
	/* write back */
	__flush_purge_region(skb->data, skb->len);
	if (skb->len < ETHERSMALL)
		txdesc->buffer_length = ETHERSMALL;
	else
		txdesc->buffer_length = skb->len;

	if (entry >= TX_RING_SIZE - 1)
		txdesc->status |= cpu_to_edmac(mdp, TD_TACT | TD_TDLE);
	else
		txdesc->status |= cpu_to_edmac(mdp, TD_TACT);

	mdp->cur_tx++;

	if (!(ctrl_inl(ndev->base_addr + EDTRR) & EDTRR_TRNS))
		ctrl_outl(EDTRR_TRNS, ndev->base_addr + EDTRR);

	ndev->trans_start = jiffies;

	return 0;
}

/* device close function */
static int sh_eth_close(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 ioaddr = ndev->base_addr;
	int ringsize;

	netif_stop_queue(ndev);

	/* Disable interrupts by clearing the interrupt mask. */
	ctrl_outl(0x0000, ioaddr + EESIPR);

	/* Stop the chip's Tx and Rx processes. */
	ctrl_outl(0, ioaddr + EDTRR);
	ctrl_outl(0, ioaddr + EDRRR);

	/* PHY Disconnect */
	if (mdp->phydev) {
		phy_stop(mdp->phydev);
		phy_disconnect(mdp->phydev);
	}

	free_irq(ndev->irq, ndev);

	del_timer_sync(&mdp->timer);

	/* Free all the skbuffs in the Rx queue. */
	sh_eth_ring_free(ndev);

	/* free DMA buffer */
	ringsize = sizeof(struct sh_eth_rxdesc) * RX_RING_SIZE;
	dma_free_coherent(NULL, ringsize, mdp->rx_ring, mdp->rx_desc_dma);

	/* free DMA buffer */
	ringsize = sizeof(struct sh_eth_txdesc) * TX_RING_SIZE;
	dma_free_coherent(NULL, ringsize, mdp->tx_ring, mdp->tx_desc_dma);

	return 0;
}

static struct net_device_stats *sh_eth_get_stats(struct net_device *ndev)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	u32 ioaddr = ndev->base_addr;

	mdp->stats.tx_dropped += ctrl_inl(ioaddr + TROCR);
	ctrl_outl(0, ioaddr + TROCR);	/* (write clear) */
	mdp->stats.collisions += ctrl_inl(ioaddr + CDCR);
	ctrl_outl(0, ioaddr + CDCR);	/* (write clear) */
	mdp->stats.tx_carrier_errors += ctrl_inl(ioaddr + LCCR);
	ctrl_outl(0, ioaddr + LCCR);	/* (write clear) */
#if defined(CONFIG_CPU_SUBTYPE_SH7763)
	mdp->stats.tx_carrier_errors += ctrl_inl(ioaddr + CERCR);/* CERCR */
	ctrl_outl(0, ioaddr + CERCR);	/* (write clear) */
	mdp->stats.tx_carrier_errors += ctrl_inl(ioaddr + CEECR);/* CEECR */
	ctrl_outl(0, ioaddr + CEECR);	/* (write clear) */
#else
	mdp->stats.tx_carrier_errors += ctrl_inl(ioaddr + CNDCR);
	ctrl_outl(0, ioaddr + CNDCR);	/* (write clear) */
#endif
	return &mdp->stats;
}

/* ioctl to device funciotn*/
static int sh_eth_do_ioctl(struct net_device *ndev, struct ifreq *rq,
				int cmd)
{
	struct sh_eth_private *mdp = netdev_priv(ndev);
	struct phy_device *phydev = mdp->phydev;

	if (!netif_running(ndev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

	return phy_mii_ioctl(phydev, if_mii(rq), cmd);
}

#if defined(SH_ETH_HAS_TSU)
/* Multicast reception directions set */
static void sh_eth_set_multicast_list(struct net_device *ndev)
{
	u32 ioaddr = ndev->base_addr;

	if (ndev->flags & IFF_PROMISC) {
		/* Set promiscuous. */
		ctrl_outl((ctrl_inl(ioaddr + ECMR) & ~ECMR_MCT) | ECMR_PRM,
			  ioaddr + ECMR);
	} else {
		/* Normal, unicast/broadcast-only mode. */
		ctrl_outl((ctrl_inl(ioaddr + ECMR) & ~ECMR_PRM) | ECMR_MCT,
			  ioaddr + ECMR);
	}
}

/* SuperH's TSU register init function */
static void sh_eth_tsu_init(u32 ioaddr)
{
	ctrl_outl(0, ioaddr + TSU_FWEN0);	/* Disable forward(0->1) */
	ctrl_outl(0, ioaddr + TSU_FWEN1);	/* Disable forward(1->0) */
	ctrl_outl(0, ioaddr + TSU_FCM);	/* forward fifo 3k-3k */
	ctrl_outl(0xc, ioaddr + TSU_BSYSL0);
	ctrl_outl(0xc, ioaddr + TSU_BSYSL1);
	ctrl_outl(0, ioaddr + TSU_PRISL0);
	ctrl_outl(0, ioaddr + TSU_PRISL1);
	ctrl_outl(0, ioaddr + TSU_FWSL0);
	ctrl_outl(0, ioaddr + TSU_FWSL1);
	ctrl_outl(TSU_FWSLC_POSTENU | TSU_FWSLC_POSTENL, ioaddr + TSU_FWSLC);
#if defined(CONFIG_CPU_SUBTYPE_SH7763)
	ctrl_outl(0, ioaddr + TSU_QTAG0);	/* Disable QTAG(0->1) */
	ctrl_outl(0, ioaddr + TSU_QTAG1);	/* Disable QTAG(1->0) */
#else
	ctrl_outl(0, ioaddr + TSU_QTAGM0);	/* Disable QTAG(0->1) */
	ctrl_outl(0, ioaddr + TSU_QTAGM1);	/* Disable QTAG(1->0) */
#endif
	ctrl_outl(0, ioaddr + TSU_FWSR);	/* all interrupt status clear */
	ctrl_outl(0, ioaddr + TSU_FWINMK);	/* Disable all interrupt */
	ctrl_outl(0, ioaddr + TSU_TEN);	/* Disable all CAM entry */
	ctrl_outl(0, ioaddr + TSU_POST1);	/* Disable CAM entry [ 0- 7] */
	ctrl_outl(0, ioaddr + TSU_POST2);	/* Disable CAM entry [ 8-15] */
	ctrl_outl(0, ioaddr + TSU_POST3);	/* Disable CAM entry [16-23] */
	ctrl_outl(0, ioaddr + TSU_POST4);	/* Disable CAM entry [24-31] */
}
#endif /* SH_ETH_HAS_TSU */

/* MDIO bus release function */
static int sh_mdio_release(struct net_device *ndev)
{
	struct mii_bus *bus = dev_get_drvdata(&ndev->dev);

	/* unregister mdio bus */
	mdiobus_unregister(bus);

	/* remove mdio bus info from net_device */
	dev_set_drvdata(&ndev->dev, NULL);

	/* free bitbang info */
	free_mdio_bitbang(bus);

	return 0;
}

/* MDIO bus init function */
static int sh_mdio_init(struct net_device *ndev, int id)
{
	int ret, i;
	struct bb_info *bitbang;
	struct sh_eth_private *mdp = netdev_priv(ndev);

	/* create bit control struct for PHY */
	bitbang = kzalloc(sizeof(struct bb_info), GFP_KERNEL);
	if (!bitbang) {
		ret = -ENOMEM;
		goto out;
	}

	/* bitbang init */
	bitbang->addr = ndev->base_addr + PIR;
	bitbang->mdi_msk = 0x08;
	bitbang->mdo_msk = 0x04;
	bitbang->mmd_msk = 0x02;/* MMD */
	bitbang->mdc_msk = 0x01;
	bitbang->ctrl.ops = &bb_ops;

	/* MII contorller setting */
	mdp->mii_bus = alloc_mdio_bitbang(&bitbang->ctrl);
	if (!mdp->mii_bus) {
		ret = -ENOMEM;
		goto out_free_bitbang;
	}

	/* Hook up MII support for ethtool */
	mdp->mii_bus->name = "sh_mii";
	mdp->mii_bus->parent = &ndev->dev;
	snprintf(mdp->mii_bus->id, MII_BUS_ID_SIZE, "%x", id);

	/* PHY IRQ */
	mdp->mii_bus->irq = kmalloc(sizeof(int)*PHY_MAX_ADDR, GFP_KERNEL);
	if (!mdp->mii_bus->irq) {
		ret = -ENOMEM;
		goto out_free_bus;
	}

	for (i = 0; i < PHY_MAX_ADDR; i++)
		mdp->mii_bus->irq[i] = PHY_POLL;

	/* regist mdio bus */
	ret = mdiobus_register(mdp->mii_bus);
	if (ret)
		goto out_free_irq;

	dev_set_drvdata(&ndev->dev, mdp->mii_bus);

	return 0;

out_free_irq:
	kfree(mdp->mii_bus->irq);

out_free_bus:
	free_mdio_bitbang(mdp->mii_bus);

out_free_bitbang:
	kfree(bitbang);

out:
	return ret;
}

static const struct net_device_ops sh_eth_netdev_ops = {
	.ndo_open		= sh_eth_open,
	.ndo_stop		= sh_eth_close,
	.ndo_start_xmit		= sh_eth_start_xmit,
	.ndo_get_stats		= sh_eth_get_stats,
#if defined(SH_ETH_HAS_TSU)
	.ndo_set_multicast_list	= sh_eth_set_multicast_list,
#endif
	.ndo_tx_timeout		= sh_eth_tx_timeout,
	.ndo_do_ioctl		= sh_eth_do_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

static int sh_eth_drv_probe(struct platform_device *pdev)
{
	int ret, i, devno = 0;
	struct resource *res;
	struct net_device *ndev = NULL;
	struct sh_eth_private *mdp;
	struct sh_eth_plat_data *pd;

	/* get base addr */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(res == NULL)) {
		dev_err(&pdev->dev, "invalid resource\n");
		ret = -EINVAL;
		goto out;
	}

	ndev = alloc_etherdev(sizeof(struct sh_eth_private));
	if (!ndev) {
		dev_err(&pdev->dev, "Could not allocate device.\n");
		ret = -ENOMEM;
		goto out;
	}

	/* The sh Ether-specific entries in the device structure. */
	ndev->base_addr = res->start;
	devno = pdev->id;
	if (devno < 0)
		devno = 0;

	ndev->dma = -1;
	ret = platform_get_irq(pdev, 0);
	if (ret < 0) {
		ret = -ENODEV;
		goto out_release;
	}
	ndev->irq = ret;

	SET_NETDEV_DEV(ndev, &pdev->dev);

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(ndev);

	mdp = netdev_priv(ndev);
	spin_lock_init(&mdp->lock);

	pd = (struct sh_eth_plat_data *)(pdev->dev.platform_data);
	/* get PHY ID */
	mdp->phy_id = pd->phy;
	/* EDMAC endian */
	mdp->edmac_endian = pd->edmac_endian;

	/* set cpu data */
	mdp->cd = &sh_eth_my_cpu_data;
	sh_eth_set_default_cpu_data(mdp->cd);

	/* set function */
	ndev->netdev_ops = &sh_eth_netdev_ops;
	ndev->watchdog_timeo = TX_TIMEOUT;

	mdp->post_rx = POST_RX >> (devno << 1);
	mdp->post_fw = POST_FW >> (devno << 1);

	/* read and set MAC address */
	read_mac_address(ndev);

	/* First device only init */
	if (!devno) {
		if (mdp->cd->chip_reset)
			mdp->cd->chip_reset(ndev);

#if defined(SH_ETH_HAS_TSU)
		/* TSU init (Init only)*/
		sh_eth_tsu_init(SH_TSU_ADDR);
#endif
	}

	/* network device register */
	ret = register_netdev(ndev);
	if (ret)
		goto out_release;

	/* mdio bus init */
	ret = sh_mdio_init(ndev, pdev->id);
	if (ret)
		goto out_unregister;

	/* pritnt device infomation */
	pr_info("Base address at 0x%x, ",
	       (u32)ndev->base_addr);

	for (i = 0; i < 5; i++)
		printk("%02X:", ndev->dev_addr[i]);
	printk("%02X, IRQ %d.\n", ndev->dev_addr[i], ndev->irq);

	platform_set_drvdata(pdev, ndev);

	return ret;

out_unregister:
	unregister_netdev(ndev);

out_release:
	/* net_dev free */
	if (ndev)
		free_netdev(ndev);

out:
	return ret;
}

static int sh_eth_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);

	sh_mdio_release(ndev);
	unregister_netdev(ndev);
	flush_scheduled_work();

	free_netdev(ndev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver sh_eth_driver = {
	.probe = sh_eth_drv_probe,
	.remove = sh_eth_drv_remove,
	.driver = {
		   .name = CARDNAME,
	},
};

static int __init sh_eth_init(void)
{
	return platform_driver_register(&sh_eth_driver);
}

static void __exit sh_eth_cleanup(void)
{
	platform_driver_unregister(&sh_eth_driver);
}

module_init(sh_eth_init);
module_exit(sh_eth_cleanup);

MODULE_AUTHOR("Nobuhiro Iwamatsu, Yoshihiro Shimoda");
MODULE_DESCRIPTION("Renesas SuperH Ethernet driver");
MODULE_LICENSE("GPL v2");
