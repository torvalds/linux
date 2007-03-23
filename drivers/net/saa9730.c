/*
 * Copyright (C) 2000, 2005  MIPS Technologies, Inc.  All rights reserved.
 *	Authors: Carsten Langgaard <carstenl@mips.com>
 *		 Maciej W. Rozycki <macro@mips.com>
 * Copyright (C) 2004 Ralf Baechle <ralf@linux-mips.org>
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * SAA9730 ethernet driver.
 *
 * Changes:
 * Angelo Dell'Aera <buffer@antifork.org> :	Conversion to the new PCI API
 *						(pci_driver).
 *						Conversion to spinlocks.
 *						Error handling fixes.
 */

#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/io.h>

#include <asm/mips-boards/prom.h>

#include "saa9730.h"

#ifdef LAN_SAA9730_DEBUG
int lan_saa9730_debug = LAN_SAA9730_DEBUG;
#else
int lan_saa9730_debug;
#endif

#define DRV_MODULE_NAME "saa9730"

static struct pci_device_id saa9730_pci_tbl[] = {
	{ PCI_VENDOR_ID_PHILIPS, PCI_DEVICE_ID_PHILIPS_SAA9730,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, saa9730_pci_tbl);

/* Non-zero only if the current card is a PCI with BIOS-set IRQ. */
static unsigned int pci_irq_line;

static void evm_saa9730_enable_lan_int(struct lan_saa9730_private *lp)
{
	writel(readl(&lp->evm_saa9730_regs->InterruptBlock1) | EVM_LAN_INT,
	       &lp->evm_saa9730_regs->InterruptBlock1);
	writel(readl(&lp->evm_saa9730_regs->InterruptStatus1) | EVM_LAN_INT,
	       &lp->evm_saa9730_regs->InterruptStatus1);
	writel(readl(&lp->evm_saa9730_regs->InterruptEnable1) | EVM_LAN_INT |
	       EVM_MASTER_EN, &lp->evm_saa9730_regs->InterruptEnable1);
}

static void evm_saa9730_disable_lan_int(struct lan_saa9730_private *lp)
{
	writel(readl(&lp->evm_saa9730_regs->InterruptBlock1) & ~EVM_LAN_INT,
	       &lp->evm_saa9730_regs->InterruptBlock1);
	writel(readl(&lp->evm_saa9730_regs->InterruptEnable1) & ~EVM_LAN_INT,
	       &lp->evm_saa9730_regs->InterruptEnable1);
}

static void evm_saa9730_clear_lan_int(struct lan_saa9730_private *lp)
{
	writel(EVM_LAN_INT, &lp->evm_saa9730_regs->InterruptStatus1);
}

static void evm_saa9730_block_lan_int(struct lan_saa9730_private *lp)
{
	writel(readl(&lp->evm_saa9730_regs->InterruptBlock1) & ~EVM_LAN_INT,
	       &lp->evm_saa9730_regs->InterruptBlock1);
}

static void evm_saa9730_unblock_lan_int(struct lan_saa9730_private *lp)
{
	writel(readl(&lp->evm_saa9730_regs->InterruptBlock1) | EVM_LAN_INT,
	       &lp->evm_saa9730_regs->InterruptBlock1);
}

static void __attribute_used__ show_saa9730_regs(struct lan_saa9730_private *lp)
{
	int i, j;
	printk("TxmBufferA = %p\n", lp->TxmBuffer[0][0]);
	printk("TxmBufferB = %p\n", lp->TxmBuffer[1][0]);
	printk("RcvBufferA = %p\n", lp->RcvBuffer[0][0]);
	printk("RcvBufferB = %p\n", lp->RcvBuffer[1][0]);
	for (i = 0; i < LAN_SAA9730_BUFFERS; i++) {
		for (j = 0; j < LAN_SAA9730_TXM_Q_SIZE; j++) {
			printk("TxmBuffer[%d][%d] = %x\n", i, j,
			       le32_to_cpu(*(unsigned int *)
					   lp->TxmBuffer[i][j]));
		}
	}
	for (i = 0; i < LAN_SAA9730_BUFFERS; i++) {
		for (j = 0; j < LAN_SAA9730_RCV_Q_SIZE; j++) {
			printk("RcvBuffer[%d][%d] = %x\n", i, j,
			       le32_to_cpu(*(unsigned int *)
					   lp->RcvBuffer[i][j]));
		}
	}
	printk("lp->evm_saa9730_regs->InterruptBlock1 = %x\n",
	       readl(&lp->evm_saa9730_regs->InterruptBlock1));
	printk("lp->evm_saa9730_regs->InterruptStatus1 = %x\n",
	       readl(&lp->evm_saa9730_regs->InterruptStatus1));
	printk("lp->evm_saa9730_regs->InterruptEnable1 = %x\n",
	       readl(&lp->evm_saa9730_regs->InterruptEnable1));
	printk("lp->lan_saa9730_regs->Ok2Use = %x\n",
	       readl(&lp->lan_saa9730_regs->Ok2Use));
	printk("lp->NextTxmBufferIndex = %x\n", lp->NextTxmBufferIndex);
	printk("lp->NextTxmPacketIndex = %x\n", lp->NextTxmPacketIndex);
	printk("lp->PendingTxmBufferIndex = %x\n",
	       lp->PendingTxmBufferIndex);
	printk("lp->PendingTxmPacketIndex = %x\n",
	       lp->PendingTxmPacketIndex);
	printk("lp->lan_saa9730_regs->LanDmaCtl = %x\n",
	       readl(&lp->lan_saa9730_regs->LanDmaCtl));
	printk("lp->lan_saa9730_regs->DmaStatus = %x\n",
	       readl(&lp->lan_saa9730_regs->DmaStatus));
	printk("lp->lan_saa9730_regs->CamCtl = %x\n",
	       readl(&lp->lan_saa9730_regs->CamCtl));
	printk("lp->lan_saa9730_regs->TxCtl = %x\n",
	       readl(&lp->lan_saa9730_regs->TxCtl));
	printk("lp->lan_saa9730_regs->TxStatus = %x\n",
	       readl(&lp->lan_saa9730_regs->TxStatus));
	printk("lp->lan_saa9730_regs->RxCtl = %x\n",
	       readl(&lp->lan_saa9730_regs->RxCtl));
	printk("lp->lan_saa9730_regs->RxStatus = %x\n",
	       readl(&lp->lan_saa9730_regs->RxStatus));
	for (i = 0; i < LAN_SAA9730_CAM_DWORDS; i++) {
		writel(i, &lp->lan_saa9730_regs->CamAddress);
		printk("lp->lan_saa9730_regs->CamData = %x\n",
		       readl(&lp->lan_saa9730_regs->CamData));
	}
	printk("lp->stats.tx_packets = %lx\n", lp->stats.tx_packets);
	printk("lp->stats.tx_errors = %lx\n", lp->stats.tx_errors);
	printk("lp->stats.tx_aborted_errors = %lx\n",
	       lp->stats.tx_aborted_errors);
	printk("lp->stats.tx_window_errors = %lx\n",
	       lp->stats.tx_window_errors);
	printk("lp->stats.tx_carrier_errors = %lx\n",
	       lp->stats.tx_carrier_errors);
	printk("lp->stats.tx_fifo_errors = %lx\n",
	       lp->stats.tx_fifo_errors);
	printk("lp->stats.tx_heartbeat_errors = %lx\n",
	       lp->stats.tx_heartbeat_errors);
	printk("lp->stats.collisions = %lx\n", lp->stats.collisions);

	printk("lp->stats.rx_packets = %lx\n", lp->stats.rx_packets);
	printk("lp->stats.rx_errors = %lx\n", lp->stats.rx_errors);
	printk("lp->stats.rx_dropped = %lx\n", lp->stats.rx_dropped);
	printk("lp->stats.rx_crc_errors = %lx\n", lp->stats.rx_crc_errors);
	printk("lp->stats.rx_frame_errors = %lx\n",
	       lp->stats.rx_frame_errors);
	printk("lp->stats.rx_fifo_errors = %lx\n",
	       lp->stats.rx_fifo_errors);
	printk("lp->stats.rx_length_errors = %lx\n",
	       lp->stats.rx_length_errors);

	printk("lp->lan_saa9730_regs->DebugPCIMasterAddr = %x\n",
	       readl(&lp->lan_saa9730_regs->DebugPCIMasterAddr));
	printk("lp->lan_saa9730_regs->DebugLanTxStateMachine = %x\n",
	       readl(&lp->lan_saa9730_regs->DebugLanTxStateMachine));
	printk("lp->lan_saa9730_regs->DebugLanRxStateMachine = %x\n",
	       readl(&lp->lan_saa9730_regs->DebugLanRxStateMachine));
	printk("lp->lan_saa9730_regs->DebugLanTxFifoPointers = %x\n",
	       readl(&lp->lan_saa9730_regs->DebugLanTxFifoPointers));
	printk("lp->lan_saa9730_regs->DebugLanRxFifoPointers = %x\n",
	       readl(&lp->lan_saa9730_regs->DebugLanRxFifoPointers));
	printk("lp->lan_saa9730_regs->DebugLanCtlStateMachine = %x\n",
	       readl(&lp->lan_saa9730_regs->DebugLanCtlStateMachine));
}

static void lan_saa9730_buffer_init(struct lan_saa9730_private *lp)
{
	int i, j;

	/* Init RX buffers */
	for (i = 0; i < LAN_SAA9730_BUFFERS; i++) {
		for (j = 0; j < LAN_SAA9730_RCV_Q_SIZE; j++) {
			*(unsigned int *) lp->RcvBuffer[i][j] =
			    cpu_to_le32(RXSF_READY <<
					RX_STAT_CTL_OWNER_SHF);
		}
	}

	/* Init TX buffers */
	for (i = 0; i < LAN_SAA9730_BUFFERS; i++) {
		for (j = 0; j < LAN_SAA9730_TXM_Q_SIZE; j++) {
			*(unsigned int *) lp->TxmBuffer[i][j] =
			    cpu_to_le32(TXSF_EMPTY <<
					TX_STAT_CTL_OWNER_SHF);
		}
	}
}

static void lan_saa9730_free_buffers(struct pci_dev *pdev,
				     struct lan_saa9730_private *lp)
{
	pci_free_consistent(pdev, lp->buffer_size, lp->buffer_start,
			    lp->dma_addr);
}

static int lan_saa9730_allocate_buffers(struct pci_dev *pdev,
					struct lan_saa9730_private *lp)
{
	void *Pa;
	unsigned int i, j, rxoffset, txoffset;
	int ret;

