
/* Advanced  Micro Devices Inc. AMD8111E Linux Network Driver
 * Copyright (C) 2004 Advanced Micro Devices
 *
 *
 * Copyright 2001,2002 Jeff Garzik <jgarzik@mandrakesoft.com> [ 8139cp.c,tg3.c ]
 * Copyright (C) 2001, 2002 David S. Miller (davem@redhat.com)[ tg3.c]
 * Copyright 1996-1999 Thomas Bogendoerfer [ pcnet32.c ]
 * Derived from the lance driver written 1993,1994,1995 by Donald Becker.
 * Copyright 1993 United States Government as represented by the
 *	Director, National Security Agency.[ pcnet32.c ]
 * Carsten Langgaard, carstenl@mips.com [ pcnet32.c ]
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.

Module Name:

	amd8111e.c

Abstract:

 	 AMD8111 based 10/100 Ethernet Controller Driver.

Environment:

	Kernel Mode

Revision History:
 	3.0.0
	   Initial Revision.
	3.0.1
	 1. Dynamic interrupt coalescing.
	 2. Removed prev_stats.
	 3. MII support.
	 4. Dynamic IPG support
	3.0.2  05/29/2003
	 1. Bug fix: Fixed failure to send jumbo packets larger than 4k.
	 2. Bug fix: Fixed VLAN support failure.
	 3. Bug fix: Fixed receive interrupt coalescing bug.
	 4. Dynamic IPG support is disabled by default.
	3.0.3 06/05/2003
	 1. Bug fix: Fixed failure to close the interface if SMP is enabled.
	3.0.4 12/09/2003
	 1. Added set_mac_address routine for bonding driver support.
	 2. Tested the driver for bonding support
	 3. Bug fix: Fixed mismach in actual receive buffer lenth and lenth
	    indicated to the h/w.
	 4. Modified amd8111e_rx() routine to receive all the received packets
	    in the first interrupt.
	 5. Bug fix: Corrected  rx_errors  reported in get_stats() function.
	3.0.5 03/22/2004
	 1. Added NAPI support

*/


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/ctype.h>
#include <linux/crc32.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
#define AMD8111E_VLAN_TAG_USED 1
#else
#define AMD8111E_VLAN_TAG_USED 0
#endif

#include "amd8111e.h"
#define MODULE_NAME	"amd8111e"
#define MODULE_VERS	"3.0.7"
MODULE_AUTHOR("Advanced Micro Devices, Inc.");
MODULE_DESCRIPTION ("AMD8111 based 10/100 Ethernet Controller. Driver Version "MODULE_VERS);
MODULE_LICENSE("GPL");
module_param_array(speed_duplex, int, NULL, 0);
MODULE_PARM_DESC(speed_duplex, "Set device speed and duplex modes, 0: Auto Negotiate, 1: 10Mbps Half Duplex, 2: 10Mbps Full Duplex, 3: 100Mbps Half Duplex, 4: 100Mbps Full Duplex");
module_param_array(coalesce, bool, NULL, 0);
MODULE_PARM_DESC(coalesce, "Enable or Disable interrupt coalescing, 1: Enable, 0: Disable");
module_param_array(dynamic_ipg, bool, NULL, 0);
MODULE_PARM_DESC(dynamic_ipg, "Enable or Disable dynamic IPG, 1: Enable, 0: Disable");

/* This function will read the PHY registers. */
static int amd8111e_read_phy(struct amd8111e_priv *lp,
			     int phy_id, int reg, u32 *val)
{
	void __iomem *mmio = lp->mmio;
	unsigned int reg_val;
	unsigned int repeat= REPEAT_CNT;

	reg_val = readl(mmio + PHY_ACCESS);
	while (reg_val & PHY_CMD_ACTIVE)
		reg_val = readl( mmio + PHY_ACCESS );

	writel( PHY_RD_CMD | ((phy_id & 0x1f) << 21) |
			   ((reg & 0x1f) << 16),  mmio +PHY_ACCESS);
	do{
		reg_val = readl(mmio + PHY_ACCESS);
		udelay(30);  /* It takes 30 us to read/write data */
	} while (--repeat && (reg_val & PHY_CMD_ACTIVE));
	if(reg_val & PHY_RD_ERR)
		goto err_phy_read;

	*val = reg_val & 0xffff;
	return 0;
err_phy_read:
	*val = 0;
	return -EINVAL;

}

/* This function will write into PHY registers. */
static int amd8111e_write_phy(struct amd8111e_priv *lp,
			      int phy_id, int reg, u32 val)
{
	unsigned int repeat = REPEAT_CNT;
	void __iomem *mmio = lp->mmio;
	unsigned int reg_val;

	reg_val = readl(mmio + PHY_ACCESS);
	while (reg_val & PHY_CMD_ACTIVE)
		reg_val = readl( mmio + PHY_ACCESS );

	writel( PHY_WR_CMD | ((phy_id & 0x1f) << 21) |
			   ((reg & 0x1f) << 16)|val, mmio + PHY_ACCESS);

	do{
		reg_val = readl(mmio + PHY_ACCESS);
		udelay(30);  /* It takes 30 us to read/write the data */
	} while (--repeat && (reg_val & PHY_CMD_ACTIVE));

	if(reg_val & PHY_RD_ERR)
		goto err_phy_write;

	return 0;

err_phy_write:
	return -EINVAL;

}

/* This is the mii register read function provided to the mii interface. */
static int amd8111e_mdio_read(struct net_device *dev, int phy_id, int reg_num)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	unsigned int reg_val;

	amd8111e_read_phy(lp,phy_id,reg_num,&reg_val);
	return reg_val;

}

/* This is the mii register write function provided to the mii interface. */
static void amd8111e_mdio_write(struct net_device *dev,
				int phy_id, int reg_num, int val)
{
	struct amd8111e_priv *lp = netdev_priv(dev);

	amd8111e_write_phy(lp, phy_id, reg_num, val);
}

/* This function will set PHY speed. During initialization sets
 * the original speed to 100 full
 */
static void amd8111e_set_ext_phy(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	u32 bmcr,advert,tmp;

	/* Determine mii register values to set the speed */
	advert = amd8111e_mdio_read(dev, lp->ext_phy_addr, MII_ADVERTISE);
	tmp = advert & ~(ADVERTISE_ALL | ADVERTISE_100BASE4);
	switch (lp->ext_phy_option){

		default:
		case SPEED_AUTONEG: /* advertise all values */
			tmp |= ( ADVERTISE_10HALF|ADVERTISE_10FULL|
				ADVERTISE_100HALF|ADVERTISE_100FULL) ;
			break;
		case SPEED10_HALF:
			tmp |= ADVERTISE_10HALF;
			break;
		case SPEED10_FULL:
			tmp |= ADVERTISE_10FULL;
			break;
		case SPEED100_HALF:
			tmp |= ADVERTISE_100HALF;
			break;
		case SPEED100_FULL:
			tmp |= ADVERTISE_100FULL;
			break;
	}

	if(advert != tmp)
		amd8111e_mdio_write(dev, lp->ext_phy_addr, MII_ADVERTISE, tmp);
	/* Restart auto negotiation */
	bmcr = amd8111e_mdio_read(dev, lp->ext_phy_addr, MII_BMCR);
	bmcr |= (BMCR_ANENABLE | BMCR_ANRESTART);
	amd8111e_mdio_write(dev, lp->ext_phy_addr, MII_BMCR, bmcr);

}

/* This function will unmap skb->data space and will free
 * all transmit and receive skbuffs.
 */
static int amd8111e_free_skbs(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	struct sk_buff *rx_skbuff;
	int i;

	/* Freeing transmit skbs */
	for(i = 0; i < NUM_TX_BUFFERS; i++){
		if(lp->tx_skbuff[i]){
			pci_unmap_single(lp->pci_dev,lp->tx_dma_addr[i],					lp->tx_skbuff[i]->len,PCI_DMA_TODEVICE);
			dev_kfree_skb (lp->tx_skbuff[i]);
			lp->tx_skbuff[i] = NULL;
			lp->tx_dma_addr[i] = 0;
		}
	}
	/* Freeing previously allocated receive buffers */
	for (i = 0; i < NUM_RX_BUFFERS; i++){
		rx_skbuff = lp->rx_skbuff[i];
		if(rx_skbuff != NULL){
			pci_unmap_single(lp->pci_dev,lp->rx_dma_addr[i],
				  lp->rx_buff_len - 2,PCI_DMA_FROMDEVICE);
			dev_kfree_skb(lp->rx_skbuff[i]);
			lp->rx_skbuff[i] = NULL;
			lp->rx_dma_addr[i] = 0;
		}
	}

	return 0;
}

