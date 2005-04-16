/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *
 * ########################################################################
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
 * ########################################################################
 *
 * SAA9730 ethernet driver.
 *
 * Changes:
 * Angelo Dell'Aera <buffer@antifork.org> : Conversion to the new PCI API (pci_driver).
 *                                          Conversion to spinlocks.
 *                                          Error handling fixes.
 *                                           
 */

#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/pci.h>
#include <linux/spinlock.h>

#include <asm/addrspace.h>
#include <asm/mips-boards/prom.h>

#include "saa9730.h"

#ifdef LAN_SAA9730_DEBUG
int lan_saa9730_debug = LAN_SAA9730_DEBUG;
#else
int lan_saa9730_debug;
#endif

#define DRV_MODULE_NAME "saa9730"

static struct pci_device_id saa9730_pci_tbl[] = {
	{ PCI_VENDOR_ID_PHILIPS, PCI_DEVICE_ID_PHILIPS_SAA9370,
          PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, saa9730_pci_tbl);

/* Non-zero only if the current card is a PCI with BIOS-set IRQ. */
static unsigned int pci_irq_line;

#define INL(a)     inl((unsigned long)a)
#define OUTL(x,a)  outl(x,(unsigned long)a)

static void evm_saa9730_enable_lan_int(struct lan_saa9730_private *lp)
{
	OUTL(INL(&lp->evm_saa9730_regs->InterruptBlock1) | EVM_LAN_INT,
	     &lp->evm_saa9730_regs->InterruptBlock1);
	OUTL(INL(&lp->evm_saa9730_regs->InterruptStatus1) | EVM_LAN_INT,
	     &lp->evm_saa9730_regs->InterruptStatus1);
	OUTL(INL(&lp->evm_saa9730_regs->InterruptEnable1) | EVM_LAN_INT |
	     EVM_MASTER_EN, &lp->evm_saa9730_regs->InterruptEnable1);
}
static void evm_saa9730_disable_lan_int(struct lan_saa9730_private *lp)
{
	OUTL(INL(&lp->evm_saa9730_regs->InterruptBlock1) & ~EVM_LAN_INT,
	     &lp->evm_saa9730_regs->InterruptBlock1);
	OUTL(INL(&lp->evm_saa9730_regs->InterruptEnable1) & ~EVM_LAN_INT,
	     &lp->evm_saa9730_regs->InterruptEnable1);
}

static void evm_saa9730_clear_lan_int(struct lan_saa9730_private *lp)
{
	OUTL(EVM_LAN_INT, &lp->evm_saa9730_regs->InterruptStatus1);
}

static void evm_saa9730_block_lan_int(struct lan_saa9730_private *lp)
{
	OUTL(INL(&lp->evm_saa9730_regs->InterruptBlock1) & ~EVM_LAN_INT,
	     &lp->evm_saa9730_regs->InterruptBlock1);
}

static void evm_saa9730_unblock_lan_int(struct lan_saa9730_private *lp)
{
	OUTL(INL(&lp->evm_saa9730_regs->InterruptBlock1) | EVM_LAN_INT,
	     &lp->evm_saa9730_regs->InterruptBlock1);
}

static void show_saa9730_regs(struct lan_saa9730_private *lp)
{
	int i, j;
	printk("TxmBufferA = %x\n", lp->TxmBuffer[0][0]);
	printk("TxmBufferB = %x\n", lp->TxmBuffer[1][0]);
	printk("RcvBufferA = %x\n", lp->RcvBuffer[0][0]);
	printk("RcvBufferB = %x\n", lp->RcvBuffer[1][0]);
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
	       INL(&lp->evm_saa9730_regs->InterruptBlock1));
	printk("lp->evm_saa9730_regs->InterruptStatus1 = %x\n",
	       INL(&lp->evm_saa9730_regs->InterruptStatus1));
	printk("lp->evm_saa9730_regs->InterruptEnable1 = %x\n",
	       INL(&lp->evm_saa9730_regs->InterruptEnable1));
	printk("lp->lan_saa9730_regs->Ok2Use = %x\n",
	       INL(&lp->lan_saa9730_regs->Ok2Use));
	printk("lp->NextTxmBufferIndex = %x\n", lp->NextTxmBufferIndex);
	printk("lp->NextTxmPacketIndex = %x\n", lp->NextTxmPacketIndex);
	printk("lp->PendingTxmBufferIndex = %x\n",
	       lp->PendingTxmBufferIndex);
	printk("lp->PendingTxmPacketIndex = %x\n",
	       lp->PendingTxmPacketIndex);
	printk("lp->lan_saa9730_regs->LanDmaCtl = %x\n",
	       INL(&lp->lan_saa9730_regs->LanDmaCtl));
	printk("lp->lan_saa9730_regs->DmaStatus = %x\n",
	       INL(&lp->lan_saa9730_regs->DmaStatus));
	printk("lp->lan_saa9730_regs->CamCtl = %x\n",
	       INL(&lp->lan_saa9730_regs->CamCtl));
	printk("lp->lan_saa9730_regs->TxCtl = %x\n",
	       INL(&lp->lan_saa9730_regs->TxCtl));
	printk("lp->lan_saa9730_regs->TxStatus = %x\n",
	       INL(&lp->lan_saa9730_regs->TxStatus));
	printk("lp->lan_saa9730_regs->RxCtl = %x\n",
	       INL(&lp->lan_saa9730_regs->RxCtl));
	printk("lp->lan_saa9730_regs->RxStatus = %x\n",
	       INL(&lp->lan_saa9730_regs->RxStatus));
	for (i = 0; i < LAN_SAA9730_CAM_DWORDS; i++) {
		OUTL(i, &lp->lan_saa9730_regs->CamAddress);
		printk("lp->lan_saa9730_regs->CamData = %x\n",
		       INL(&lp->lan_saa9730_regs->CamData));
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
	       INL(&lp->lan_saa9730_regs->DebugPCIMasterAddr));
	printk("lp->lan_saa9730_regs->DebugLanTxStateMachine = %x\n",
	       INL(&lp->lan_saa9730_regs->DebugLanTxStateMachine));
	printk("lp->lan_saa9730_regs->DebugLanRxStateMachine = %x\n",
	       INL(&lp->lan_saa9730_regs->DebugLanRxStateMachine));
	printk("lp->lan_saa9730_regs->DebugLanTxFifoPointers = %x\n",
	       INL(&lp->lan_saa9730_regs->DebugLanTxFifoPointers));
	printk("lp->lan_saa9730_regs->DebugLanRxFifoPointers = %x\n",
	       INL(&lp->lan_saa9730_regs->DebugLanRxFifoPointers));
	printk("lp->lan_saa9730_regs->DebugLanCtlStateMachine = %x\n",
	       INL(&lp->lan_saa9730_regs->DebugLanCtlStateMachine));
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

static int lan_saa9730_allocate_buffers(struct lan_saa9730_private *lp)
{
	unsigned int mem_size;
	void *Pa;
	unsigned int i, j, RcvBufferSize, TxmBufferSize;
	unsigned int buffer_start;

	/* 
	 * Allocate all RX and TX packets in one chunk. 
	 * The Rx and Tx packets must be PACKET_SIZE aligned.
	 */
	mem_size = ((LAN_SAA9730_RCV_Q_SIZE + LAN_SAA9730_TXM_Q_SIZE) *
		    LAN_SAA9730_PACKET_SIZE * LAN_SAA9730_BUFFERS) +
	    LAN_SAA9730_PACKET_SIZE;
	buffer_start =
	    (unsigned int) kmalloc(mem_size, GFP_DMA | GFP_KERNEL);

	if (!buffer_start)
		return -ENOMEM;

	/* 
	 * Set DMA buffer to kseg1 (uncached).
	 * Make sure to flush before using it uncached.
	 */
	Pa = (void *) KSEG1ADDR((buffer_start + LAN_SAA9730_PACKET_SIZE) &
				~(LAN_SAA9730_PACKET_SIZE - 1));
	dma_cache_wback_inv((unsigned long) Pa, mem_size);

	/* Initialize buffer space */
	RcvBufferSize = LAN_SAA9730_PACKET_SIZE;
	TxmBufferSize = LAN_SAA9730_PACKET_SIZE;
	lp->DmaRcvPackets = LAN_SAA9730_RCV_Q_SIZE;
	lp->DmaTxmPackets = LAN_SAA9730_TXM_Q_SIZE;

	/* Init RX buffers */
	for (i = 0; i < LAN_SAA9730_BUFFERS; i++) {
		for (j = 0; j < LAN_SAA9730_RCV_Q_SIZE; j++) {
			*(unsigned int *) Pa =
			    cpu_to_le32(RXSF_READY <<
					RX_STAT_CTL_OWNER_SHF);
			lp->RcvBuffer[i][j] = (unsigned int) Pa;
			Pa += RcvBufferSize;
		}
	}

	/* Init TX buffers */
	for (i = 0; i < LAN_SAA9730_BUFFERS; i++) {
		for (j = 0; j < LAN_SAA9730_TXM_Q_SIZE; j++) {
			*(unsigned int *) Pa =
			    cpu_to_le32(TXSF_EMPTY <<
					TX_STAT_CTL_OWNER_SHF);
			lp->TxmBuffer[i][j] = (unsigned int) Pa;
			Pa += TxmBufferSize;
		}
	}

	/* 
	 * Set rx buffer A and rx buffer B to point to the first two buffer 
	 * spaces.
	 */
	OUTL(PHYSADDR(lp->RcvBuffer[0][0]),
	     &lp->lan_saa9730_regs->RxBuffA);
	OUTL(PHYSADDR(lp->RcvBuffer[1][0]),
	     &lp->lan_saa9730_regs->RxBuffB);

	/* Initialize Buffer Index */
	lp->NextRcvPacketIndex = 0;
	lp->NextRcvToUseIsA = 1;

	/* Set current buffer index & next availble packet index */
	lp->NextTxmPacketIndex = 0;
	lp->NextTxmBufferIndex = 0;
	lp->PendingTxmPacketIndex = 0;
	lp->PendingTxmBufferIndex = 0;

	/* 
	 * Set txm_buf_a and txm_buf_b to point to the first two buffer
	 * space 
	 */
	OUTL(PHYSADDR(lp->TxmBuffer[0][0]),
	     &lp->lan_saa9730_regs->TxBuffA);
	OUTL(PHYSADDR(lp->TxmBuffer[1][0]),
	     &lp->lan_saa9730_regs->TxBuffB);

	/* Set packet number */
	OUTL((lp->DmaRcvPackets << PK_COUNT_RX_A_SHF) |
	     (lp->DmaRcvPackets << PK_COUNT_RX_B_SHF) |
	     (lp->DmaTxmPackets << PK_COUNT_TX_A_SHF) |
	     (lp->DmaTxmPackets << PK_COUNT_TX_B_SHF),
	     &lp->lan_saa9730_regs->PacketCount);

	return 0;
}

static int lan_saa9730_cam_load(struct lan_saa9730_private *lp)
{
	unsigned int i;
	unsigned char *NetworkAddress;

	NetworkAddress = (unsigned char *) &lp->PhysicalAddress[0][0];

	for (i = 0; i < LAN_SAA9730_CAM_DWORDS; i++) {
		/* First set address to where data is written */
		OUTL(i, &lp->lan_saa9730_regs->CamAddress);
		OUTL((NetworkAddress[0] << 24) | (NetworkAddress[1] << 16)
		     | (NetworkAddress[2] << 8) | NetworkAddress[3],
		     &lp->lan_saa9730_regs->CamData);
		NetworkAddress += 4;
	}
	return 0;
}

static int lan_saa9730_cam_init(struct net_device *dev)
{
	struct lan_saa9730_private *lp =
	    (struct lan_saa9730_private *) dev->priv;
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
	while (INL(&lp->lan_saa9730_regs->StationMgmtCtl) & MD_CA_BUSY) {
		i++;
		if (i > 100) {
			printk("Error: lan_saa9730_mii_init: timeout\n");
			return -1;
		}
		mdelay(1);	/* wait 1 ms. */
	}

	/* Now set the control and address register. */
	OUTL(MD_CA_BUSY | PHY_STATUS | PHY_ADDRESS << MD_CA_PHY_SHF,
	     &lp->lan_saa9730_regs->StationMgmtCtl);

	/* check link status, spin here till station is not busy */
	i = 0;
	while (INL(&lp->lan_saa9730_regs->StationMgmtCtl) & MD_CA_BUSY) {
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
	if (INL(&lp->lan_saa9730_regs->StationMgmtData) &
	    PHY_STATUS_LINK_UP) {
		/* Link is up. */
		return 0;
	} else {
		/* Link is down, reset the PHY first. */

		/* set PHY address = 'CONTROL' */
		OUTL(PHY_ADDRESS << MD_CA_PHY_SHF | MD_CA_WR | PHY_CONTROL,
		     &lp->lan_saa9730_regs->StationMgmtCtl);

		/* Wait for 1 ms. */
		mdelay(1);

		/* set 'CONTROL' = force reset and renegotiate */
		OUTL(PHY_CONTROL_RESET | PHY_CONTROL_AUTO_NEG |
		     PHY_CONTROL_RESTART_AUTO_NEG,
		     &lp->lan_saa9730_regs->StationMgmtData);

		/* Wait for 50 ms. */
		mdelay(50);

		/* set 'BUSY' to start operation */
		OUTL(MD_CA_BUSY | PHY_ADDRESS << MD_CA_PHY_SHF | MD_CA_WR |
		     PHY_CONTROL, &lp->lan_saa9730_regs->StationMgmtCtl);

		/* await completion */
		i = 0;
		while (INL(&lp->lan_saa9730_regs->StationMgmtCtl) &
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
			OUTL(MD_CA_BUSY | PHY_ADDRESS << MD_CA_PHY_SHF |
			     PHY_STATUS,
			     &lp->lan_saa9730_regs->StationMgmtCtl);

			/* await completion */
			i = 0;
			while (INL(&lp->lan_saa9730_regs->StationMgmtCtl) &
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
			if (INL(&lp->lan_saa9730_regs->StationMgmtData) &
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
	OUTL((LANMB_ANY << DMA_CTL_MAX_XFER_SHF) |
	     (LANEND_LITTLE << DMA_CTL_ENDIAN_SHF) |
	     (LAN_SAA9730_RCV_Q_INT_THRESHOLD << DMA_CTL_RX_INT_COUNT_SHF)
	     | DMA_CTL_RX_INT_TO_EN | DMA_CTL_RX_INT_EN |
	     DMA_CTL_MAC_RX_INT_EN | DMA_CTL_MAC_TX_INT_EN,
	     &lp->lan_saa9730_regs->LanDmaCtl);

	/* Initial MAC control register. */
	OUTL((MACCM_MII << MAC_CONTROL_CONN_SHF) | MAC_CONTROL_FULL_DUP,
	     &lp->lan_saa9730_regs->MacCtl);

	/* Initialize CAM control register. */
	OUTL(CAM_CONTROL_COMP_EN | CAM_CONTROL_BROAD_ACC,
	     &lp->lan_saa9730_regs->CamCtl);

	/* 
	 * Initialize CAM enable register, only turn on first entry, should
	 * contain own addr. 
	 */
	OUTL(0x0001, &lp->lan_saa9730_regs->CamEnable);

	/* Initialize Tx control register */
	OUTL(TX_CTL_EN_COMP, &lp->lan_saa9730_regs->TxCtl);

	/* Initialize Rcv control register */
	OUTL(RX_CTL_STRIP_CRC, &lp->lan_saa9730_regs->RxCtl);

	/* Reset DMA engine */
	OUTL(DMA_TEST_SW_RESET, &lp->lan_saa9730_regs->DmaTest);

	return 0;
}

static int lan_saa9730_stop(struct lan_saa9730_private *lp)
{
	int i;

	/* Stop DMA first */
	OUTL(INL(&lp->lan_saa9730_regs->LanDmaCtl) &
	     ~(DMA_CTL_EN_TX_DMA | DMA_CTL_EN_RX_DMA),
	     &lp->lan_saa9730_regs->LanDmaCtl);

	/* Set the SW Reset bits in DMA and MAC control registers */
	OUTL(DMA_TEST_SW_RESET, &lp->lan_saa9730_regs->DmaTest);
	OUTL(INL(&lp->lan_saa9730_regs->MacCtl) | MAC_CONTROL_RESET,
	     &lp->lan_saa9730_regs->MacCtl);

	/* 
	 * Wait for MAC reset to have finished. The reset bit is auto cleared
	 * when the reset is done.
	 */
	i = 0;
	while (INL(&lp->lan_saa9730_regs->MacCtl) & MAC_CONTROL_RESET) {
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

	OUTL(LAN_SAA9730_DEFAULT_TIME_OUT_CNT,
	     &lp->lan_saa9730_regs->Timeout);

	return 0;
}

static int lan_saa9730_start(struct lan_saa9730_private *lp)
{
	lan_saa9730_buffer_init(lp);

	/* Initialize Rx Buffer Index */
	lp->NextRcvPacketIndex = 0;
	lp->NextRcvToUseIsA = 1;

	/* Set current buffer index & next availble packet index */
	lp->NextTxmPacketIndex = 0;
	lp->NextTxmBufferIndex = 0;
	lp->PendingTxmPacketIndex = 0;
	lp->PendingTxmBufferIndex = 0;

	OUTL(INL(&lp->lan_saa9730_regs->LanDmaCtl) | DMA_CTL_EN_TX_DMA |
	     DMA_CTL_EN_RX_DMA, &lp->lan_saa9730_regs->LanDmaCtl);

	/* For Tx, turn on MAC then DMA */
	OUTL(INL(&lp->lan_saa9730_regs->TxCtl) | TX_CTL_TX_EN,
	     &lp->lan_saa9730_regs->TxCtl);

	/* For Rx, turn on DMA then MAC */
	OUTL(INL(&lp->lan_saa9730_regs->RxCtl) | RX_CTL_RX_EN,
	     &lp->lan_saa9730_regs->RxCtl);

	/* Set Ok2Use to let hardware owns the buffers */
	OUTL(OK2USE_RX_A | OK2USE_RX_B | OK2USE_TX_A | OK2USE_TX_B,
	     &lp->lan_saa9730_regs->Ok2Use);

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
	struct lan_saa9730_private *lp =
	    (struct lan_saa9730_private *) dev->priv;
	unsigned int *pPacket;
	unsigned int tx_status;

	if (lan_saa9730_debug > 5)
		printk("lan_saa9730_tx interrupt\n");

	/* Clear interrupt. */
	OUTL(DMA_STATUS_MAC_TX_INT, &lp->lan_saa9730_regs->DmaStatus);

	while (1) {
		pPacket =
		    (unsigned int *) lp->TxmBuffer[lp->
						   PendingTxmBufferIndex]
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
			    (TX_STATUS_LATE_COLL <<
			     TX_STAT_CTL_STATUS_SHF)) lp->stats.
	     tx_window_errors++;
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

	/* Make sure A and B are available to hardware. */
	OUTL(OK2USE_TX_A | OK2USE_TX_B, &lp->lan_saa9730_regs->Ok2Use);

	if (netif_queue_stopped(dev)) {
		/* The tx buffer is no longer full. */
		netif_wake_queue(dev);
	}

	return 0;
}

static int lan_saa9730_rx(struct net_device *dev)
{
	struct lan_saa9730_private *lp =
	    (struct lan_saa9730_private *) dev->priv;
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
	OUTL(DMA_STATUS_MAC_RX_INT | DMA_STATUS_RX_INT |
	     DMA_STATUS_RX_TO_INT, &lp->lan_saa9730_regs->DmaStatus);

	/* Address next packet */
	if (lp->NextRcvToUseIsA)
		BufferIndex = 0;
	else
		BufferIndex = 1;
	PacketIndex = lp->NextRcvPacketIndex;
	pPacket = (unsigned int *) lp->RcvBuffer[BufferIndex][PacketIndex];
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
			    (RX_STATUS_ALIGN_ERR <<
			     RX_STAT_CTL_STATUS_SHF)) lp->stats.
	     rx_frame_errors++;
			if (rx_status &
			    (RX_STATUS_OVERFLOW << RX_STAT_CTL_STATUS_SHF))
				    lp->stats.rx_fifo_errors++;
			if (rx_status &
			    (RX_STATUS_LONG_ERR << RX_STAT_CTL_STATUS_SHF))
				    lp->stats.rx_length_errors++;
		}

		/* Indicate we have processed the buffer. */
		*pPacket =
		    cpu_to_le32(RXSF_READY << RX_STAT_CTL_OWNER_SHF);

		/* Go to next packet in sequence. */
		lp->NextRcvPacketIndex++;
		if (lp->NextRcvPacketIndex >= LAN_SAA9730_RCV_Q_SIZE) {
			lp->NextRcvPacketIndex = 0;
			if (BufferIndex) {
				lp->NextRcvToUseIsA = 1;
			} else {
				lp->NextRcvToUseIsA = 0;
			}
		}
		OUTL(OK2USE_RX_A | OK2USE_RX_B,
		     &lp->lan_saa9730_regs->Ok2Use);

		/* Address next packet */
		if (lp->NextRcvToUseIsA)
			BufferIndex = 0;
		else
			BufferIndex = 1;
		PacketIndex = lp->NextRcvPacketIndex;
		pPacket =
		    (unsigned int *) lp->
		    RcvBuffer[BufferIndex][PacketIndex];
		rx_status = le32_to_cpu(*pPacket);
	}

	/* Make sure A and B are available to hardware. */
	OUTL(OK2USE_RX_A | OK2USE_RX_B, &lp->lan_saa9730_regs->Ok2Use);

	return 0;
}

static irqreturn_t lan_saa9730_interrupt(const int irq, void *dev_id,
				  struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct lan_saa9730_private *lp =
	    (struct lan_saa9730_private *) dev->priv;

	if (lan_saa9730_debug > 5)
		printk("lan_saa9730_interrupt\n");

	/* Disable the EVM LAN interrupt. */
	evm_saa9730_block_lan_int(lp);

	/* Clear the EVM LAN interrupt. */
	evm_saa9730_clear_lan_int(lp);

	/* Service pending transmit interrupts. */
	if (INL(&lp->lan_saa9730_regs->DmaStatus) & DMA_STATUS_MAC_TX_INT)
		lan_saa9730_tx(dev);

	/* Service pending receive interrupts. */
	if (INL(&lp->lan_saa9730_regs->DmaStatus) &
	    (DMA_STATUS_MAC_RX_INT | DMA_STATUS_RX_INT |
	     DMA_STATUS_RX_TO_INT)) lan_saa9730_rx(dev);

	/* Enable the EVM LAN interrupt. */
	evm_saa9730_unblock_lan_int(lp);

	return IRQ_HANDLED;
}

static int lan_saa9730_open_fail(struct net_device *dev)
{
	return -ENODEV;
}

static int lan_saa9730_open(struct net_device *dev)
{
	struct lan_saa9730_private *lp =
	    (struct lan_saa9730_private *) dev->priv;

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
		printk("lan_saa9730_write: skb=%08x\n",
		       (unsigned int) skb);

	BufferIndex = lp->NextTxmBufferIndex;
	PacketIndex = lp->NextTxmPacketIndex;

	tx_status =
	    le32_to_cpu(*(unsigned int *) lp->
			TxmBuffer[BufferIndex][PacketIndex]);
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

	pbPacketData =
	    (unsigned char *) lp->TxmBuffer[BufferIndex][PacketIndex];
	pbPacketData += 4;

	/* copy the bits */
	memcpy(pbPacketData, pbData, len);

	/* Set transmit status for hardware */
	*(unsigned int *) lp->TxmBuffer[BufferIndex][PacketIndex] =
	    cpu_to_le32((TXSF_READY << TX_STAT_CTL_OWNER_SHF) |
			(TX_STAT_CTL_INT_AFTER_TX << TX_STAT_CTL_FRAME_SHF)
			| (len << TX_STAT_CTL_LENGTH_SHF));

	/* Set hardware tx buffer. */
	OUTL(OK2USE_TX_A | OK2USE_TX_B, &lp->lan_saa9730_regs->Ok2Use);

	return 0;
}

static void lan_saa9730_tx_timeout(struct net_device *dev)
{
	struct lan_saa9730_private *lp =
	    (struct lan_saa9730_private *) dev->priv;

	/* Transmitter timeout, serious problems */
	lp->stats.tx_errors++;
	printk("%s: transmit timed out, reset\n", dev->name);
	/*show_saa9730_regs(lp); */
	lan_saa9730_restart(lp);

	dev->trans_start = jiffies;
	netif_start_queue(dev);
}

static int lan_saa9730_start_xmit(struct sk_buff *skb,
				  struct net_device *dev)
{
	struct lan_saa9730_private *lp =
	    (struct lan_saa9730_private *) dev->priv;
	unsigned long flags;
	int skblen;
	int len;

	if (lan_saa9730_debug > 4)
		printk("Send packet: skb=%08x\n", (unsigned int) skb);

	skblen = skb->len;

	spin_lock_irqsave(&lp->lock, flags);

	len = (skblen <= ETH_ZLEN) ? ETH_ZLEN : skblen;

	if (lan_saa9730_write(lp, skb, skblen)) {
		spin_unlock_irqrestore(&lp->lock, flags);
		printk("Error when writing packet to controller: skb=%08x\n",
		     (unsigned int) skb);
		netif_stop_queue(dev);
		return -1;
	}

	lp->stats.tx_bytes += len;
	lp->stats.tx_packets++;

	dev->trans_start = jiffies;
	netif_start_queue(dev);
	dev_kfree_skb(skb);

	spin_unlock_irqrestore(&lp->lock, flags);

	return 0;
}

static int lan_saa9730_close(struct net_device *dev)
{
	struct lan_saa9730_private *lp =
	    (struct lan_saa9730_private *) dev->priv;

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
	struct lan_saa9730_private *lp =
	    (struct lan_saa9730_private *) dev->priv;

	return &lp->stats;
}

static void lan_saa9730_set_multicast(struct net_device *dev)
{
	struct lan_saa9730_private *lp =
	    (struct lan_saa9730_private *) dev->priv;

	/* Stop the controller */
	lan_saa9730_stop(lp);

	if (dev->flags & IFF_PROMISC) {
		/* accept all packets */
		OUTL(CAM_CONTROL_COMP_EN | CAM_CONTROL_STATION_ACC |
		     CAM_CONTROL_GROUP_ACC | CAM_CONTROL_BROAD_ACC,
		     &lp->lan_saa9730_regs->CamCtl);
	} else {
		if (dev->flags & IFF_ALLMULTI) {
			/* accept all multicast packets */
			OUTL(CAM_CONTROL_COMP_EN | CAM_CONTROL_GROUP_ACC |
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

        if (dev) {
                unregister_netdev(dev);

		if (dev->priv)
			kfree(dev->priv);

                free_netdev(dev);
                pci_release_regions(pdev);
                pci_disable_device(pdev);
                pci_set_drvdata(pdev, NULL);
        }
}


static int lan_saa9730_init(struct net_device *dev, int ioaddr, int irq)
{
	struct lan_saa9730_private *lp;
	unsigned char ethernet_addr[6];
	int ret = 0;

	dev->open = lan_saa9730_open_fail;

	if (get_ethernet_addr(ethernet_addr))
		return -ENODEV;
	
	memcpy(dev->dev_addr, ethernet_addr, 6);
	dev->base_addr = ioaddr;
	dev->irq = irq;
	
	/* 
	 * Make certain the data structures used by the controller are aligned 
	 * and DMAble. 
	 */
	/*
	 *  XXX: that is obviously broken - kfree() won't be happy with us.
	 */
	lp = (struct lan_saa9730_private *) (((unsigned long)
					      kmalloc(sizeof(*lp) + 7,
						      GFP_DMA | GFP_KERNEL)
					      + 7) & ~7);

	if (!lp)
		return -ENOMEM;

	dev->priv = lp;
	memset(lp, 0, sizeof(*lp));

	/* Set SAA9730 LAN base address. */
	lp->lan_saa9730_regs = (t_lan_saa9730_regmap *) (ioaddr +
							 SAA9730_LAN_REGS_ADDR);

	/* Set SAA9730 EVM base address. */
	lp->evm_saa9730_regs = (t_evm_saa9730_regmap *) (ioaddr +
							 SAA9730_EVM_REGS_ADDR);

	/* Allocate LAN RX/TX frame buffer space. */
	/* FIXME: a leak */
	if ((ret = lan_saa9730_allocate_buffers(lp)))
		goto out;

	/* Stop LAN controller. */
	if ((ret = lan_saa9730_stop(lp))) 
		goto out;
	
	/* Initialize CAM registers. */
	if ((ret = lan_saa9730_cam_init(dev)))
		goto out;

	/* Initialize MII registers. */
	if ((ret = lan_saa9730_mii_init(lp)))
		goto out;

	/* Initialize control registers. */
	if ((ret = lan_saa9730_control_init(lp))) 
		goto out;
        
	/* Load CAM registers. */
	if ((ret = lan_saa9730_cam_load(lp))) 
		goto out;
	
	/* Initialize DMA context registers. */
	if ((ret = lan_saa9730_dma_init(lp)))
		goto out;
	
	spin_lock_init(&lp->lock);
		
	dev->open = lan_saa9730_open;
	dev->hard_start_xmit = lan_saa9730_start_xmit;
	dev->stop = lan_saa9730_close;
	dev->get_stats = lan_saa9730_get_stats;
	dev->set_multicast_list = lan_saa9730_set_multicast;
	dev->tx_timeout = lan_saa9730_tx_timeout;
	dev->watchdog_timeo = (HZ >> 1);
	dev->dma = 0;
	
	ret = register_netdev(dev);
	if (ret)
		goto out;
	return 0;

 out:
	if (dev->priv)
		kfree(dev->priv);
	return ret;
}


static int __devinit saa9730_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *dev;
	unsigned int pci_ioaddr;
	int err;

	if (lan_saa9730_debug > 1)
		printk("saa9730.c: PCI bios is present, checking for devices...\n");

	err = -ENOMEM;
	dev = alloc_etherdev(0);
	if (!dev)
		goto out;

	SET_MODULE_OWNER(dev);

	err = pci_enable_device(pdev);
        if (err) {
                printk(KERN_ERR "Cannot enable PCI device, aborting.\n");
                goto out1;
        }

	err = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (err) {
		printk(KERN_ERR "Cannot obtain PCI resources, aborting.\n");
		goto out2;
	}

	pci_irq_line = pdev->irq;
	/* LAN base address in located at BAR 1. */

	pci_ioaddr = pci_resource_start(pdev, 1);
	pci_set_master(pdev);

	printk("Found SAA9730 (PCI) at %#x, irq %d.\n",
	       pci_ioaddr, pci_irq_line);

	err = lan_saa9730_init(dev, pci_ioaddr, pci_irq_line);
	if (err) {
		printk("Lan init failed");
		goto out2;
	}

	pci_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);
	return 0;
	
out2:
	pci_disable_device(pdev);
out1:
	free_netdev(dev);
out:
	return err;
}


static struct pci_driver saa9730_driver = {
	.name           = DRV_MODULE_NAME,
	.id_table       = saa9730_pci_tbl,
	.probe          = saa9730_init_one,
	.remove         = __devexit_p(saa9730_remove_one),
};


static int __init saa9730_init(void)
{
        return pci_module_init(&saa9730_driver);
}

static void __exit saa9730_cleanup(void)
{
        pci_unregister_driver(&saa9730_driver);
}

module_init(saa9730_init);
module_exit(saa9730_cleanup);



MODULE_LICENSE("GPL");