	/* Initialize buffer space */
	lp->DmaRcvPackets = LAN_SAA9730_RCV_Q_SIZE;
	lp->DmaTxmPackets = LAN_SAA9730_TXM_Q_SIZE;

	/* Initialize Rx Buffer Index */
	lp->NextRcvPacketIndex = 0;
	lp->NextRcvBufferIndex = 0;

	/* Set current buffer index & next available packet index */
	lp->NextTxmPacketIndex = 0;
	lp->NextTxmBufferIndex = 0;
	lp->PendingTxmPacketIndex = 0;
	lp->PendingTxmBufferIndex = 0;

	/*
	 * Allocate all RX and TX packets in one chunk.
	 * The Rx and Tx packets must be PACKET_SIZE aligned.
	 */
	lp->buffer_size = ((LAN_SAA9730_RCV_Q_SIZE + LAN_SAA9730_TXM_Q_SIZE) *
			   LAN_SAA9730_PACKET_SIZE * LAN_SAA9730_BUFFERS) +
			  LAN_SAA9730_PACKET_SIZE;
	lp->buffer_start = pci_alloc_consistent(pdev, lp->buffer_size,
						&lp->dma_addr);
	if (!lp->buffer_start) {
		ret = -ENOMEM;
		goto out;
	}

	Pa = (void *)ALIGN((unsigned long)lp->buffer_start,
			   LAN_SAA9730_PACKET_SIZE);