/* This will set the receive buffer length corresponding
 * to the mtu size of networkinterface.
 */
static inline void amd8111e_set_rx_buff_len(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	unsigned int mtu = dev->mtu;

	if (mtu > ETH_DATA_LEN){
		/* MTU + ethernet header + FCS
		 * + optional VLAN tag + skb reserve space 2
		 */
		lp->rx_buff_len = mtu + ETH_HLEN + 10;
		lp->options |= OPTION_JUMBO_ENABLE;
	} else{
		lp->rx_buff_len = PKT_BUFF_SZ;
		lp->options &= ~OPTION_JUMBO_ENABLE;
	}
}

/* This function will free all the previously allocated buffers,
 * determine new receive buffer length  and will allocate new receive buffers.
 * This function also allocates and initializes both the transmitter
 * and receive hardware descriptors.
 */
static int amd8111e_init_ring(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	int i;

	lp->rx_idx = lp->tx_idx = 0;
	lp->tx_complete_idx = 0;
	lp->tx_ring_idx = 0;


	if(lp->opened)
		/* Free previously allocated transmit and receive skbs */
		amd8111e_free_skbs(dev);

	else{
		 /* allocate the tx and rx descriptors */
	     	if((lp->tx_ring = pci_alloc_consistent(lp->pci_dev,
			sizeof(struct amd8111e_tx_dr)*NUM_TX_RING_DR,
			&lp->tx_ring_dma_addr)) == NULL)

			goto err_no_mem;

	     	if((lp->rx_ring = pci_alloc_consistent(lp->pci_dev,
			sizeof(struct amd8111e_rx_dr)*NUM_RX_RING_DR,
			&lp->rx_ring_dma_addr)) == NULL)

			goto err_free_tx_ring;

	}
	/* Set new receive buff size */
	amd8111e_set_rx_buff_len(dev);

	/* Allocating receive  skbs */
	for (i = 0; i < NUM_RX_BUFFERS; i++) {

		lp->rx_skbuff[i] = netdev_alloc_skb(dev, lp->rx_buff_len);
		if (!lp->rx_skbuff[i]) {
				/* Release previos allocated skbs */
				for(--i; i >= 0 ;i--)
					dev_kfree_skb(lp->rx_skbuff[i]);
				goto err_free_rx_ring;
		}
		skb_reserve(lp->rx_skbuff[i],2);
	}
        /* Initilaizing receive descriptors */
	for (i = 0; i < NUM_RX_BUFFERS; i++) {
		lp->rx_dma_addr[i] = pci_map_single(lp->pci_dev,
			lp->rx_skbuff[i]->data,lp->rx_buff_len-2, PCI_DMA_FROMDEVICE);

		lp->rx_ring[i].buff_phy_addr = cpu_to_le32(lp->rx_dma_addr[i]);
		lp->rx_ring[i].buff_count = cpu_to_le16(lp->rx_buff_len-2);
		wmb();
		lp->rx_ring[i].rx_flags = cpu_to_le16(OWN_BIT);
	}

	/* Initializing transmit descriptors */
	for (i = 0; i < NUM_TX_RING_DR; i++) {
		lp->tx_ring[i].buff_phy_addr = 0;
		lp->tx_ring[i].tx_flags = 0;
		lp->tx_ring[i].buff_count = 0;
	}

	return 0;

err_free_rx_ring:

	pci_free_consistent(lp->pci_dev,
		sizeof(struct amd8111e_rx_dr)*NUM_RX_RING_DR,lp->rx_ring,
		lp->rx_ring_dma_addr);

err_free_tx_ring:

	pci_free_consistent(lp->pci_dev,
		 sizeof(struct amd8111e_tx_dr)*NUM_TX_RING_DR,lp->tx_ring,
		 lp->tx_ring_dma_addr);

err_no_mem:
	return -ENOMEM;
}

/* This function will set the interrupt coalescing according
 * to the input arguments
 */
static int amd8111e_set_coalesce(struct net_device *dev, enum coal_mode cmod)
{
	unsigned int timeout;
	unsigned int event_count;

	struct amd8111e_priv *lp = netdev_priv(dev);
	void __iomem *mmio = lp->mmio;
	struct amd8111e_coalesce_conf *coal_conf = &lp->coal_conf;


	switch(cmod)
	{
		case RX_INTR_COAL :
			timeout = coal_conf->rx_timeout;
			event_count = coal_conf->rx_event_count;
			if( timeout > MAX_TIMEOUT ||
					event_count > MAX_EVENT_COUNT )
				return -EINVAL;

			timeout = timeout * DELAY_TIMER_CONV;
			writel(VAL0|STINTEN, mmio+INTEN0);
			writel((u32)DLY_INT_A_R0|( event_count<< 16 )|timeout,
							mmio+DLY_INT_A);
			break;

		case TX_INTR_COAL :
			timeout = coal_conf->tx_timeout;
			event_count = coal_conf->tx_event_count;
			if( timeout > MAX_TIMEOUT ||
					event_count > MAX_EVENT_COUNT )
				return -EINVAL;


			timeout = timeout * DELAY_TIMER_CONV;
			writel(VAL0|STINTEN,mmio+INTEN0);
			writel((u32)DLY_INT_B_T0|( event_count<< 16 )|timeout,
							 mmio+DLY_INT_B);
			break;

		case DISABLE_COAL:
			writel(0,mmio+STVAL);
			writel(STINTEN, mmio+INTEN0);
			writel(0, mmio +DLY_INT_B);
			writel(0, mmio+DLY_INT_A);
			break;
		 case ENABLE_COAL:
		       /* Start the timer */
			writel((u32)SOFT_TIMER_FREQ, mmio+STVAL); /*  0.5 sec */
			writel(VAL0|STINTEN, mmio+INTEN0);
			break;
		default:
			break;

   }
	return 0;

}

/* This function initializes the device registers  and starts the device. */
static int amd8111e_restart(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	void __iomem *mmio = lp->mmio;
	int i,reg_val;

	/* stop the chip */
	 writel(RUN, mmio + CMD0);

	if(amd8111e_init_ring(dev))
		return -ENOMEM;

	/* enable the port manager and set auto negotiation always */
	writel((u32) VAL1|EN_PMGR, mmio + CMD3 );
	writel((u32)XPHYANE|XPHYRST , mmio + CTRL2);

	amd8111e_set_ext_phy(dev);

	/* set control registers */
	reg_val = readl(mmio + CTRL1);
	reg_val &= ~XMTSP_MASK;
	writel( reg_val| XMTSP_128 | CACHE_ALIGN, mmio + CTRL1 );

	/* enable interrupt */
	writel( APINT5EN | APINT4EN | APINT3EN | APINT2EN | APINT1EN |
		APINT0EN | MIIPDTINTEN | MCCIINTEN | MCCINTEN | MREINTEN |
		SPNDINTEN | MPINTEN | SINTEN | STINTEN, mmio + INTEN0);

	writel(VAL3 | LCINTEN | VAL1 | TINTEN0 | VAL0 | RINTEN0, mmio + INTEN0);

	/* initialize tx and rx ring base addresses */
	writel((u32)lp->tx_ring_dma_addr,mmio + XMT_RING_BASE_ADDR0);
	writel((u32)lp->rx_ring_dma_addr,mmio+ RCV_RING_BASE_ADDR0);

	writew((u32)NUM_TX_RING_DR, mmio + XMT_RING_LEN0);
	writew((u16)NUM_RX_RING_DR, mmio + RCV_RING_LEN0);

	/* set default IPG to 96 */
	writew((u32)DEFAULT_IPG,mmio+IPG);
	writew((u32)(DEFAULT_IPG-IFS1_DELTA), mmio + IFS1);

	if(lp->options & OPTION_JUMBO_ENABLE){
		writel((u32)VAL2|JUMBO, mmio + CMD3);
		/* Reset REX_UFLO */
		writel( REX_UFLO, mmio + CMD2);
		/* Should not set REX_UFLO for jumbo frames */
		writel( VAL0 | APAD_XMT|REX_RTRY , mmio + CMD2);
	}else{
		writel( VAL0 | APAD_XMT | REX_RTRY|REX_UFLO, mmio + CMD2);
		writel((u32)JUMBO, mmio + CMD3);
	}

#if AMD8111E_VLAN_TAG_USED
	writel((u32) VAL2|VSIZE|VL_TAG_DEL, mmio + CMD3);
#endif
	writel( VAL0 | APAD_XMT | REX_RTRY, mmio + CMD2 );

	/* Setting the MAC address to the device */
	for (i = 0; i < ETH_ALEN; i++)
		writeb( dev->dev_addr[i], mmio + PADR + i );

	/* Enable interrupt coalesce */
	if(lp->options & OPTION_INTR_COAL_ENABLE){
		netdev_info(dev, "Interrupt Coalescing Enabled.\n");
		amd8111e_set_coalesce(dev,ENABLE_COAL);
	}

	/* set RUN bit to start the chip */
	writel(VAL2 | RDMD0, mmio + CMD0);
	writel(VAL0 | INTREN | RUN, mmio + CMD0);

	/* To avoid PCI posting bug */
	readl(mmio+CMD0);
	return 0;
}