	rxoffset = Pa - lp->buffer_start;

	/* Init RX buffers */
	for (i = 0; i < LAN_SAA9730_BUFFERS; i++) {
		for (j = 0; j < LAN_SAA9730_RCV_Q_SIZE; j++) {
			*(unsigned int *) Pa =
			    cpu_to_le32(RXSF_READY <<
					RX_STAT_CTL_OWNER_SHF);
			lp->RcvBuffer[i][j] = Pa;
			Pa += LAN_SAA9730_PACKET_SIZE;
		}
	}

	txoffset = Pa - lp->buffer_start;

	/* Init TX buffers */
	for (i = 0; i < LAN_SAA9730_BUFFERS; i++) {
		for (j = 0; j < LAN_SAA9730_TXM_Q_SIZE; j++) {
			*(unsigned int *) Pa =
			    cpu_to_le32(TXSF_EMPTY <<
					TX_STAT_CTL_OWNER_SHF);
			lp->TxmBuffer[i][j] = Pa;
			Pa += LAN_SAA9730_PACKET_SIZE;
		}
	}

	/*
	 * Set rx buffer A and rx buffer B to point to the first two buffer
	 * spaces.
	 */
	writel(lp->dma_addr + rxoffset, &lp->lan_saa9730_regs->RxBuffA);
	writel(lp->dma_addr + rxoffset +
	       LAN_SAA9730_PACKET_SIZE * LAN_SAA9730_RCV_Q_SIZE,
	       &lp->lan_saa9730_regs->RxBuffB);

	/*
	 * Set txm_buf_a and txm_buf_b to point to the first two buffer
	 * space
	 */
	writel(lp->dma_addr + txoffset,
	       &lp->lan_saa9730_regs->TxBuffA);
	writel(lp->dma_addr + txoffset +
	       LAN_SAA9730_PACKET_SIZE * LAN_SAA9730_TXM_Q_SIZE,
	       &lp->lan_saa9730_regs->TxBuffB);

	/* Set packet number */
	writel((lp->DmaRcvPackets << PK_COUNT_RX_A_SHF) |
	       (lp->DmaRcvPackets << PK_COUNT_RX_B_SHF) |
	       (lp->DmaTxmPackets << PK_COUNT_TX_A_SHF) |
	       (lp->DmaTxmPackets << PK_COUNT_TX_B_SHF),
	       &lp->lan_saa9730_regs->PacketCount);