/* This function clears necessary the device registers. */
static void amd8111e_init_hw_default(struct amd8111e_priv *lp)
{
	unsigned int reg_val;
	unsigned int logic_filter[2] ={0,};
	void __iomem *mmio = lp->mmio;


        /* stop the chip */
	writel(RUN, mmio + CMD0);

	/* AUTOPOLL0 Register *//*TBD default value is 8100 in FPS */
	writew( 0x8100 | lp->ext_phy_addr, mmio + AUTOPOLL0);

	/* Clear RCV_RING_BASE_ADDR */
	writel(0, mmio + RCV_RING_BASE_ADDR0);

	/* Clear XMT_RING_BASE_ADDR */
	writel(0, mmio + XMT_RING_BASE_ADDR0);
	writel(0, mmio + XMT_RING_BASE_ADDR1);
	writel(0, mmio + XMT_RING_BASE_ADDR2);
	writel(0, mmio + XMT_RING_BASE_ADDR3);

	/* Clear CMD0  */
	writel(CMD0_CLEAR,mmio + CMD0);

	/* Clear CMD2 */
	writel(CMD2_CLEAR, mmio +CMD2);

	/* Clear CMD7 */
	writel(CMD7_CLEAR , mmio + CMD7);

	/* Clear DLY_INT_A and DLY_INT_B */
	writel(0x0, mmio + DLY_INT_A);
	writel(0x0, mmio + DLY_INT_B);

	/* Clear FLOW_CONTROL */
	writel(0x0, mmio + FLOW_CONTROL);

	/* Clear INT0  write 1 to clear register */
	reg_val = readl(mmio + INT0);
	writel(reg_val, mmio + INT0);

	/* Clear STVAL */
	writel(0x0, mmio + STVAL);

	/* Clear INTEN0 */
	writel( INTEN0_CLEAR, mmio + INTEN0);

	/* Clear LADRF */
	writel(0x0 , mmio + LADRF);

	/* Set SRAM_SIZE & SRAM_BOUNDARY registers  */
	writel( 0x80010,mmio + SRAM_SIZE);

	/* Clear RCV_RING0_LEN */
	writel(0x0, mmio +  RCV_RING_LEN0);

	/* Clear XMT_RING0/1/2/3_LEN */
	writel(0x0, mmio +  XMT_RING_LEN0);
	writel(0x0, mmio +  XMT_RING_LEN1);
	writel(0x0, mmio +  XMT_RING_LEN2);
	writel(0x0, mmio +  XMT_RING_LEN3);

	/* Clear XMT_RING_LIMIT */
	writel(0x0, mmio + XMT_RING_LIMIT);

	/* Clear MIB */
	writew(MIB_CLEAR, mmio + MIB_ADDR);

	/* Clear LARF */
	amd8111e_writeq(*(u64 *)logic_filter, mmio + LADRF);

	/* SRAM_SIZE register */
	reg_val = readl(mmio + SRAM_SIZE);

	if(lp->options & OPTION_JUMBO_ENABLE)
		writel( VAL2|JUMBO, mmio + CMD3);
#if AMD8111E_VLAN_TAG_USED
	writel(VAL2|VSIZE|VL_TAG_DEL, mmio + CMD3 );
#endif
	/* Set default value to CTRL1 Register */
	writel(CTRL1_DEFAULT, mmio + CTRL1);

	/* To avoid PCI posting bug */
	readl(mmio + CMD2);

}

/* This function disables the interrupt and clears all the pending
 * interrupts in INT0
 */
static void amd8111e_disable_interrupt(struct amd8111e_priv *lp)
{
	u32 intr0;

	/* Disable interrupt */
	writel(INTREN, lp->mmio + CMD0);

	/* Clear INT0 */
	intr0 = readl(lp->mmio + INT0);
	writel(intr0, lp->mmio + INT0);

	/* To avoid PCI posting bug */
	readl(lp->mmio + INT0);

}

/* This function stops the chip. */
static void amd8111e_stop_chip(struct amd8111e_priv *lp)
{
	writel(RUN, lp->mmio + CMD0);

	/* To avoid PCI posting bug */
	readl(lp->mmio + CMD0);
}

/* This function frees the  transmiter and receiver descriptor rings. */
static void amd8111e_free_ring(struct amd8111e_priv *lp)
{
	/* Free transmit and receive descriptor rings */
	if(lp->rx_ring){
		pci_free_consistent(lp->pci_dev,
			sizeof(struct amd8111e_rx_dr)*NUM_RX_RING_DR,
			lp->rx_ring, lp->rx_ring_dma_addr);
		lp->rx_ring = NULL;
	}

	if(lp->tx_ring){
		pci_free_consistent(lp->pci_dev,
			sizeof(struct amd8111e_tx_dr)*NUM_TX_RING_DR,
			lp->tx_ring, lp->tx_ring_dma_addr);

		lp->tx_ring = NULL;
	}

}

/* This function will free all the transmit skbs that are actually
 * transmitted by the device. It will check the ownership of the
 * skb before freeing the skb.
 */
static int amd8111e_tx(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	int tx_index = lp->tx_complete_idx & TX_RING_DR_MOD_MASK;
	int status;
	/* Complete all the transmit packet */
	while (lp->tx_complete_idx != lp->tx_idx){
		tx_index =  lp->tx_complete_idx & TX_RING_DR_MOD_MASK;
		status = le16_to_cpu(lp->tx_ring[tx_index].tx_flags);

		if(status & OWN_BIT)
			break;	/* It still hasn't been Txed */

		lp->tx_ring[tx_index].buff_phy_addr = 0;

		/* We must free the original skb */
		if (lp->tx_skbuff[tx_index]) {
			pci_unmap_single(lp->pci_dev, lp->tx_dma_addr[tx_index],
				  	lp->tx_skbuff[tx_index]->len,
					PCI_DMA_TODEVICE);
			dev_kfree_skb_irq (lp->tx_skbuff[tx_index]);
			lp->tx_skbuff[tx_index] = NULL;
			lp->tx_dma_addr[tx_index] = 0;
		}
		lp->tx_complete_idx++;
		/*COAL update tx coalescing parameters */
		lp->coal_conf.tx_packets++;
		lp->coal_conf.tx_bytes +=
			le16_to_cpu(lp->tx_ring[tx_index].buff_count);

		if (netif_queue_stopped(dev) &&
			lp->tx_complete_idx > lp->tx_idx - NUM_TX_BUFFERS +2){
			/* The ring is no longer full, clear tbusy. */
			/* lp->tx_full = 0; */
			netif_wake_queue (dev);
		}
	}
	return 0;
}