	return 0;

out:
	return ret;
}

static int lan_saa9730_cam_load(struct lan_saa9730_private *lp)
{
	unsigned int i;
	unsigned char *NetworkAddress;

	NetworkAddress = (unsigned char *) &lp->PhysicalAddress[0][0];

	for (i = 0; i < LAN_SAA9730_CAM_DWORDS; i++) {
		/* First set address to where data is written */
		writel(i, &lp->lan_saa9730_regs->CamAddress);
		writel((NetworkAddress[0] << 24) | (NetworkAddress[1] << 16) |
		       (NetworkAddress[2] << 8) | NetworkAddress[3],
		       &lp->lan_saa9730_regs->CamData);
		NetworkAddress += 4;
	}
	return 0;
}

static int lan_saa9730_cam_init(struct net_device *dev)
{
	struct lan_saa9730_private *lp = netdev_priv(dev);
	unsigned int i;

	/* Copy MAC-address into all entries. */
	for (i = 0; i < LAN_SAA9730_CAM_ENTRIES; i++) {
		memcpy((unsigned char *) lp->PhysicalAddress[i],
		       (unsigned char *) dev->dev_addr, 6);
	}

	return 0;
}

static int lan_saa9730_mii_init(struct lan_saa9730_private *lp)
{
	int i, l;

	/* Check link status, spin here till station is not busy. */
	i = 0;
	while (readl(&lp->lan_saa9730_regs->StationMgmtCtl) & MD_CA_BUSY) {
		i++;
		if (i > 100) {
			printk("Error: lan_saa9730_mii_init: timeout\n");
			return -1;
		}
		mdelay(1);	/* wait 1 ms. */
	}

	/* Now set the control and address register. */
	writel(MD_CA_BUSY | PHY_STATUS | PHY_ADDRESS << MD_CA_PHY_SHF,
	       &lp->lan_saa9730_regs->StationMgmtCtl);

	/* check link status, spin here till station is not busy */
	i = 0;
	while (readl(&lp->lan_saa9730_regs->StationMgmtCtl) & MD_CA_BUSY) {
		i++;
		if (i > 100) {
			printk("Error: lan_saa9730_mii_init: timeout\n");
			return -1;
		}
		mdelay(1);	/* wait 1 ms. */
	}

	/* Wait for 1 ms. */
	mdelay(1);

	/* Check the link status. */
	if (readl(&lp->lan_saa9730_regs->StationMgmtData) &
	    PHY_STATUS_LINK_UP) {
		/* Link is up. */
		return 0;
	} else {
		/* Link is down, reset the PHY first. */

		/* set PHY address = 'CONTROL' */
		writel(PHY_ADDRESS << MD_CA_PHY_SHF | MD_CA_WR | PHY_CONTROL,
		       &lp->lan_saa9730_regs->StationMgmtCtl);

		/* Wait for 1 ms. */
		mdelay(1);

		/* set 'CONTROL' = force reset and renegotiate */
		writel(PHY_CONTROL_RESET | PHY_CONTROL_AUTO_NEG |
		       PHY_CONTROL_RESTART_AUTO_NEG,
		       &lp->lan_saa9730_regs->StationMgmtData);

		/* Wait for 50 ms. */
		mdelay(50);

		/* set 'BUSY' to start operation */
		writel(MD_CA_BUSY | PHY_ADDRESS << MD_CA_PHY_SHF | MD_CA_WR |
		       PHY_CONTROL, &lp->lan_saa9730_regs->StationMgmtCtl);

		/* await completion */
		i = 0;
		while (readl(&lp->lan_saa9730_regs->StationMgmtCtl) &
		       MD_CA_BUSY) {
			i++;
			if (i > 100) {
				printk
				    ("Error: lan_saa9730_mii_init: timeout\n");
				return -1;
			}
			mdelay(1);	/* wait 1 ms. */
		}

		/* Wait for 1 ms. */
		mdelay(1);

		for (l = 0; l < 2; l++) {
			/* set PHY address = 'STATUS' */
			writel(MD_CA_BUSY | PHY_ADDRESS << MD_CA_PHY_SHF |
			       PHY_STATUS,
			       &lp->lan_saa9730_regs->StationMgmtCtl);

			/* await completion */
			i = 0;
			while (readl(&lp->lan_saa9730_regs->StationMgmtCtl) &
			       MD_CA_BUSY) {
				i++;
				if (i > 100) {
					printk
					    ("Error: lan_saa9730_mii_init: timeout\n");
					return -1;
				}
				mdelay(1);	/* wait 1 ms. */
			}

			/* wait for 3 sec. */
			mdelay(3000);

			/* check the link status */
			if (readl(&lp->lan_saa9730_regs->StationMgmtData) &
			    PHY_STATUS_LINK_UP) {
				/* link is up */
				break;
			}
		}
	}

	return 0;
}

static int lan_saa9730_control_init(struct lan_saa9730_private *lp)
{
	/* Initialize DMA control register. */
	writel((LANMB_ANY << DMA_CTL_MAX_XFER_SHF) |
	       (LANEND_LITTLE << DMA_CTL_ENDIAN_SHF) |
	       (LAN_SAA9730_RCV_Q_INT_THRESHOLD << DMA_CTL_RX_INT_COUNT_SHF)
	       | DMA_CTL_RX_INT_TO_EN | DMA_CTL_RX_INT_EN |
	       DMA_CTL_MAC_RX_INT_EN | DMA_CTL_MAC_TX_INT_EN,
	       &lp->lan_saa9730_regs->LanDmaCtl);

	/* Initial MAC control register. */
	writel((MACCM_MII << MAC_CONTROL_CONN_SHF) | MAC_CONTROL_FULL_DUP,
	       &lp->lan_saa9730_regs->MacCtl);

	/* Initialize CAM control register. */
	writel(CAM_CONTROL_COMP_EN | CAM_CONTROL_BROAD_ACC,
	       &lp->lan_saa9730_regs->CamCtl);

	/*
	 * Initialize CAM enable register, only turn on first entry, should
	 * contain own addr.
	 */
	writel(0x0001, &lp->lan_saa9730_regs->CamEnable);

	/* Initialize Tx control register */
	writel(TX_CTL_EN_COMP, &lp->lan_saa9730_regs->TxCtl);

	/* Initialize Rcv control register */
	writel(RX_CTL_STRIP_CRC, &lp->lan_saa9730_regs->RxCtl);

	/* Reset DMA engine */
	writel(DMA_TEST_SW_RESET, &lp->lan_saa9730_regs->DmaTest);

	return 0;
}

static int lan_saa9730_stop(struct lan_saa9730_private *lp)
{
	int i;

	/* Stop DMA first */
	writel(readl(&lp->lan_saa9730_regs->LanDmaCtl) &
	       ~(DMA_CTL_EN_TX_DMA | DMA_CTL_EN_RX_DMA),
	       &lp->lan_saa9730_regs->LanDmaCtl);

	/* Set the SW Reset bits in DMA and MAC control registers */
	writel(DMA_TEST_SW_RESET, &lp->lan_saa9730_regs->DmaTest);
	writel(readl(&lp->lan_saa9730_regs->MacCtl) | MAC_CONTROL_RESET,
	       &lp->lan_saa9730_regs->MacCtl);

	/*
	 * Wait for MAC reset to have finished. The reset bit is auto cleared
	 * when the reset is done.
	 */
	i = 0;
	while (readl(&lp->lan_saa9730_regs->MacCtl) & MAC_CONTROL_RESET) {
		i++;
		if (i > 100) {
			printk
			    ("Error: lan_sa9730_stop: MAC reset timeout\n");
			return -1;
		}
		mdelay(1);	/* wait 1 ms. */
	}

	return 0;
}

static int lan_saa9730_dma_init(struct lan_saa9730_private *lp)
{
	/* Stop lan controller. */
	lan_saa9730_stop(lp);

	writel(LAN_SAA9730_DEFAULT_TIME_OUT_CNT,
	       &lp->lan_saa9730_regs->Timeout);

	return 0;
}

static int lan_saa9730_start(struct lan_saa9730_private *lp)
{
	lan_saa9730_buffer_init(lp);

	/* Initialize Rx Buffer Index */
	lp->NextRcvPacketIndex = 0;
	lp->NextRcvBufferIndex = 0;

	/* Set current buffer index & next available packet index */
	lp->NextTxmPacketIndex = 0;
	lp->NextTxmBufferIndex = 0;
	lp->PendingTxmPacketIndex = 0;
	lp->PendingTxmBufferIndex = 0;

	writel(readl(&lp->lan_saa9730_regs->LanDmaCtl) | DMA_CTL_EN_TX_DMA |
	       DMA_CTL_EN_RX_DMA, &lp->lan_saa9730_regs->LanDmaCtl);

	/* For Tx, turn on MAC then DMA */
	writel(readl(&lp->lan_saa9730_regs->TxCtl) | TX_CTL_TX_EN,
	       &lp->lan_saa9730_regs->TxCtl);

	/* For Rx, turn on DMA then MAC */
	writel(readl(&lp->lan_saa9730_regs->RxCtl) | RX_CTL_RX_EN,
	       &lp->lan_saa9730_regs->RxCtl);

	/* Set Ok2Use to let hardware own the buffers.	*/
	writel(OK2USE_RX_A | OK2USE_RX_B, &lp->lan_saa9730_regs->Ok2Use);

	return 0;
}

static int lan_saa9730_restart(struct lan_saa9730_private *lp)
{
	lan_saa9730_stop(lp);
	lan_saa9730_start(lp);

	return 0;
}

static int lan_saa9730_tx(struct net_device *dev)
{
	struct lan_saa9730_private *lp = netdev_priv(dev);
	unsigned int *pPacket;
	unsigned int tx_status;

	if (lan_saa9730_debug > 5)
		printk("lan_saa9730_tx interrupt\n");

	/* Clear interrupt. */
	writel(DMA_STATUS_MAC_TX_INT, &lp->lan_saa9730_regs->DmaStatus);

	while (1) {
		pPacket = lp->TxmBuffer[lp->PendingTxmBufferIndex]
				       [lp->PendingTxmPacketIndex];

		/* Get status of first packet transmitted. */
		tx_status = le32_to_cpu(*pPacket);

		/* Check ownership. */
		if ((tx_status & TX_STAT_CTL_OWNER_MSK) !=
		    (TXSF_HWDONE << TX_STAT_CTL_OWNER_SHF)) break;

		/* Check for error. */
		if (tx_status & TX_STAT_CTL_ERROR_MSK) {
			if (lan_saa9730_debug > 1)
				printk("lan_saa9730_tx: tx error = %x\n",
				       tx_status);

			lp->stats.tx_errors++;
			if (tx_status &
			    (TX_STATUS_EX_COLL << TX_STAT_CTL_STATUS_SHF))
				lp->stats.tx_aborted_errors++;
			if (tx_status &
			    (TX_STATUS_LATE_COLL << TX_STAT_CTL_STATUS_SHF))
				lp->stats.tx_window_errors++;
			if (tx_status &
			    (TX_STATUS_L_CARR << TX_STAT_CTL_STATUS_SHF))
				lp->stats.tx_carrier_errors++;
			if (tx_status &
			    (TX_STATUS_UNDER << TX_STAT_CTL_STATUS_SHF))
				lp->stats.tx_fifo_errors++;
			if (tx_status &
			    (TX_STATUS_SQ_ERR << TX_STAT_CTL_STATUS_SHF))
				lp->stats.tx_heartbeat_errors++;

			lp->stats.collisions +=
				tx_status & TX_STATUS_TX_COLL_MSK;
		}

		/* Free buffer. */
		*pPacket =
		    cpu_to_le32(TXSF_EMPTY << TX_STAT_CTL_OWNER_SHF);

		/* Update pending index pointer. */
		lp->PendingTxmPacketIndex++;
		if (lp->PendingTxmPacketIndex >= LAN_SAA9730_TXM_Q_SIZE) {
			lp->PendingTxmPacketIndex = 0;
			lp->PendingTxmBufferIndex ^= 1;
		}
	}

	/* The tx buffer is no longer full. */
	netif_wake_queue(dev);

	return 0;
}

static int lan_saa9730_rx(struct net_device *dev)
{
	struct lan_saa9730_private *lp = netdev_priv(dev);
	int len = 0;
	struct sk_buff *skb = 0;
	unsigned int rx_status;
	int BufferIndex;
	int PacketIndex;
	unsigned int *pPacket;
	unsigned char *pData;

	if (lan_saa9730_debug > 5)
		printk("lan_saa9730_rx interrupt\n");

	/* Clear receive interrupts. */
	writel(DMA_STATUS_MAC_RX_INT | DMA_STATUS_RX_INT |
	       DMA_STATUS_RX_TO_INT, &lp->lan_saa9730_regs->DmaStatus);

	/* Address next packet */
	BufferIndex = lp->NextRcvBufferIndex;
	PacketIndex = lp->NextRcvPacketIndex;
	pPacket = lp->RcvBuffer[BufferIndex][PacketIndex];
	rx_status = le32_to_cpu(*pPacket);

	/* Process each packet. */
	while ((rx_status & RX_STAT_CTL_OWNER_MSK) ==
	       (RXSF_HWDONE << RX_STAT_CTL_OWNER_SHF)) {
		/* Check the rx status. */
		if (rx_status & (RX_STATUS_GOOD << RX_STAT_CTL_STATUS_SHF)) {
			/* Received packet is good. */
			len = (rx_status & RX_STAT_CTL_LENGTH_MSK) >>
			    RX_STAT_CTL_LENGTH_SHF;

			pData = (unsigned char *) pPacket;
			pData += 4;
			skb = dev_alloc_skb(len + 2);
			if (skb == 0) {
				printk
				    ("%s: Memory squeeze, deferring packet.\n",
				     dev->name);
				lp->stats.rx_dropped++;
			} else {
				lp->stats.rx_bytes += len;
				lp->stats.rx_packets++;
				skb->dev = dev;
				skb_reserve(skb, 2);	/* 16 byte align */
				skb_put(skb, len);	/* make room */
				eth_copy_and_sum(skb,
						 (unsigned char *) pData,
						 len, 0);
				skb->protocol = eth_type_trans(skb, dev);
				netif_rx(skb);
				dev->last_rx = jiffies;
			}
		} else {
			/* We got an error packet. */
			if (lan_saa9730_debug > 2)
				printk
				    ("lan_saa9730_rx: We got an error packet = %x\n",
				     rx_status);

			lp->stats.rx_errors++;
			if (rx_status &
			    (RX_STATUS_CRC_ERR << RX_STAT_CTL_STATUS_SHF))
				lp->stats.rx_crc_errors++;
			if (rx_status &
			    (RX_STATUS_ALIGN_ERR << RX_STAT_CTL_STATUS_SHF))
				lp->stats.rx_frame_errors++;
			if (rx_status &
			    (RX_STATUS_OVERFLOW << RX_STAT_CTL_STATUS_SHF))
				lp->stats.rx_fifo_errors++;
			if (rx_status &
			    (RX_STATUS_LONG_ERR << RX_STAT_CTL_STATUS_SHF))
				lp->stats.rx_length_errors++;
		}

		/* Indicate we have processed the buffer. */
		*pPacket = cpu_to_le32(RXSF_READY << RX_STAT_CTL_OWNER_SHF);

		/* Make sure A or B is available to hardware as appropriate. */
		writel(BufferIndex ? OK2USE_RX_B : OK2USE_RX_A,
		       &lp->lan_saa9730_regs->Ok2Use);

		/* Go to next packet in sequence. */
		lp->NextRcvPacketIndex++;
		if (lp->NextRcvPacketIndex >= LAN_SAA9730_RCV_Q_SIZE) {
			lp->NextRcvPacketIndex = 0;
			lp->NextRcvBufferIndex ^= 1;
		}

		/* Address next packet */
		BufferIndex = lp->NextRcvBufferIndex;
		PacketIndex = lp->NextRcvPacketIndex;
		pPacket = lp->RcvBuffer[BufferIndex][PacketIndex];
		rx_status = le32_to_cpu(*pPacket);
	}

	return 0;
}

static irqreturn_t lan_saa9730_interrupt(const int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct lan_saa9730_private *lp = netdev_priv(dev);

	if (lan_saa9730_debug > 5)
		printk("lan_saa9730_interrupt\n");

	/* Disable the EVM LAN interrupt. */
	evm_saa9730_block_lan_int(lp);

	/* Clear the EVM LAN interrupt. */
	evm_saa9730_clear_lan_int(lp);

	/* Service pending transmit interrupts. */
	if (readl(&lp->lan_saa9730_regs->DmaStatus) & DMA_STATUS_MAC_TX_INT)
		lan_saa9730_tx(dev);

	/* Service pending receive interrupts. */
	if (readl(&lp->lan_saa9730_regs->DmaStatus) &
	    (DMA_STATUS_MAC_RX_INT | DMA_STATUS_RX_INT |
	     DMA_STATUS_RX_TO_INT)) lan_saa9730_rx(dev);

	/* Enable the EVM LAN interrupt. */
	evm_saa9730_unblock_lan_int(lp);

	return IRQ_HANDLED;
}

static int lan_saa9730_open(struct net_device *dev)
{
	struct lan_saa9730_private *lp = netdev_priv(dev);

	/* Associate IRQ with lan_saa9730_interrupt */
	if (request_irq(dev->irq, &lan_saa9730_interrupt, 0, "SAA9730 Eth",
			dev)) {
		printk("lan_saa9730_open: Can't get irq %d\n", dev->irq);
		return -EAGAIN;
	}

	/* Enable the Lan interrupt in the event manager. */
	evm_saa9730_enable_lan_int(lp);

	/* Start the LAN controller */
	if (lan_saa9730_start(lp))
		return -1;

	netif_start_queue(dev);

	return 0;
}

static int lan_saa9730_write(struct lan_saa9730_private *lp,
			     struct sk_buff *skb, int skblen)
{
	unsigned char *pbData = skb->data;
	unsigned int len = skblen;
	unsigned char *pbPacketData;
	unsigned int tx_status;
	int BufferIndex;
	int PacketIndex;