/* This function handles the driver receive operation in polling mode */
static int amd8111e_rx_poll(struct napi_struct *napi, int budget)
{
	struct amd8111e_priv *lp = container_of(napi, struct amd8111e_priv, napi);
	struct net_device *dev = lp->amd8111e_net_dev;
	int rx_index = lp->rx_idx & RX_RING_DR_MOD_MASK;
	void __iomem *mmio = lp->mmio;
	struct sk_buff *skb,*new_skb;
	int min_pkt_len, status;
	unsigned int intr0;
	int num_rx_pkt = 0;
	short pkt_len;
#if AMD8111E_VLAN_TAG_USED
	short vtag;
#endif
	int rx_pkt_limit = budget;
	unsigned long flags;

	if (rx_pkt_limit <= 0)
		goto rx_not_empty;

	do{
		/* process receive packets until we use the quota.
		 * If we own the next entry, it's a new packet. Send it up.
		 */
		while(1) {
			status = le16_to_cpu(lp->rx_ring[rx_index].rx_flags);
			if (status & OWN_BIT)
				break;

			/* There is a tricky error noted by John Murphy,
			 * <murf@perftech.com> to Russ Nelson: Even with
			 * full-sized * buffers it's possible for a
			 * jabber packet to use two buffers, with only
			 * the last correctly noting the error.
			 */
			if(status & ERR_BIT) {
				/* reseting flags */
				lp->rx_ring[rx_index].rx_flags &= RESET_RX_FLAGS;
				goto err_next_pkt;
			}
			/* check for STP and ENP */
			if(!((status & STP_BIT) && (status & ENP_BIT))){
				/* reseting flags */
				lp->rx_ring[rx_index].rx_flags &= RESET_RX_FLAGS;
				goto err_next_pkt;
			}
			pkt_len = le16_to_cpu(lp->rx_ring[rx_index].msg_count) - 4;

#if AMD8111E_VLAN_TAG_USED
			vtag = status & TT_MASK;
			/*MAC will strip vlan tag*/
			if (vtag != 0)
				min_pkt_len =MIN_PKT_LEN - 4;
			else
#endif
				min_pkt_len =MIN_PKT_LEN;

			if (pkt_len < min_pkt_len) {
				lp->rx_ring[rx_index].rx_flags &= RESET_RX_FLAGS;
				lp->drv_rx_errors++;
				goto err_next_pkt;
			}
			if(--rx_pkt_limit < 0)
				goto rx_not_empty;
			new_skb = netdev_alloc_skb(dev, lp->rx_buff_len);
			if (!new_skb) {
				/* if allocation fail,
				 * ignore that pkt and go to next one
				 */
				lp->rx_ring[rx_index].rx_flags &= RESET_RX_FLAGS;
				lp->drv_rx_errors++;
				goto err_next_pkt;
			}

			skb_reserve(new_skb, 2);
			skb = lp->rx_skbuff[rx_index];
			pci_unmap_single(lp->pci_dev,lp->rx_dma_addr[rx_index],
					 lp->rx_buff_len-2, PCI_DMA_FROMDEVICE);
			skb_put(skb, pkt_len);
			lp->rx_skbuff[rx_index] = new_skb;
			lp->rx_dma_addr[rx_index] = pci_map_single(lp->pci_dev,
								   new_skb->data,
								   lp->rx_buff_len-2,
								   PCI_DMA_FROMDEVICE);

			skb->protocol = eth_type_trans(skb, dev);

#if AMD8111E_VLAN_TAG_USED
			if (vtag == TT_VLAN_TAGGED){
				u16 vlan_tag = le16_to_cpu(lp->rx_ring[rx_index].tag_ctrl_info);
				__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), vlan_tag);
			}
#endif
			netif_receive_skb(skb);
			/*COAL update rx coalescing parameters*/
			lp->coal_conf.rx_packets++;
			lp->coal_conf.rx_bytes += pkt_len;
			num_rx_pkt++;

		err_next_pkt:
			lp->rx_ring[rx_index].buff_phy_addr
				= cpu_to_le32(lp->rx_dma_addr[rx_index]);
			lp->rx_ring[rx_index].buff_count =
				cpu_to_le16(lp->rx_buff_len-2);
			wmb();
			lp->rx_ring[rx_index].rx_flags |= cpu_to_le16(OWN_BIT);
			rx_index = (++lp->rx_idx) & RX_RING_DR_MOD_MASK;
		}
		/* Check the interrupt status register for more packets in the
		 * mean time. Process them since we have not used up our quota.
		 */
		intr0 = readl(mmio + INT0);
		/*Ack receive packets */
		writel(intr0 & RINT0,mmio + INT0);

	} while(intr0 & RINT0);

	if (rx_pkt_limit > 0) {
		/* Receive descriptor is empty now */
		spin_lock_irqsave(&lp->lock, flags);
		__napi_complete(napi);
		writel(VAL0|RINTEN0, mmio + INTEN0);
		writel(VAL2 | RDMD0, mmio + CMD0);
		spin_unlock_irqrestore(&lp->lock, flags);
	}

rx_not_empty:
	return num_rx_pkt;
}

/* This function will indicate the link status to the kernel. */
static int amd8111e_link_change(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	int status0,speed;

	/* read the link change */
     	status0 = readl(lp->mmio + STAT0);

	if(status0 & LINK_STATS){
		if(status0 & AUTONEG_COMPLETE)
			lp->link_config.autoneg = AUTONEG_ENABLE;
		else
			lp->link_config.autoneg = AUTONEG_DISABLE;

		if(status0 & FULL_DPLX)
			lp->link_config.duplex = DUPLEX_FULL;
		else
			lp->link_config.duplex = DUPLEX_HALF;
		speed = (status0 & SPEED_MASK) >> 7;
		if(speed == PHY_SPEED_10)
			lp->link_config.speed = SPEED_10;
		else if(speed == PHY_SPEED_100)
			lp->link_config.speed = SPEED_100;

		netdev_info(dev, "Link is Up. Speed is %s Mbps %s Duplex\n",
			    (lp->link_config.speed == SPEED_100) ?
							"100" : "10",
			    (lp->link_config.duplex == DUPLEX_FULL) ?
							"Full" : "Half");

		netif_carrier_on(dev);
	}
	else{
		lp->link_config.speed = SPEED_INVALID;
		lp->link_config.duplex = DUPLEX_INVALID;
		lp->link_config.autoneg = AUTONEG_INVALID;
		netdev_info(dev, "Link is Down.\n");
		netif_carrier_off(dev);
	}

	return 0;
}

/* This function reads the mib counters. */
static int amd8111e_read_mib(void __iomem *mmio, u8 MIB_COUNTER)
{
	unsigned int  status;
	unsigned  int data;
	unsigned int repeat = REPEAT_CNT;

	writew( MIB_RD_CMD | MIB_COUNTER, mmio + MIB_ADDR);
	do {
		status = readw(mmio + MIB_ADDR);
		udelay(2);	/* controller takes MAX 2 us to get mib data */
	}
	while (--repeat && (status & MIB_CMD_ACTIVE));

	data = readl(mmio + MIB_DATA);
	return data;
}

/* This function reads the mib registers and returns the hardware statistics.
 * It updates previous internal driver statistics with new values.
 */
static struct net_device_stats *amd8111e_get_stats(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	void __iomem *mmio = lp->mmio;
	unsigned long flags;
	struct net_device_stats *new_stats = &dev->stats;

	if (!lp->opened)
		return new_stats;
	spin_lock_irqsave (&lp->lock, flags);

	/* stats.rx_packets */
	new_stats->rx_packets = amd8111e_read_mib(mmio, rcv_broadcast_pkts)+
				amd8111e_read_mib(mmio, rcv_multicast_pkts)+
				amd8111e_read_mib(mmio, rcv_unicast_pkts);

	/* stats.tx_packets */
	new_stats->tx_packets = amd8111e_read_mib(mmio, xmt_packets);

	/*stats.rx_bytes */
	new_stats->rx_bytes = amd8111e_read_mib(mmio, rcv_octets);

	/* stats.tx_bytes */
	new_stats->tx_bytes = amd8111e_read_mib(mmio, xmt_octets);

	/* stats.rx_errors */
	/* hw errors + errors driver reported */
	new_stats->rx_errors = amd8111e_read_mib(mmio, rcv_undersize_pkts)+
				amd8111e_read_mib(mmio, rcv_fragments)+
				amd8111e_read_mib(mmio, rcv_jabbers)+
				amd8111e_read_mib(mmio, rcv_alignment_errors)+
				amd8111e_read_mib(mmio, rcv_fcs_errors)+
				amd8111e_read_mib(mmio, rcv_miss_pkts)+
				lp->drv_rx_errors;

	/* stats.tx_errors */
	new_stats->tx_errors = amd8111e_read_mib(mmio, xmt_underrun_pkts);

	/* stats.rx_dropped*/
	new_stats->rx_dropped = amd8111e_read_mib(mmio, rcv_miss_pkts);

	/* stats.tx_dropped*/
	new_stats->tx_dropped = amd8111e_read_mib(mmio,  xmt_underrun_pkts);

	/* stats.multicast*/
	new_stats->multicast = amd8111e_read_mib(mmio, rcv_multicast_pkts);

	/* stats.collisions*/
	new_stats->collisions = amd8111e_read_mib(mmio, xmt_collisions);

	/* stats.rx_length_errors*/
	new_stats->rx_length_errors =
		amd8111e_read_mib(mmio, rcv_undersize_pkts)+
		amd8111e_read_mib(mmio, rcv_oversize_pkts);

	/* stats.rx_over_errors*/
	new_stats->rx_over_errors = amd8111e_read_mib(mmio, rcv_miss_pkts);

	/* stats.rx_crc_errors*/
	new_stats->rx_crc_errors = amd8111e_read_mib(mmio, rcv_fcs_errors);

	/* stats.rx_frame_errors*/
	new_stats->rx_frame_errors =
		amd8111e_read_mib(mmio, rcv_alignment_errors);

	/* stats.rx_fifo_errors */
	new_stats->rx_fifo_errors = amd8111e_read_mib(mmio, rcv_miss_pkts);

	/* stats.rx_missed_errors */
	new_stats->rx_missed_errors = amd8111e_read_mib(mmio, rcv_miss_pkts);

	/* stats.tx_aborted_errors*/
	new_stats->tx_aborted_errors =
		amd8111e_read_mib(mmio, xmt_excessive_collision);

	/* stats.tx_carrier_errors*/
	new_stats->tx_carrier_errors =
		amd8111e_read_mib(mmio, xmt_loss_carrier);

	/* stats.tx_fifo_errors*/
	new_stats->tx_fifo_errors = amd8111e_read_mib(mmio, xmt_underrun_pkts);

	/* stats.tx_window_errors*/
	new_stats->tx_window_errors =
		amd8111e_read_mib(mmio, xmt_late_collision);

	/* Reset the mibs for collecting new statistics */
	/* writew(MIB_CLEAR, mmio + MIB_ADDR);*/

	spin_unlock_irqrestore (&lp->lock, flags);

	return new_stats;
}