	if (lan_saa9730_debug > 5)
		printk("lan_saa9730_write: skb=%p\n", skb);

	BufferIndex = lp->NextTxmBufferIndex;
	PacketIndex = lp->NextTxmPacketIndex;

	tx_status = le32_to_cpu(*(unsigned int *)lp->TxmBuffer[BufferIndex]
							      [PacketIndex]);
	if ((tx_status & TX_STAT_CTL_OWNER_MSK) !=
	    (TXSF_EMPTY << TX_STAT_CTL_OWNER_SHF)) {
		if (lan_saa9730_debug > 4)
			printk
			    ("lan_saa9730_write: Tx buffer not available: tx_status = %x\n",
			     tx_status);
		return -1;
	}

	lp->NextTxmPacketIndex++;
	if (lp->NextTxmPacketIndex >= LAN_SAA9730_TXM_Q_SIZE) {
		lp->NextTxmPacketIndex = 0;
		lp->NextTxmBufferIndex ^= 1;
	}

	pbPacketData = lp->TxmBuffer[BufferIndex][PacketIndex];
	pbPacketData += 4;

	/* copy the bits */
	memcpy(pbPacketData, pbData, len);

	/* Set transmit status for hardware */
	*(unsigned int *)lp->TxmBuffer[BufferIndex][PacketIndex] =
		cpu_to_le32((TXSF_READY << TX_STAT_CTL_OWNER_SHF) |
			    (TX_STAT_CTL_INT_AFTER_TX <<
			     TX_STAT_CTL_FRAME_SHF) |
			    (len << TX_STAT_CTL_LENGTH_SHF));