/* This function recalculate the interrupt coalescing  mode on every interrupt
 * according to the datarate and the packet rate.
 */
static int amd8111e_calc_coalesce(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	struct amd8111e_coalesce_conf *coal_conf = &lp->coal_conf;
	int tx_pkt_rate;
	int rx_pkt_rate;
	int tx_data_rate;
	int rx_data_rate;
	int rx_pkt_size;
	int tx_pkt_size;

	tx_pkt_rate = coal_conf->tx_packets - coal_conf->tx_prev_packets;
	coal_conf->tx_prev_packets =  coal_conf->tx_packets;

	tx_data_rate = coal_conf->tx_bytes - coal_conf->tx_prev_bytes;
	coal_conf->tx_prev_bytes =  coal_conf->tx_bytes;

	rx_pkt_rate = coal_conf->rx_packets - coal_conf->rx_prev_packets;
	coal_conf->rx_prev_packets =  coal_conf->rx_packets;

	rx_data_rate = coal_conf->rx_bytes - coal_conf->rx_prev_bytes;
	coal_conf->rx_prev_bytes =  coal_conf->rx_bytes;

	if(rx_pkt_rate < 800){
		if(coal_conf->rx_coal_type != NO_COALESCE){

			coal_conf->rx_timeout = 0x0;
			coal_conf->rx_event_count = 0;
			amd8111e_set_coalesce(dev,RX_INTR_COAL);
			coal_conf->rx_coal_type = NO_COALESCE;
		}
	}
	else{

		rx_pkt_size = rx_data_rate/rx_pkt_rate;
		if (rx_pkt_size < 128){
			if(coal_conf->rx_coal_type != NO_COALESCE){

				coal_conf->rx_timeout = 0;
				coal_conf->rx_event_count = 0;
				amd8111e_set_coalesce(dev,RX_INTR_COAL);
				coal_conf->rx_coal_type = NO_COALESCE;
			}

		}
		else if ( (rx_pkt_size >= 128) && (rx_pkt_size < 512) ){

			if(coal_conf->rx_coal_type !=  LOW_COALESCE){
				coal_conf->rx_timeout = 1;
				coal_conf->rx_event_count = 4;
				amd8111e_set_coalesce(dev,RX_INTR_COAL);
				coal_conf->rx_coal_type = LOW_COALESCE;
			}
		}
		else if ((rx_pkt_size >= 512) && (rx_pkt_size < 1024)){

			if(coal_conf->rx_coal_type !=  MEDIUM_COALESCE){
				coal_conf->rx_timeout = 1;
				coal_conf->rx_event_count = 4;
				amd8111e_set_coalesce(dev,RX_INTR_COAL);
				coal_conf->rx_coal_type = MEDIUM_COALESCE;
			}

		}
		else if(rx_pkt_size >= 1024){
			if(coal_conf->rx_coal_type !=  HIGH_COALESCE){
				coal_conf->rx_timeout = 2;
				coal_conf->rx_event_count = 3;
				amd8111e_set_coalesce(dev,RX_INTR_COAL);
				coal_conf->rx_coal_type = HIGH_COALESCE;
			}
		}
	}
    	/* NOW FOR TX INTR COALESC */
	if(tx_pkt_rate < 800){
		if(coal_conf->tx_coal_type != NO_COALESCE){

			coal_conf->tx_timeout = 0x0;
			coal_conf->tx_event_count = 0;
			amd8111e_set_coalesce(dev,TX_INTR_COAL);
			coal_conf->tx_coal_type = NO_COALESCE;
		}
	}
	else{

		tx_pkt_size = tx_data_rate/tx_pkt_rate;
		if (tx_pkt_size < 128){

			if(coal_conf->tx_coal_type != NO_COALESCE){

				coal_conf->tx_timeout = 0;
				coal_conf->tx_event_count = 0;
				amd8111e_set_coalesce(dev,TX_INTR_COAL);
				coal_conf->tx_coal_type = NO_COALESCE;
			}

		}
		else if ( (tx_pkt_size >= 128) && (tx_pkt_size < 512) ){

			if(coal_conf->tx_coal_type !=  LOW_COALESCE){
				coal_conf->tx_timeout = 1;
				coal_conf->tx_event_count = 2;
				amd8111e_set_coalesce(dev,TX_INTR_COAL);
				coal_conf->tx_coal_type = LOW_COALESCE;

			}
		}
		else if ((tx_pkt_size >= 512) && (tx_pkt_size < 1024)){

			if(coal_conf->tx_coal_type !=  MEDIUM_COALESCE){
				coal_conf->tx_timeout = 2;
				coal_conf->tx_event_count = 5;
				amd8111e_set_coalesce(dev,TX_INTR_COAL);
				coal_conf->tx_coal_type = MEDIUM_COALESCE;
			}

		}
		else if(tx_pkt_size >= 1024){
			if (tx_pkt_size >= 1024){
				if(coal_conf->tx_coal_type !=  HIGH_COALESCE){
					coal_conf->tx_timeout = 4;
					coal_conf->tx_event_count = 8;
					amd8111e_set_coalesce(dev,TX_INTR_COAL);
					coal_conf->tx_coal_type = HIGH_COALESCE;
				}
			}
		}
	}
	return 0;

}

/* This is device interrupt function. It handles transmit,
 * receive,link change and hardware timer interrupts.
 */