	/* Make sure A or B is available to hardware as appropriate. */
	writel(BufferIndex ? OK2USE_TX_B : OK2USE_TX_A,
	       &lp->lan_saa9730_regs->Ok2Use);

	return 0;
}

static void lan_saa9730_tx_timeout(struct net_device *dev)
{
	struct lan_saa9730_private *lp = netdev_priv(dev);

	/* Transmitter timeout, serious problems */
	lp->stats.tx_errors++;
	printk("%s: transmit timed out, reset\n", dev->name);
	/*show_saa9730_regs(lp); */
	lan_saa9730_restart(lp);

	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

static int lan_saa9730_start_xmit(struct sk_buff *skb,
				  struct net_device *dev)
{
	struct lan_saa9730_private *lp = netdev_priv(dev);
	unsigned long flags;
	int skblen;
	int len;

	if (lan_saa9730_debug > 4)
		printk("Send packet: skb=%p\n", skb);

	skblen = skb->len;

	spin_lock_irqsave(&lp->lock, flags);

	len = (skblen <= ETH_ZLEN) ? ETH_ZLEN : skblen;

	if (lan_saa9730_write(lp, skb, skblen)) {
		spin_unlock_irqrestore(&lp->lock, flags);
		printk("Error when writing packet to controller: skb=%p\n", skb);
		netif_stop_queue(dev);
		return -1;
	}

	lp->stats.tx_bytes += len;
	lp->stats.tx_packets++;

	dev->trans_start = jiffies;
	netif_wake_queue(dev);
	dev_kfree_skb(skb);

	spin_unlock_irqrestore(&lp->lock, flags);

	return 0;
}

static int lan_saa9730_close(struct net_device *dev)
{
	struct lan_saa9730_private *lp = netdev_priv(dev);

	if (lan_saa9730_debug > 1)
		printk("lan_saa9730_close:\n");

	netif_stop_queue(dev);

	/* Disable the Lan interrupt in the event manager. */
	evm_saa9730_disable_lan_int(lp);

	/* Stop the controller */
	if (lan_saa9730_stop(lp))
		return -1;

	free_irq(dev->irq, (void *) dev);

	return 0;
}

static struct net_device_stats *lan_saa9730_get_stats(struct net_device
						      *dev)
{
	struct lan_saa9730_private *lp = netdev_priv(dev);

	return &lp->stats;
}

static void lan_saa9730_set_multicast(struct net_device *dev)
{
	struct lan_saa9730_private *lp = netdev_priv(dev);

	/* Stop the controller */
	lan_saa9730_stop(lp);

	if (dev->flags & IFF_PROMISC) {
		/* accept all packets */
		writel(CAM_CONTROL_COMP_EN | CAM_CONTROL_STATION_ACC |
		       CAM_CONTROL_GROUP_ACC | CAM_CONTROL_BROAD_ACC,
		       &lp->lan_saa9730_regs->CamCtl);
	} else {
		if (dev->flags & IFF_ALLMULTI) {
			/* accept all multicast packets */
			writel(CAM_CONTROL_COMP_EN | CAM_CONTROL_GROUP_ACC |
			       CAM_CONTROL_BROAD_ACC,
			       &lp->lan_saa9730_regs->CamCtl);
		} else {
			/*
			 * Will handle the multicast stuff later. -carstenl
			 */
		}
	}

	lan_saa9730_restart(lp);
}


static void __devexit saa9730_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct lan_saa9730_private *lp = netdev_priv(dev);

	if (dev) {
		unregister_netdev(dev);
		lan_saa9730_free_buffers(pdev, lp);
		iounmap(lp->lan_saa9730_regs);
		iounmap(lp->evm_saa9730_regs);
		free_netdev(dev);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
	}
}


static int lan_saa9730_init(struct net_device *dev, struct pci_dev *pdev,
	unsigned long ioaddr, int irq)
{
	struct lan_saa9730_private *lp = netdev_priv(dev);
	unsigned char ethernet_addr[6];
	int ret;

	if (get_ethernet_addr(ethernet_addr)) {
		ret = -ENODEV;
		goto out;
	}

	memcpy(dev->dev_addr, ethernet_addr, 6);
	dev->base_addr = ioaddr;
	dev->irq = irq;

	lp->pci_dev = pdev;

	/* Set SAA9730 LAN base address. */
	lp->lan_saa9730_regs = ioremap(ioaddr + SAA9730_LAN_REGS_ADDR,
				       SAA9730_LAN_REGS_SIZE);
	if (!lp->lan_saa9730_regs) {
		ret = -ENOMEM;
		goto out;
	}

	/* Set SAA9730 EVM base address. */
	lp->evm_saa9730_regs = ioremap(ioaddr + SAA9730_EVM_REGS_ADDR,
				       SAA9730_EVM_REGS_SIZE);
	if (!lp->evm_saa9730_regs) {
		ret = -ENOMEM;
		goto out_iounmap_lan;
	}

	/* Allocate LAN RX/TX frame buffer space. */
	if ((ret = lan_saa9730_allocate_buffers(pdev, lp)))
		goto out_iounmap;

	/* Stop LAN controller. */
	if ((ret = lan_saa9730_stop(lp)))
		goto out_free_consistent;

	/* Initialize CAM registers. */
	if ((ret = lan_saa9730_cam_init(dev)))
		goto out_free_consistent;

	/* Initialize MII registers. */
	if ((ret = lan_saa9730_mii_init(lp)))
		goto out_free_consistent;

	/* Initialize control registers. */
	if ((ret = lan_saa9730_control_init(lp)))
		goto out_free_consistent;

	/* Load CAM registers. */
	if ((ret = lan_saa9730_cam_load(lp)))
		goto out_free_consistent;

	/* Initialize DMA context registers. */
	if ((ret = lan_saa9730_dma_init(lp)))
		goto out_free_consistent;

	spin_lock_init(&lp->lock);

	dev->open = lan_saa9730_open;
	dev->hard_start_xmit = lan_saa9730_start_xmit;
	dev->stop = lan_saa9730_close;
	dev->get_stats = lan_saa9730_get_stats;
	dev->set_multicast_list = lan_saa9730_set_multicast;
	dev->tx_timeout = lan_saa9730_tx_timeout;
	dev->watchdog_timeo = (HZ >> 1);
	dev->dma = 0;

	ret = register_netdev (dev);
	if (ret)
		goto out_free_consistent;

	return 0;

out_free_consistent:
	lan_saa9730_free_buffers(pdev, lp);
out_iounmap:
	iounmap(lp->evm_saa9730_regs);
out_iounmap_lan:
	iounmap(lp->lan_saa9730_regs);
out:
	return ret;
}


static int __devinit saa9730_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *dev = NULL;
	unsigned long pci_ioaddr;
	int err;