static irqreturn_t amd8111e_interrupt(int irq, void *dev_id)
{

	struct net_device *dev = (struct net_device *)dev_id;
	struct amd8111e_priv *lp = netdev_priv(dev);
	void __iomem *mmio = lp->mmio;
	unsigned int intr0, intren0;
	unsigned int handled = 1;

	if(unlikely(dev == NULL))
		return IRQ_NONE;

	spin_lock(&lp->lock);

	/* disabling interrupt */
	writel(INTREN, mmio + CMD0);

	/* Read interrupt status */
	intr0 = readl(mmio + INT0);
	intren0 = readl(mmio + INTEN0);

	/* Process all the INT event until INTR bit is clear. */

	if (!(intr0 & INTR)){
		handled = 0;
		goto err_no_interrupt;
	}

	/* Current driver processes 4 interrupts : RINT,TINT,LCINT,STINT */
	writel(intr0, mmio + INT0);

	/* Check if Receive Interrupt has occurred. */
	if (intr0 & RINT0) {
		if (napi_schedule_prep(&lp->napi)) {
			/* Disable receive interupts */
			writel(RINTEN0, mmio + INTEN0);
			/* Schedule a polling routine */
			__napi_schedule(&lp->napi);
		} else if (intren0 & RINTEN0) {
			netdev_dbg(dev, "************Driver bug! interrupt while in poll\n");
			/* Fix by disable receive interrupts */
			writel(RINTEN0, mmio + INTEN0);
		}
	}

	/* Check if  Transmit Interrupt has occurred. */
	if (intr0 & TINT0)
		amd8111e_tx(dev);

	/* Check if  Link Change Interrupt has occurred. */
	if (intr0 & LCINT)
		amd8111e_link_change(dev);

	/* Check if Hardware Timer Interrupt has occurred. */
	if (intr0 & STINT)
		amd8111e_calc_coalesce(dev);

err_no_interrupt:
	writel( VAL0 | INTREN,mmio + CMD0);

	spin_unlock(&lp->lock);

	return IRQ_RETVAL(handled);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void amd8111e_poll(struct net_device *dev)
{
	unsigned long flags;
	local_irq_save(flags);
	amd8111e_interrupt(0, dev);
	local_irq_restore(flags);
}
#endif


/* This function closes the network interface and updates
 * the statistics so that most recent statistics will be
 * available after the interface is down.
 */
static int amd8111e_close(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	netif_stop_queue(dev);

	napi_disable(&lp->napi);

	spin_lock_irq(&lp->lock);

	amd8111e_disable_interrupt(lp);
	amd8111e_stop_chip(lp);

	/* Free transmit and receive skbs */
	amd8111e_free_skbs(lp->amd8111e_net_dev);

	netif_carrier_off(lp->amd8111e_net_dev);

	/* Delete ipg timer */
	if(lp->options & OPTION_DYN_IPG_ENABLE)
		del_timer_sync(&lp->ipg_data.ipg_timer);

	spin_unlock_irq(&lp->lock);
	free_irq(dev->irq, dev);
	amd8111e_free_ring(lp);

	/* Update the statistics before closing */
	amd8111e_get_stats(dev);
	lp->opened = 0;
	return 0;
}

/* This function opens new interface.It requests irq for the device,
 * initializes the device,buffers and descriptors, and starts the device.
 */
static int amd8111e_open(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);

	if(dev->irq ==0 || request_irq(dev->irq, amd8111e_interrupt, IRQF_SHARED,
					 dev->name, dev))
		return -EAGAIN;

	napi_enable(&lp->napi);

	spin_lock_irq(&lp->lock);

	amd8111e_init_hw_default(lp);

	if(amd8111e_restart(dev)){
		spin_unlock_irq(&lp->lock);
		napi_disable(&lp->napi);
		if (dev->irq)
			free_irq(dev->irq, dev);
		return -ENOMEM;
	}
	/* Start ipg timer */
	if(lp->options & OPTION_DYN_IPG_ENABLE){
		add_timer(&lp->ipg_data.ipg_timer);
		netdev_info(dev, "Dynamic IPG Enabled\n");
	}

	lp->opened = 1;

	spin_unlock_irq(&lp->lock);

	netif_start_queue(dev);

	return 0;
}

/* This function checks if there is any transmit  descriptors
 * available to queue more packet.
 */
static int amd8111e_tx_queue_avail(struct amd8111e_priv *lp)
{
	int tx_index = lp->tx_idx & TX_BUFF_MOD_MASK;
	if (lp->tx_skbuff[tx_index])
		return -1;
	else
		return 0;

}

/* This function will queue the transmit packets to the
 * descriptors and will trigger the send operation. It also
 * initializes the transmit descriptors with buffer physical address,
 * byte count, ownership to hardware etc.
 */
static netdev_tx_t amd8111e_start_xmit(struct sk_buff *skb,
				       struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	int tx_index;
	unsigned long flags;

	spin_lock_irqsave(&lp->lock, flags);

	tx_index = lp->tx_idx & TX_RING_DR_MOD_MASK;

	lp->tx_ring[tx_index].buff_count = cpu_to_le16(skb->len);

	lp->tx_skbuff[tx_index] = skb;
	lp->tx_ring[tx_index].tx_flags = 0;

#if AMD8111E_VLAN_TAG_USED
	if (vlan_tx_tag_present(skb)) {
		lp->tx_ring[tx_index].tag_ctrl_cmd |=
				cpu_to_le16(TCC_VLAN_INSERT);
		lp->tx_ring[tx_index].tag_ctrl_info =
				cpu_to_le16(vlan_tx_tag_get(skb));

	}
#endif
	lp->tx_dma_addr[tx_index] =
	    pci_map_single(lp->pci_dev, skb->data, skb->len, PCI_DMA_TODEVICE);
	lp->tx_ring[tx_index].buff_phy_addr =
	    cpu_to_le32(lp->tx_dma_addr[tx_index]);

	/*  Set FCS and LTINT bits */
	wmb();
	lp->tx_ring[tx_index].tx_flags |=
	    cpu_to_le16(OWN_BIT | STP_BIT | ENP_BIT|ADD_FCS_BIT|LTINT_BIT);

	lp->tx_idx++;

	/* Trigger an immediate send poll. */
	writel( VAL1 | TDMD0, lp->mmio + CMD0);
	writel( VAL2 | RDMD0,lp->mmio + CMD0);

	if(amd8111e_tx_queue_avail(lp) < 0){
		netif_stop_queue(dev);
	}
	spin_unlock_irqrestore(&lp->lock, flags);
	return NETDEV_TX_OK;
}
/* This function returns all the memory mapped registers of the device. */
static void amd8111e_read_regs(struct amd8111e_priv *lp, u32 *buf)
{
	void __iomem *mmio = lp->mmio;
	/* Read only necessary registers */
	buf[0] = readl(mmio + XMT_RING_BASE_ADDR0);
	buf[1] = readl(mmio + XMT_RING_LEN0);
	buf[2] = readl(mmio + RCV_RING_BASE_ADDR0);
	buf[3] = readl(mmio + RCV_RING_LEN0);
	buf[4] = readl(mmio + CMD0);
	buf[5] = readl(mmio + CMD2);
	buf[6] = readl(mmio + CMD3);
	buf[7] = readl(mmio + CMD7);
	buf[8] = readl(mmio + INT0);
	buf[9] = readl(mmio + INTEN0);
	buf[10] = readl(mmio + LADRF);
	buf[11] = readl(mmio + LADRF+4);
	buf[12] = readl(mmio + STAT0);
}


/* This function sets promiscuos mode, all-multi mode or the multicast address
 * list to the device.
 */
static void amd8111e_set_multicast_list(struct net_device *dev)
{
	struct netdev_hw_addr *ha;
	struct amd8111e_priv *lp = netdev_priv(dev);
	u32 mc_filter[2] ;
	int bit_num;

	if(dev->flags & IFF_PROMISC){
		writel( VAL2 | PROM, lp->mmio + CMD2);
		return;
	}
	else
		writel( PROM, lp->mmio + CMD2);
	if (dev->flags & IFF_ALLMULTI ||
	    netdev_mc_count(dev) > MAX_FILTER_SIZE) {
		/* get all multicast packet */
		mc_filter[1] = mc_filter[0] = 0xffffffff;
		lp->options |= OPTION_MULTICAST_ENABLE;
		amd8111e_writeq(*(u64 *)mc_filter, lp->mmio + LADRF);
		return;
	}
	if (netdev_mc_empty(dev)) {
		/* get only own packets */
		mc_filter[1] = mc_filter[0] = 0;
		lp->options &= ~OPTION_MULTICAST_ENABLE;
		amd8111e_writeq(*(u64 *)mc_filter, lp->mmio + LADRF);
		/* disable promiscuous mode */
		writel(PROM, lp->mmio + CMD2);
		return;
	}
	/* load all the multicast addresses in the logic filter */
	lp->options |= OPTION_MULTICAST_ENABLE;
	mc_filter[1] = mc_filter[0] = 0;
	netdev_for_each_mc_addr(ha, dev) {
		bit_num = (ether_crc_le(ETH_ALEN, ha->addr) >> 26) & 0x3f;
		mc_filter[bit_num >> 5] |= 1 << (bit_num & 31);
	}
	amd8111e_writeq(*(u64 *)mc_filter, lp->mmio + LADRF);

	/* To eliminate PCI posting bug */
	readl(lp->mmio + CMD2);

}

static void amd8111e_get_drvinfo(struct net_device *dev,
				 struct ethtool_drvinfo *info)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	struct pci_dev *pci_dev = lp->pci_dev;
	strlcpy(info->driver, MODULE_NAME, sizeof(info->driver));
	strlcpy(info->version, MODULE_VERS, sizeof(info->version));
	snprintf(info->fw_version, sizeof(info->fw_version),
		"%u", chip_version);
	strlcpy(info->bus_info, pci_name(pci_dev), sizeof(info->bus_info));
}

static int amd8111e_get_regs_len(struct net_device *dev)
{
	return AMD8111E_REG_DUMP_LEN;
}

static void amd8111e_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *buf)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	regs->version = 0;
	amd8111e_read_regs(lp, buf);
}

static int amd8111e_get_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	spin_lock_irq(&lp->lock);
	mii_ethtool_gset(&lp->mii_if, ecmd);
	spin_unlock_irq(&lp->lock);
	return 0;
}

static int amd8111e_set_settings(struct net_device *dev, struct ethtool_cmd *ecmd)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	int res;
	spin_lock_irq(&lp->lock);
	res = mii_ethtool_sset(&lp->mii_if, ecmd);
	spin_unlock_irq(&lp->lock);
	return res;
}

static int amd8111e_nway_reset(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	return mii_nway_restart(&lp->mii_if);
}

static u32 amd8111e_get_link(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	return mii_link_ok(&lp->mii_if);
}

static void amd8111e_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol_info)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	wol_info->supported = WAKE_MAGIC|WAKE_PHY;
	if (lp->options & OPTION_WOL_ENABLE)
		wol_info->wolopts = WAKE_MAGIC;
}

static int amd8111e_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol_info)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	if (wol_info->wolopts & ~(WAKE_MAGIC|WAKE_PHY))
		return -EINVAL;
	spin_lock_irq(&lp->lock);
	if (wol_info->wolopts & WAKE_MAGIC)
		lp->options |=
			(OPTION_WOL_ENABLE | OPTION_WAKE_MAGIC_ENABLE);
	else if(wol_info->wolopts & WAKE_PHY)
		lp->options |=
			(OPTION_WOL_ENABLE | OPTION_WAKE_PHY_ENABLE);
	else
		lp->options &= ~OPTION_WOL_ENABLE;
	spin_unlock_irq(&lp->lock);
	return 0;
}

static const struct ethtool_ops ops = {
	.get_drvinfo = amd8111e_get_drvinfo,
	.get_regs_len = amd8111e_get_regs_len,
	.get_regs = amd8111e_get_regs,
	.get_settings = amd8111e_get_settings,
	.set_settings = amd8111e_set_settings,
	.nway_reset = amd8111e_nway_reset,
	.get_link = amd8111e_get_link,
	.get_wol = amd8111e_get_wol,
	.set_wol = amd8111e_set_wol,
};

/* This function handles all the  ethtool ioctls. It gives driver info,
 * gets/sets driver speed, gets memory mapped register values, forces
 * auto negotiation, sets/gets WOL options for ethtool application.
 */
static int amd8111e_ioctl(struct net_device *dev , struct ifreq *ifr, int cmd)
{
	struct mii_ioctl_data *data = if_mii(ifr);
	struct amd8111e_priv *lp = netdev_priv(dev);
	int err;
	u32 mii_regval;

	switch(cmd) {
	case SIOCGMIIPHY:
		data->phy_id = lp->ext_phy_addr;

	/* fallthru */
	case SIOCGMIIREG:

		spin_lock_irq(&lp->lock);
		err = amd8111e_read_phy(lp, data->phy_id,
			data->reg_num & PHY_REG_ADDR_MASK, &mii_regval);
		spin_unlock_irq(&lp->lock);

		data->val_out = mii_regval;
		return err;

	case SIOCSMIIREG:

		spin_lock_irq(&lp->lock);
		err = amd8111e_write_phy(lp, data->phy_id,
			data->reg_num & PHY_REG_ADDR_MASK, data->val_in);
		spin_unlock_irq(&lp->lock);

		return err;

	default:
		/* do nothing */
		break;
	}
	return -EOPNOTSUPP;
}
static int amd8111e_set_mac_address(struct net_device *dev, void *p)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	int i;
	struct sockaddr *addr = p;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	spin_lock_irq(&lp->lock);
	/* Setting the MAC address to the device */
	for (i = 0; i < ETH_ALEN; i++)
		writeb( dev->dev_addr[i], lp->mmio + PADR + i );

	spin_unlock_irq(&lp->lock);

	return 0;
}

/* This function changes the mtu of the device. It restarts the device  to
 * initialize the descriptor with new receive buffers.
 */
static int amd8111e_change_mtu(struct net_device *dev, int new_mtu)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	int err;

	if ((new_mtu < AMD8111E_MIN_MTU) || (new_mtu > AMD8111E_MAX_MTU))
		return -EINVAL;

	if (!netif_running(dev)) {
		/* new_mtu will be used
		 * when device starts netxt time
		 */
		dev->mtu = new_mtu;
		return 0;
	}

	spin_lock_irq(&lp->lock);

        /* stop the chip */
	writel(RUN, lp->mmio + CMD0);

	dev->mtu = new_mtu;

	err = amd8111e_restart(dev);
	spin_unlock_irq(&lp->lock);
	if(!err)
		netif_start_queue(dev);
	return err;
}

static int amd8111e_enable_magicpkt(struct amd8111e_priv *lp)
{
	writel( VAL1|MPPLBA, lp->mmio + CMD3);
	writel( VAL0|MPEN_SW, lp->mmio + CMD7);

	/* To eliminate PCI posting bug */
	readl(lp->mmio + CMD7);
	return 0;
}

static int amd8111e_enable_link_change(struct amd8111e_priv *lp)
{

	/* Adapter is already stoped/suspended/interrupt-disabled */
	writel(VAL0|LCMODE_SW,lp->mmio + CMD7);

	/* To eliminate PCI posting bug */
	readl(lp->mmio + CMD7);
	return 0;
}

/* This function is called when a packet transmission fails to complete
 * within a reasonable period, on the assumption that an interrupt have
 * failed or the interface is locked up. This function will reinitialize
 * the hardware.
 */
static void amd8111e_tx_timeout(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	int err;

	netdev_err(dev, "transmit timed out, resetting\n");

	spin_lock_irq(&lp->lock);
	err = amd8111e_restart(dev);
	spin_unlock_irq(&lp->lock);
	if(!err)
		netif_wake_queue(dev);
}
static int amd8111e_suspend(struct pci_dev *pci_dev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pci_dev);
	struct amd8111e_priv *lp = netdev_priv(dev);

	if (!netif_running(dev))
		return 0;

	/* disable the interrupt */
	spin_lock_irq(&lp->lock);
	amd8111e_disable_interrupt(lp);
	spin_unlock_irq(&lp->lock);

	netif_device_detach(dev);

	/* stop chip */
	spin_lock_irq(&lp->lock);
	if(lp->options & OPTION_DYN_IPG_ENABLE)
		del_timer_sync(&lp->ipg_data.ipg_timer);
	amd8111e_stop_chip(lp);
	spin_unlock_irq(&lp->lock);

	if(lp->options & OPTION_WOL_ENABLE){
		 /* enable wol */
		if(lp->options & OPTION_WAKE_MAGIC_ENABLE)
			amd8111e_enable_magicpkt(lp);
		if(lp->options & OPTION_WAKE_PHY_ENABLE)
			amd8111e_enable_link_change(lp);

		pci_enable_wake(pci_dev, PCI_D3hot, 1);
		pci_enable_wake(pci_dev, PCI_D3cold, 1);

	}
	else{
		pci_enable_wake(pci_dev, PCI_D3hot, 0);
		pci_enable_wake(pci_dev, PCI_D3cold, 0);
	}

	pci_save_state(pci_dev);
	pci_set_power_state(pci_dev, PCI_D3hot);

	return 0;
}
static int amd8111e_resume(struct pci_dev *pci_dev)
{
	struct net_device *dev = pci_get_drvdata(pci_dev);
	struct amd8111e_priv *lp = netdev_priv(dev);

	if (!netif_running(dev))
		return 0;

	pci_set_power_state(pci_dev, PCI_D0);
	pci_restore_state(pci_dev);

	pci_enable_wake(pci_dev, PCI_D3hot, 0);
	pci_enable_wake(pci_dev, PCI_D3cold, 0); /* D3 cold */

	netif_device_attach(dev);

	spin_lock_irq(&lp->lock);
	amd8111e_restart(dev);
	/* Restart ipg timer */
	if(lp->options & OPTION_DYN_IPG_ENABLE)
		mod_timer(&lp->ipg_data.ipg_timer,
				jiffies + IPG_CONVERGE_JIFFIES);
	spin_unlock_irq(&lp->lock);

	return 0;
}