	if (lan_saa9730_debug > 1)
		printk("saa9730.c: PCI bios is present, checking for devices...\n");

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR "Cannot enable PCI device, aborting.\n");
		goto out;
	}

	err = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (err) {
		printk(KERN_ERR "Cannot obtain PCI resources, aborting.\n");
		goto out_disable_pdev;
	}

	pci_irq_line = pdev->irq;
	/* LAN base address in located at BAR 1. */

	pci_ioaddr = pci_resource_start(pdev, 1);
	pci_set_master(pdev);

	printk("Found SAA9730 (PCI) at %lx, irq %d.\n",
	       pci_ioaddr, pci_irq_line);

	dev = alloc_etherdev(sizeof(struct lan_saa9730_private));
	if (!dev)
		goto out_disable_pdev;

	err = lan_saa9730_init(dev, pdev, pci_ioaddr, pci_irq_line);
	if (err) {
		printk("LAN init failed");
		goto out_free_netdev;
	}

	pci_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);
	return 0;

out_free_netdev:
	free_netdev(dev);
out_disable_pdev:
	pci_disable_device(pdev);
out:
	pci_set_drvdata(pdev, NULL);
	return err;
}


static struct pci_driver saa9730_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= saa9730_pci_tbl,
	.probe		= saa9730_init_one,
	.remove		= __devexit_p(saa9730_remove_one),
};


static int __init saa9730_init(void)
{
	return pci_register_driver(&saa9730_driver);
}

static void __exit saa9730_cleanup(void)
{
	pci_unregister_driver(&saa9730_driver);
}

module_init(saa9730_init);
module_exit(saa9730_cleanup);

MODULE_AUTHOR("Ralf Baechle <ralf@linux-mips.org>");
MODULE_DESCRIPTION("Philips SAA9730 ethernet driver");
MODULE_LICENSE("GPL");