static void amd8111e_config_ipg(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	struct ipg_info *ipg_data = &lp->ipg_data;
	void __iomem *mmio = lp->mmio;
	unsigned int prev_col_cnt = ipg_data->col_cnt;
	unsigned int total_col_cnt;
	unsigned int tmp_ipg;

	if(lp->link_config.duplex == DUPLEX_FULL){
		ipg_data->ipg = DEFAULT_IPG;
		return;
	}

	if(ipg_data->ipg_state == SSTATE){

		if(ipg_data->timer_tick == IPG_STABLE_TIME){

			ipg_data->timer_tick = 0;
			ipg_data->ipg = MIN_IPG - IPG_STEP;
			ipg_data->current_ipg = MIN_IPG;
			ipg_data->diff_col_cnt = 0xFFFFFFFF;
			ipg_data->ipg_state = CSTATE;
		}
		else
			ipg_data->timer_tick++;
	}

	if(ipg_data->ipg_state == CSTATE){

		/* Get the current collision count */

		total_col_cnt = ipg_data->col_cnt =
				amd8111e_read_mib(mmio, xmt_collisions);

		if ((total_col_cnt - prev_col_cnt) <
				(ipg_data->diff_col_cnt)){

			ipg_data->diff_col_cnt =
				total_col_cnt - prev_col_cnt ;

			ipg_data->ipg = ipg_data->current_ipg;
		}

		ipg_data->current_ipg += IPG_STEP;

		if (ipg_data->current_ipg <= MAX_IPG)
			tmp_ipg = ipg_data->current_ipg;
		else{
			tmp_ipg = ipg_data->ipg;
			ipg_data->ipg_state = SSTATE;
		}
		writew((u32)tmp_ipg, mmio + IPG);
		writew((u32)(tmp_ipg - IFS1_DELTA), mmio + IFS1);
	}
	 mod_timer(&lp->ipg_data.ipg_timer, jiffies + IPG_CONVERGE_JIFFIES);
	return;

}

static void amd8111e_probe_ext_phy(struct net_device *dev)
{
	struct amd8111e_priv *lp = netdev_priv(dev);
	int i;

	for (i = 0x1e; i >= 0; i--) {
		u32 id1, id2;

		if (amd8111e_read_phy(lp, i, MII_PHYSID1, &id1))
			continue;
		if (amd8111e_read_phy(lp, i, MII_PHYSID2, &id2))
			continue;
		lp->ext_phy_id = (id1 << 16) | id2;
		lp->ext_phy_addr = i;
		return;
	}
	lp->ext_phy_id = 0;
	lp->ext_phy_addr = 1;
}

static const struct net_device_ops amd8111e_netdev_ops = {
	.ndo_open		= amd8111e_open,
	.ndo_stop		= amd8111e_close,
	.ndo_start_xmit		= amd8111e_start_xmit,
	.ndo_tx_timeout		= amd8111e_tx_timeout,
	.ndo_get_stats		= amd8111e_get_stats,
	.ndo_set_rx_mode	= amd8111e_set_multicast_list,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= amd8111e_set_mac_address,
	.ndo_do_ioctl		= amd8111e_ioctl,
	.ndo_change_mtu		= amd8111e_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	 = amd8111e_poll,
#endif
};

static int amd8111e_probe_one(struct pci_dev *pdev,
				  const struct pci_device_id *ent)
{
	int err, i;
	unsigned long reg_addr,reg_len;
	struct amd8111e_priv *lp;
	struct net_device *dev;

	err = pci_enable_device(pdev);
	if(err){
		dev_err(&pdev->dev, "Cannot enable new PCI device\n");
		return err;
	}

	if(!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)){
		dev_err(&pdev->dev, "Cannot find PCI base address\n");
		err = -ENODEV;
		goto err_disable_pdev;
	}

	err = pci_request_regions(pdev, MODULE_NAME);
	if(err){
		dev_err(&pdev->dev, "Cannot obtain PCI resources\n");
		goto err_disable_pdev;
	}

	pci_set_master(pdev);

	/* Find power-management capability. */
	if (!pdev->pm_cap) {
		dev_err(&pdev->dev, "No Power Management capability\n");
		err = -ENODEV;
		goto err_free_reg;
	}

	/* Initialize DMA */
	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(32)) < 0) {
		dev_err(&pdev->dev, "DMA not supported\n");
		err = -ENODEV;
		goto err_free_reg;
	}

	reg_addr = pci_resource_start(pdev, 0);
	reg_len = pci_resource_len(pdev, 0);

	dev = alloc_etherdev(sizeof(struct amd8111e_priv));
	if (!dev) {
		err = -ENOMEM;
		goto err_free_reg;
	}

	SET_NETDEV_DEV(dev, &pdev->dev);

#if AMD8111E_VLAN_TAG_USED
	dev->features |= NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX ;
#endif

	lp = netdev_priv(dev);
	lp->pci_dev = pdev;
	lp->amd8111e_net_dev = dev;
	lp->pm_cap = pdev->pm_cap;

	spin_lock_init(&lp->lock);

	lp->mmio = devm_ioremap(&pdev->dev, reg_addr, reg_len);
	if (!lp->mmio) {
		dev_err(&pdev->dev, "Cannot map device registers\n");
		err = -ENOMEM;
		goto err_free_dev;
	}

	/* Initializing MAC address */
	for (i = 0; i < ETH_ALEN; i++)
		dev->dev_addr[i] = readb(lp->mmio + PADR + i);

	/* Setting user defined parametrs */
	lp->ext_phy_option = speed_duplex[card_idx];
	if(coalesce[card_idx])
		lp->options |= OPTION_INTR_COAL_ENABLE;
	if(dynamic_ipg[card_idx++])
		lp->options |= OPTION_DYN_IPG_ENABLE;


	/* Initialize driver entry points */
	dev->netdev_ops = &amd8111e_netdev_ops;
	dev->ethtool_ops = &ops;
	dev->irq =pdev->irq;
	dev->watchdog_timeo = AMD8111E_TX_TIMEOUT;
	netif_napi_add(dev, &lp->napi, amd8111e_rx_poll, 32);

#if AMD8111E_VLAN_TAG_USED
	dev->features |= NETIF_F_HW_VLAN_CTAG_TX | NETIF_F_HW_VLAN_CTAG_RX;
#endif
	/* Probe the external PHY */
	amd8111e_probe_ext_phy(dev);

	/* setting mii default values */
	lp->mii_if.dev = dev;
	lp->mii_if.mdio_read = amd8111e_mdio_read;
	lp->mii_if.mdio_write = amd8111e_mdio_write;
	lp->mii_if.phy_id = lp->ext_phy_addr;

	/* Set receive buffer length and set jumbo option*/
	amd8111e_set_rx_buff_len(dev);


	err = register_netdev(dev);
	if (err) {
		dev_err(&pdev->dev, "Cannot register net device\n");
		goto err_free_dev;
	}

	pci_set_drvdata(pdev, dev);

	/* Initialize software ipg timer */
	if(lp->options & OPTION_DYN_IPG_ENABLE){
		init_timer(&lp->ipg_data.ipg_timer);
		lp->ipg_data.ipg_timer.data = (unsigned long) dev;
		lp->ipg_data.ipg_timer.function = (void *)&amd8111e_config_ipg;
		lp->ipg_data.ipg_timer.expires = jiffies +
						 IPG_CONVERGE_JIFFIES;
		lp->ipg_data.ipg = DEFAULT_IPG;
		lp->ipg_data.ipg_state = CSTATE;
	}

	/*  display driver and device information */
    	chip_version = (readl(lp->mmio + CHIPID) & 0xf0000000)>>28;
	dev_info(&pdev->dev, "AMD-8111e Driver Version: %s\n", MODULE_VERS);
	dev_info(&pdev->dev, "[ Rev %x ] PCI 10/100BaseT Ethernet %pM\n",
		 chip_version, dev->dev_addr);
	if (lp->ext_phy_id)
		dev_info(&pdev->dev, "Found MII PHY ID 0x%08x at address 0x%02x\n",
			 lp->ext_phy_id, lp->ext_phy_addr);
	else
		dev_info(&pdev->dev, "Couldn't detect MII PHY, assuming address 0x01\n");

    	return 0;

err_free_dev:
	free_netdev(dev);

err_free_reg:
	pci_release_regions(pdev);

err_disable_pdev:
	pci_disable_device(pdev);
	return err;

}

static void amd8111e_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (dev) {
		unregister_netdev(dev);
		free_netdev(dev);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
	}
}

static const struct pci_device_id amd8111e_pci_tbl[] = {
	{
	 .vendor = PCI_VENDOR_ID_AMD,
	 .device = PCI_DEVICE_ID_AMD8111E_7462,
	},
	{
	 .vendor = 0,
	}
};
MODULE_DEVICE_TABLE(pci, amd8111e_pci_tbl);

static struct pci_driver amd8111e_driver = {
	.name   	= MODULE_NAME,
	.id_table	= amd8111e_pci_tbl,
	.probe		= amd8111e_probe_one,
	.remove		= amd8111e_remove_one,
	.suspend	= amd8111e_suspend,
	.resume		= amd8111e_resume
};

module_pci_driver(amd8111e_driver);
