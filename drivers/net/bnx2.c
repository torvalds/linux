/* bnx2.c: Broadcom NX2 network driver.
 *
 * Copyright (c) 2004-2008 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Michael Chan  (mchan@broadcom.com)
 */


#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/bitops.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include <asm/page.h>
#include <linux/time.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
#define BCM_VLAN 1
#endif
#include <net/ip.h>
#include <net/tcp.h>
#include <net/checksum.h>
#include <linux/workqueue.h>
#include <linux/crc32.h>
#include <linux/prefetch.h>
#include <linux/cache.h>
#include <linux/zlib.h>
#include <linux/log2.h>

#include "bnx2.h"
#include "bnx2_fw.h"
#include "bnx2_fw2.h"

#define FW_BUF_SIZE		0x10000

#define DRV_MODULE_NAME		"bnx2"
#define PFX DRV_MODULE_NAME	": "
#define DRV_MODULE_VERSION	"1.8.1"
#define DRV_MODULE_RELDATE	"Oct 7, 2008"

#define RUN_AT(x) (jiffies + (x))

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (5*HZ)

static char version[] __devinitdata =
	"Broadcom NetXtreme II Gigabit Ethernet Driver " DRV_MODULE_NAME " v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";

MODULE_AUTHOR("Michael Chan <mchan@broadcom.com>");
MODULE_DESCRIPTION("Broadcom NetXtreme II BCM5706/5708/5709/5716 Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

static int disable_msi = 0;

module_param(disable_msi, int, 0);
MODULE_PARM_DESC(disable_msi, "Disable Message Signaled Interrupt (MSI)");

typedef enum {
	BCM5706 = 0,
	NC370T,
	NC370I,
	BCM5706S,
	NC370F,
	BCM5708,
	BCM5708S,
	BCM5709,
	BCM5709S,
	BCM5716,
} board_t;

/* indexed by board_t, above */
static struct {
	char *name;
} board_info[] __devinitdata = {
	{ "Broadcom NetXtreme II BCM5706 1000Base-T" },
	{ "HP NC370T Multifunction Gigabit Server Adapter" },
	{ "HP NC370i Multifunction Gigabit Server Adapter" },
	{ "Broadcom NetXtreme II BCM5706 1000Base-SX" },
	{ "HP NC370F Multifunction Gigabit Server Adapter" },
	{ "Broadcom NetXtreme II BCM5708 1000Base-T" },
	{ "Broadcom NetXtreme II BCM5708 1000Base-SX" },
	{ "Broadcom NetXtreme II BCM5709 1000Base-T" },
	{ "Broadcom NetXtreme II BCM5709 1000Base-SX" },
	{ "Broadcom NetXtreme II BCM5716 1000Base-T" },
	};

static DEFINE_PCI_DEVICE_TABLE(bnx2_pci_tbl) = {
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5706,
	  PCI_VENDOR_ID_HP, 0x3101, 0, 0, NC370T },
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5706,
	  PCI_VENDOR_ID_HP, 0x3106, 0, 0, NC370I },
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5706,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM5706 },
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5708,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM5708 },
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5706S,
	  PCI_VENDOR_ID_HP, 0x3102, 0, 0, NC370F },
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5706S,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM5706S },
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5708S,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM5708S },
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5709,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM5709 },
	{ PCI_VENDOR_ID_BROADCOM, PCI_DEVICE_ID_NX2_5709S,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM5709S },
	{ PCI_VENDOR_ID_BROADCOM, 0x163b,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, BCM5716 },
	{ 0, }
};

static struct flash_spec flash_table[] =
{
#define BUFFERED_FLAGS		(BNX2_NV_BUFFERED | BNX2_NV_TRANSLATE)
#define NONBUFFERED_FLAGS	(BNX2_NV_WREN)
	/* Slow EEPROM */
	{0x00000000, 0x40830380, 0x009f0081, 0xa184a053, 0xaf000400,
	 BUFFERED_FLAGS, SEEPROM_PAGE_BITS, SEEPROM_PAGE_SIZE,
	 SEEPROM_BYTE_ADDR_MASK, SEEPROM_TOTAL_SIZE,
	 "EEPROM - slow"},
	/* Expansion entry 0001 */
	{0x08000002, 0x4b808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 0001"},
	/* Saifun SA25F010 (non-buffered flash) */
	/* strap, cfg1, & write1 need updates */
	{0x04000001, 0x47808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, SAIFUN_FLASH_BASE_TOTAL_SIZE*2,
	 "Non-buffered flash (128kB)"},
	/* Saifun SA25F020 (non-buffered flash) */
	/* strap, cfg1, & write1 need updates */
	{0x0c000003, 0x4f808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, SAIFUN_FLASH_BASE_TOTAL_SIZE*4,
	 "Non-buffered flash (256kB)"},
	/* Expansion entry 0100 */
	{0x11000000, 0x53808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 0100"},
	/* Entry 0101: ST M45PE10 (non-buffered flash, TetonII B0) */
	{0x19000002, 0x5b808201, 0x000500db, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, ST_MICRO_FLASH_PAGE_BITS, ST_MICRO_FLASH_PAGE_SIZE,
	 ST_MICRO_FLASH_BYTE_ADDR_MASK, ST_MICRO_FLASH_BASE_TOTAL_SIZE*2,
	 "Entry 0101: ST M45PE10 (128kB non-bufferred)"},
	/* Entry 0110: ST M45PE20 (non-buffered flash)*/
	{0x15000001, 0x57808201, 0x000500db, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, ST_MICRO_FLASH_PAGE_BITS, ST_MICRO_FLASH_PAGE_SIZE,
	 ST_MICRO_FLASH_BYTE_ADDR_MASK, ST_MICRO_FLASH_BASE_TOTAL_SIZE*4,
	 "Entry 0110: ST M45PE20 (256kB non-bufferred)"},
	/* Saifun SA25F005 (non-buffered flash) */
	/* strap, cfg1, & write1 need updates */
	{0x1d000003, 0x5f808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, SAIFUN_FLASH_BASE_TOTAL_SIZE,
	 "Non-buffered flash (64kB)"},
	/* Fast EEPROM */
	{0x22000000, 0x62808380, 0x009f0081, 0xa184a053, 0xaf000400,
	 BUFFERED_FLAGS, SEEPROM_PAGE_BITS, SEEPROM_PAGE_SIZE,
	 SEEPROM_BYTE_ADDR_MASK, SEEPROM_TOTAL_SIZE,
	 "EEPROM - fast"},
	/* Expansion entry 1001 */
	{0x2a000002, 0x6b808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1001"},
	/* Expansion entry 1010 */
	{0x26000001, 0x67808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1010"},
	/* ATMEL AT45DB011B (buffered flash) */
	{0x2e000003, 0x6e808273, 0x00570081, 0x68848353, 0xaf000400,
	 BUFFERED_FLAGS, BUFFERED_FLASH_PAGE_BITS, BUFFERED_FLASH_PAGE_SIZE,
	 BUFFERED_FLASH_BYTE_ADDR_MASK, BUFFERED_FLASH_TOTAL_SIZE,
	 "Buffered flash (128kB)"},
	/* Expansion entry 1100 */
	{0x33000000, 0x73808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1100"},
	/* Expansion entry 1101 */
	{0x3b000002, 0x7b808201, 0x00050081, 0x03840253, 0xaf020406,
	 NONBUFFERED_FLAGS, SAIFUN_FLASH_PAGE_BITS, SAIFUN_FLASH_PAGE_SIZE,
	 SAIFUN_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1101"},
	/* Ateml Expansion entry 1110 */
	{0x37000001, 0x76808273, 0x00570081, 0x68848353, 0xaf000400,
	 BUFFERED_FLAGS, BUFFERED_FLASH_PAGE_BITS, BUFFERED_FLASH_PAGE_SIZE,
	 BUFFERED_FLASH_BYTE_ADDR_MASK, 0,
	 "Entry 1110 (Atmel)"},
	/* ATMEL AT45DB021B (buffered flash) */
	{0x3f000003, 0x7e808273, 0x00570081, 0x68848353, 0xaf000400,
	 BUFFERED_FLAGS, BUFFERED_FLASH_PAGE_BITS, BUFFERED_FLASH_PAGE_SIZE,
	 BUFFERED_FLASH_BYTE_ADDR_MASK, BUFFERED_FLASH_TOTAL_SIZE*2,
	 "Buffered flash (256kB)"},
};

static struct flash_spec flash_5709 = {
	.flags		= BNX2_NV_BUFFERED,
	.page_bits	= BCM5709_FLASH_PAGE_BITS,
	.page_size	= BCM5709_FLASH_PAGE_SIZE,
	.addr_mask	= BCM5709_FLASH_BYTE_ADDR_MASK,
	.total_size	= BUFFERED_FLASH_TOTAL_SIZE*2,
	.name		= "5709 Buffered flash (256kB)",
};

MODULE_DEVICE_TABLE(pci, bnx2_pci_tbl);

static inline u32 bnx2_tx_avail(struct bnx2 *bp, struct bnx2_tx_ring_info *txr)
{
	u32 diff;

	smp_mb();

	/* The ring uses 256 indices for 255 entries, one of them
	 * needs to be skipped.
	 */
	diff = txr->tx_prod - txr->tx_cons;
	if (unlikely(diff >= TX_DESC_CNT)) {
		diff &= 0xffff;
		if (diff == TX_DESC_CNT)
			diff = MAX_TX_DESC_CNT;
	}
	return (bp->tx_ring_size - diff);
}

static u32
bnx2_reg_rd_ind(struct bnx2 *bp, u32 offset)
{
	u32 val;

	spin_lock_bh(&bp->indirect_lock);
	REG_WR(bp, BNX2_PCICFG_REG_WINDOW_ADDRESS, offset);
	val = REG_RD(bp, BNX2_PCICFG_REG_WINDOW);
	spin_unlock_bh(&bp->indirect_lock);
	return val;
}

static void
bnx2_reg_wr_ind(struct bnx2 *bp, u32 offset, u32 val)
{
	spin_lock_bh(&bp->indirect_lock);
	REG_WR(bp, BNX2_PCICFG_REG_WINDOW_ADDRESS, offset);
	REG_WR(bp, BNX2_PCICFG_REG_WINDOW, val);
	spin_unlock_bh(&bp->indirect_lock);
}

static void
bnx2_shmem_wr(struct bnx2 *bp, u32 offset, u32 val)
{
	bnx2_reg_wr_ind(bp, bp->shmem_base + offset, val);
}

static u32
bnx2_shmem_rd(struct bnx2 *bp, u32 offset)
{
	return (bnx2_reg_rd_ind(bp, bp->shmem_base + offset));
}

static void
bnx2_ctx_wr(struct bnx2 *bp, u32 cid_addr, u32 offset, u32 val)
{
	offset += cid_addr;
	spin_lock_bh(&bp->indirect_lock);
	if (CHIP_NUM(bp) == CHIP_NUM_5709) {
		int i;

		REG_WR(bp, BNX2_CTX_CTX_DATA, val);
		REG_WR(bp, BNX2_CTX_CTX_CTRL,
		       offset | BNX2_CTX_CTX_CTRL_WRITE_REQ);
		for (i = 0; i < 5; i++) {
			val = REG_RD(bp, BNX2_CTX_CTX_CTRL);
			if ((val & BNX2_CTX_CTX_CTRL_WRITE_REQ) == 0)
				break;
			udelay(5);
		}
	} else {
		REG_WR(bp, BNX2_CTX_DATA_ADR, offset);
		REG_WR(bp, BNX2_CTX_DATA, val);
	}
	spin_unlock_bh(&bp->indirect_lock);
}

static int
bnx2_read_phy(struct bnx2 *bp, u32 reg, u32 *val)
{
	u32 val1;
	int i, ret;

	if (bp->phy_flags & BNX2_PHY_FLAG_INT_MODE_AUTO_POLLING) {
		val1 = REG_RD(bp, BNX2_EMAC_MDIO_MODE);
		val1 &= ~BNX2_EMAC_MDIO_MODE_AUTO_POLL;

		REG_WR(bp, BNX2_EMAC_MDIO_MODE, val1);
		REG_RD(bp, BNX2_EMAC_MDIO_MODE);

		udelay(40);
	}

	val1 = (bp->phy_addr << 21) | (reg << 16) |
		BNX2_EMAC_MDIO_COMM_COMMAND_READ | BNX2_EMAC_MDIO_COMM_DISEXT |
		BNX2_EMAC_MDIO_COMM_START_BUSY;
	REG_WR(bp, BNX2_EMAC_MDIO_COMM, val1);

	for (i = 0; i < 50; i++) {
		udelay(10);

		val1 = REG_RD(bp, BNX2_EMAC_MDIO_COMM);
		if (!(val1 & BNX2_EMAC_MDIO_COMM_START_BUSY)) {
			udelay(5);

			val1 = REG_RD(bp, BNX2_EMAC_MDIO_COMM);
			val1 &= BNX2_EMAC_MDIO_COMM_DATA;

			break;
		}
	}

	if (val1 & BNX2_EMAC_MDIO_COMM_START_BUSY) {
		*val = 0x0;
		ret = -EBUSY;
	}
	else {
		*val = val1;
		ret = 0;
	}

	if (bp->phy_flags & BNX2_PHY_FLAG_INT_MODE_AUTO_POLLING) {
		val1 = REG_RD(bp, BNX2_EMAC_MDIO_MODE);
		val1 |= BNX2_EMAC_MDIO_MODE_AUTO_POLL;

		REG_WR(bp, BNX2_EMAC_MDIO_MODE, val1);
		REG_RD(bp, BNX2_EMAC_MDIO_MODE);

		udelay(40);
	}

	return ret;
}

static int
bnx2_write_phy(struct bnx2 *bp, u32 reg, u32 val)
{
	u32 val1;
	int i, ret;

	if (bp->phy_flags & BNX2_PHY_FLAG_INT_MODE_AUTO_POLLING) {
		val1 = REG_RD(bp, BNX2_EMAC_MDIO_MODE);
		val1 &= ~BNX2_EMAC_MDIO_MODE_AUTO_POLL;

		REG_WR(bp, BNX2_EMAC_MDIO_MODE, val1);
		REG_RD(bp, BNX2_EMAC_MDIO_MODE);

		udelay(40);
	}

	val1 = (bp->phy_addr << 21) | (reg << 16) | val |
		BNX2_EMAC_MDIO_COMM_COMMAND_WRITE |
		BNX2_EMAC_MDIO_COMM_START_BUSY | BNX2_EMAC_MDIO_COMM_DISEXT;
	REG_WR(bp, BNX2_EMAC_MDIO_COMM, val1);

	for (i = 0; i < 50; i++) {
		udelay(10);

		val1 = REG_RD(bp, BNX2_EMAC_MDIO_COMM);
		if (!(val1 & BNX2_EMAC_MDIO_COMM_START_BUSY)) {
			udelay(5);
			break;
		}
	}

	if (val1 & BNX2_EMAC_MDIO_COMM_START_BUSY)
        	ret = -EBUSY;
	else
		ret = 0;

	if (bp->phy_flags & BNX2_PHY_FLAG_INT_MODE_AUTO_POLLING) {
		val1 = REG_RD(bp, BNX2_EMAC_MDIO_MODE);
		val1 |= BNX2_EMAC_MDIO_MODE_AUTO_POLL;

		REG_WR(bp, BNX2_EMAC_MDIO_MODE, val1);
		REG_RD(bp, BNX2_EMAC_MDIO_MODE);

		udelay(40);
	}

	return ret;
}

static void
bnx2_disable_int(struct bnx2 *bp)
{
	int i;
	struct bnx2_napi *bnapi;

	for (i = 0; i < bp->irq_nvecs; i++) {
		bnapi = &bp->bnx2_napi[i];
		REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD, bnapi->int_num |
		       BNX2_PCICFG_INT_ACK_CMD_MASK_INT);
	}
	REG_RD(bp, BNX2_PCICFG_INT_ACK_CMD);
}

static void
bnx2_enable_int(struct bnx2 *bp)
{
	int i;
	struct bnx2_napi *bnapi;

	for (i = 0; i < bp->irq_nvecs; i++) {
		bnapi = &bp->bnx2_napi[i];

		REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD, bnapi->int_num |
		       BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID |
		       BNX2_PCICFG_INT_ACK_CMD_MASK_INT |
		       bnapi->last_status_idx);

		REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD, bnapi->int_num |
		       BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID |
		       bnapi->last_status_idx);
	}
	REG_WR(bp, BNX2_HC_COMMAND, bp->hc_cmd | BNX2_HC_COMMAND_COAL_NOW);
}

static void
bnx2_disable_int_sync(struct bnx2 *bp)
{
	int i;

	atomic_inc(&bp->intr_sem);
	bnx2_disable_int(bp);
	for (i = 0; i < bp->irq_nvecs; i++)
		synchronize_irq(bp->irq_tbl[i].vector);
}

static void
bnx2_napi_disable(struct bnx2 *bp)
{
	int i;

	for (i = 0; i < bp->irq_nvecs; i++)
		napi_disable(&bp->bnx2_napi[i].napi);
}

static void
bnx2_napi_enable(struct bnx2 *bp)
{
	int i;

	for (i = 0; i < bp->irq_nvecs; i++)
		napi_enable(&bp->bnx2_napi[i].napi);
}

static void
bnx2_netif_stop(struct bnx2 *bp)
{
	bnx2_disable_int_sync(bp);
	if (netif_running(bp->dev)) {
		bnx2_napi_disable(bp);
		netif_tx_disable(bp->dev);
		bp->dev->trans_start = jiffies;	/* prevent tx timeout */
	}
}

static void
bnx2_netif_start(struct bnx2 *bp)
{
	if (atomic_dec_and_test(&bp->intr_sem)) {
		if (netif_running(bp->dev)) {
			netif_tx_wake_all_queues(bp->dev);
			bnx2_napi_enable(bp);
			bnx2_enable_int(bp);
		}
	}
}

static void
bnx2_free_tx_mem(struct bnx2 *bp)
{
	int i;

	for (i = 0; i < bp->num_tx_rings; i++) {
		struct bnx2_napi *bnapi = &bp->bnx2_napi[i];
		struct bnx2_tx_ring_info *txr = &bnapi->tx_ring;

		if (txr->tx_desc_ring) {
			pci_free_consistent(bp->pdev, TXBD_RING_SIZE,
					    txr->tx_desc_ring,
					    txr->tx_desc_mapping);
			txr->tx_desc_ring = NULL;
		}
		kfree(txr->tx_buf_ring);
		txr->tx_buf_ring = NULL;
	}
}

static void
bnx2_free_rx_mem(struct bnx2 *bp)
{
	int i;

	for (i = 0; i < bp->num_rx_rings; i++) {
		struct bnx2_napi *bnapi = &bp->bnx2_napi[i];
		struct bnx2_rx_ring_info *rxr = &bnapi->rx_ring;
		int j;

		for (j = 0; j < bp->rx_max_ring; j++) {
			if (rxr->rx_desc_ring[j])
				pci_free_consistent(bp->pdev, RXBD_RING_SIZE,
						    rxr->rx_desc_ring[j],
						    rxr->rx_desc_mapping[j]);
			rxr->rx_desc_ring[j] = NULL;
		}
		if (rxr->rx_buf_ring)
			vfree(rxr->rx_buf_ring);
		rxr->rx_buf_ring = NULL;

		for (j = 0; j < bp->rx_max_pg_ring; j++) {
			if (rxr->rx_pg_desc_ring[j])
				pci_free_consistent(bp->pdev, RXBD_RING_SIZE,
						    rxr->rx_pg_desc_ring[i],
						    rxr->rx_pg_desc_mapping[i]);
			rxr->rx_pg_desc_ring[i] = NULL;
		}
		if (rxr->rx_pg_ring)
			vfree(rxr->rx_pg_ring);
		rxr->rx_pg_ring = NULL;
	}
}

static int
bnx2_alloc_tx_mem(struct bnx2 *bp)
{
	int i;

	for (i = 0; i < bp->num_tx_rings; i++) {
		struct bnx2_napi *bnapi = &bp->bnx2_napi[i];
		struct bnx2_tx_ring_info *txr = &bnapi->tx_ring;

		txr->tx_buf_ring = kzalloc(SW_TXBD_RING_SIZE, GFP_KERNEL);
		if (txr->tx_buf_ring == NULL)
			return -ENOMEM;

		txr->tx_desc_ring =
			pci_alloc_consistent(bp->pdev, TXBD_RING_SIZE,
					     &txr->tx_desc_mapping);
		if (txr->tx_desc_ring == NULL)
			return -ENOMEM;
	}
	return 0;
}

static int
bnx2_alloc_rx_mem(struct bnx2 *bp)
{
	int i;

	for (i = 0; i < bp->num_rx_rings; i++) {
		struct bnx2_napi *bnapi = &bp->bnx2_napi[i];
		struct bnx2_rx_ring_info *rxr = &bnapi->rx_ring;
		int j;

		rxr->rx_buf_ring =
			vmalloc(SW_RXBD_RING_SIZE * bp->rx_max_ring);
		if (rxr->rx_buf_ring == NULL)
			return -ENOMEM;

		memset(rxr->rx_buf_ring, 0,
		       SW_RXBD_RING_SIZE * bp->rx_max_ring);

		for (j = 0; j < bp->rx_max_ring; j++) {
			rxr->rx_desc_ring[j] =
				pci_alloc_consistent(bp->pdev, RXBD_RING_SIZE,
						     &rxr->rx_desc_mapping[j]);
			if (rxr->rx_desc_ring[j] == NULL)
				return -ENOMEM;

		}

		if (bp->rx_pg_ring_size) {
			rxr->rx_pg_ring = vmalloc(SW_RXPG_RING_SIZE *
						  bp->rx_max_pg_ring);
			if (rxr->rx_pg_ring == NULL)
				return -ENOMEM;

			memset(rxr->rx_pg_ring, 0, SW_RXPG_RING_SIZE *
			       bp->rx_max_pg_ring);
		}

		for (j = 0; j < bp->rx_max_pg_ring; j++) {
			rxr->rx_pg_desc_ring[j] =
				pci_alloc_consistent(bp->pdev, RXBD_RING_SIZE,
						&rxr->rx_pg_desc_mapping[j]);
			if (rxr->rx_pg_desc_ring[j] == NULL)
				return -ENOMEM;

		}
	}
	return 0;
}

static void
bnx2_free_mem(struct bnx2 *bp)
{
	int i;
	struct bnx2_napi *bnapi = &bp->bnx2_napi[0];

	bnx2_free_tx_mem(bp);
	bnx2_free_rx_mem(bp);

	for (i = 0; i < bp->ctx_pages; i++) {
		if (bp->ctx_blk[i]) {
			pci_free_consistent(bp->pdev, BCM_PAGE_SIZE,
					    bp->ctx_blk[i],
					    bp->ctx_blk_mapping[i]);
			bp->ctx_blk[i] = NULL;
		}
	}
	if (bnapi->status_blk.msi) {
		pci_free_consistent(bp->pdev, bp->status_stats_size,
				    bnapi->status_blk.msi,
				    bp->status_blk_mapping);
		bnapi->status_blk.msi = NULL;
		bp->stats_blk = NULL;
	}
}

static int
bnx2_alloc_mem(struct bnx2 *bp)
{
	int i, status_blk_size, err;
	struct bnx2_napi *bnapi;
	void *status_blk;

	/* Combine status and statistics blocks into one allocation. */
	status_blk_size = L1_CACHE_ALIGN(sizeof(struct status_block));
	if (bp->flags & BNX2_FLAG_MSIX_CAP)
		status_blk_size = L1_CACHE_ALIGN(BNX2_MAX_MSIX_HW_VEC *
						 BNX2_SBLK_MSIX_ALIGN_SIZE);
	bp->status_stats_size = status_blk_size +
				sizeof(struct statistics_block);

	status_blk = pci_alloc_consistent(bp->pdev, bp->status_stats_size,
					  &bp->status_blk_mapping);
	if (status_blk == NULL)
		goto alloc_mem_err;

	memset(status_blk, 0, bp->status_stats_size);

	bnapi = &bp->bnx2_napi[0];
	bnapi->status_blk.msi = status_blk;
	bnapi->hw_tx_cons_ptr =
		&bnapi->status_blk.msi->status_tx_quick_consumer_index0;
	bnapi->hw_rx_cons_ptr =
		&bnapi->status_blk.msi->status_rx_quick_consumer_index0;
	if (bp->flags & BNX2_FLAG_MSIX_CAP) {
		for (i = 1; i < BNX2_MAX_MSIX_VEC; i++) {
			struct status_block_msix *sblk;

			bnapi = &bp->bnx2_napi[i];

			sblk = (void *) (status_blk +
					 BNX2_SBLK_MSIX_ALIGN_SIZE * i);
			bnapi->status_blk.msix = sblk;
			bnapi->hw_tx_cons_ptr =
				&sblk->status_tx_quick_consumer_index;
			bnapi->hw_rx_cons_ptr =
				&sblk->status_rx_quick_consumer_index;
			bnapi->int_num = i << 24;
		}
	}

	bp->stats_blk = status_blk + status_blk_size;

	bp->stats_blk_mapping = bp->status_blk_mapping + status_blk_size;

	if (CHIP_NUM(bp) == CHIP_NUM_5709) {
		bp->ctx_pages = 0x2000 / BCM_PAGE_SIZE;
		if (bp->ctx_pages == 0)
			bp->ctx_pages = 1;
		for (i = 0; i < bp->ctx_pages; i++) {
			bp->ctx_blk[i] = pci_alloc_consistent(bp->pdev,
						BCM_PAGE_SIZE,
						&bp->ctx_blk_mapping[i]);
			if (bp->ctx_blk[i] == NULL)
				goto alloc_mem_err;
		}
	}

	err = bnx2_alloc_rx_mem(bp);
	if (err)
		goto alloc_mem_err;

	err = bnx2_alloc_tx_mem(bp);
	if (err)
		goto alloc_mem_err;

	return 0;

alloc_mem_err:
	bnx2_free_mem(bp);
	return -ENOMEM;
}

static void
bnx2_report_fw_link(struct bnx2 *bp)
{
	u32 fw_link_status = 0;

	if (bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP)
		return;

	if (bp->link_up) {
		u32 bmsr;

		switch (bp->line_speed) {
		case SPEED_10:
			if (bp->duplex == DUPLEX_HALF)
				fw_link_status = BNX2_LINK_STATUS_10HALF;
			else
				fw_link_status = BNX2_LINK_STATUS_10FULL;
			break;
		case SPEED_100:
			if (bp->duplex == DUPLEX_HALF)
				fw_link_status = BNX2_LINK_STATUS_100HALF;
			else
				fw_link_status = BNX2_LINK_STATUS_100FULL;
			break;
		case SPEED_1000:
			if (bp->duplex == DUPLEX_HALF)
				fw_link_status = BNX2_LINK_STATUS_1000HALF;
			else
				fw_link_status = BNX2_LINK_STATUS_1000FULL;
			break;
		case SPEED_2500:
			if (bp->duplex == DUPLEX_HALF)
				fw_link_status = BNX2_LINK_STATUS_2500HALF;
			else
				fw_link_status = BNX2_LINK_STATUS_2500FULL;
			break;
		}

		fw_link_status |= BNX2_LINK_STATUS_LINK_UP;

		if (bp->autoneg) {
			fw_link_status |= BNX2_LINK_STATUS_AN_ENABLED;

			bnx2_read_phy(bp, bp->mii_bmsr, &bmsr);
			bnx2_read_phy(bp, bp->mii_bmsr, &bmsr);

			if (!(bmsr & BMSR_ANEGCOMPLETE) ||
			    bp->phy_flags & BNX2_PHY_FLAG_PARALLEL_DETECT)
				fw_link_status |= BNX2_LINK_STATUS_PARALLEL_DET;
			else
				fw_link_status |= BNX2_LINK_STATUS_AN_COMPLETE;
		}
	}
	else
		fw_link_status = BNX2_LINK_STATUS_LINK_DOWN;

	bnx2_shmem_wr(bp, BNX2_LINK_STATUS, fw_link_status);
}

static char *
bnx2_xceiver_str(struct bnx2 *bp)
{
	return ((bp->phy_port == PORT_FIBRE) ? "SerDes" :
		((bp->phy_flags & BNX2_PHY_FLAG_SERDES) ? "Remote Copper" :
		 "Copper"));
}

static void
bnx2_report_link(struct bnx2 *bp)
{
	if (bp->link_up) {
		netif_carrier_on(bp->dev);
		printk(KERN_INFO PFX "%s NIC %s Link is Up, ", bp->dev->name,
		       bnx2_xceiver_str(bp));

		printk("%d Mbps ", bp->line_speed);

		if (bp->duplex == DUPLEX_FULL)
			printk("full duplex");
		else
			printk("half duplex");

		if (bp->flow_ctrl) {
			if (bp->flow_ctrl & FLOW_CTRL_RX) {
				printk(", receive ");
				if (bp->flow_ctrl & FLOW_CTRL_TX)
					printk("& transmit ");
			}
			else {
				printk(", transmit ");
			}
			printk("flow control ON");
		}
		printk("\n");
	}
	else {
		netif_carrier_off(bp->dev);
		printk(KERN_ERR PFX "%s NIC %s Link is Down\n", bp->dev->name,
		       bnx2_xceiver_str(bp));
	}

	bnx2_report_fw_link(bp);
}

static void
bnx2_resolve_flow_ctrl(struct bnx2 *bp)
{
	u32 local_adv, remote_adv;

	bp->flow_ctrl = 0;
	if ((bp->autoneg & (AUTONEG_SPEED | AUTONEG_FLOW_CTRL)) !=
		(AUTONEG_SPEED | AUTONEG_FLOW_CTRL)) {

		if (bp->duplex == DUPLEX_FULL) {
			bp->flow_ctrl = bp->req_flow_ctrl;
		}
		return;
	}

	if (bp->duplex != DUPLEX_FULL) {
		return;
	}

	if ((bp->phy_flags & BNX2_PHY_FLAG_SERDES) &&
	    (CHIP_NUM(bp) == CHIP_NUM_5708)) {
		u32 val;

		bnx2_read_phy(bp, BCM5708S_1000X_STAT1, &val);
		if (val & BCM5708S_1000X_STAT1_TX_PAUSE)
			bp->flow_ctrl |= FLOW_CTRL_TX;
		if (val & BCM5708S_1000X_STAT1_RX_PAUSE)
			bp->flow_ctrl |= FLOW_CTRL_RX;
		return;
	}

	bnx2_read_phy(bp, bp->mii_adv, &local_adv);
	bnx2_read_phy(bp, bp->mii_lpa, &remote_adv);

	if (bp->phy_flags & BNX2_PHY_FLAG_SERDES) {
		u32 new_local_adv = 0;
		u32 new_remote_adv = 0;

		if (local_adv & ADVERTISE_1000XPAUSE)
			new_local_adv |= ADVERTISE_PAUSE_CAP;
		if (local_adv & ADVERTISE_1000XPSE_ASYM)
			new_local_adv |= ADVERTISE_PAUSE_ASYM;
		if (remote_adv & ADVERTISE_1000XPAUSE)
			new_remote_adv |= ADVERTISE_PAUSE_CAP;
		if (remote_adv & ADVERTISE_1000XPSE_ASYM)
			new_remote_adv |= ADVERTISE_PAUSE_ASYM;

		local_adv = new_local_adv;
		remote_adv = new_remote_adv;
	}

	/* See Table 28B-3 of 802.3ab-1999 spec. */
	if (local_adv & ADVERTISE_PAUSE_CAP) {
		if(local_adv & ADVERTISE_PAUSE_ASYM) {
	                if (remote_adv & ADVERTISE_PAUSE_CAP) {
				bp->flow_ctrl = FLOW_CTRL_TX | FLOW_CTRL_RX;
			}
			else if (remote_adv & ADVERTISE_PAUSE_ASYM) {
				bp->flow_ctrl = FLOW_CTRL_RX;
			}
		}
		else {
			if (remote_adv & ADVERTISE_PAUSE_CAP) {
				bp->flow_ctrl = FLOW_CTRL_TX | FLOW_CTRL_RX;
			}
		}
	}
	else if (local_adv & ADVERTISE_PAUSE_ASYM) {
		if ((remote_adv & ADVERTISE_PAUSE_CAP) &&
			(remote_adv & ADVERTISE_PAUSE_ASYM)) {

			bp->flow_ctrl = FLOW_CTRL_TX;
		}
	}
}

static int
bnx2_5709s_linkup(struct bnx2 *bp)
{
	u32 val, speed;

	bp->link_up = 1;

	bnx2_write_phy(bp, MII_BNX2_BLK_ADDR, MII_BNX2_BLK_ADDR_GP_STATUS);
	bnx2_read_phy(bp, MII_BNX2_GP_TOP_AN_STATUS1, &val);
	bnx2_write_phy(bp, MII_BNX2_BLK_ADDR, MII_BNX2_BLK_ADDR_COMBO_IEEEB0);

	if ((bp->autoneg & AUTONEG_SPEED) == 0) {
		bp->line_speed = bp->req_line_speed;
		bp->duplex = bp->req_duplex;
		return 0;
	}
	speed = val & MII_BNX2_GP_TOP_AN_SPEED_MSK;
	switch (speed) {
		case MII_BNX2_GP_TOP_AN_SPEED_10:
			bp->line_speed = SPEED_10;
			break;
		case MII_BNX2_GP_TOP_AN_SPEED_100:
			bp->line_speed = SPEED_100;
			break;
		case MII_BNX2_GP_TOP_AN_SPEED_1G:
		case MII_BNX2_GP_TOP_AN_SPEED_1GKV:
			bp->line_speed = SPEED_1000;
			break;
		case MII_BNX2_GP_TOP_AN_SPEED_2_5G:
			bp->line_speed = SPEED_2500;
			break;
	}
	if (val & MII_BNX2_GP_TOP_AN_FD)
		bp->duplex = DUPLEX_FULL;
	else
		bp->duplex = DUPLEX_HALF;
	return 0;
}

static int
bnx2_5708s_linkup(struct bnx2 *bp)
{
	u32 val;

	bp->link_up = 1;
	bnx2_read_phy(bp, BCM5708S_1000X_STAT1, &val);
	switch (val & BCM5708S_1000X_STAT1_SPEED_MASK) {
		case BCM5708S_1000X_STAT1_SPEED_10:
			bp->line_speed = SPEED_10;
			break;
		case BCM5708S_1000X_STAT1_SPEED_100:
			bp->line_speed = SPEED_100;
			break;
		case BCM5708S_1000X_STAT1_SPEED_1G:
			bp->line_speed = SPEED_1000;
			break;
		case BCM5708S_1000X_STAT1_SPEED_2G5:
			bp->line_speed = SPEED_2500;
			break;
	}
	if (val & BCM5708S_1000X_STAT1_FD)
		bp->duplex = DUPLEX_FULL;
	else
		bp->duplex = DUPLEX_HALF;

	return 0;
}

static int
bnx2_5706s_linkup(struct bnx2 *bp)
{
	u32 bmcr, local_adv, remote_adv, common;

	bp->link_up = 1;
	bp->line_speed = SPEED_1000;

	bnx2_read_phy(bp, bp->mii_bmcr, &bmcr);
	if (bmcr & BMCR_FULLDPLX) {
		bp->duplex = DUPLEX_FULL;
	}
	else {
		bp->duplex = DUPLEX_HALF;
	}

	if (!(bmcr & BMCR_ANENABLE)) {
		return 0;
	}

	bnx2_read_phy(bp, bp->mii_adv, &local_adv);
	bnx2_read_phy(bp, bp->mii_lpa, &remote_adv);

	common = local_adv & remote_adv;
	if (common & (ADVERTISE_1000XHALF | ADVERTISE_1000XFULL)) {

		if (common & ADVERTISE_1000XFULL) {
			bp->duplex = DUPLEX_FULL;
		}
		else {
			bp->duplex = DUPLEX_HALF;
		}
	}

	return 0;
}

static int
bnx2_copper_linkup(struct bnx2 *bp)
{
	u32 bmcr;

	bnx2_read_phy(bp, bp->mii_bmcr, &bmcr);
	if (bmcr & BMCR_ANENABLE) {
		u32 local_adv, remote_adv, common;

		bnx2_read_phy(bp, MII_CTRL1000, &local_adv);
		bnx2_read_phy(bp, MII_STAT1000, &remote_adv);

		common = local_adv & (remote_adv >> 2);
		if (common & ADVERTISE_1000FULL) {
			bp->line_speed = SPEED_1000;
			bp->duplex = DUPLEX_FULL;
		}
		else if (common & ADVERTISE_1000HALF) {
			bp->line_speed = SPEED_1000;
			bp->duplex = DUPLEX_HALF;
		}
		else {
			bnx2_read_phy(bp, bp->mii_adv, &local_adv);
			bnx2_read_phy(bp, bp->mii_lpa, &remote_adv);

			common = local_adv & remote_adv;
			if (common & ADVERTISE_100FULL) {
				bp->line_speed = SPEED_100;
				bp->duplex = DUPLEX_FULL;
			}
			else if (common & ADVERTISE_100HALF) {
				bp->line_speed = SPEED_100;
				bp->duplex = DUPLEX_HALF;
			}
			else if (common & ADVERTISE_10FULL) {
				bp->line_speed = SPEED_10;
				bp->duplex = DUPLEX_FULL;
			}
			else if (common & ADVERTISE_10HALF) {
				bp->line_speed = SPEED_10;
				bp->duplex = DUPLEX_HALF;
			}
			else {
				bp->line_speed = 0;
				bp->link_up = 0;
			}
		}
	}
	else {
		if (bmcr & BMCR_SPEED100) {
			bp->line_speed = SPEED_100;
		}
		else {
			bp->line_speed = SPEED_10;
		}
		if (bmcr & BMCR_FULLDPLX) {
			bp->duplex = DUPLEX_FULL;
		}
		else {
			bp->duplex = DUPLEX_HALF;
		}
	}

	return 0;
}

static void
bnx2_init_rx_context(struct bnx2 *bp, u32 cid)
{
	u32 val, rx_cid_addr = GET_CID_ADDR(cid);

	val = BNX2_L2CTX_CTX_TYPE_CTX_BD_CHN_TYPE_VALUE;
	val |= BNX2_L2CTX_CTX_TYPE_SIZE_L2;
	val |= 0x02 << 8;

	if (CHIP_NUM(bp) == CHIP_NUM_5709) {
		u32 lo_water, hi_water;

		if (bp->flow_ctrl & FLOW_CTRL_TX)
			lo_water = BNX2_L2CTX_LO_WATER_MARK_DEFAULT;
		else
			lo_water = BNX2_L2CTX_LO_WATER_MARK_DIS;
		if (lo_water >= bp->rx_ring_size)
			lo_water = 0;

		hi_water = bp->rx_ring_size / 4;

		if (hi_water <= lo_water)
			lo_water = 0;

		hi_water /= BNX2_L2CTX_HI_WATER_MARK_SCALE;
		lo_water /= BNX2_L2CTX_LO_WATER_MARK_SCALE;

		if (hi_water > 0xf)
			hi_water = 0xf;
		else if (hi_water == 0)
			lo_water = 0;
		val |= lo_water | (hi_water << BNX2_L2CTX_HI_WATER_MARK_SHIFT);
	}
	bnx2_ctx_wr(bp, rx_cid_addr, BNX2_L2CTX_CTX_TYPE, val);
}

static void
bnx2_init_all_rx_contexts(struct bnx2 *bp)
{
	int i;
	u32 cid;

	for (i = 0, cid = RX_CID; i < bp->num_rx_rings; i++, cid++) {
		if (i == 1)
			cid = RX_RSS_CID;
		bnx2_init_rx_context(bp, cid);
	}
}

static void
bnx2_set_mac_link(struct bnx2 *bp)
{
	u32 val;

	REG_WR(bp, BNX2_EMAC_TX_LENGTHS, 0x2620);
	if (bp->link_up && (bp->line_speed == SPEED_1000) &&
		(bp->duplex == DUPLEX_HALF)) {
		REG_WR(bp, BNX2_EMAC_TX_LENGTHS, 0x26ff);
	}

	/* Configure the EMAC mode register. */
	val = REG_RD(bp, BNX2_EMAC_MODE);

	val &= ~(BNX2_EMAC_MODE_PORT | BNX2_EMAC_MODE_HALF_DUPLEX |
		BNX2_EMAC_MODE_MAC_LOOP | BNX2_EMAC_MODE_FORCE_LINK |
		BNX2_EMAC_MODE_25G_MODE);

	if (bp->link_up) {
		switch (bp->line_speed) {
			case SPEED_10:
				if (CHIP_NUM(bp) != CHIP_NUM_5706) {
					val |= BNX2_EMAC_MODE_PORT_MII_10M;
					break;
				}
				/* fall through */
			case SPEED_100:
				val |= BNX2_EMAC_MODE_PORT_MII;
				break;
			case SPEED_2500:
				val |= BNX2_EMAC_MODE_25G_MODE;
				/* fall through */
			case SPEED_1000:
				val |= BNX2_EMAC_MODE_PORT_GMII;
				break;
		}
	}
	else {
		val |= BNX2_EMAC_MODE_PORT_GMII;
	}

	/* Set the MAC to operate in the appropriate duplex mode. */
	if (bp->duplex == DUPLEX_HALF)
		val |= BNX2_EMAC_MODE_HALF_DUPLEX;
	REG_WR(bp, BNX2_EMAC_MODE, val);

	/* Enable/disable rx PAUSE. */
	bp->rx_mode &= ~BNX2_EMAC_RX_MODE_FLOW_EN;

	if (bp->flow_ctrl & FLOW_CTRL_RX)
		bp->rx_mode |= BNX2_EMAC_RX_MODE_FLOW_EN;
	REG_WR(bp, BNX2_EMAC_RX_MODE, bp->rx_mode);

	/* Enable/disable tx PAUSE. */
	val = REG_RD(bp, BNX2_EMAC_TX_MODE);
	val &= ~BNX2_EMAC_TX_MODE_FLOW_EN;

	if (bp->flow_ctrl & FLOW_CTRL_TX)
		val |= BNX2_EMAC_TX_MODE_FLOW_EN;
	REG_WR(bp, BNX2_EMAC_TX_MODE, val);

	/* Acknowledge the interrupt. */
	REG_WR(bp, BNX2_EMAC_STATUS, BNX2_EMAC_STATUS_LINK_CHANGE);

	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		bnx2_init_all_rx_contexts(bp);
}

static void
bnx2_enable_bmsr1(struct bnx2 *bp)
{
	if ((bp->phy_flags & BNX2_PHY_FLAG_SERDES) &&
	    (CHIP_NUM(bp) == CHIP_NUM_5709))
		bnx2_write_phy(bp, MII_BNX2_BLK_ADDR,
			       MII_BNX2_BLK_ADDR_GP_STATUS);
}

static void
bnx2_disable_bmsr1(struct bnx2 *bp)
{
	if ((bp->phy_flags & BNX2_PHY_FLAG_SERDES) &&
	    (CHIP_NUM(bp) == CHIP_NUM_5709))
		bnx2_write_phy(bp, MII_BNX2_BLK_ADDR,
			       MII_BNX2_BLK_ADDR_COMBO_IEEEB0);
}

static int
bnx2_test_and_enable_2g5(struct bnx2 *bp)
{
	u32 up1;
	int ret = 1;

	if (!(bp->phy_flags & BNX2_PHY_FLAG_2_5G_CAPABLE))
		return 0;

	if (bp->autoneg & AUTONEG_SPEED)
		bp->advertising |= ADVERTISED_2500baseX_Full;

	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		bnx2_write_phy(bp, MII_BNX2_BLK_ADDR, MII_BNX2_BLK_ADDR_OVER1G);

	bnx2_read_phy(bp, bp->mii_up1, &up1);
	if (!(up1 & BCM5708S_UP1_2G5)) {
		up1 |= BCM5708S_UP1_2G5;
		bnx2_write_phy(bp, bp->mii_up1, up1);
		ret = 0;
	}

	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		bnx2_write_phy(bp, MII_BNX2_BLK_ADDR,
			       MII_BNX2_BLK_ADDR_COMBO_IEEEB0);

	return ret;
}

static int
bnx2_test_and_disable_2g5(struct bnx2 *bp)
{
	u32 up1;
	int ret = 0;

	if (!(bp->phy_flags & BNX2_PHY_FLAG_2_5G_CAPABLE))
		return 0;

	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		bnx2_write_phy(bp, MII_BNX2_BLK_ADDR, MII_BNX2_BLK_ADDR_OVER1G);

	bnx2_read_phy(bp, bp->mii_up1, &up1);
	if (up1 & BCM5708S_UP1_2G5) {
		up1 &= ~BCM5708S_UP1_2G5;
		bnx2_write_phy(bp, bp->mii_up1, up1);
		ret = 1;
	}

	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		bnx2_write_phy(bp, MII_BNX2_BLK_ADDR,
			       MII_BNX2_BLK_ADDR_COMBO_IEEEB0);

	return ret;
}

static void
bnx2_enable_forced_2g5(struct bnx2 *bp)
{
	u32 bmcr;

	if (!(bp->phy_flags & BNX2_PHY_FLAG_2_5G_CAPABLE))
		return;

	if (CHIP_NUM(bp) == CHIP_NUM_5709) {
		u32 val;

		bnx2_write_phy(bp, MII_BNX2_BLK_ADDR,
			       MII_BNX2_BLK_ADDR_SERDES_DIG);
		bnx2_read_phy(bp, MII_BNX2_SERDES_DIG_MISC1, &val);
		val &= ~MII_BNX2_SD_MISC1_FORCE_MSK;
		val |= MII_BNX2_SD_MISC1_FORCE | MII_BNX2_SD_MISC1_FORCE_2_5G;
		bnx2_write_phy(bp, MII_BNX2_SERDES_DIG_MISC1, val);

		bnx2_write_phy(bp, MII_BNX2_BLK_ADDR,
			       MII_BNX2_BLK_ADDR_COMBO_IEEEB0);
		bnx2_read_phy(bp, bp->mii_bmcr, &bmcr);

	} else if (CHIP_NUM(bp) == CHIP_NUM_5708) {
		bnx2_read_phy(bp, bp->mii_bmcr, &bmcr);
		bmcr |= BCM5708S_BMCR_FORCE_2500;
	}

	if (bp->autoneg & AUTONEG_SPEED) {
		bmcr &= ~BMCR_ANENABLE;
		if (bp->req_duplex == DUPLEX_FULL)
			bmcr |= BMCR_FULLDPLX;
	}
	bnx2_write_phy(bp, bp->mii_bmcr, bmcr);
}

static void
bnx2_disable_forced_2g5(struct bnx2 *bp)
{
	u32 bmcr;

	if (!(bp->phy_flags & BNX2_PHY_FLAG_2_5G_CAPABLE))
		return;

	if (CHIP_NUM(bp) == CHIP_NUM_5709) {
		u32 val;

		bnx2_write_phy(bp, MII_BNX2_BLK_ADDR,
			       MII_BNX2_BLK_ADDR_SERDES_DIG);
		bnx2_read_phy(bp, MII_BNX2_SERDES_DIG_MISC1, &val);
		val &= ~MII_BNX2_SD_MISC1_FORCE;
		bnx2_write_phy(bp, MII_BNX2_SERDES_DIG_MISC1, val);

		bnx2_write_phy(bp, MII_BNX2_BLK_ADDR,
			       MII_BNX2_BLK_ADDR_COMBO_IEEEB0);
		bnx2_read_phy(bp, bp->mii_bmcr, &bmcr);

	} else if (CHIP_NUM(bp) == CHIP_NUM_5708) {
		bnx2_read_phy(bp, bp->mii_bmcr, &bmcr);
		bmcr &= ~BCM5708S_BMCR_FORCE_2500;
	}

	if (bp->autoneg & AUTONEG_SPEED)
		bmcr |= BMCR_SPEED1000 | BMCR_ANENABLE | BMCR_ANRESTART;
	bnx2_write_phy(bp, bp->mii_bmcr, bmcr);
}

static void
bnx2_5706s_force_link_dn(struct bnx2 *bp, int start)
{
	u32 val;

	bnx2_write_phy(bp, MII_BNX2_DSP_ADDRESS, MII_EXPAND_SERDES_CTL);
	bnx2_read_phy(bp, MII_BNX2_DSP_RW_PORT, &val);
	if (start)
		bnx2_write_phy(bp, MII_BNX2_DSP_RW_PORT, val & 0xff0f);
	else
		bnx2_write_phy(bp, MII_BNX2_DSP_RW_PORT, val | 0xc0);
}

static int
bnx2_set_link(struct bnx2 *bp)
{
	u32 bmsr;
	u8 link_up;

	if (bp->loopback == MAC_LOOPBACK || bp->loopback == PHY_LOOPBACK) {
		bp->link_up = 1;
		return 0;
	}

	if (bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP)
		return 0;

	link_up = bp->link_up;

	bnx2_enable_bmsr1(bp);
	bnx2_read_phy(bp, bp->mii_bmsr1, &bmsr);
	bnx2_read_phy(bp, bp->mii_bmsr1, &bmsr);
	bnx2_disable_bmsr1(bp);

	if ((bp->phy_flags & BNX2_PHY_FLAG_SERDES) &&
	    (CHIP_NUM(bp) == CHIP_NUM_5706)) {
		u32 val, an_dbg;

		if (bp->phy_flags & BNX2_PHY_FLAG_FORCED_DOWN) {
			bnx2_5706s_force_link_dn(bp, 0);
			bp->phy_flags &= ~BNX2_PHY_FLAG_FORCED_DOWN;
		}
		val = REG_RD(bp, BNX2_EMAC_STATUS);

		bnx2_write_phy(bp, MII_BNX2_MISC_SHADOW, MISC_SHDW_AN_DBG);
		bnx2_read_phy(bp, MII_BNX2_MISC_SHADOW, &an_dbg);
		bnx2_read_phy(bp, MII_BNX2_MISC_SHADOW, &an_dbg);

		if ((val & BNX2_EMAC_STATUS_LINK) &&
		    !(an_dbg & MISC_SHDW_AN_DBG_NOSYNC))
			bmsr |= BMSR_LSTATUS;
		else
			bmsr &= ~BMSR_LSTATUS;
	}

	if (bmsr & BMSR_LSTATUS) {
		bp->link_up = 1;

		if (bp->phy_flags & BNX2_PHY_FLAG_SERDES) {
			if (CHIP_NUM(bp) == CHIP_NUM_5706)
				bnx2_5706s_linkup(bp);
			else if (CHIP_NUM(bp) == CHIP_NUM_5708)
				bnx2_5708s_linkup(bp);
			else if (CHIP_NUM(bp) == CHIP_NUM_5709)
				bnx2_5709s_linkup(bp);
		}
		else {
			bnx2_copper_linkup(bp);
		}
		bnx2_resolve_flow_ctrl(bp);
	}
	else {
		if ((bp->phy_flags & BNX2_PHY_FLAG_SERDES) &&
		    (bp->autoneg & AUTONEG_SPEED))
			bnx2_disable_forced_2g5(bp);

		if (bp->phy_flags & BNX2_PHY_FLAG_PARALLEL_DETECT) {
			u32 bmcr;

			bnx2_read_phy(bp, bp->mii_bmcr, &bmcr);
			bmcr |= BMCR_ANENABLE;
			bnx2_write_phy(bp, bp->mii_bmcr, bmcr);

			bp->phy_flags &= ~BNX2_PHY_FLAG_PARALLEL_DETECT;
		}
		bp->link_up = 0;
	}

	if (bp->link_up != link_up) {
		bnx2_report_link(bp);
	}

	bnx2_set_mac_link(bp);

	return 0;
}

static int
bnx2_reset_phy(struct bnx2 *bp)
{
	int i;
	u32 reg;

        bnx2_write_phy(bp, bp->mii_bmcr, BMCR_RESET);

#define PHY_RESET_MAX_WAIT 100
	for (i = 0; i < PHY_RESET_MAX_WAIT; i++) {
		udelay(10);

		bnx2_read_phy(bp, bp->mii_bmcr, &reg);
		if (!(reg & BMCR_RESET)) {
			udelay(20);
			break;
		}
	}
	if (i == PHY_RESET_MAX_WAIT) {
		return -EBUSY;
	}
	return 0;
}

static u32
bnx2_phy_get_pause_adv(struct bnx2 *bp)
{
	u32 adv = 0;

	if ((bp->req_flow_ctrl & (FLOW_CTRL_RX | FLOW_CTRL_TX)) ==
		(FLOW_CTRL_RX | FLOW_CTRL_TX)) {

		if (bp->phy_flags & BNX2_PHY_FLAG_SERDES) {
			adv = ADVERTISE_1000XPAUSE;
		}
		else {
			adv = ADVERTISE_PAUSE_CAP;
		}
	}
	else if (bp->req_flow_ctrl & FLOW_CTRL_TX) {
		if (bp->phy_flags & BNX2_PHY_FLAG_SERDES) {
			adv = ADVERTISE_1000XPSE_ASYM;
		}
		else {
			adv = ADVERTISE_PAUSE_ASYM;
		}
	}
	else if (bp->req_flow_ctrl & FLOW_CTRL_RX) {
		if (bp->phy_flags & BNX2_PHY_FLAG_SERDES) {
			adv = ADVERTISE_1000XPAUSE | ADVERTISE_1000XPSE_ASYM;
		}
		else {
			adv = ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM;
		}
	}
	return adv;
}

static int bnx2_fw_sync(struct bnx2 *, u32, int, int);

static int
bnx2_setup_remote_phy(struct bnx2 *bp, u8 port)
{
	u32 speed_arg = 0, pause_adv;

	pause_adv = bnx2_phy_get_pause_adv(bp);

	if (bp->autoneg & AUTONEG_SPEED) {
		speed_arg |= BNX2_NETLINK_SET_LINK_ENABLE_AUTONEG;
		if (bp->advertising & ADVERTISED_10baseT_Half)
			speed_arg |= BNX2_NETLINK_SET_LINK_SPEED_10HALF;
		if (bp->advertising & ADVERTISED_10baseT_Full)
			speed_arg |= BNX2_NETLINK_SET_LINK_SPEED_10FULL;
		if (bp->advertising & ADVERTISED_100baseT_Half)
			speed_arg |= BNX2_NETLINK_SET_LINK_SPEED_100HALF;
		if (bp->advertising & ADVERTISED_100baseT_Full)
			speed_arg |= BNX2_NETLINK_SET_LINK_SPEED_100FULL;
		if (bp->advertising & ADVERTISED_1000baseT_Full)
			speed_arg |= BNX2_NETLINK_SET_LINK_SPEED_1GFULL;
		if (bp->advertising & ADVERTISED_2500baseX_Full)
			speed_arg |= BNX2_NETLINK_SET_LINK_SPEED_2G5FULL;
	} else {
		if (bp->req_line_speed == SPEED_2500)
			speed_arg = BNX2_NETLINK_SET_LINK_SPEED_2G5FULL;
		else if (bp->req_line_speed == SPEED_1000)
			speed_arg = BNX2_NETLINK_SET_LINK_SPEED_1GFULL;
		else if (bp->req_line_speed == SPEED_100) {
			if (bp->req_duplex == DUPLEX_FULL)
				speed_arg = BNX2_NETLINK_SET_LINK_SPEED_100FULL;
			else
				speed_arg = BNX2_NETLINK_SET_LINK_SPEED_100HALF;
		} else if (bp->req_line_speed == SPEED_10) {
			if (bp->req_duplex == DUPLEX_FULL)
				speed_arg = BNX2_NETLINK_SET_LINK_SPEED_10FULL;
			else
				speed_arg = BNX2_NETLINK_SET_LINK_SPEED_10HALF;
		}
	}

	if (pause_adv & (ADVERTISE_1000XPAUSE | ADVERTISE_PAUSE_CAP))
		speed_arg |= BNX2_NETLINK_SET_LINK_FC_SYM_PAUSE;
	if (pause_adv & (ADVERTISE_1000XPSE_ASYM | ADVERTISE_PAUSE_ASYM))
		speed_arg |= BNX2_NETLINK_SET_LINK_FC_ASYM_PAUSE;

	if (port == PORT_TP)
		speed_arg |= BNX2_NETLINK_SET_LINK_PHY_APP_REMOTE |
			     BNX2_NETLINK_SET_LINK_ETH_AT_WIRESPEED;

	bnx2_shmem_wr(bp, BNX2_DRV_MB_ARG0, speed_arg);

	spin_unlock_bh(&bp->phy_lock);
	bnx2_fw_sync(bp, BNX2_DRV_MSG_CODE_CMD_SET_LINK, 1, 0);
	spin_lock_bh(&bp->phy_lock);

	return 0;
}

static int
bnx2_setup_serdes_phy(struct bnx2 *bp, u8 port)
{
	u32 adv, bmcr;
	u32 new_adv = 0;

	if (bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP)
		return (bnx2_setup_remote_phy(bp, port));

	if (!(bp->autoneg & AUTONEG_SPEED)) {
		u32 new_bmcr;
		int force_link_down = 0;

		if (bp->req_line_speed == SPEED_2500) {
			if (!bnx2_test_and_enable_2g5(bp))
				force_link_down = 1;
		} else if (bp->req_line_speed == SPEED_1000) {
			if (bnx2_test_and_disable_2g5(bp))
				force_link_down = 1;
		}
		bnx2_read_phy(bp, bp->mii_adv, &adv);
		adv &= ~(ADVERTISE_1000XFULL | ADVERTISE_1000XHALF);

		bnx2_read_phy(bp, bp->mii_bmcr, &bmcr);
		new_bmcr = bmcr & ~BMCR_ANENABLE;
		new_bmcr |= BMCR_SPEED1000;

		if (CHIP_NUM(bp) == CHIP_NUM_5709) {
			if (bp->req_line_speed == SPEED_2500)
				bnx2_enable_forced_2g5(bp);
			else if (bp->req_line_speed == SPEED_1000) {
				bnx2_disable_forced_2g5(bp);
				new_bmcr &= ~0x2000;
			}

		} else if (CHIP_NUM(bp) == CHIP_NUM_5708) {
			if (bp->req_line_speed == SPEED_2500)
				new_bmcr |= BCM5708S_BMCR_FORCE_2500;
			else
				new_bmcr = bmcr & ~BCM5708S_BMCR_FORCE_2500;
		}

		if (bp->req_duplex == DUPLEX_FULL) {
			adv |= ADVERTISE_1000XFULL;
			new_bmcr |= BMCR_FULLDPLX;
		}
		else {
			adv |= ADVERTISE_1000XHALF;
			new_bmcr &= ~BMCR_FULLDPLX;
		}
		if ((new_bmcr != bmcr) || (force_link_down)) {
			/* Force a link down visible on the other side */
			if (bp->link_up) {
				bnx2_write_phy(bp, bp->mii_adv, adv &
					       ~(ADVERTISE_1000XFULL |
						 ADVERTISE_1000XHALF));
				bnx2_write_phy(bp, bp->mii_bmcr, bmcr |
					BMCR_ANRESTART | BMCR_ANENABLE);

				bp->link_up = 0;
				netif_carrier_off(bp->dev);
				bnx2_write_phy(bp, bp->mii_bmcr, new_bmcr);
				bnx2_report_link(bp);
			}
			bnx2_write_phy(bp, bp->mii_adv, adv);
			bnx2_write_phy(bp, bp->mii_bmcr, new_bmcr);
		} else {
			bnx2_resolve_flow_ctrl(bp);
			bnx2_set_mac_link(bp);
		}
		return 0;
	}

	bnx2_test_and_enable_2g5(bp);

	if (bp->advertising & ADVERTISED_1000baseT_Full)
		new_adv |= ADVERTISE_1000XFULL;

	new_adv |= bnx2_phy_get_pause_adv(bp);

	bnx2_read_phy(bp, bp->mii_adv, &adv);
	bnx2_read_phy(bp, bp->mii_bmcr, &bmcr);

	bp->serdes_an_pending = 0;
	if ((adv != new_adv) || ((bmcr & BMCR_ANENABLE) == 0)) {
		/* Force a link down visible on the other side */
		if (bp->link_up) {
			bnx2_write_phy(bp, bp->mii_bmcr, BMCR_LOOPBACK);
			spin_unlock_bh(&bp->phy_lock);
			msleep(20);
			spin_lock_bh(&bp->phy_lock);
		}

		bnx2_write_phy(bp, bp->mii_adv, new_adv);
		bnx2_write_phy(bp, bp->mii_bmcr, bmcr | BMCR_ANRESTART |
			BMCR_ANENABLE);
		/* Speed up link-up time when the link partner
		 * does not autonegotiate which is very common
		 * in blade servers. Some blade servers use
		 * IPMI for kerboard input and it's important
		 * to minimize link disruptions. Autoneg. involves
		 * exchanging base pages plus 3 next pages and
		 * normally completes in about 120 msec.
		 */
		bp->current_interval = SERDES_AN_TIMEOUT;
		bp->serdes_an_pending = 1;
		mod_timer(&bp->timer, jiffies + bp->current_interval);
	} else {
		bnx2_resolve_flow_ctrl(bp);
		bnx2_set_mac_link(bp);
	}

	return 0;
}

#define ETHTOOL_ALL_FIBRE_SPEED						\
	(bp->phy_flags & BNX2_PHY_FLAG_2_5G_CAPABLE) ?			\
		(ADVERTISED_2500baseX_Full | ADVERTISED_1000baseT_Full) :\
		(ADVERTISED_1000baseT_Full)

#define ETHTOOL_ALL_COPPER_SPEED					\
	(ADVERTISED_10baseT_Half | ADVERTISED_10baseT_Full |		\
	ADVERTISED_100baseT_Half | ADVERTISED_100baseT_Full |		\
	ADVERTISED_1000baseT_Full)

#define PHY_ALL_10_100_SPEED (ADVERTISE_10HALF | ADVERTISE_10FULL | \
	ADVERTISE_100HALF | ADVERTISE_100FULL | ADVERTISE_CSMA)

#define PHY_ALL_1000_SPEED (ADVERTISE_1000HALF | ADVERTISE_1000FULL)

static void
bnx2_set_default_remote_link(struct bnx2 *bp)
{
	u32 link;

	if (bp->phy_port == PORT_TP)
		link = bnx2_shmem_rd(bp, BNX2_RPHY_COPPER_LINK);
	else
		link = bnx2_shmem_rd(bp, BNX2_RPHY_SERDES_LINK);

	if (link & BNX2_NETLINK_SET_LINK_ENABLE_AUTONEG) {
		bp->req_line_speed = 0;
		bp->autoneg |= AUTONEG_SPEED;
		bp->advertising = ADVERTISED_Autoneg;
		if (link & BNX2_NETLINK_SET_LINK_SPEED_10HALF)
			bp->advertising |= ADVERTISED_10baseT_Half;
		if (link & BNX2_NETLINK_SET_LINK_SPEED_10FULL)
			bp->advertising |= ADVERTISED_10baseT_Full;
		if (link & BNX2_NETLINK_SET_LINK_SPEED_100HALF)
			bp->advertising |= ADVERTISED_100baseT_Half;
		if (link & BNX2_NETLINK_SET_LINK_SPEED_100FULL)
			bp->advertising |= ADVERTISED_100baseT_Full;
		if (link & BNX2_NETLINK_SET_LINK_SPEED_1GFULL)
			bp->advertising |= ADVERTISED_1000baseT_Full;
		if (link & BNX2_NETLINK_SET_LINK_SPEED_2G5FULL)
			bp->advertising |= ADVERTISED_2500baseX_Full;
	} else {
		bp->autoneg = 0;
		bp->advertising = 0;
		bp->req_duplex = DUPLEX_FULL;
		if (link & BNX2_NETLINK_SET_LINK_SPEED_10) {
			bp->req_line_speed = SPEED_10;
			if (link & BNX2_NETLINK_SET_LINK_SPEED_10HALF)
				bp->req_duplex = DUPLEX_HALF;
		}
		if (link & BNX2_NETLINK_SET_LINK_SPEED_100) {
			bp->req_line_speed = SPEED_100;
			if (link & BNX2_NETLINK_SET_LINK_SPEED_100HALF)
				bp->req_duplex = DUPLEX_HALF;
		}
		if (link & BNX2_NETLINK_SET_LINK_SPEED_1GFULL)
			bp->req_line_speed = SPEED_1000;
		if (link & BNX2_NETLINK_SET_LINK_SPEED_2G5FULL)
			bp->req_line_speed = SPEED_2500;
	}
}

static void
bnx2_set_default_link(struct bnx2 *bp)
{
	if (bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP) {
		bnx2_set_default_remote_link(bp);
		return;
	}

	bp->autoneg = AUTONEG_SPEED | AUTONEG_FLOW_CTRL;
	bp->req_line_speed = 0;
	if (bp->phy_flags & BNX2_PHY_FLAG_SERDES) {
		u32 reg;

		bp->advertising = ETHTOOL_ALL_FIBRE_SPEED | ADVERTISED_Autoneg;

		reg = bnx2_shmem_rd(bp, BNX2_PORT_HW_CFG_CONFIG);
		reg &= BNX2_PORT_HW_CFG_CFG_DFLT_LINK_MASK;
		if (reg == BNX2_PORT_HW_CFG_CFG_DFLT_LINK_1G) {
			bp->autoneg = 0;
			bp->req_line_speed = bp->line_speed = SPEED_1000;
			bp->req_duplex = DUPLEX_FULL;
		}
	} else
		bp->advertising = ETHTOOL_ALL_COPPER_SPEED | ADVERTISED_Autoneg;
}

static void
bnx2_send_heart_beat(struct bnx2 *bp)
{
	u32 msg;
	u32 addr;

	spin_lock(&bp->indirect_lock);
	msg = (u32) (++bp->fw_drv_pulse_wr_seq & BNX2_DRV_PULSE_SEQ_MASK);
	addr = bp->shmem_base + BNX2_DRV_PULSE_MB;
	REG_WR(bp, BNX2_PCICFG_REG_WINDOW_ADDRESS, addr);
	REG_WR(bp, BNX2_PCICFG_REG_WINDOW, msg);
	spin_unlock(&bp->indirect_lock);
}

static void
bnx2_remote_phy_event(struct bnx2 *bp)
{
	u32 msg;
	u8 link_up = bp->link_up;
	u8 old_port;

	msg = bnx2_shmem_rd(bp, BNX2_LINK_STATUS);

	if (msg & BNX2_LINK_STATUS_HEART_BEAT_EXPIRED)
		bnx2_send_heart_beat(bp);

	msg &= ~BNX2_LINK_STATUS_HEART_BEAT_EXPIRED;

	if ((msg & BNX2_LINK_STATUS_LINK_UP) == BNX2_LINK_STATUS_LINK_DOWN)
		bp->link_up = 0;
	else {
		u32 speed;

		bp->link_up = 1;
		speed = msg & BNX2_LINK_STATUS_SPEED_MASK;
		bp->duplex = DUPLEX_FULL;
		switch (speed) {
			case BNX2_LINK_STATUS_10HALF:
				bp->duplex = DUPLEX_HALF;
			case BNX2_LINK_STATUS_10FULL:
				bp->line_speed = SPEED_10;
				break;
			case BNX2_LINK_STATUS_100HALF:
				bp->duplex = DUPLEX_HALF;
			case BNX2_LINK_STATUS_100BASE_T4:
			case BNX2_LINK_STATUS_100FULL:
				bp->line_speed = SPEED_100;
				break;
			case BNX2_LINK_STATUS_1000HALF:
				bp->duplex = DUPLEX_HALF;
			case BNX2_LINK_STATUS_1000FULL:
				bp->line_speed = SPEED_1000;
				break;
			case BNX2_LINK_STATUS_2500HALF:
				bp->duplex = DUPLEX_HALF;
			case BNX2_LINK_STATUS_2500FULL:
				bp->line_speed = SPEED_2500;
				break;
			default:
				bp->line_speed = 0;
				break;
		}

		bp->flow_ctrl = 0;
		if ((bp->autoneg & (AUTONEG_SPEED | AUTONEG_FLOW_CTRL)) !=
		    (AUTONEG_SPEED | AUTONEG_FLOW_CTRL)) {
			if (bp->duplex == DUPLEX_FULL)
				bp->flow_ctrl = bp->req_flow_ctrl;
		} else {
			if (msg & BNX2_LINK_STATUS_TX_FC_ENABLED)
				bp->flow_ctrl |= FLOW_CTRL_TX;
			if (msg & BNX2_LINK_STATUS_RX_FC_ENABLED)
				bp->flow_ctrl |= FLOW_CTRL_RX;
		}

		old_port = bp->phy_port;
		if (msg & BNX2_LINK_STATUS_SERDES_LINK)
			bp->phy_port = PORT_FIBRE;
		else
			bp->phy_port = PORT_TP;

		if (old_port != bp->phy_port)
			bnx2_set_default_link(bp);

	}
	if (bp->link_up != link_up)
		bnx2_report_link(bp);

	bnx2_set_mac_link(bp);
}

static int
bnx2_set_remote_link(struct bnx2 *bp)
{
	u32 evt_code;

	evt_code = bnx2_shmem_rd(bp, BNX2_FW_EVT_CODE_MB);
	switch (evt_code) {
		case BNX2_FW_EVT_CODE_LINK_EVENT:
			bnx2_remote_phy_event(bp);
			break;
		case BNX2_FW_EVT_CODE_SW_TIMER_EXPIRATION_EVENT:
		default:
			bnx2_send_heart_beat(bp);
			break;
	}
	return 0;
}

static int
bnx2_setup_copper_phy(struct bnx2 *bp)
{
	u32 bmcr;
	u32 new_bmcr;

	bnx2_read_phy(bp, bp->mii_bmcr, &bmcr);

	if (bp->autoneg & AUTONEG_SPEED) {
		u32 adv_reg, adv1000_reg;
		u32 new_adv_reg = 0;
		u32 new_adv1000_reg = 0;

		bnx2_read_phy(bp, bp->mii_adv, &adv_reg);
		adv_reg &= (PHY_ALL_10_100_SPEED | ADVERTISE_PAUSE_CAP |
			ADVERTISE_PAUSE_ASYM);

		bnx2_read_phy(bp, MII_CTRL1000, &adv1000_reg);
		adv1000_reg &= PHY_ALL_1000_SPEED;

		if (bp->advertising & ADVERTISED_10baseT_Half)
			new_adv_reg |= ADVERTISE_10HALF;
		if (bp->advertising & ADVERTISED_10baseT_Full)
			new_adv_reg |= ADVERTISE_10FULL;
		if (bp->advertising & ADVERTISED_100baseT_Half)
			new_adv_reg |= ADVERTISE_100HALF;
		if (bp->advertising & ADVERTISED_100baseT_Full)
			new_adv_reg |= ADVERTISE_100FULL;
		if (bp->advertising & ADVERTISED_1000baseT_Full)
			new_adv1000_reg |= ADVERTISE_1000FULL;

		new_adv_reg |= ADVERTISE_CSMA;

		new_adv_reg |= bnx2_phy_get_pause_adv(bp);

		if ((adv1000_reg != new_adv1000_reg) ||
			(adv_reg != new_adv_reg) ||
			((bmcr & BMCR_ANENABLE) == 0)) {

			bnx2_write_phy(bp, bp->mii_adv, new_adv_reg);
			bnx2_write_phy(bp, MII_CTRL1000, new_adv1000_reg);
			bnx2_write_phy(bp, bp->mii_bmcr, BMCR_ANRESTART |
				BMCR_ANENABLE);
		}
		else if (bp->link_up) {
			/* Flow ctrl may have changed from auto to forced */
			/* or vice-versa. */

			bnx2_resolve_flow_ctrl(bp);
			bnx2_set_mac_link(bp);
		}
		return 0;
	}

	new_bmcr = 0;
	if (bp->req_line_speed == SPEED_100) {
		new_bmcr |= BMCR_SPEED100;
	}
	if (bp->req_duplex == DUPLEX_FULL) {
		new_bmcr |= BMCR_FULLDPLX;
	}
	if (new_bmcr != bmcr) {
		u32 bmsr;

		bnx2_read_phy(bp, bp->mii_bmsr, &bmsr);
		bnx2_read_phy(bp, bp->mii_bmsr, &bmsr);

		if (bmsr & BMSR_LSTATUS) {
			/* Force link down */
			bnx2_write_phy(bp, bp->mii_bmcr, BMCR_LOOPBACK);
			spin_unlock_bh(&bp->phy_lock);
			msleep(50);
			spin_lock_bh(&bp->phy_lock);

			bnx2_read_phy(bp, bp->mii_bmsr, &bmsr);
			bnx2_read_phy(bp, bp->mii_bmsr, &bmsr);
		}

		bnx2_write_phy(bp, bp->mii_bmcr, new_bmcr);

		/* Normally, the new speed is setup after the link has
		 * gone down and up again. In some cases, link will not go
		 * down so we need to set up the new speed here.
		 */
		if (bmsr & BMSR_LSTATUS) {
			bp->line_speed = bp->req_line_speed;
			bp->duplex = bp->req_duplex;
			bnx2_resolve_flow_ctrl(bp);
			bnx2_set_mac_link(bp);
		}
	} else {
		bnx2_resolve_flow_ctrl(bp);
		bnx2_set_mac_link(bp);
	}
	return 0;
}

static int
bnx2_setup_phy(struct bnx2 *bp, u8 port)
{
	if (bp->loopback == MAC_LOOPBACK)
		return 0;

	if (bp->phy_flags & BNX2_PHY_FLAG_SERDES) {
		return (bnx2_setup_serdes_phy(bp, port));
	}
	else {
		return (bnx2_setup_copper_phy(bp));
	}
}

static int
bnx2_init_5709s_phy(struct bnx2 *bp, int reset_phy)
{
	u32 val;

	bp->mii_bmcr = MII_BMCR + 0x10;
	bp->mii_bmsr = MII_BMSR + 0x10;
	bp->mii_bmsr1 = MII_BNX2_GP_TOP_AN_STATUS1;
	bp->mii_adv = MII_ADVERTISE + 0x10;
	bp->mii_lpa = MII_LPA + 0x10;
	bp->mii_up1 = MII_BNX2_OVER1G_UP1;

	bnx2_write_phy(bp, MII_BNX2_BLK_ADDR, MII_BNX2_BLK_ADDR_AER);
	bnx2_write_phy(bp, MII_BNX2_AER_AER, MII_BNX2_AER_AER_AN_MMD);

	bnx2_write_phy(bp, MII_BNX2_BLK_ADDR, MII_BNX2_BLK_ADDR_COMBO_IEEEB0);
	if (reset_phy)
		bnx2_reset_phy(bp);

	bnx2_write_phy(bp, MII_BNX2_BLK_ADDR, MII_BNX2_BLK_ADDR_SERDES_DIG);

	bnx2_read_phy(bp, MII_BNX2_SERDES_DIG_1000XCTL1, &val);
	val &= ~MII_BNX2_SD_1000XCTL1_AUTODET;
	val |= MII_BNX2_SD_1000XCTL1_FIBER;
	bnx2_write_phy(bp, MII_BNX2_SERDES_DIG_1000XCTL1, val);

	bnx2_write_phy(bp, MII_BNX2_BLK_ADDR, MII_BNX2_BLK_ADDR_OVER1G);
	bnx2_read_phy(bp, MII_BNX2_OVER1G_UP1, &val);
	if (bp->phy_flags & BNX2_PHY_FLAG_2_5G_CAPABLE)
		val |= BCM5708S_UP1_2G5;
	else
		val &= ~BCM5708S_UP1_2G5;
	bnx2_write_phy(bp, MII_BNX2_OVER1G_UP1, val);

	bnx2_write_phy(bp, MII_BNX2_BLK_ADDR, MII_BNX2_BLK_ADDR_BAM_NXTPG);
	bnx2_read_phy(bp, MII_BNX2_BAM_NXTPG_CTL, &val);
	val |= MII_BNX2_NXTPG_CTL_T2 | MII_BNX2_NXTPG_CTL_BAM;
	bnx2_write_phy(bp, MII_BNX2_BAM_NXTPG_CTL, val);

	bnx2_write_phy(bp, MII_BNX2_BLK_ADDR, MII_BNX2_BLK_ADDR_CL73_USERB0);

	val = MII_BNX2_CL73_BAM_EN | MII_BNX2_CL73_BAM_STA_MGR_EN |
	      MII_BNX2_CL73_BAM_NP_AFT_BP_EN;
	bnx2_write_phy(bp, MII_BNX2_CL73_BAM_CTL1, val);

	bnx2_write_phy(bp, MII_BNX2_BLK_ADDR, MII_BNX2_BLK_ADDR_COMBO_IEEEB0);

	return 0;
}

static int
bnx2_init_5708s_phy(struct bnx2 *bp, int reset_phy)
{
	u32 val;

	if (reset_phy)
		bnx2_reset_phy(bp);

	bp->mii_up1 = BCM5708S_UP1;

	bnx2_write_phy(bp, BCM5708S_BLK_ADDR, BCM5708S_BLK_ADDR_DIG3);
	bnx2_write_phy(bp, BCM5708S_DIG_3_0, BCM5708S_DIG_3_0_USE_IEEE);
	bnx2_write_phy(bp, BCM5708S_BLK_ADDR, BCM5708S_BLK_ADDR_DIG);

	bnx2_read_phy(bp, BCM5708S_1000X_CTL1, &val);
	val |= BCM5708S_1000X_CTL1_FIBER_MODE | BCM5708S_1000X_CTL1_AUTODET_EN;
	bnx2_write_phy(bp, BCM5708S_1000X_CTL1, val);

	bnx2_read_phy(bp, BCM5708S_1000X_CTL2, &val);
	val |= BCM5708S_1000X_CTL2_PLLEL_DET_EN;
	bnx2_write_phy(bp, BCM5708S_1000X_CTL2, val);

	if (bp->phy_flags & BNX2_PHY_FLAG_2_5G_CAPABLE) {
		bnx2_read_phy(bp, BCM5708S_UP1, &val);
		val |= BCM5708S_UP1_2G5;
		bnx2_write_phy(bp, BCM5708S_UP1, val);
	}

	if ((CHIP_ID(bp) == CHIP_ID_5708_A0) ||
	    (CHIP_ID(bp) == CHIP_ID_5708_B0) ||
	    (CHIP_ID(bp) == CHIP_ID_5708_B1)) {
		/* increase tx signal amplitude */
		bnx2_write_phy(bp, BCM5708S_BLK_ADDR,
			       BCM5708S_BLK_ADDR_TX_MISC);
		bnx2_read_phy(bp, BCM5708S_TX_ACTL1, &val);
		val &= ~BCM5708S_TX_ACTL1_DRIVER_VCM;
		bnx2_write_phy(bp, BCM5708S_TX_ACTL1, val);
		bnx2_write_phy(bp, BCM5708S_BLK_ADDR, BCM5708S_BLK_ADDR_DIG);
	}

	val = bnx2_shmem_rd(bp, BNX2_PORT_HW_CFG_CONFIG) &
	      BNX2_PORT_HW_CFG_CFG_TXCTL3_MASK;

	if (val) {
		u32 is_backplane;

		is_backplane = bnx2_shmem_rd(bp, BNX2_SHARED_HW_CFG_CONFIG);
		if (is_backplane & BNX2_SHARED_HW_CFG_PHY_BACKPLANE) {
			bnx2_write_phy(bp, BCM5708S_BLK_ADDR,
				       BCM5708S_BLK_ADDR_TX_MISC);
			bnx2_write_phy(bp, BCM5708S_TX_ACTL3, val);
			bnx2_write_phy(bp, BCM5708S_BLK_ADDR,
				       BCM5708S_BLK_ADDR_DIG);
		}
	}
	return 0;
}

static int
bnx2_init_5706s_phy(struct bnx2 *bp, int reset_phy)
{
	if (reset_phy)
		bnx2_reset_phy(bp);

	bp->phy_flags &= ~BNX2_PHY_FLAG_PARALLEL_DETECT;

	if (CHIP_NUM(bp) == CHIP_NUM_5706)
        	REG_WR(bp, BNX2_MISC_GP_HW_CTL0, 0x300);

	if (bp->dev->mtu > 1500) {
		u32 val;

		/* Set extended packet length bit */
		bnx2_write_phy(bp, 0x18, 0x7);
		bnx2_read_phy(bp, 0x18, &val);
		bnx2_write_phy(bp, 0x18, (val & 0xfff8) | 0x4000);

		bnx2_write_phy(bp, 0x1c, 0x6c00);
		bnx2_read_phy(bp, 0x1c, &val);
		bnx2_write_phy(bp, 0x1c, (val & 0x3ff) | 0xec02);
	}
	else {
		u32 val;

		bnx2_write_phy(bp, 0x18, 0x7);
		bnx2_read_phy(bp, 0x18, &val);
		bnx2_write_phy(bp, 0x18, val & ~0x4007);

		bnx2_write_phy(bp, 0x1c, 0x6c00);
		bnx2_read_phy(bp, 0x1c, &val);
		bnx2_write_phy(bp, 0x1c, (val & 0x3fd) | 0xec00);
	}

	return 0;
}

static int
bnx2_init_copper_phy(struct bnx2 *bp, int reset_phy)
{
	u32 val;

	if (reset_phy)
		bnx2_reset_phy(bp);

	if (bp->phy_flags & BNX2_PHY_FLAG_CRC_FIX) {
		bnx2_write_phy(bp, 0x18, 0x0c00);
		bnx2_write_phy(bp, 0x17, 0x000a);
		bnx2_write_phy(bp, 0x15, 0x310b);
		bnx2_write_phy(bp, 0x17, 0x201f);
		bnx2_write_phy(bp, 0x15, 0x9506);
		bnx2_write_phy(bp, 0x17, 0x401f);
		bnx2_write_phy(bp, 0x15, 0x14e2);
		bnx2_write_phy(bp, 0x18, 0x0400);
	}

	if (bp->phy_flags & BNX2_PHY_FLAG_DIS_EARLY_DAC) {
		bnx2_write_phy(bp, MII_BNX2_DSP_ADDRESS,
			       MII_BNX2_DSP_EXPAND_REG | 0x8);
		bnx2_read_phy(bp, MII_BNX2_DSP_RW_PORT, &val);
		val &= ~(1 << 8);
		bnx2_write_phy(bp, MII_BNX2_DSP_RW_PORT, val);
	}

	if (bp->dev->mtu > 1500) {
		/* Set extended packet length bit */
		bnx2_write_phy(bp, 0x18, 0x7);
		bnx2_read_phy(bp, 0x18, &val);
		bnx2_write_phy(bp, 0x18, val | 0x4000);

		bnx2_read_phy(bp, 0x10, &val);
		bnx2_write_phy(bp, 0x10, val | 0x1);
	}
	else {
		bnx2_write_phy(bp, 0x18, 0x7);
		bnx2_read_phy(bp, 0x18, &val);
		bnx2_write_phy(bp, 0x18, val & ~0x4007);

		bnx2_read_phy(bp, 0x10, &val);
		bnx2_write_phy(bp, 0x10, val & ~0x1);
	}

	/* ethernet@wirespeed */
	bnx2_write_phy(bp, 0x18, 0x7007);
	bnx2_read_phy(bp, 0x18, &val);
	bnx2_write_phy(bp, 0x18, val | (1 << 15) | (1 << 4));
	return 0;
}


static int
bnx2_init_phy(struct bnx2 *bp, int reset_phy)
{
	u32 val;
	int rc = 0;

	bp->phy_flags &= ~BNX2_PHY_FLAG_INT_MODE_MASK;
	bp->phy_flags |= BNX2_PHY_FLAG_INT_MODE_LINK_READY;

	bp->mii_bmcr = MII_BMCR;
	bp->mii_bmsr = MII_BMSR;
	bp->mii_bmsr1 = MII_BMSR;
	bp->mii_adv = MII_ADVERTISE;
	bp->mii_lpa = MII_LPA;

        REG_WR(bp, BNX2_EMAC_ATTENTION_ENA, BNX2_EMAC_ATTENTION_ENA_LINK);

	if (bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP)
		goto setup_phy;

	bnx2_read_phy(bp, MII_PHYSID1, &val);
	bp->phy_id = val << 16;
	bnx2_read_phy(bp, MII_PHYSID2, &val);
	bp->phy_id |= val & 0xffff;

	if (bp->phy_flags & BNX2_PHY_FLAG_SERDES) {
		if (CHIP_NUM(bp) == CHIP_NUM_5706)
			rc = bnx2_init_5706s_phy(bp, reset_phy);
		else if (CHIP_NUM(bp) == CHIP_NUM_5708)
			rc = bnx2_init_5708s_phy(bp, reset_phy);
		else if (CHIP_NUM(bp) == CHIP_NUM_5709)
			rc = bnx2_init_5709s_phy(bp, reset_phy);
	}
	else {
		rc = bnx2_init_copper_phy(bp, reset_phy);
	}

setup_phy:
	if (!rc)
		rc = bnx2_setup_phy(bp, bp->phy_port);

	return rc;
}

static int
bnx2_set_mac_loopback(struct bnx2 *bp)
{
	u32 mac_mode;

	mac_mode = REG_RD(bp, BNX2_EMAC_MODE);
	mac_mode &= ~BNX2_EMAC_MODE_PORT;
	mac_mode |= BNX2_EMAC_MODE_MAC_LOOP | BNX2_EMAC_MODE_FORCE_LINK;
	REG_WR(bp, BNX2_EMAC_MODE, mac_mode);
	bp->link_up = 1;
	return 0;
}

static int bnx2_test_link(struct bnx2 *);

static int
bnx2_set_phy_loopback(struct bnx2 *bp)
{
	u32 mac_mode;
	int rc, i;

	spin_lock_bh(&bp->phy_lock);
	rc = bnx2_write_phy(bp, bp->mii_bmcr, BMCR_LOOPBACK | BMCR_FULLDPLX |
			    BMCR_SPEED1000);
	spin_unlock_bh(&bp->phy_lock);
	if (rc)
		return rc;

	for (i = 0; i < 10; i++) {
		if (bnx2_test_link(bp) == 0)
			break;
		msleep(100);
	}

	mac_mode = REG_RD(bp, BNX2_EMAC_MODE);
	mac_mode &= ~(BNX2_EMAC_MODE_PORT | BNX2_EMAC_MODE_HALF_DUPLEX |
		      BNX2_EMAC_MODE_MAC_LOOP | BNX2_EMAC_MODE_FORCE_LINK |
		      BNX2_EMAC_MODE_25G_MODE);

	mac_mode |= BNX2_EMAC_MODE_PORT_GMII;
	REG_WR(bp, BNX2_EMAC_MODE, mac_mode);
	bp->link_up = 1;
	return 0;
}

static int
bnx2_fw_sync(struct bnx2 *bp, u32 msg_data, int ack, int silent)
{
	int i;
	u32 val;

	bp->fw_wr_seq++;
	msg_data |= bp->fw_wr_seq;

	bnx2_shmem_wr(bp, BNX2_DRV_MB, msg_data);

	if (!ack)
		return 0;

	/* wait for an acknowledgement. */
	for (i = 0; i < (FW_ACK_TIME_OUT_MS / 10); i++) {
		msleep(10);

		val = bnx2_shmem_rd(bp, BNX2_FW_MB);

		if ((val & BNX2_FW_MSG_ACK) == (msg_data & BNX2_DRV_MSG_SEQ))
			break;
	}
	if ((msg_data & BNX2_DRV_MSG_DATA) == BNX2_DRV_MSG_DATA_WAIT0)
		return 0;

	/* If we timed out, inform the firmware that this is the case. */
	if ((val & BNX2_FW_MSG_ACK) != (msg_data & BNX2_DRV_MSG_SEQ)) {
		if (!silent)
			printk(KERN_ERR PFX "fw sync timeout, reset code = "
					    "%x\n", msg_data);

		msg_data &= ~BNX2_DRV_MSG_CODE;
		msg_data |= BNX2_DRV_MSG_CODE_FW_TIMEOUT;

		bnx2_shmem_wr(bp, BNX2_DRV_MB, msg_data);

		return -EBUSY;
	}

	if ((val & BNX2_FW_MSG_STATUS_MASK) != BNX2_FW_MSG_STATUS_OK)
		return -EIO;

	return 0;
}

static int
bnx2_init_5709_context(struct bnx2 *bp)
{
	int i, ret = 0;
	u32 val;

	val = BNX2_CTX_COMMAND_ENABLED | BNX2_CTX_COMMAND_MEM_INIT | (1 << 12);
	val |= (BCM_PAGE_BITS - 8) << 16;
	REG_WR(bp, BNX2_CTX_COMMAND, val);
	for (i = 0; i < 10; i++) {
		val = REG_RD(bp, BNX2_CTX_COMMAND);
		if (!(val & BNX2_CTX_COMMAND_MEM_INIT))
			break;
		udelay(2);
	}
	if (val & BNX2_CTX_COMMAND_MEM_INIT)
		return -EBUSY;

	for (i = 0; i < bp->ctx_pages; i++) {
		int j;

		if (bp->ctx_blk[i])
			memset(bp->ctx_blk[i], 0, BCM_PAGE_SIZE);
		else
			return -ENOMEM;

		REG_WR(bp, BNX2_CTX_HOST_PAGE_TBL_DATA0,
		       (bp->ctx_blk_mapping[i] & 0xffffffff) |
		       BNX2_CTX_HOST_PAGE_TBL_DATA0_VALID);
		REG_WR(bp, BNX2_CTX_HOST_PAGE_TBL_DATA1,
		       (u64) bp->ctx_blk_mapping[i] >> 32);
		REG_WR(bp, BNX2_CTX_HOST_PAGE_TBL_CTRL, i |
		       BNX2_CTX_HOST_PAGE_TBL_CTRL_WRITE_REQ);
		for (j = 0; j < 10; j++) {

			val = REG_RD(bp, BNX2_CTX_HOST_PAGE_TBL_CTRL);
			if (!(val & BNX2_CTX_HOST_PAGE_TBL_CTRL_WRITE_REQ))
				break;
			udelay(5);
		}
		if (val & BNX2_CTX_HOST_PAGE_TBL_CTRL_WRITE_REQ) {
			ret = -EBUSY;
			break;
		}
	}
	return ret;
}

static void
bnx2_init_context(struct bnx2 *bp)
{
	u32 vcid;

	vcid = 96;
	while (vcid) {
		u32 vcid_addr, pcid_addr, offset;
		int i;

		vcid--;

		if (CHIP_ID(bp) == CHIP_ID_5706_A0) {
			u32 new_vcid;

			vcid_addr = GET_PCID_ADDR(vcid);
			if (vcid & 0x8) {
				new_vcid = 0x60 + (vcid & 0xf0) + (vcid & 0x7);
			}
			else {
				new_vcid = vcid;
			}
			pcid_addr = GET_PCID_ADDR(new_vcid);
		}
		else {
	    		vcid_addr = GET_CID_ADDR(vcid);
			pcid_addr = vcid_addr;
		}

		for (i = 0; i < (CTX_SIZE / PHY_CTX_SIZE); i++) {
			vcid_addr += (i << PHY_CTX_SHIFT);
			pcid_addr += (i << PHY_CTX_SHIFT);

			REG_WR(bp, BNX2_CTX_VIRT_ADDR, vcid_addr);
			REG_WR(bp, BNX2_CTX_PAGE_TBL, pcid_addr);

			/* Zero out the context. */
			for (offset = 0; offset < PHY_CTX_SIZE; offset += 4)
				bnx2_ctx_wr(bp, vcid_addr, offset, 0);
		}
	}
}

static int
bnx2_alloc_bad_rbuf(struct bnx2 *bp)
{
	u16 *good_mbuf;
	u32 good_mbuf_cnt;
	u32 val;

	good_mbuf = kmalloc(512 * sizeof(u16), GFP_KERNEL);
	if (good_mbuf == NULL) {
		printk(KERN_ERR PFX "Failed to allocate memory in "
				    "bnx2_alloc_bad_rbuf\n");
		return -ENOMEM;
	}

	REG_WR(bp, BNX2_MISC_ENABLE_SET_BITS,
		BNX2_MISC_ENABLE_SET_BITS_RX_MBUF_ENABLE);

	good_mbuf_cnt = 0;

	/* Allocate a bunch of mbufs and save the good ones in an array. */
	val = bnx2_reg_rd_ind(bp, BNX2_RBUF_STATUS1);
	while (val & BNX2_RBUF_STATUS1_FREE_COUNT) {
		bnx2_reg_wr_ind(bp, BNX2_RBUF_COMMAND,
				BNX2_RBUF_COMMAND_ALLOC_REQ);

		val = bnx2_reg_rd_ind(bp, BNX2_RBUF_FW_BUF_ALLOC);

		val &= BNX2_RBUF_FW_BUF_ALLOC_VALUE;

		/* The addresses with Bit 9 set are bad memory blocks. */
		if (!(val & (1 << 9))) {
			good_mbuf[good_mbuf_cnt] = (u16) val;
			good_mbuf_cnt++;
		}

		val = bnx2_reg_rd_ind(bp, BNX2_RBUF_STATUS1);
	}

	/* Free the good ones back to the mbuf pool thus discarding
	 * all the bad ones. */
	while (good_mbuf_cnt) {
		good_mbuf_cnt--;

		val = good_mbuf[good_mbuf_cnt];
		val = (val << 9) | val | 1;

		bnx2_reg_wr_ind(bp, BNX2_RBUF_FW_BUF_FREE, val);
	}
	kfree(good_mbuf);
	return 0;
}

static void
bnx2_set_mac_addr(struct bnx2 *bp, u8 *mac_addr, u32 pos)
{
	u32 val;

	val = (mac_addr[0] << 8) | mac_addr[1];

	REG_WR(bp, BNX2_EMAC_MAC_MATCH0 + (pos * 8), val);

	val = (mac_addr[2] << 24) | (mac_addr[3] << 16) |
		(mac_addr[4] << 8) | mac_addr[5];

	REG_WR(bp, BNX2_EMAC_MAC_MATCH1 + (pos * 8), val);
}

static inline int
bnx2_alloc_rx_page(struct bnx2 *bp, struct bnx2_rx_ring_info *rxr, u16 index)
{
	dma_addr_t mapping;
	struct sw_pg *rx_pg = &rxr->rx_pg_ring[index];
	struct rx_bd *rxbd =
		&rxr->rx_pg_desc_ring[RX_RING(index)][RX_IDX(index)];
	struct page *page = alloc_page(GFP_ATOMIC);

	if (!page)
		return -ENOMEM;
	mapping = pci_map_page(bp->pdev, page, 0, PAGE_SIZE,
			       PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(bp->pdev, mapping)) {
		__free_page(page);
		return -EIO;
	}

	rx_pg->page = page;
	pci_unmap_addr_set(rx_pg, mapping, mapping);
	rxbd->rx_bd_haddr_hi = (u64) mapping >> 32;
	rxbd->rx_bd_haddr_lo = (u64) mapping & 0xffffffff;
	return 0;
}

static void
bnx2_free_rx_page(struct bnx2 *bp, struct bnx2_rx_ring_info *rxr, u16 index)
{
	struct sw_pg *rx_pg = &rxr->rx_pg_ring[index];
	struct page *page = rx_pg->page;

	if (!page)
		return;

	pci_unmap_page(bp->pdev, pci_unmap_addr(rx_pg, mapping), PAGE_SIZE,
		       PCI_DMA_FROMDEVICE);

	__free_page(page);
	rx_pg->page = NULL;
}

static inline int
bnx2_alloc_rx_skb(struct bnx2 *bp, struct bnx2_rx_ring_info *rxr, u16 index)
{
	struct sk_buff *skb;
	struct sw_bd *rx_buf = &rxr->rx_buf_ring[index];
	dma_addr_t mapping;
	struct rx_bd *rxbd = &rxr->rx_desc_ring[RX_RING(index)][RX_IDX(index)];
	unsigned long align;

	skb = netdev_alloc_skb(bp->dev, bp->rx_buf_size);
	if (skb == NULL) {
		return -ENOMEM;
	}

	if (unlikely((align = (unsigned long) skb->data & (BNX2_RX_ALIGN - 1))))
		skb_reserve(skb, BNX2_RX_ALIGN - align);

	mapping = pci_map_single(bp->pdev, skb->data, bp->rx_buf_use_size,
		PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(bp->pdev, mapping)) {
		dev_kfree_skb(skb);
		return -EIO;
	}

	rx_buf->skb = skb;
	pci_unmap_addr_set(rx_buf, mapping, mapping);

	rxbd->rx_bd_haddr_hi = (u64) mapping >> 32;
	rxbd->rx_bd_haddr_lo = (u64) mapping & 0xffffffff;

	rxr->rx_prod_bseq += bp->rx_buf_use_size;

	return 0;
}

static int
bnx2_phy_event_is_set(struct bnx2 *bp, struct bnx2_napi *bnapi, u32 event)
{
	struct status_block *sblk = bnapi->status_blk.msi;
	u32 new_link_state, old_link_state;
	int is_set = 1;

	new_link_state = sblk->status_attn_bits & event;
	old_link_state = sblk->status_attn_bits_ack & event;
	if (new_link_state != old_link_state) {
		if (new_link_state)
			REG_WR(bp, BNX2_PCICFG_STATUS_BIT_SET_CMD, event);
		else
			REG_WR(bp, BNX2_PCICFG_STATUS_BIT_CLEAR_CMD, event);
	} else
		is_set = 0;

	return is_set;
}

static void
bnx2_phy_int(struct bnx2 *bp, struct bnx2_napi *bnapi)
{
	spin_lock(&bp->phy_lock);

	if (bnx2_phy_event_is_set(bp, bnapi, STATUS_ATTN_BITS_LINK_STATE))
		bnx2_set_link(bp);
	if (bnx2_phy_event_is_set(bp, bnapi, STATUS_ATTN_BITS_TIMER_ABORT))
		bnx2_set_remote_link(bp);

	spin_unlock(&bp->phy_lock);

}

static inline u16
bnx2_get_hw_tx_cons(struct bnx2_napi *bnapi)
{
	u16 cons;

	/* Tell compiler that status block fields can change. */
	barrier();
	cons = *bnapi->hw_tx_cons_ptr;
	if (unlikely((cons & MAX_TX_DESC_CNT) == MAX_TX_DESC_CNT))
		cons++;
	return cons;
}

static int
bnx2_tx_int(struct bnx2 *bp, struct bnx2_napi *bnapi, int budget)
{
	struct bnx2_tx_ring_info *txr = &bnapi->tx_ring;
	u16 hw_cons, sw_cons, sw_ring_cons;
	int tx_pkt = 0, index;
	struct netdev_queue *txq;

	index = (bnapi - bp->bnx2_napi);
	txq = netdev_get_tx_queue(bp->dev, index);

	hw_cons = bnx2_get_hw_tx_cons(bnapi);
	sw_cons = txr->tx_cons;

	while (sw_cons != hw_cons) {
		struct sw_tx_bd *tx_buf;
		struct sk_buff *skb;
		int i, last;

		sw_ring_cons = TX_RING_IDX(sw_cons);

		tx_buf = &txr->tx_buf_ring[sw_ring_cons];
		skb = tx_buf->skb;

		/* partial BD completions possible with TSO packets */
		if (skb_is_gso(skb)) {
			u16 last_idx, last_ring_idx;

			last_idx = sw_cons +
				skb_shinfo(skb)->nr_frags + 1;
			last_ring_idx = sw_ring_cons +
				skb_shinfo(skb)->nr_frags + 1;
			if (unlikely(last_ring_idx >= MAX_TX_DESC_CNT)) {
				last_idx++;
			}
			if (((s16) ((s16) last_idx - (s16) hw_cons)) > 0) {
				break;
			}
		}

		skb_dma_unmap(&bp->pdev->dev, skb, DMA_TO_DEVICE);

		tx_buf->skb = NULL;
		last = skb_shinfo(skb)->nr_frags;

		for (i = 0; i < last; i++) {
			sw_cons = NEXT_TX_BD(sw_cons);
		}

		sw_cons = NEXT_TX_BD(sw_cons);

		dev_kfree_skb(skb);
		tx_pkt++;
		if (tx_pkt == budget)
			break;

		hw_cons = bnx2_get_hw_tx_cons(bnapi);
	}

	txr->hw_tx_cons = hw_cons;
	txr->tx_cons = sw_cons;

	/* Need to make the tx_cons update visible to bnx2_start_xmit()
	 * before checking for netif_tx_queue_stopped().  Without the
	 * memory barrier, there is a small possibility that bnx2_start_xmit()
	 * will miss it and cause the queue to be stopped forever.
	 */
	smp_mb();

	if (unlikely(netif_tx_queue_stopped(txq)) &&
		     (bnx2_tx_avail(bp, txr) > bp->tx_wake_thresh)) {
		__netif_tx_lock(txq, smp_processor_id());
		if ((netif_tx_queue_stopped(txq)) &&
		    (bnx2_tx_avail(bp, txr) > bp->tx_wake_thresh))
			netif_tx_wake_queue(txq);
		__netif_tx_unlock(txq);
	}

	return tx_pkt;
}

static void
bnx2_reuse_rx_skb_pages(struct bnx2 *bp, struct bnx2_rx_ring_info *rxr,
			struct sk_buff *skb, int count)
{
	struct sw_pg *cons_rx_pg, *prod_rx_pg;
	struct rx_bd *cons_bd, *prod_bd;
	int i;
	u16 hw_prod, prod;
	u16 cons = rxr->rx_pg_cons;

	cons_rx_pg = &rxr->rx_pg_ring[cons];

	/* The caller was unable to allocate a new page to replace the
	 * last one in the frags array, so we need to recycle that page
	 * and then free the skb.
	 */
	if (skb) {
		struct page *page;
		struct skb_shared_info *shinfo;

		shinfo = skb_shinfo(skb);
		shinfo->nr_frags--;
		page = shinfo->frags[shinfo->nr_frags].page;
		shinfo->frags[shinfo->nr_frags].page = NULL;

		cons_rx_pg->page = page;
		dev_kfree_skb(skb);
	}

	hw_prod = rxr->rx_pg_prod;

	for (i = 0; i < count; i++) {
		prod = RX_PG_RING_IDX(hw_prod);

		prod_rx_pg = &rxr->rx_pg_ring[prod];
		cons_rx_pg = &rxr->rx_pg_ring[cons];
		cons_bd = &rxr->rx_pg_desc_ring[RX_RING(cons)][RX_IDX(cons)];
		prod_bd = &rxr->rx_pg_desc_ring[RX_RING(prod)][RX_IDX(prod)];

		if (prod != cons) {
			prod_rx_pg->page = cons_rx_pg->page;
			cons_rx_pg->page = NULL;
			pci_unmap_addr_set(prod_rx_pg, mapping,
				pci_unmap_addr(cons_rx_pg, mapping));

			prod_bd->rx_bd_haddr_hi = cons_bd->rx_bd_haddr_hi;
			prod_bd->rx_bd_haddr_lo = cons_bd->rx_bd_haddr_lo;

		}
		cons = RX_PG_RING_IDX(NEXT_RX_BD(cons));
		hw_prod = NEXT_RX_BD(hw_prod);
	}
	rxr->rx_pg_prod = hw_prod;
	rxr->rx_pg_cons = cons;
}

static inline void
bnx2_reuse_rx_skb(struct bnx2 *bp, struct bnx2_rx_ring_info *rxr,
		  struct sk_buff *skb, u16 cons, u16 prod)
{
	struct sw_bd *cons_rx_buf, *prod_rx_buf;
	struct rx_bd *cons_bd, *prod_bd;

	cons_rx_buf = &rxr->rx_buf_ring[cons];
	prod_rx_buf = &rxr->rx_buf_ring[prod];

	pci_dma_sync_single_for_device(bp->pdev,
		pci_unmap_addr(cons_rx_buf, mapping),
		BNX2_RX_OFFSET + BNX2_RX_COPY_THRESH, PCI_DMA_FROMDEVICE);

	rxr->rx_prod_bseq += bp->rx_buf_use_size;

	prod_rx_buf->skb = skb;

	if (cons == prod)
		return;

	pci_unmap_addr_set(prod_rx_buf, mapping,
			pci_unmap_addr(cons_rx_buf, mapping));

	cons_bd = &rxr->rx_desc_ring[RX_RING(cons)][RX_IDX(cons)];
	prod_bd = &rxr->rx_desc_ring[RX_RING(prod)][RX_IDX(prod)];
	prod_bd->rx_bd_haddr_hi = cons_bd->rx_bd_haddr_hi;
	prod_bd->rx_bd_haddr_lo = cons_bd->rx_bd_haddr_lo;
}

static int
bnx2_rx_skb(struct bnx2 *bp, struct bnx2_rx_ring_info *rxr, struct sk_buff *skb,
	    unsigned int len, unsigned int hdr_len, dma_addr_t dma_addr,
	    u32 ring_idx)
{
	int err;
	u16 prod = ring_idx & 0xffff;

	err = bnx2_alloc_rx_skb(bp, rxr, prod);
	if (unlikely(err)) {
		bnx2_reuse_rx_skb(bp, rxr, skb, (u16) (ring_idx >> 16), prod);
		if (hdr_len) {
			unsigned int raw_len = len + 4;
			int pages = PAGE_ALIGN(raw_len - hdr_len) >> PAGE_SHIFT;

			bnx2_reuse_rx_skb_pages(bp, rxr, NULL, pages);
		}
		return err;
	}

	skb_reserve(skb, BNX2_RX_OFFSET);
	pci_unmap_single(bp->pdev, dma_addr, bp->rx_buf_use_size,
			 PCI_DMA_FROMDEVICE);

	if (hdr_len == 0) {
		skb_put(skb, len);
		return 0;
	} else {
		unsigned int i, frag_len, frag_size, pages;
		struct sw_pg *rx_pg;
		u16 pg_cons = rxr->rx_pg_cons;
		u16 pg_prod = rxr->rx_pg_prod;

		frag_size = len + 4 - hdr_len;
		pages = PAGE_ALIGN(frag_size) >> PAGE_SHIFT;
		skb_put(skb, hdr_len);

		for (i = 0; i < pages; i++) {
			dma_addr_t mapping_old;

			frag_len = min(frag_size, (unsigned int) PAGE_SIZE);
			if (unlikely(frag_len <= 4)) {
				unsigned int tail = 4 - frag_len;

				rxr->rx_pg_cons = pg_cons;
				rxr->rx_pg_prod = pg_prod;
				bnx2_reuse_rx_skb_pages(bp, rxr, NULL,
							pages - i);
				skb->len -= tail;
				if (i == 0) {
					skb->tail -= tail;
				} else {
					skb_frag_t *frag =
						&skb_shinfo(skb)->frags[i - 1];
					frag->size -= tail;
					skb->data_len -= tail;
					skb->truesize -= tail;
				}
				return 0;
			}
			rx_pg = &rxr->rx_pg_ring[pg_cons];

			/* Don't unmap yet.  If we're unable to allocate a new
			 * page, we need to recycle the page and the DMA addr.
			 */
			mapping_old = pci_unmap_addr(rx_pg, mapping);
			if (i == pages - 1)
				frag_len -= 4;

			skb_fill_page_desc(skb, i, rx_pg->page, 0, frag_len);
			rx_pg->page = NULL;

			err = bnx2_alloc_rx_page(bp, rxr,
						 RX_PG_RING_IDX(pg_prod));
			if (unlikely(err)) {
				rxr->rx_pg_cons = pg_cons;
				rxr->rx_pg_prod = pg_prod;
				bnx2_reuse_rx_skb_pages(bp, rxr, skb,
							pages - i);
				return err;
			}

			pci_unmap_page(bp->pdev, mapping_old,
				       PAGE_SIZE, PCI_DMA_FROMDEVICE);

			frag_size -= frag_len;
			skb->data_len += frag_len;
			skb->truesize += frag_len;
			skb->len += frag_len;

			pg_prod = NEXT_RX_BD(pg_prod);
			pg_cons = RX_PG_RING_IDX(NEXT_RX_BD(pg_cons));
		}
		rxr->rx_pg_prod = pg_prod;
		rxr->rx_pg_cons = pg_cons;
	}
	return 0;
}

static inline u16
bnx2_get_hw_rx_cons(struct bnx2_napi *bnapi)
{
	u16 cons;

	/* Tell compiler that status block fields can change. */
	barrier();
	cons = *bnapi->hw_rx_cons_ptr;
	if (unlikely((cons & MAX_RX_DESC_CNT) == MAX_RX_DESC_CNT))
		cons++;
	return cons;
}

static int
bnx2_rx_int(struct bnx2 *bp, struct bnx2_napi *bnapi, int budget)
{
	struct bnx2_rx_ring_info *rxr = &bnapi->rx_ring;
	u16 hw_cons, sw_cons, sw_ring_cons, sw_prod, sw_ring_prod;
	struct l2_fhdr *rx_hdr;
	int rx_pkt = 0, pg_ring_used = 0;

	hw_cons = bnx2_get_hw_rx_cons(bnapi);
	sw_cons = rxr->rx_cons;
	sw_prod = rxr->rx_prod;

	/* Memory barrier necessary as speculative reads of the rx
	 * buffer can be ahead of the index in the status block
	 */
	rmb();
	while (sw_cons != hw_cons) {
		unsigned int len, hdr_len;
		u32 status;
		struct sw_bd *rx_buf;
		struct sk_buff *skb;
		dma_addr_t dma_addr;
		u16 vtag = 0;
		int hw_vlan __maybe_unused = 0;

		sw_ring_cons = RX_RING_IDX(sw_cons);
		sw_ring_prod = RX_RING_IDX(sw_prod);

		rx_buf = &rxr->rx_buf_ring[sw_ring_cons];
		skb = rx_buf->skb;

		rx_buf->skb = NULL;

		dma_addr = pci_unmap_addr(rx_buf, mapping);

		pci_dma_sync_single_for_cpu(bp->pdev, dma_addr,
			BNX2_RX_OFFSET + BNX2_RX_COPY_THRESH,
			PCI_DMA_FROMDEVICE);

		rx_hdr = (struct l2_fhdr *) skb->data;
		len = rx_hdr->l2_fhdr_pkt_len;

		if ((status = rx_hdr->l2_fhdr_status) &
			(L2_FHDR_ERRORS_BAD_CRC |
			L2_FHDR_ERRORS_PHY_DECODE |
			L2_FHDR_ERRORS_ALIGNMENT |
			L2_FHDR_ERRORS_TOO_SHORT |
			L2_FHDR_ERRORS_GIANT_FRAME)) {

			bnx2_reuse_rx_skb(bp, rxr, skb, sw_ring_cons,
					  sw_ring_prod);
			goto next_rx;
		}
		hdr_len = 0;
		if (status & L2_FHDR_STATUS_SPLIT) {
			hdr_len = rx_hdr->l2_fhdr_ip_xsum;
			pg_ring_used = 1;
		} else if (len > bp->rx_jumbo_thresh) {
			hdr_len = bp->rx_jumbo_thresh;
			pg_ring_used = 1;
		}

		len -= 4;

		if (len <= bp->rx_copy_thresh) {
			struct sk_buff *new_skb;

			new_skb = netdev_alloc_skb(bp->dev, len + 6);
			if (new_skb == NULL) {
				bnx2_reuse_rx_skb(bp, rxr, skb, sw_ring_cons,
						  sw_ring_prod);
				goto next_rx;
			}

			/* aligned copy */
			skb_copy_from_linear_data_offset(skb,
							 BNX2_RX_OFFSET - 6,
				      new_skb->data, len + 6);
			skb_reserve(new_skb, 6);
			skb_put(new_skb, len);

			bnx2_reuse_rx_skb(bp, rxr, skb,
				sw_ring_cons, sw_ring_prod);

			skb = new_skb;
		} else if (unlikely(bnx2_rx_skb(bp, rxr, skb, len, hdr_len,
			   dma_addr, (sw_ring_cons << 16) | sw_ring_prod)))
			goto next_rx;

		if ((status & L2_FHDR_STATUS_L2_VLAN_TAG) &&
		    !(bp->rx_mode & BNX2_EMAC_RX_MODE_KEEP_VLAN_TAG)) {
			vtag = rx_hdr->l2_fhdr_vlan_tag;
#ifdef BCM_VLAN
			if (bp->vlgrp)
				hw_vlan = 1;
			else
#endif
			{
				struct vlan_ethhdr *ve = (struct vlan_ethhdr *)
					__skb_push(skb, 4);

				memmove(ve, skb->data + 4, ETH_ALEN * 2);
				ve->h_vlan_proto = htons(ETH_P_8021Q);
				ve->h_vlan_TCI = htons(vtag);
				len += 4;
			}
		}

		skb->protocol = eth_type_trans(skb, bp->dev);

		if ((len > (bp->dev->mtu + ETH_HLEN)) &&
			(ntohs(skb->protocol) != 0x8100)) {

			dev_kfree_skb(skb);
			goto next_rx;

		}

		skb->ip_summed = CHECKSUM_NONE;
		if (bp->rx_csum &&
			(status & (L2_FHDR_STATUS_TCP_SEGMENT |
			L2_FHDR_STATUS_UDP_DATAGRAM))) {

			if (likely((status & (L2_FHDR_ERRORS_TCP_XSUM |
					      L2_FHDR_ERRORS_UDP_XSUM)) == 0))
				skb->ip_summed = CHECKSUM_UNNECESSARY;
		}

#ifdef BCM_VLAN
		if (hw_vlan)
			vlan_hwaccel_receive_skb(skb, bp->vlgrp, vtag);
		else
#endif
			netif_receive_skb(skb);

		bp->dev->last_rx = jiffies;
		rx_pkt++;

next_rx:
		sw_cons = NEXT_RX_BD(sw_cons);
		sw_prod = NEXT_RX_BD(sw_prod);

		if ((rx_pkt == budget))
			break;

		/* Refresh hw_cons to see if there is new work */
		if (sw_cons == hw_cons) {
			hw_cons = bnx2_get_hw_rx_cons(bnapi);
			rmb();
		}
	}
	rxr->rx_cons = sw_cons;
	rxr->rx_prod = sw_prod;

	if (pg_ring_used)
		REG_WR16(bp, rxr->rx_pg_bidx_addr, rxr->rx_pg_prod);

	REG_WR16(bp, rxr->rx_bidx_addr, sw_prod);

	REG_WR(bp, rxr->rx_bseq_addr, rxr->rx_prod_bseq);

	mmiowb();

	return rx_pkt;

}

/* MSI ISR - The only difference between this and the INTx ISR
 * is that the MSI interrupt is always serviced.
 */
static irqreturn_t
bnx2_msi(int irq, void *dev_instance)
{
	struct bnx2_napi *bnapi = dev_instance;
	struct bnx2 *bp = bnapi->bp;
	struct net_device *dev = bp->dev;

	prefetch(bnapi->status_blk.msi);
	REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD,
		BNX2_PCICFG_INT_ACK_CMD_USE_INT_HC_PARAM |
		BNX2_PCICFG_INT_ACK_CMD_MASK_INT);

	/* Return here if interrupt is disabled. */
	if (unlikely(atomic_read(&bp->intr_sem) != 0))
		return IRQ_HANDLED;

	netif_rx_schedule(dev, &bnapi->napi);

	return IRQ_HANDLED;
}

static irqreturn_t
bnx2_msi_1shot(int irq, void *dev_instance)
{
	struct bnx2_napi *bnapi = dev_instance;
	struct bnx2 *bp = bnapi->bp;
	struct net_device *dev = bp->dev;

	prefetch(bnapi->status_blk.msi);

	/* Return here if interrupt is disabled. */
	if (unlikely(atomic_read(&bp->intr_sem) != 0))
		return IRQ_HANDLED;

	netif_rx_schedule(dev, &bnapi->napi);

	return IRQ_HANDLED;
}

static irqreturn_t
bnx2_interrupt(int irq, void *dev_instance)
{
	struct bnx2_napi *bnapi = dev_instance;
	struct bnx2 *bp = bnapi->bp;
	struct net_device *dev = bp->dev;
	struct status_block *sblk = bnapi->status_blk.msi;

	/* When using INTx, it is possible for the interrupt to arrive
	 * at the CPU before the status block posted prior to the
	 * interrupt. Reading a register will flush the status block.
	 * When using MSI, the MSI message will always complete after
	 * the status block write.
	 */
	if ((sblk->status_idx == bnapi->last_status_idx) &&
	    (REG_RD(bp, BNX2_PCICFG_MISC_STATUS) &
	     BNX2_PCICFG_MISC_STATUS_INTA_VALUE))
		return IRQ_NONE;

	REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD,
		BNX2_PCICFG_INT_ACK_CMD_USE_INT_HC_PARAM |
		BNX2_PCICFG_INT_ACK_CMD_MASK_INT);

	/* Read back to deassert IRQ immediately to avoid too many
	 * spurious interrupts.
	 */
	REG_RD(bp, BNX2_PCICFG_INT_ACK_CMD);

	/* Return here if interrupt is shared and is disabled. */
	if (unlikely(atomic_read(&bp->intr_sem) != 0))
		return IRQ_HANDLED;

	if (netif_rx_schedule_prep(dev, &bnapi->napi)) {
		bnapi->last_status_idx = sblk->status_idx;
		__netif_rx_schedule(dev, &bnapi->napi);
	}

	return IRQ_HANDLED;
}

static inline int
bnx2_has_fast_work(struct bnx2_napi *bnapi)
{
	struct bnx2_tx_ring_info *txr = &bnapi->tx_ring;
	struct bnx2_rx_ring_info *rxr = &bnapi->rx_ring;

	if ((bnx2_get_hw_rx_cons(bnapi) != rxr->rx_cons) ||
	    (bnx2_get_hw_tx_cons(bnapi) != txr->hw_tx_cons))
		return 1;
	return 0;
}

#define STATUS_ATTN_EVENTS	(STATUS_ATTN_BITS_LINK_STATE | \
				 STATUS_ATTN_BITS_TIMER_ABORT)

static inline int
bnx2_has_work(struct bnx2_napi *bnapi)
{
	struct status_block *sblk = bnapi->status_blk.msi;

	if (bnx2_has_fast_work(bnapi))
		return 1;

	if ((sblk->status_attn_bits & STATUS_ATTN_EVENTS) !=
	    (sblk->status_attn_bits_ack & STATUS_ATTN_EVENTS))
		return 1;

	return 0;
}

static void bnx2_poll_link(struct bnx2 *bp, struct bnx2_napi *bnapi)
{
	struct status_block *sblk = bnapi->status_blk.msi;
	u32 status_attn_bits = sblk->status_attn_bits;
	u32 status_attn_bits_ack = sblk->status_attn_bits_ack;

	if ((status_attn_bits & STATUS_ATTN_EVENTS) !=
	    (status_attn_bits_ack & STATUS_ATTN_EVENTS)) {

		bnx2_phy_int(bp, bnapi);

		/* This is needed to take care of transient status
		 * during link changes.
		 */
		REG_WR(bp, BNX2_HC_COMMAND,
		       bp->hc_cmd | BNX2_HC_COMMAND_COAL_NOW_WO_INT);
		REG_RD(bp, BNX2_HC_COMMAND);
	}
}

static int bnx2_poll_work(struct bnx2 *bp, struct bnx2_napi *bnapi,
			  int work_done, int budget)
{
	struct bnx2_tx_ring_info *txr = &bnapi->tx_ring;
	struct bnx2_rx_ring_info *rxr = &bnapi->rx_ring;

	if (bnx2_get_hw_tx_cons(bnapi) != txr->hw_tx_cons)
		bnx2_tx_int(bp, bnapi, 0);

	if (bnx2_get_hw_rx_cons(bnapi) != rxr->rx_cons)
		work_done += bnx2_rx_int(bp, bnapi, budget - work_done);

	return work_done;
}

static int bnx2_poll_msix(struct napi_struct *napi, int budget)
{
	struct bnx2_napi *bnapi = container_of(napi, struct bnx2_napi, napi);
	struct bnx2 *bp = bnapi->bp;
	int work_done = 0;
	struct status_block_msix *sblk = bnapi->status_blk.msix;

	while (1) {
		work_done = bnx2_poll_work(bp, bnapi, work_done, budget);
		if (unlikely(work_done >= budget))
			break;

		bnapi->last_status_idx = sblk->status_idx;
		/* status idx must be read before checking for more work. */
		rmb();
		if (likely(!bnx2_has_fast_work(bnapi))) {

			netif_rx_complete(bp->dev, napi);
			REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD, bnapi->int_num |
			       BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID |
			       bnapi->last_status_idx);
			break;
		}
	}
	return work_done;
}

static int bnx2_poll(struct napi_struct *napi, int budget)
{
	struct bnx2_napi *bnapi = container_of(napi, struct bnx2_napi, napi);
	struct bnx2 *bp = bnapi->bp;
	int work_done = 0;
	struct status_block *sblk = bnapi->status_blk.msi;

	while (1) {
		bnx2_poll_link(bp, bnapi);

		work_done = bnx2_poll_work(bp, bnapi, work_done, budget);

		if (unlikely(work_done >= budget))
			break;

		/* bnapi->last_status_idx is used below to tell the hw how
		 * much work has been processed, so we must read it before
		 * checking for more work.
		 */
		bnapi->last_status_idx = sblk->status_idx;
		rmb();
		if (likely(!bnx2_has_work(bnapi))) {
			netif_rx_complete(bp->dev, napi);
			if (likely(bp->flags & BNX2_FLAG_USING_MSI_OR_MSIX)) {
				REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD,
				       BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID |
				       bnapi->last_status_idx);
				break;
			}
			REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD,
			       BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID |
			       BNX2_PCICFG_INT_ACK_CMD_MASK_INT |
			       bnapi->last_status_idx);

			REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD,
			       BNX2_PCICFG_INT_ACK_CMD_INDEX_VALID |
			       bnapi->last_status_idx);
			break;
		}
	}

	return work_done;
}

/* Called with rtnl_lock from vlan functions and also netif_tx_lock
 * from set_multicast.
 */
static void
bnx2_set_rx_mode(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);
	u32 rx_mode, sort_mode;
	struct dev_addr_list *uc_ptr;
	int i;

	if (!netif_running(dev))
		return;

	spin_lock_bh(&bp->phy_lock);

	rx_mode = bp->rx_mode & ~(BNX2_EMAC_RX_MODE_PROMISCUOUS |
				  BNX2_EMAC_RX_MODE_KEEP_VLAN_TAG);
	sort_mode = 1 | BNX2_RPM_SORT_USER0_BC_EN;
#ifdef BCM_VLAN
	if (!bp->vlgrp && (bp->flags & BNX2_FLAG_CAN_KEEP_VLAN))
		rx_mode |= BNX2_EMAC_RX_MODE_KEEP_VLAN_TAG;
#else
	if (bp->flags & BNX2_FLAG_CAN_KEEP_VLAN)
		rx_mode |= BNX2_EMAC_RX_MODE_KEEP_VLAN_TAG;
#endif
	if (dev->flags & IFF_PROMISC) {
		/* Promiscuous mode. */
		rx_mode |= BNX2_EMAC_RX_MODE_PROMISCUOUS;
		sort_mode |= BNX2_RPM_SORT_USER0_PROM_EN |
			     BNX2_RPM_SORT_USER0_PROM_VLAN;
	}
	else if (dev->flags & IFF_ALLMULTI) {
		for (i = 0; i < NUM_MC_HASH_REGISTERS; i++) {
			REG_WR(bp, BNX2_EMAC_MULTICAST_HASH0 + (i * 4),
			       0xffffffff);
        	}
		sort_mode |= BNX2_RPM_SORT_USER0_MC_EN;
	}
	else {
		/* Accept one or more multicast(s). */
		struct dev_mc_list *mclist;
		u32 mc_filter[NUM_MC_HASH_REGISTERS];
		u32 regidx;
		u32 bit;
		u32 crc;

		memset(mc_filter, 0, 4 * NUM_MC_HASH_REGISTERS);

		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
		     i++, mclist = mclist->next) {

			crc = ether_crc_le(ETH_ALEN, mclist->dmi_addr);
			bit = crc & 0xff;
			regidx = (bit & 0xe0) >> 5;
			bit &= 0x1f;
			mc_filter[regidx] |= (1 << bit);
		}

		for (i = 0; i < NUM_MC_HASH_REGISTERS; i++) {
			REG_WR(bp, BNX2_EMAC_MULTICAST_HASH0 + (i * 4),
			       mc_filter[i]);
		}

		sort_mode |= BNX2_RPM_SORT_USER0_MC_HSH_EN;
	}

	uc_ptr = NULL;
	if (dev->uc_count > BNX2_MAX_UNICAST_ADDRESSES) {
		rx_mode |= BNX2_EMAC_RX_MODE_PROMISCUOUS;
		sort_mode |= BNX2_RPM_SORT_USER0_PROM_EN |
			     BNX2_RPM_SORT_USER0_PROM_VLAN;
	} else if (!(dev->flags & IFF_PROMISC)) {
		uc_ptr = dev->uc_list;

		/* Add all entries into to the match filter list */
		for (i = 0; i < dev->uc_count; i++) {
			bnx2_set_mac_addr(bp, uc_ptr->da_addr,
					  i + BNX2_START_UNICAST_ADDRESS_INDEX);
			sort_mode |= (1 <<
				      (i + BNX2_START_UNICAST_ADDRESS_INDEX));
			uc_ptr = uc_ptr->next;
		}

	}

	if (rx_mode != bp->rx_mode) {
		bp->rx_mode = rx_mode;
		REG_WR(bp, BNX2_EMAC_RX_MODE, rx_mode);
	}

	REG_WR(bp, BNX2_RPM_SORT_USER0, 0x0);
	REG_WR(bp, BNX2_RPM_SORT_USER0, sort_mode);
	REG_WR(bp, BNX2_RPM_SORT_USER0, sort_mode | BNX2_RPM_SORT_USER0_ENA);

	spin_unlock_bh(&bp->phy_lock);
}

static void
load_rv2p_fw(struct bnx2 *bp, __le32 *rv2p_code, u32 rv2p_code_len,
	u32 rv2p_proc)
{
	int i;
	u32 val;

	if (rv2p_proc == RV2P_PROC2 && CHIP_NUM(bp) == CHIP_NUM_5709) {
		val = le32_to_cpu(rv2p_code[XI_RV2P_PROC2_MAX_BD_PAGE_LOC]);
		val &= ~XI_RV2P_PROC2_BD_PAGE_SIZE_MSK;
		val |= XI_RV2P_PROC2_BD_PAGE_SIZE;
		rv2p_code[XI_RV2P_PROC2_MAX_BD_PAGE_LOC] = cpu_to_le32(val);
	}

	for (i = 0; i < rv2p_code_len; i += 8) {
		REG_WR(bp, BNX2_RV2P_INSTR_HIGH, le32_to_cpu(*rv2p_code));
		rv2p_code++;
		REG_WR(bp, BNX2_RV2P_INSTR_LOW, le32_to_cpu(*rv2p_code));
		rv2p_code++;

		if (rv2p_proc == RV2P_PROC1) {
			val = (i / 8) | BNX2_RV2P_PROC1_ADDR_CMD_RDWR;
			REG_WR(bp, BNX2_RV2P_PROC1_ADDR_CMD, val);
		}
		else {
			val = (i / 8) | BNX2_RV2P_PROC2_ADDR_CMD_RDWR;
			REG_WR(bp, BNX2_RV2P_PROC2_ADDR_CMD, val);
		}
	}

	/* Reset the processor, un-stall is done later. */
	if (rv2p_proc == RV2P_PROC1) {
		REG_WR(bp, BNX2_RV2P_COMMAND, BNX2_RV2P_COMMAND_PROC1_RESET);
	}
	else {
		REG_WR(bp, BNX2_RV2P_COMMAND, BNX2_RV2P_COMMAND_PROC2_RESET);
	}
}

static int
load_cpu_fw(struct bnx2 *bp, const struct cpu_reg *cpu_reg, struct fw_info *fw)
{
	u32 offset;
	u32 val;
	int rc;

	/* Halt the CPU. */
	val = bnx2_reg_rd_ind(bp, cpu_reg->mode);
	val |= cpu_reg->mode_value_halt;
	bnx2_reg_wr_ind(bp, cpu_reg->mode, val);
	bnx2_reg_wr_ind(bp, cpu_reg->state, cpu_reg->state_value_clear);

	/* Load the Text area. */
	offset = cpu_reg->spad_base + (fw->text_addr - cpu_reg->mips_view_base);
	if (fw->gz_text) {
		int j;

		rc = zlib_inflate_blob(fw->text, FW_BUF_SIZE, fw->gz_text,
				       fw->gz_text_len);
		if (rc < 0)
			return rc;

		for (j = 0; j < (fw->text_len / 4); j++, offset += 4) {
			bnx2_reg_wr_ind(bp, offset, le32_to_cpu(fw->text[j]));
	        }
	}

	/* Load the Data area. */
	offset = cpu_reg->spad_base + (fw->data_addr - cpu_reg->mips_view_base);
	if (fw->data) {
		int j;

		for (j = 0; j < (fw->data_len / 4); j++, offset += 4) {
			bnx2_reg_wr_ind(bp, offset, fw->data[j]);
		}
	}

	/* Load the SBSS area. */
	offset = cpu_reg->spad_base + (fw->sbss_addr - cpu_reg->mips_view_base);
	if (fw->sbss_len) {
		int j;

		for (j = 0; j < (fw->sbss_len / 4); j++, offset += 4) {
			bnx2_reg_wr_ind(bp, offset, 0);
		}
	}

	/* Load the BSS area. */
	offset = cpu_reg->spad_base + (fw->bss_addr - cpu_reg->mips_view_base);
	if (fw->bss_len) {
		int j;

		for (j = 0; j < (fw->bss_len/4); j++, offset += 4) {
			bnx2_reg_wr_ind(bp, offset, 0);
		}
	}

	/* Load the Read-Only area. */
	offset = cpu_reg->spad_base +
		(fw->rodata_addr - cpu_reg->mips_view_base);
	if (fw->rodata) {
		int j;

		for (j = 0; j < (fw->rodata_len / 4); j++, offset += 4) {
			bnx2_reg_wr_ind(bp, offset, fw->rodata[j]);
		}
	}

	/* Clear the pre-fetch instruction. */
	bnx2_reg_wr_ind(bp, cpu_reg->inst, 0);
	bnx2_reg_wr_ind(bp, cpu_reg->pc, fw->start_addr);

	/* Start the CPU. */
	val = bnx2_reg_rd_ind(bp, cpu_reg->mode);
	val &= ~cpu_reg->mode_value_halt;
	bnx2_reg_wr_ind(bp, cpu_reg->state, cpu_reg->state_value_clear);
	bnx2_reg_wr_ind(bp, cpu_reg->mode, val);

	return 0;
}

static int
bnx2_init_cpus(struct bnx2 *bp)
{
	struct fw_info *fw;
	int rc, rv2p_len;
	void *text, *rv2p;

	/* Initialize the RV2P processor. */
	text = vmalloc(FW_BUF_SIZE);
	if (!text)
		return -ENOMEM;
	if (CHIP_NUM(bp) == CHIP_NUM_5709) {
		rv2p = bnx2_xi_rv2p_proc1;
		rv2p_len = sizeof(bnx2_xi_rv2p_proc1);
	} else {
		rv2p = bnx2_rv2p_proc1;
		rv2p_len = sizeof(bnx2_rv2p_proc1);
	}
	rc = zlib_inflate_blob(text, FW_BUF_SIZE, rv2p, rv2p_len);
	if (rc < 0)
		goto init_cpu_err;

	load_rv2p_fw(bp, text, rc /* == len */, RV2P_PROC1);

	if (CHIP_NUM(bp) == CHIP_NUM_5709) {
		rv2p = bnx2_xi_rv2p_proc2;
		rv2p_len = sizeof(bnx2_xi_rv2p_proc2);
	} else {
		rv2p = bnx2_rv2p_proc2;
		rv2p_len = sizeof(bnx2_rv2p_proc2);
	}
	rc = zlib_inflate_blob(text, FW_BUF_SIZE, rv2p, rv2p_len);
	if (rc < 0)
		goto init_cpu_err;

	load_rv2p_fw(bp, text, rc /* == len */, RV2P_PROC2);

	/* Initialize the RX Processor. */
	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		fw = &bnx2_rxp_fw_09;
	else
		fw = &bnx2_rxp_fw_06;

	fw->text = text;
	rc = load_cpu_fw(bp, &cpu_reg_rxp, fw);
	if (rc)
		goto init_cpu_err;

	/* Initialize the TX Processor. */
	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		fw = &bnx2_txp_fw_09;
	else
		fw = &bnx2_txp_fw_06;

	fw->text = text;
	rc = load_cpu_fw(bp, &cpu_reg_txp, fw);
	if (rc)
		goto init_cpu_err;

	/* Initialize the TX Patch-up Processor. */
	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		fw = &bnx2_tpat_fw_09;
	else
		fw = &bnx2_tpat_fw_06;

	fw->text = text;
	rc = load_cpu_fw(bp, &cpu_reg_tpat, fw);
	if (rc)
		goto init_cpu_err;

	/* Initialize the Completion Processor. */
	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		fw = &bnx2_com_fw_09;
	else
		fw = &bnx2_com_fw_06;

	fw->text = text;
	rc = load_cpu_fw(bp, &cpu_reg_com, fw);
	if (rc)
		goto init_cpu_err;

	/* Initialize the Command Processor. */
	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		fw = &bnx2_cp_fw_09;
	else
		fw = &bnx2_cp_fw_06;

	fw->text = text;
	rc = load_cpu_fw(bp, &cpu_reg_cp, fw);

init_cpu_err:
	vfree(text);
	return rc;
}

static int
bnx2_set_power_state(struct bnx2 *bp, pci_power_t state)
{
	u16 pmcsr;

	pci_read_config_word(bp->pdev, bp->pm_cap + PCI_PM_CTRL, &pmcsr);

	switch (state) {
	case PCI_D0: {
		u32 val;

		pci_write_config_word(bp->pdev, bp->pm_cap + PCI_PM_CTRL,
			(pmcsr & ~PCI_PM_CTRL_STATE_MASK) |
			PCI_PM_CTRL_PME_STATUS);

		if (pmcsr & PCI_PM_CTRL_STATE_MASK)
			/* delay required during transition out of D3hot */
			msleep(20);

		val = REG_RD(bp, BNX2_EMAC_MODE);
		val |= BNX2_EMAC_MODE_MPKT_RCVD | BNX2_EMAC_MODE_ACPI_RCVD;
		val &= ~BNX2_EMAC_MODE_MPKT;
		REG_WR(bp, BNX2_EMAC_MODE, val);

		val = REG_RD(bp, BNX2_RPM_CONFIG);
		val &= ~BNX2_RPM_CONFIG_ACPI_ENA;
		REG_WR(bp, BNX2_RPM_CONFIG, val);
		break;
	}
	case PCI_D3hot: {
		int i;
		u32 val, wol_msg;

		if (bp->wol) {
			u32 advertising;
			u8 autoneg;

			autoneg = bp->autoneg;
			advertising = bp->advertising;

			if (bp->phy_port == PORT_TP) {
				bp->autoneg = AUTONEG_SPEED;
				bp->advertising = ADVERTISED_10baseT_Half |
					ADVERTISED_10baseT_Full |
					ADVERTISED_100baseT_Half |
					ADVERTISED_100baseT_Full |
					ADVERTISED_Autoneg;
			}

			spin_lock_bh(&bp->phy_lock);
			bnx2_setup_phy(bp, bp->phy_port);
			spin_unlock_bh(&bp->phy_lock);

			bp->autoneg = autoneg;
			bp->advertising = advertising;

			bnx2_set_mac_addr(bp, bp->dev->dev_addr, 0);

			val = REG_RD(bp, BNX2_EMAC_MODE);

			/* Enable port mode. */
			val &= ~BNX2_EMAC_MODE_PORT;
			val |= BNX2_EMAC_MODE_MPKT_RCVD |
			       BNX2_EMAC_MODE_ACPI_RCVD |
			       BNX2_EMAC_MODE_MPKT;
			if (bp->phy_port == PORT_TP)
				val |= BNX2_EMAC_MODE_PORT_MII;
			else {
				val |= BNX2_EMAC_MODE_PORT_GMII;
				if (bp->line_speed == SPEED_2500)
					val |= BNX2_EMAC_MODE_25G_MODE;
			}

			REG_WR(bp, BNX2_EMAC_MODE, val);

			/* receive all multicast */
			for (i = 0; i < NUM_MC_HASH_REGISTERS; i++) {
				REG_WR(bp, BNX2_EMAC_MULTICAST_HASH0 + (i * 4),
				       0xffffffff);
			}
			REG_WR(bp, BNX2_EMAC_RX_MODE,
			       BNX2_EMAC_RX_MODE_SORT_MODE);

			val = 1 | BNX2_RPM_SORT_USER0_BC_EN |
			      BNX2_RPM_SORT_USER0_MC_EN;
			REG_WR(bp, BNX2_RPM_SORT_USER0, 0x0);
			REG_WR(bp, BNX2_RPM_SORT_USER0, val);
			REG_WR(bp, BNX2_RPM_SORT_USER0, val |
			       BNX2_RPM_SORT_USER0_ENA);

			/* Need to enable EMAC and RPM for WOL. */
			REG_WR(bp, BNX2_MISC_ENABLE_SET_BITS,
			       BNX2_MISC_ENABLE_SET_BITS_RX_PARSER_MAC_ENABLE |
			       BNX2_MISC_ENABLE_SET_BITS_TX_HEADER_Q_ENABLE |
			       BNX2_MISC_ENABLE_SET_BITS_EMAC_ENABLE);

			val = REG_RD(bp, BNX2_RPM_CONFIG);
			val &= ~BNX2_RPM_CONFIG_ACPI_ENA;
			REG_WR(bp, BNX2_RPM_CONFIG, val);

			wol_msg = BNX2_DRV_MSG_CODE_SUSPEND_WOL;
		}
		else {
			wol_msg = BNX2_DRV_MSG_CODE_SUSPEND_NO_WOL;
		}

		if (!(bp->flags & BNX2_FLAG_NO_WOL))
			bnx2_fw_sync(bp, BNX2_DRV_MSG_DATA_WAIT3 | wol_msg,
				     1, 0);

		pmcsr &= ~PCI_PM_CTRL_STATE_MASK;
		if ((CHIP_ID(bp) == CHIP_ID_5706_A0) ||
		    (CHIP_ID(bp) == CHIP_ID_5706_A1)) {

			if (bp->wol)
				pmcsr |= 3;
		}
		else {
			pmcsr |= 3;
		}
		if (bp->wol) {
			pmcsr |= PCI_PM_CTRL_PME_ENABLE;
		}
		pci_write_config_word(bp->pdev, bp->pm_cap + PCI_PM_CTRL,
				      pmcsr);

		/* No more memory access after this point until
		 * device is brought back to D0.
		 */
		udelay(50);
		break;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static int
bnx2_acquire_nvram_lock(struct bnx2 *bp)
{
	u32 val;
	int j;

	/* Request access to the flash interface. */
	REG_WR(bp, BNX2_NVM_SW_ARB, BNX2_NVM_SW_ARB_ARB_REQ_SET2);
	for (j = 0; j < NVRAM_TIMEOUT_COUNT; j++) {
		val = REG_RD(bp, BNX2_NVM_SW_ARB);
		if (val & BNX2_NVM_SW_ARB_ARB_ARB2)
			break;

		udelay(5);
	}

	if (j >= NVRAM_TIMEOUT_COUNT)
		return -EBUSY;

	return 0;
}

static int
bnx2_release_nvram_lock(struct bnx2 *bp)
{
	int j;
	u32 val;

	/* Relinquish nvram interface. */
	REG_WR(bp, BNX2_NVM_SW_ARB, BNX2_NVM_SW_ARB_ARB_REQ_CLR2);

	for (j = 0; j < NVRAM_TIMEOUT_COUNT; j++) {
		val = REG_RD(bp, BNX2_NVM_SW_ARB);
		if (!(val & BNX2_NVM_SW_ARB_ARB_ARB2))
			break;

		udelay(5);
	}

	if (j >= NVRAM_TIMEOUT_COUNT)
		return -EBUSY;

	return 0;
}


static int
bnx2_enable_nvram_write(struct bnx2 *bp)
{
	u32 val;

	val = REG_RD(bp, BNX2_MISC_CFG);
	REG_WR(bp, BNX2_MISC_CFG, val | BNX2_MISC_CFG_NVM_WR_EN_PCI);

	if (bp->flash_info->flags & BNX2_NV_WREN) {
		int j;

		REG_WR(bp, BNX2_NVM_COMMAND, BNX2_NVM_COMMAND_DONE);
		REG_WR(bp, BNX2_NVM_COMMAND,
		       BNX2_NVM_COMMAND_WREN | BNX2_NVM_COMMAND_DOIT);

		for (j = 0; j < NVRAM_TIMEOUT_COUNT; j++) {
			udelay(5);

			val = REG_RD(bp, BNX2_NVM_COMMAND);
			if (val & BNX2_NVM_COMMAND_DONE)
				break;
		}

		if (j >= NVRAM_TIMEOUT_COUNT)
			return -EBUSY;
	}
	return 0;
}

static void
bnx2_disable_nvram_write(struct bnx2 *bp)
{
	u32 val;

	val = REG_RD(bp, BNX2_MISC_CFG);
	REG_WR(bp, BNX2_MISC_CFG, val & ~BNX2_MISC_CFG_NVM_WR_EN);
}


static void
bnx2_enable_nvram_access(struct bnx2 *bp)
{
	u32 val;

	val = REG_RD(bp, BNX2_NVM_ACCESS_ENABLE);
	/* Enable both bits, even on read. */
	REG_WR(bp, BNX2_NVM_ACCESS_ENABLE,
	       val | BNX2_NVM_ACCESS_ENABLE_EN | BNX2_NVM_ACCESS_ENABLE_WR_EN);
}

static void
bnx2_disable_nvram_access(struct bnx2 *bp)
{
	u32 val;

	val = REG_RD(bp, BNX2_NVM_ACCESS_ENABLE);
	/* Disable both bits, even after read. */
	REG_WR(bp, BNX2_NVM_ACCESS_ENABLE,
		val & ~(BNX2_NVM_ACCESS_ENABLE_EN |
			BNX2_NVM_ACCESS_ENABLE_WR_EN));
}

static int
bnx2_nvram_erase_page(struct bnx2 *bp, u32 offset)
{
	u32 cmd;
	int j;

	if (bp->flash_info->flags & BNX2_NV_BUFFERED)
		/* Buffered flash, no erase needed */
		return 0;

	/* Build an erase command */
	cmd = BNX2_NVM_COMMAND_ERASE | BNX2_NVM_COMMAND_WR |
	      BNX2_NVM_COMMAND_DOIT;

	/* Need to clear DONE bit separately. */
	REG_WR(bp, BNX2_NVM_COMMAND, BNX2_NVM_COMMAND_DONE);

	/* Address of the NVRAM to read from. */
	REG_WR(bp, BNX2_NVM_ADDR, offset & BNX2_NVM_ADDR_NVM_ADDR_VALUE);

	/* Issue an erase command. */
	REG_WR(bp, BNX2_NVM_COMMAND, cmd);

	/* Wait for completion. */
	for (j = 0; j < NVRAM_TIMEOUT_COUNT; j++) {
		u32 val;

		udelay(5);

		val = REG_RD(bp, BNX2_NVM_COMMAND);
		if (val & BNX2_NVM_COMMAND_DONE)
			break;
	}

	if (j >= NVRAM_TIMEOUT_COUNT)
		return -EBUSY;

	return 0;
}

static int
bnx2_nvram_read_dword(struct bnx2 *bp, u32 offset, u8 *ret_val, u32 cmd_flags)
{
	u32 cmd;
	int j;

	/* Build the command word. */
	cmd = BNX2_NVM_COMMAND_DOIT | cmd_flags;

	/* Calculate an offset of a buffered flash, not needed for 5709. */
	if (bp->flash_info->flags & BNX2_NV_TRANSLATE) {
		offset = ((offset / bp->flash_info->page_size) <<
			   bp->flash_info->page_bits) +
			  (offset % bp->flash_info->page_size);
	}

	/* Need to clear DONE bit separately. */
	REG_WR(bp, BNX2_NVM_COMMAND, BNX2_NVM_COMMAND_DONE);

	/* Address of the NVRAM to read from. */
	REG_WR(bp, BNX2_NVM_ADDR, offset & BNX2_NVM_ADDR_NVM_ADDR_VALUE);

	/* Issue a read command. */
	REG_WR(bp, BNX2_NVM_COMMAND, cmd);

	/* Wait for completion. */
	for (j = 0; j < NVRAM_TIMEOUT_COUNT; j++) {
		u32 val;

		udelay(5);

		val = REG_RD(bp, BNX2_NVM_COMMAND);
		if (val & BNX2_NVM_COMMAND_DONE) {
			__be32 v = cpu_to_be32(REG_RD(bp, BNX2_NVM_READ));
			memcpy(ret_val, &v, 4);
			break;
		}
	}
	if (j >= NVRAM_TIMEOUT_COUNT)
		return -EBUSY;

	return 0;
}


static int
bnx2_nvram_write_dword(struct bnx2 *bp, u32 offset, u8 *val, u32 cmd_flags)
{
	u32 cmd;
	__be32 val32;
	int j;

	/* Build the command word. */
	cmd = BNX2_NVM_COMMAND_DOIT | BNX2_NVM_COMMAND_WR | cmd_flags;

	/* Calculate an offset of a buffered flash, not needed for 5709. */
	if (bp->flash_info->flags & BNX2_NV_TRANSLATE) {
		offset = ((offset / bp->flash_info->page_size) <<
			  bp->flash_info->page_bits) +
			 (offset % bp->flash_info->page_size);
	}

	/* Need to clear DONE bit separately. */
	REG_WR(bp, BNX2_NVM_COMMAND, BNX2_NVM_COMMAND_DONE);

	memcpy(&val32, val, 4);

	/* Write the data. */
	REG_WR(bp, BNX2_NVM_WRITE, be32_to_cpu(val32));

	/* Address of the NVRAM to write to. */
	REG_WR(bp, BNX2_NVM_ADDR, offset & BNX2_NVM_ADDR_NVM_ADDR_VALUE);

	/* Issue the write command. */
	REG_WR(bp, BNX2_NVM_COMMAND, cmd);

	/* Wait for completion. */
	for (j = 0; j < NVRAM_TIMEOUT_COUNT; j++) {
		udelay(5);

		if (REG_RD(bp, BNX2_NVM_COMMAND) & BNX2_NVM_COMMAND_DONE)
			break;
	}
	if (j >= NVRAM_TIMEOUT_COUNT)
		return -EBUSY;

	return 0;
}

static int
bnx2_init_nvram(struct bnx2 *bp)
{
	u32 val;
	int j, entry_count, rc = 0;
	struct flash_spec *flash;

	if (CHIP_NUM(bp) == CHIP_NUM_5709) {
		bp->flash_info = &flash_5709;
		goto get_flash_size;
	}

	/* Determine the selected interface. */
	val = REG_RD(bp, BNX2_NVM_CFG1);

	entry_count = ARRAY_SIZE(flash_table);

	if (val & 0x40000000) {

		/* Flash interface has been reconfigured */
		for (j = 0, flash = &flash_table[0]; j < entry_count;
		     j++, flash++) {
			if ((val & FLASH_BACKUP_STRAP_MASK) ==
			    (flash->config1 & FLASH_BACKUP_STRAP_MASK)) {
				bp->flash_info = flash;
				break;
			}
		}
	}
	else {
		u32 mask;
		/* Not yet been reconfigured */

		if (val & (1 << 23))
			mask = FLASH_BACKUP_STRAP_MASK;
		else
			mask = FLASH_STRAP_MASK;

		for (j = 0, flash = &flash_table[0]; j < entry_count;
			j++, flash++) {

			if ((val & mask) == (flash->strapping & mask)) {
				bp->flash_info = flash;

				/* Request access to the flash interface. */
				if ((rc = bnx2_acquire_nvram_lock(bp)) != 0)
					return rc;

				/* Enable access to flash interface */
				bnx2_enable_nvram_access(bp);

				/* Reconfigure the flash interface */
				REG_WR(bp, BNX2_NVM_CFG1, flash->config1);
				REG_WR(bp, BNX2_NVM_CFG2, flash->config2);
				REG_WR(bp, BNX2_NVM_CFG3, flash->config3);
				REG_WR(bp, BNX2_NVM_WRITE1, flash->write1);

				/* Disable access to flash interface */
				bnx2_disable_nvram_access(bp);
				bnx2_release_nvram_lock(bp);

				break;
			}
		}
	} /* if (val & 0x40000000) */

	if (j == entry_count) {
		bp->flash_info = NULL;
		printk(KERN_ALERT PFX "Unknown flash/EEPROM type.\n");
		return -ENODEV;
	}

get_flash_size:
	val = bnx2_shmem_rd(bp, BNX2_SHARED_HW_CFG_CONFIG2);
	val &= BNX2_SHARED_HW_CFG2_NVM_SIZE_MASK;
	if (val)
		bp->flash_size = val;
	else
		bp->flash_size = bp->flash_info->total_size;

	return rc;
}

static int
bnx2_nvram_read(struct bnx2 *bp, u32 offset, u8 *ret_buf,
		int buf_size)
{
	int rc = 0;
	u32 cmd_flags, offset32, len32, extra;

	if (buf_size == 0)
		return 0;

	/* Request access to the flash interface. */
	if ((rc = bnx2_acquire_nvram_lock(bp)) != 0)
		return rc;

	/* Enable access to flash interface */
	bnx2_enable_nvram_access(bp);

	len32 = buf_size;
	offset32 = offset;
	extra = 0;

	cmd_flags = 0;

	if (offset32 & 3) {
		u8 buf[4];
		u32 pre_len;

		offset32 &= ~3;
		pre_len = 4 - (offset & 3);

		if (pre_len >= len32) {
			pre_len = len32;
			cmd_flags = BNX2_NVM_COMMAND_FIRST |
				    BNX2_NVM_COMMAND_LAST;
		}
		else {
			cmd_flags = BNX2_NVM_COMMAND_FIRST;
		}

		rc = bnx2_nvram_read_dword(bp, offset32, buf, cmd_flags);

		if (rc)
			return rc;

		memcpy(ret_buf, buf + (offset & 3), pre_len);

		offset32 += 4;
		ret_buf += pre_len;
		len32 -= pre_len;
	}
	if (len32 & 3) {
		extra = 4 - (len32 & 3);
		len32 = (len32 + 4) & ~3;
	}

	if (len32 == 4) {
		u8 buf[4];

		if (cmd_flags)
			cmd_flags = BNX2_NVM_COMMAND_LAST;
		else
			cmd_flags = BNX2_NVM_COMMAND_FIRST |
				    BNX2_NVM_COMMAND_LAST;

		rc = bnx2_nvram_read_dword(bp, offset32, buf, cmd_flags);

		memcpy(ret_buf, buf, 4 - extra);
	}
	else if (len32 > 0) {
		u8 buf[4];

		/* Read the first word. */
		if (cmd_flags)
			cmd_flags = 0;
		else
			cmd_flags = BNX2_NVM_COMMAND_FIRST;

		rc = bnx2_nvram_read_dword(bp, offset32, ret_buf, cmd_flags);

		/* Advance to the next dword. */
		offset32 += 4;
		ret_buf += 4;
		len32 -= 4;

		while (len32 > 4 && rc == 0) {
			rc = bnx2_nvram_read_dword(bp, offset32, ret_buf, 0);

			/* Advance to the next dword. */
			offset32 += 4;
			ret_buf += 4;
			len32 -= 4;
		}

		if (rc)
			return rc;

		cmd_flags = BNX2_NVM_COMMAND_LAST;
		rc = bnx2_nvram_read_dword(bp, offset32, buf, cmd_flags);

		memcpy(ret_buf, buf, 4 - extra);
	}

	/* Disable access to flash interface */
	bnx2_disable_nvram_access(bp);

	bnx2_release_nvram_lock(bp);

	return rc;
}

static int
bnx2_nvram_write(struct bnx2 *bp, u32 offset, u8 *data_buf,
		int buf_size)
{
	u32 written, offset32, len32;
	u8 *buf, start[4], end[4], *align_buf = NULL, *flash_buffer = NULL;
	int rc = 0;
	int align_start, align_end;

	buf = data_buf;
	offset32 = offset;
	len32 = buf_size;
	align_start = align_end = 0;

	if ((align_start = (offset32 & 3))) {
		offset32 &= ~3;
		len32 += align_start;
		if (len32 < 4)
			len32 = 4;
		if ((rc = bnx2_nvram_read(bp, offset32, start, 4)))
			return rc;
	}

	if (len32 & 3) {
		align_end = 4 - (len32 & 3);
		len32 += align_end;
		if ((rc = bnx2_nvram_read(bp, offset32 + len32 - 4, end, 4)))
			return rc;
	}

	if (align_start || align_end) {
		align_buf = kmalloc(len32, GFP_KERNEL);
		if (align_buf == NULL)
			return -ENOMEM;
		if (align_start) {
			memcpy(align_buf, start, 4);
		}
		if (align_end) {
			memcpy(align_buf + len32 - 4, end, 4);
		}
		memcpy(align_buf + align_start, data_buf, buf_size);
		buf = align_buf;
	}

	if (!(bp->flash_info->flags & BNX2_NV_BUFFERED)) {
		flash_buffer = kmalloc(264, GFP_KERNEL);
		if (flash_buffer == NULL) {
			rc = -ENOMEM;
			goto nvram_write_end;
		}
	}

	written = 0;
	while ((written < len32) && (rc == 0)) {
		u32 page_start, page_end, data_start, data_end;
		u32 addr, cmd_flags;
		int i;

	        /* Find the page_start addr */
		page_start = offset32 + written;
		page_start -= (page_start % bp->flash_info->page_size);
		/* Find the page_end addr */
		page_end = page_start + bp->flash_info->page_size;
		/* Find the data_start addr */
		data_start = (written == 0) ? offset32 : page_start;
		/* Find the data_end addr */
		data_end = (page_end > offset32 + len32) ?
			(offset32 + len32) : page_end;

		/* Request access to the flash interface. */
		if ((rc = bnx2_acquire_nvram_lock(bp)) != 0)
			goto nvram_write_end;

		/* Enable access to flash interface */
		bnx2_enable_nvram_access(bp);

		cmd_flags = BNX2_NVM_COMMAND_FIRST;
		if (!(bp->flash_info->flags & BNX2_NV_BUFFERED)) {
			int j;

			/* Read the whole page into the buffer
			 * (non-buffer flash only) */
			for (j = 0; j < bp->flash_info->page_size; j += 4) {
				if (j == (bp->flash_info->page_size - 4)) {
					cmd_flags |= BNX2_NVM_COMMAND_LAST;
				}
				rc = bnx2_nvram_read_dword(bp,
					page_start + j,
					&flash_buffer[j],
					cmd_flags);

				if (rc)
					goto nvram_write_end;

				cmd_flags = 0;
			}
		}

		/* Enable writes to flash interface (unlock write-protect) */
		if ((rc = bnx2_enable_nvram_write(bp)) != 0)
			goto nvram_write_end;

		/* Loop to write back the buffer data from page_start to
		 * data_start */
		i = 0;
		if (!(bp->flash_info->flags & BNX2_NV_BUFFERED)) {
			/* Erase the page */
			if ((rc = bnx2_nvram_erase_page(bp, page_start)) != 0)
				goto nvram_write_end;

			/* Re-enable the write again for the actual write */
			bnx2_enable_nvram_write(bp);

			for (addr = page_start; addr < data_start;
				addr += 4, i += 4) {

				rc = bnx2_nvram_write_dword(bp, addr,
					&flash_buffer[i], cmd_flags);

				if (rc != 0)
					goto nvram_write_end;

				cmd_flags = 0;
			}
		}

		/* Loop to write the new data from data_start to data_end */
		for (addr = data_start; addr < data_end; addr += 4, i += 4) {
			if ((addr == page_end - 4) ||
				((bp->flash_info->flags & BNX2_NV_BUFFERED) &&
				 (addr == data_end - 4))) {

				cmd_flags |= BNX2_NVM_COMMAND_LAST;
			}
			rc = bnx2_nvram_write_dword(bp, addr, buf,
				cmd_flags);

			if (rc != 0)
				goto nvram_write_end;

			cmd_flags = 0;
			buf += 4;
		}

		/* Loop to write back the buffer data from data_end
		 * to page_end */
		if (!(bp->flash_info->flags & BNX2_NV_BUFFERED)) {
			for (addr = data_end; addr < page_end;
				addr += 4, i += 4) {

				if (addr == page_end-4) {
					cmd_flags = BNX2_NVM_COMMAND_LAST;
                		}
				rc = bnx2_nvram_write_dword(bp, addr,
					&flash_buffer[i], cmd_flags);

				if (rc != 0)
					goto nvram_write_end;

				cmd_flags = 0;
			}
		}

		/* Disable writes to flash interface (lock write-protect) */
		bnx2_disable_nvram_write(bp);

		/* Disable access to flash interface */
		bnx2_disable_nvram_access(bp);
		bnx2_release_nvram_lock(bp);

		/* Increment written */
		written += data_end - data_start;
	}

nvram_write_end:
	kfree(flash_buffer);
	kfree(align_buf);
	return rc;
}

static void
bnx2_init_fw_cap(struct bnx2 *bp)
{
	u32 val, sig = 0;

	bp->phy_flags &= ~BNX2_PHY_FLAG_REMOTE_PHY_CAP;
	bp->flags &= ~BNX2_FLAG_CAN_KEEP_VLAN;

	if (!(bp->flags & BNX2_FLAG_ASF_ENABLE))
		bp->flags |= BNX2_FLAG_CAN_KEEP_VLAN;

	val = bnx2_shmem_rd(bp, BNX2_FW_CAP_MB);
	if ((val & BNX2_FW_CAP_SIGNATURE_MASK) != BNX2_FW_CAP_SIGNATURE)
		return;

	if ((val & BNX2_FW_CAP_CAN_KEEP_VLAN) == BNX2_FW_CAP_CAN_KEEP_VLAN) {
		bp->flags |= BNX2_FLAG_CAN_KEEP_VLAN;
		sig |= BNX2_DRV_ACK_CAP_SIGNATURE | BNX2_FW_CAP_CAN_KEEP_VLAN;
	}

	if ((bp->phy_flags & BNX2_PHY_FLAG_SERDES) &&
	    (val & BNX2_FW_CAP_REMOTE_PHY_CAPABLE)) {
		u32 link;

		bp->phy_flags |= BNX2_PHY_FLAG_REMOTE_PHY_CAP;

		link = bnx2_shmem_rd(bp, BNX2_LINK_STATUS);
		if (link & BNX2_LINK_STATUS_SERDES_LINK)
			bp->phy_port = PORT_FIBRE;
		else
			bp->phy_port = PORT_TP;

		sig |= BNX2_DRV_ACK_CAP_SIGNATURE |
		       BNX2_FW_CAP_REMOTE_PHY_CAPABLE;
	}

	if (netif_running(bp->dev) && sig)
		bnx2_shmem_wr(bp, BNX2_DRV_ACK_CAP_MB, sig);
}

static void
bnx2_setup_msix_tbl(struct bnx2 *bp)
{
	REG_WR(bp, BNX2_PCI_GRC_WINDOW_ADDR, BNX2_PCI_GRC_WINDOW_ADDR_SEP_WIN);

	REG_WR(bp, BNX2_PCI_GRC_WINDOW2_ADDR, BNX2_MSIX_TABLE_ADDR);
	REG_WR(bp, BNX2_PCI_GRC_WINDOW3_ADDR, BNX2_MSIX_PBA_ADDR);
}

static int
bnx2_reset_chip(struct bnx2 *bp, u32 reset_code)
{
	u32 val;
	int i, rc = 0;
	u8 old_port;

	/* Wait for the current PCI transaction to complete before
	 * issuing a reset. */
	REG_WR(bp, BNX2_MISC_ENABLE_CLR_BITS,
	       BNX2_MISC_ENABLE_CLR_BITS_TX_DMA_ENABLE |
	       BNX2_MISC_ENABLE_CLR_BITS_DMA_ENGINE_ENABLE |
	       BNX2_MISC_ENABLE_CLR_BITS_RX_DMA_ENABLE |
	       BNX2_MISC_ENABLE_CLR_BITS_HOST_COALESCE_ENABLE);
	val = REG_RD(bp, BNX2_MISC_ENABLE_CLR_BITS);
	udelay(5);

	/* Wait for the firmware to tell us it is ok to issue a reset. */
	bnx2_fw_sync(bp, BNX2_DRV_MSG_DATA_WAIT0 | reset_code, 1, 1);

	/* Deposit a driver reset signature so the firmware knows that
	 * this is a soft reset. */
	bnx2_shmem_wr(bp, BNX2_DRV_RESET_SIGNATURE,
		      BNX2_DRV_RESET_SIGNATURE_MAGIC);

	/* Do a dummy read to force the chip to complete all current transaction
	 * before we issue a reset. */
	val = REG_RD(bp, BNX2_MISC_ID);

	if (CHIP_NUM(bp) == CHIP_NUM_5709) {
		REG_WR(bp, BNX2_MISC_COMMAND, BNX2_MISC_COMMAND_SW_RESET);
		REG_RD(bp, BNX2_MISC_COMMAND);
		udelay(5);

		val = BNX2_PCICFG_MISC_CONFIG_REG_WINDOW_ENA |
		      BNX2_PCICFG_MISC_CONFIG_TARGET_MB_WORD_SWAP;

		pci_write_config_dword(bp->pdev, BNX2_PCICFG_MISC_CONFIG, val);

	} else {
		val = BNX2_PCICFG_MISC_CONFIG_CORE_RST_REQ |
		      BNX2_PCICFG_MISC_CONFIG_REG_WINDOW_ENA |
		      BNX2_PCICFG_MISC_CONFIG_TARGET_MB_WORD_SWAP;

		/* Chip reset. */
		REG_WR(bp, BNX2_PCICFG_MISC_CONFIG, val);

		/* Reading back any register after chip reset will hang the
		 * bus on 5706 A0 and A1.  The msleep below provides plenty
		 * of margin for write posting.
		 */
		if ((CHIP_ID(bp) == CHIP_ID_5706_A0) ||
		    (CHIP_ID(bp) == CHIP_ID_5706_A1))
			msleep(20);

		/* Reset takes approximate 30 usec */
		for (i = 0; i < 10; i++) {
			val = REG_RD(bp, BNX2_PCICFG_MISC_CONFIG);
			if ((val & (BNX2_PCICFG_MISC_CONFIG_CORE_RST_REQ |
				    BNX2_PCICFG_MISC_CONFIG_CORE_RST_BSY)) == 0)
				break;
			udelay(10);
		}

		if (val & (BNX2_PCICFG_MISC_CONFIG_CORE_RST_REQ |
			   BNX2_PCICFG_MISC_CONFIG_CORE_RST_BSY)) {
			printk(KERN_ERR PFX "Chip reset did not complete\n");
			return -EBUSY;
		}
	}

	/* Make sure byte swapping is properly configured. */
	val = REG_RD(bp, BNX2_PCI_SWAP_DIAG0);
	if (val != 0x01020304) {
		printk(KERN_ERR PFX "Chip not in correct endian mode\n");
		return -ENODEV;
	}

	/* Wait for the firmware to finish its initialization. */
	rc = bnx2_fw_sync(bp, BNX2_DRV_MSG_DATA_WAIT1 | reset_code, 1, 0);
	if (rc)
		return rc;

	spin_lock_bh(&bp->phy_lock);
	old_port = bp->phy_port;
	bnx2_init_fw_cap(bp);
	if ((bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP) &&
	    old_port != bp->phy_port)
		bnx2_set_default_remote_link(bp);
	spin_unlock_bh(&bp->phy_lock);

	if (CHIP_ID(bp) == CHIP_ID_5706_A0) {
		/* Adjust the voltage regular to two steps lower.  The default
		 * of this register is 0x0000000e. */
		REG_WR(bp, BNX2_MISC_VREG_CONTROL, 0x000000fa);

		/* Remove bad rbuf memory from the free pool. */
		rc = bnx2_alloc_bad_rbuf(bp);
	}

	if (bp->flags & BNX2_FLAG_USING_MSIX)
		bnx2_setup_msix_tbl(bp);

	return rc;
}

static int
bnx2_init_chip(struct bnx2 *bp)
{
	u32 val;
	int rc, i;

	/* Make sure the interrupt is not active. */
	REG_WR(bp, BNX2_PCICFG_INT_ACK_CMD, BNX2_PCICFG_INT_ACK_CMD_MASK_INT);

	val = BNX2_DMA_CONFIG_DATA_BYTE_SWAP |
	      BNX2_DMA_CONFIG_DATA_WORD_SWAP |
#ifdef __BIG_ENDIAN
	      BNX2_DMA_CONFIG_CNTL_BYTE_SWAP |
#endif
	      BNX2_DMA_CONFIG_CNTL_WORD_SWAP |
	      DMA_READ_CHANS << 12 |
	      DMA_WRITE_CHANS << 16;

	val |= (0x2 << 20) | (1 << 11);

	if ((bp->flags & BNX2_FLAG_PCIX) && (bp->bus_speed_mhz == 133))
		val |= (1 << 23);

	if ((CHIP_NUM(bp) == CHIP_NUM_5706) &&
	    (CHIP_ID(bp) != CHIP_ID_5706_A0) && !(bp->flags & BNX2_FLAG_PCIX))
		val |= BNX2_DMA_CONFIG_CNTL_PING_PONG_DMA;

	REG_WR(bp, BNX2_DMA_CONFIG, val);

	if (CHIP_ID(bp) == CHIP_ID_5706_A0) {
		val = REG_RD(bp, BNX2_TDMA_CONFIG);
		val |= BNX2_TDMA_CONFIG_ONE_DMA;
		REG_WR(bp, BNX2_TDMA_CONFIG, val);
	}

	if (bp->flags & BNX2_FLAG_PCIX) {
		u16 val16;

		pci_read_config_word(bp->pdev, bp->pcix_cap + PCI_X_CMD,
				     &val16);
		pci_write_config_word(bp->pdev, bp->pcix_cap + PCI_X_CMD,
				      val16 & ~PCI_X_CMD_ERO);
	}

	REG_WR(bp, BNX2_MISC_ENABLE_SET_BITS,
	       BNX2_MISC_ENABLE_SET_BITS_HOST_COALESCE_ENABLE |
	       BNX2_MISC_ENABLE_STATUS_BITS_RX_V2P_ENABLE |
	       BNX2_MISC_ENABLE_STATUS_BITS_CONTEXT_ENABLE);

	/* Initialize context mapping and zero out the quick contexts.  The
	 * context block must have already been enabled. */
	if (CHIP_NUM(bp) == CHIP_NUM_5709) {
		rc = bnx2_init_5709_context(bp);
		if (rc)
			return rc;
	} else
		bnx2_init_context(bp);

	if ((rc = bnx2_init_cpus(bp)) != 0)
		return rc;

	bnx2_init_nvram(bp);

	bnx2_set_mac_addr(bp, bp->dev->dev_addr, 0);

	val = REG_RD(bp, BNX2_MQ_CONFIG);
	val &= ~BNX2_MQ_CONFIG_KNL_BYP_BLK_SIZE;
	val |= BNX2_MQ_CONFIG_KNL_BYP_BLK_SIZE_256;
	if (CHIP_ID(bp) == CHIP_ID_5709_A0 || CHIP_ID(bp) == CHIP_ID_5709_A1)
		val |= BNX2_MQ_CONFIG_HALT_DIS;

	REG_WR(bp, BNX2_MQ_CONFIG, val);

	val = 0x10000 + (MAX_CID_CNT * MB_KERNEL_CTX_SIZE);
	REG_WR(bp, BNX2_MQ_KNL_BYP_WIND_START, val);
	REG_WR(bp, BNX2_MQ_KNL_WIND_END, val);

	val = (BCM_PAGE_BITS - 8) << 24;
	REG_WR(bp, BNX2_RV2P_CONFIG, val);

	/* Configure page size. */
	val = REG_RD(bp, BNX2_TBDR_CONFIG);
	val &= ~BNX2_TBDR_CONFIG_PAGE_SIZE;
	val |= (BCM_PAGE_BITS - 8) << 24 | 0x40;
	REG_WR(bp, BNX2_TBDR_CONFIG, val);

	val = bp->mac_addr[0] +
	      (bp->mac_addr[1] << 8) +
	      (bp->mac_addr[2] << 16) +
	      bp->mac_addr[3] +
	      (bp->mac_addr[4] << 8) +
	      (bp->mac_addr[5] << 16);
	REG_WR(bp, BNX2_EMAC_BACKOFF_SEED, val);

	/* Program the MTU.  Also include 4 bytes for CRC32. */
	val = bp->dev->mtu + ETH_HLEN + 4;
	if (val > (MAX_ETHERNET_PACKET_SIZE + 4))
		val |= BNX2_EMAC_RX_MTU_SIZE_JUMBO_ENA;
	REG_WR(bp, BNX2_EMAC_RX_MTU_SIZE, val);

	for (i = 0; i < BNX2_MAX_MSIX_VEC; i++)
		bp->bnx2_napi[i].last_status_idx = 0;

	bp->rx_mode = BNX2_EMAC_RX_MODE_SORT_MODE;

	/* Set up how to generate a link change interrupt. */
	REG_WR(bp, BNX2_EMAC_ATTENTION_ENA, BNX2_EMAC_ATTENTION_ENA_LINK);

	REG_WR(bp, BNX2_HC_STATUS_ADDR_L,
	       (u64) bp->status_blk_mapping & 0xffffffff);
	REG_WR(bp, BNX2_HC_STATUS_ADDR_H, (u64) bp->status_blk_mapping >> 32);

	REG_WR(bp, BNX2_HC_STATISTICS_ADDR_L,
	       (u64) bp->stats_blk_mapping & 0xffffffff);
	REG_WR(bp, BNX2_HC_STATISTICS_ADDR_H,
	       (u64) bp->stats_blk_mapping >> 32);

	REG_WR(bp, BNX2_HC_TX_QUICK_CONS_TRIP,
	       (bp->tx_quick_cons_trip_int << 16) | bp->tx_quick_cons_trip);

	REG_WR(bp, BNX2_HC_RX_QUICK_CONS_TRIP,
	       (bp->rx_quick_cons_trip_int << 16) | bp->rx_quick_cons_trip);

	REG_WR(bp, BNX2_HC_COMP_PROD_TRIP,
	       (bp->comp_prod_trip_int << 16) | bp->comp_prod_trip);

	REG_WR(bp, BNX2_HC_TX_TICKS, (bp->tx_ticks_int << 16) | bp->tx_ticks);

	REG_WR(bp, BNX2_HC_RX_TICKS, (bp->rx_ticks_int << 16) | bp->rx_ticks);

	REG_WR(bp, BNX2_HC_COM_TICKS,
	       (bp->com_ticks_int << 16) | bp->com_ticks);

	REG_WR(bp, BNX2_HC_CMD_TICKS,
	       (bp->cmd_ticks_int << 16) | bp->cmd_ticks);

	if (CHIP_NUM(bp) == CHIP_NUM_5708)
		REG_WR(bp, BNX2_HC_STATS_TICKS, 0);
	else
		REG_WR(bp, BNX2_HC_STATS_TICKS, bp->stats_ticks);
	REG_WR(bp, BNX2_HC_STAT_COLLECT_TICKS, 0xbb8);  /* 3ms */

	if (CHIP_ID(bp) == CHIP_ID_5706_A1)
		val = BNX2_HC_CONFIG_COLLECT_STATS;
	else {
		val = BNX2_HC_CONFIG_RX_TMR_MODE | BNX2_HC_CONFIG_TX_TMR_MODE |
		      BNX2_HC_CONFIG_COLLECT_STATS;
	}

	if (bp->irq_nvecs > 1) {
		REG_WR(bp, BNX2_HC_MSIX_BIT_VECTOR,
		       BNX2_HC_MSIX_BIT_VECTOR_VAL);

		val |= BNX2_HC_CONFIG_SB_ADDR_INC_128B;
	}

	if (bp->flags & BNX2_FLAG_ONE_SHOT_MSI)
		val |= BNX2_HC_CONFIG_ONE_SHOT;

	REG_WR(bp, BNX2_HC_CONFIG, val);

	for (i = 1; i < bp->irq_nvecs; i++) {
		u32 base = ((i - 1) * BNX2_HC_SB_CONFIG_SIZE) +
			   BNX2_HC_SB_CONFIG_1;

		REG_WR(bp, base,
			BNX2_HC_SB_CONFIG_1_TX_TMR_MODE |
			BNX2_HC_SB_CONFIG_1_RX_TMR_MODE |
			BNX2_HC_SB_CONFIG_1_ONE_SHOT);

		REG_WR(bp, base + BNX2_HC_TX_QUICK_CONS_TRIP_OFF,
			(bp->tx_quick_cons_trip_int << 16) |
			 bp->tx_quick_cons_trip);

		REG_WR(bp, base + BNX2_HC_TX_TICKS_OFF,
			(bp->tx_ticks_int << 16) | bp->tx_ticks);

		REG_WR(bp, base + BNX2_HC_RX_QUICK_CONS_TRIP_OFF,
		       (bp->rx_quick_cons_trip_int << 16) |
			bp->rx_quick_cons_trip);

		REG_WR(bp, base + BNX2_HC_RX_TICKS_OFF,
			(bp->rx_ticks_int << 16) | bp->rx_ticks);
	}

	/* Clear internal stats counters. */
	REG_WR(bp, BNX2_HC_COMMAND, BNX2_HC_COMMAND_CLR_STAT_NOW);

	REG_WR(bp, BNX2_HC_ATTN_BITS_ENABLE, STATUS_ATTN_EVENTS);

	/* Initialize the receive filter. */
	bnx2_set_rx_mode(bp->dev);

	if (CHIP_NUM(bp) == CHIP_NUM_5709) {
		val = REG_RD(bp, BNX2_MISC_NEW_CORE_CTL);
		val |= BNX2_MISC_NEW_CORE_CTL_DMA_ENABLE;
		REG_WR(bp, BNX2_MISC_NEW_CORE_CTL, val);
	}
	rc = bnx2_fw_sync(bp, BNX2_DRV_MSG_DATA_WAIT2 | BNX2_DRV_MSG_CODE_RESET,
			  1, 0);

	REG_WR(bp, BNX2_MISC_ENABLE_SET_BITS, BNX2_MISC_ENABLE_DEFAULT);
	REG_RD(bp, BNX2_MISC_ENABLE_SET_BITS);

	udelay(20);

	bp->hc_cmd = REG_RD(bp, BNX2_HC_COMMAND);

	return rc;
}

static void
bnx2_clear_ring_states(struct bnx2 *bp)
{
	struct bnx2_napi *bnapi;
	struct bnx2_tx_ring_info *txr;
	struct bnx2_rx_ring_info *rxr;
	int i;

	for (i = 0; i < BNX2_MAX_MSIX_VEC; i++) {
		bnapi = &bp->bnx2_napi[i];
		txr = &bnapi->tx_ring;
		rxr = &bnapi->rx_ring;

		txr->tx_cons = 0;
		txr->hw_tx_cons = 0;
		rxr->rx_prod_bseq = 0;
		rxr->rx_prod = 0;
		rxr->rx_cons = 0;
		rxr->rx_pg_prod = 0;
		rxr->rx_pg_cons = 0;
	}
}

static void
bnx2_init_tx_context(struct bnx2 *bp, u32 cid, struct bnx2_tx_ring_info *txr)
{
	u32 val, offset0, offset1, offset2, offset3;
	u32 cid_addr = GET_CID_ADDR(cid);

	if (CHIP_NUM(bp) == CHIP_NUM_5709) {
		offset0 = BNX2_L2CTX_TYPE_XI;
		offset1 = BNX2_L2CTX_CMD_TYPE_XI;
		offset2 = BNX2_L2CTX_TBDR_BHADDR_HI_XI;
		offset3 = BNX2_L2CTX_TBDR_BHADDR_LO_XI;
	} else {
		offset0 = BNX2_L2CTX_TYPE;
		offset1 = BNX2_L2CTX_CMD_TYPE;
		offset2 = BNX2_L2CTX_TBDR_BHADDR_HI;
		offset3 = BNX2_L2CTX_TBDR_BHADDR_LO;
	}
	val = BNX2_L2CTX_TYPE_TYPE_L2 | BNX2_L2CTX_TYPE_SIZE_L2;
	bnx2_ctx_wr(bp, cid_addr, offset0, val);

	val = BNX2_L2CTX_CMD_TYPE_TYPE_L2 | (8 << 16);
	bnx2_ctx_wr(bp, cid_addr, offset1, val);

	val = (u64) txr->tx_desc_mapping >> 32;
	bnx2_ctx_wr(bp, cid_addr, offset2, val);

	val = (u64) txr->tx_desc_mapping & 0xffffffff;
	bnx2_ctx_wr(bp, cid_addr, offset3, val);
}

static void
bnx2_init_tx_ring(struct bnx2 *bp, int ring_num)
{
	struct tx_bd *txbd;
	u32 cid = TX_CID;
	struct bnx2_napi *bnapi;
	struct bnx2_tx_ring_info *txr;

	bnapi = &bp->bnx2_napi[ring_num];
	txr = &bnapi->tx_ring;

	if (ring_num == 0)
		cid = TX_CID;
	else
		cid = TX_TSS_CID + ring_num - 1;

	bp->tx_wake_thresh = bp->tx_ring_size / 2;

	txbd = &txr->tx_desc_ring[MAX_TX_DESC_CNT];

	txbd->tx_bd_haddr_hi = (u64) txr->tx_desc_mapping >> 32;
	txbd->tx_bd_haddr_lo = (u64) txr->tx_desc_mapping & 0xffffffff;

	txr->tx_prod = 0;
	txr->tx_prod_bseq = 0;

	txr->tx_bidx_addr = MB_GET_CID_ADDR(cid) + BNX2_L2CTX_TX_HOST_BIDX;
	txr->tx_bseq_addr = MB_GET_CID_ADDR(cid) + BNX2_L2CTX_TX_HOST_BSEQ;

	bnx2_init_tx_context(bp, cid, txr);
}

static void
bnx2_init_rxbd_rings(struct rx_bd *rx_ring[], dma_addr_t dma[], u32 buf_size,
		     int num_rings)
{
	int i;
	struct rx_bd *rxbd;

	for (i = 0; i < num_rings; i++) {
		int j;

		rxbd = &rx_ring[i][0];
		for (j = 0; j < MAX_RX_DESC_CNT; j++, rxbd++) {
			rxbd->rx_bd_len = buf_size;
			rxbd->rx_bd_flags = RX_BD_FLAGS_START | RX_BD_FLAGS_END;
		}
		if (i == (num_rings - 1))
			j = 0;
		else
			j = i + 1;
		rxbd->rx_bd_haddr_hi = (u64) dma[j] >> 32;
		rxbd->rx_bd_haddr_lo = (u64) dma[j] & 0xffffffff;
	}
}

static void
bnx2_init_rx_ring(struct bnx2 *bp, int ring_num)
{
	int i;
	u16 prod, ring_prod;
	u32 cid, rx_cid_addr, val;
	struct bnx2_napi *bnapi = &bp->bnx2_napi[ring_num];
	struct bnx2_rx_ring_info *rxr = &bnapi->rx_ring;

	if (ring_num == 0)
		cid = RX_CID;
	else
		cid = RX_RSS_CID + ring_num - 1;

	rx_cid_addr = GET_CID_ADDR(cid);

	bnx2_init_rxbd_rings(rxr->rx_desc_ring, rxr->rx_desc_mapping,
			     bp->rx_buf_use_size, bp->rx_max_ring);

	bnx2_init_rx_context(bp, cid);

	if (CHIP_NUM(bp) == CHIP_NUM_5709) {
		val = REG_RD(bp, BNX2_MQ_MAP_L2_5);
		REG_WR(bp, BNX2_MQ_MAP_L2_5, val | BNX2_MQ_MAP_L2_5_ARM);
	}

	bnx2_ctx_wr(bp, rx_cid_addr, BNX2_L2CTX_PG_BUF_SIZE, 0);
	if (bp->rx_pg_ring_size) {
		bnx2_init_rxbd_rings(rxr->rx_pg_desc_ring,
				     rxr->rx_pg_desc_mapping,
				     PAGE_SIZE, bp->rx_max_pg_ring);
		val = (bp->rx_buf_use_size << 16) | PAGE_SIZE;
		bnx2_ctx_wr(bp, rx_cid_addr, BNX2_L2CTX_PG_BUF_SIZE, val);
		bnx2_ctx_wr(bp, rx_cid_addr, BNX2_L2CTX_RBDC_KEY,
		       BNX2_L2CTX_RBDC_JUMBO_KEY - ring_num);

		val = (u64) rxr->rx_pg_desc_mapping[0] >> 32;
		bnx2_ctx_wr(bp, rx_cid_addr, BNX2_L2CTX_NX_PG_BDHADDR_HI, val);

		val = (u64) rxr->rx_pg_desc_mapping[0] & 0xffffffff;
		bnx2_ctx_wr(bp, rx_cid_addr, BNX2_L2CTX_NX_PG_BDHADDR_LO, val);

		if (CHIP_NUM(bp) == CHIP_NUM_5709)
			REG_WR(bp, BNX2_MQ_MAP_L2_3, BNX2_MQ_MAP_L2_3_DEFAULT);
	}

	val = (u64) rxr->rx_desc_mapping[0] >> 32;
	bnx2_ctx_wr(bp, rx_cid_addr, BNX2_L2CTX_NX_BDHADDR_HI, val);

	val = (u64) rxr->rx_desc_mapping[0] & 0xffffffff;
	bnx2_ctx_wr(bp, rx_cid_addr, BNX2_L2CTX_NX_BDHADDR_LO, val);

	ring_prod = prod = rxr->rx_pg_prod;
	for (i = 0; i < bp->rx_pg_ring_size; i++) {
		if (bnx2_alloc_rx_page(bp, rxr, ring_prod) < 0)
			break;
		prod = NEXT_RX_BD(prod);
		ring_prod = RX_PG_RING_IDX(prod);
	}
	rxr->rx_pg_prod = prod;

	ring_prod = prod = rxr->rx_prod;
	for (i = 0; i < bp->rx_ring_size; i++) {
		if (bnx2_alloc_rx_skb(bp, rxr, ring_prod) < 0)
			break;
		prod = NEXT_RX_BD(prod);
		ring_prod = RX_RING_IDX(prod);
	}
	rxr->rx_prod = prod;

	rxr->rx_bidx_addr = MB_GET_CID_ADDR(cid) + BNX2_L2CTX_HOST_BDIDX;
	rxr->rx_bseq_addr = MB_GET_CID_ADDR(cid) + BNX2_L2CTX_HOST_BSEQ;
	rxr->rx_pg_bidx_addr = MB_GET_CID_ADDR(cid) + BNX2_L2CTX_HOST_PG_BDIDX;

	REG_WR16(bp, rxr->rx_pg_bidx_addr, rxr->rx_pg_prod);
	REG_WR16(bp, rxr->rx_bidx_addr, prod);

	REG_WR(bp, rxr->rx_bseq_addr, rxr->rx_prod_bseq);
}

static void
bnx2_init_all_rings(struct bnx2 *bp)
{
	int i;
	u32 val;

	bnx2_clear_ring_states(bp);

	REG_WR(bp, BNX2_TSCH_TSS_CFG, 0);
	for (i = 0; i < bp->num_tx_rings; i++)
		bnx2_init_tx_ring(bp, i);

	if (bp->num_tx_rings > 1)
		REG_WR(bp, BNX2_TSCH_TSS_CFG, ((bp->num_tx_rings - 1) << 24) |
		       (TX_TSS_CID << 7));

	REG_WR(bp, BNX2_RLUP_RSS_CONFIG, 0);
	bnx2_reg_wr_ind(bp, BNX2_RXP_SCRATCH_RSS_TBL_SZ, 0);

	for (i = 0; i < bp->num_rx_rings; i++)
		bnx2_init_rx_ring(bp, i);

	if (bp->num_rx_rings > 1) {
		u32 tbl_32;
		u8 *tbl = (u8 *) &tbl_32;

		bnx2_reg_wr_ind(bp, BNX2_RXP_SCRATCH_RSS_TBL_SZ,
				BNX2_RXP_SCRATCH_RSS_TBL_MAX_ENTRIES);

		for (i = 0; i < BNX2_RXP_SCRATCH_RSS_TBL_MAX_ENTRIES; i++) {
			tbl[i % 4] = i % (bp->num_rx_rings - 1);
			if ((i % 4) == 3)
				bnx2_reg_wr_ind(bp,
						BNX2_RXP_SCRATCH_RSS_TBL + i,
						cpu_to_be32(tbl_32));
		}

		val = BNX2_RLUP_RSS_CONFIG_IPV4_RSS_TYPE_ALL_XI |
		      BNX2_RLUP_RSS_CONFIG_IPV6_RSS_TYPE_ALL_XI;

		REG_WR(bp, BNX2_RLUP_RSS_CONFIG, val);

	}
}

static u32 bnx2_find_max_ring(u32 ring_size, u32 max_size)
{
	u32 max, num_rings = 1;

	while (ring_size > MAX_RX_DESC_CNT) {
		ring_size -= MAX_RX_DESC_CNT;
		num_rings++;
	}
	/* round to next power of 2 */
	max = max_size;
	while ((max & num_rings) == 0)
		max >>= 1;

	if (num_rings != max)
		max <<= 1;

	return max;
}

static void
bnx2_set_rx_ring_size(struct bnx2 *bp, u32 size)
{
	u32 rx_size, rx_space, jumbo_size;

	/* 8 for CRC and VLAN */
	rx_size = bp->dev->mtu + ETH_HLEN + BNX2_RX_OFFSET + 8;

	rx_space = SKB_DATA_ALIGN(rx_size + BNX2_RX_ALIGN) + NET_SKB_PAD +
		sizeof(struct skb_shared_info);

	bp->rx_copy_thresh = BNX2_RX_COPY_THRESH;
	bp->rx_pg_ring_size = 0;
	bp->rx_max_pg_ring = 0;
	bp->rx_max_pg_ring_idx = 0;
	if ((rx_space > PAGE_SIZE) && !(bp->flags & BNX2_FLAG_JUMBO_BROKEN)) {
		int pages = PAGE_ALIGN(bp->dev->mtu - 40) >> PAGE_SHIFT;

		jumbo_size = size * pages;
		if (jumbo_size > MAX_TOTAL_RX_PG_DESC_CNT)
			jumbo_size = MAX_TOTAL_RX_PG_DESC_CNT;

		bp->rx_pg_ring_size = jumbo_size;
		bp->rx_max_pg_ring = bnx2_find_max_ring(jumbo_size,
							MAX_RX_PG_RINGS);
		bp->rx_max_pg_ring_idx = (bp->rx_max_pg_ring * RX_DESC_CNT) - 1;
		rx_size = BNX2_RX_COPY_THRESH + BNX2_RX_OFFSET;
		bp->rx_copy_thresh = 0;
	}

	bp->rx_buf_use_size = rx_size;
	/* hw alignment */
	bp->rx_buf_size = bp->rx_buf_use_size + BNX2_RX_ALIGN;
	bp->rx_jumbo_thresh = rx_size - BNX2_RX_OFFSET;
	bp->rx_ring_size = size;
	bp->rx_max_ring = bnx2_find_max_ring(size, MAX_RX_RINGS);
	bp->rx_max_ring_idx = (bp->rx_max_ring * RX_DESC_CNT) - 1;
}

static void
bnx2_free_tx_skbs(struct bnx2 *bp)
{
	int i;

	for (i = 0; i < bp->num_tx_rings; i++) {
		struct bnx2_napi *bnapi = &bp->bnx2_napi[i];
		struct bnx2_tx_ring_info *txr = &bnapi->tx_ring;
		int j;

		if (txr->tx_buf_ring == NULL)
			continue;

		for (j = 0; j < TX_DESC_CNT; ) {
			struct sw_tx_bd *tx_buf = &txr->tx_buf_ring[j];
			struct sk_buff *skb = tx_buf->skb;

			if (skb == NULL) {
				j++;
				continue;
			}

			skb_dma_unmap(&bp->pdev->dev, skb, DMA_TO_DEVICE);

			tx_buf->skb = NULL;

			j += skb_shinfo(skb)->nr_frags + 1;
			dev_kfree_skb(skb);
		}
	}
}

static void
bnx2_free_rx_skbs(struct bnx2 *bp)
{
	int i;

	for (i = 0; i < bp->num_rx_rings; i++) {
		struct bnx2_napi *bnapi = &bp->bnx2_napi[i];
		struct bnx2_rx_ring_info *rxr = &bnapi->rx_ring;
		int j;

		if (rxr->rx_buf_ring == NULL)
			return;

		for (j = 0; j < bp->rx_max_ring_idx; j++) {
			struct sw_bd *rx_buf = &rxr->rx_buf_ring[j];
			struct sk_buff *skb = rx_buf->skb;

			if (skb == NULL)
				continue;

			pci_unmap_single(bp->pdev,
					 pci_unmap_addr(rx_buf, mapping),
					 bp->rx_buf_use_size,
					 PCI_DMA_FROMDEVICE);

			rx_buf->skb = NULL;

			dev_kfree_skb(skb);
		}
		for (j = 0; j < bp->rx_max_pg_ring_idx; j++)
			bnx2_free_rx_page(bp, rxr, j);
	}
}

static void
bnx2_free_skbs(struct bnx2 *bp)
{
	bnx2_free_tx_skbs(bp);
	bnx2_free_rx_skbs(bp);
}

static int
bnx2_reset_nic(struct bnx2 *bp, u32 reset_code)
{
	int rc;

	rc = bnx2_reset_chip(bp, reset_code);
	bnx2_free_skbs(bp);
	if (rc)
		return rc;

	if ((rc = bnx2_init_chip(bp)) != 0)
		return rc;

	bnx2_init_all_rings(bp);
	return 0;
}

static int
bnx2_init_nic(struct bnx2 *bp, int reset_phy)
{
	int rc;

	if ((rc = bnx2_reset_nic(bp, BNX2_DRV_MSG_CODE_RESET)) != 0)
		return rc;

	spin_lock_bh(&bp->phy_lock);
	bnx2_init_phy(bp, reset_phy);
	bnx2_set_link(bp);
	if (bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP)
		bnx2_remote_phy_event(bp);
	spin_unlock_bh(&bp->phy_lock);
	return 0;
}

static int
bnx2_shutdown_chip(struct bnx2 *bp)
{
	u32 reset_code;

	if (bp->flags & BNX2_FLAG_NO_WOL)
		reset_code = BNX2_DRV_MSG_CODE_UNLOAD_LNK_DN;
	else if (bp->wol)
		reset_code = BNX2_DRV_MSG_CODE_SUSPEND_WOL;
	else
		reset_code = BNX2_DRV_MSG_CODE_SUSPEND_NO_WOL;

	return bnx2_reset_chip(bp, reset_code);
}

static int
bnx2_test_registers(struct bnx2 *bp)
{
	int ret;
	int i, is_5709;
	static const struct {
		u16   offset;
		u16   flags;
#define BNX2_FL_NOT_5709	1
		u32   rw_mask;
		u32   ro_mask;
	} reg_tbl[] = {
		{ 0x006c, 0, 0x00000000, 0x0000003f },
		{ 0x0090, 0, 0xffffffff, 0x00000000 },
		{ 0x0094, 0, 0x00000000, 0x00000000 },

		{ 0x0404, BNX2_FL_NOT_5709, 0x00003f00, 0x00000000 },
		{ 0x0418, BNX2_FL_NOT_5709, 0x00000000, 0xffffffff },
		{ 0x041c, BNX2_FL_NOT_5709, 0x00000000, 0xffffffff },
		{ 0x0420, BNX2_FL_NOT_5709, 0x00000000, 0x80ffffff },
		{ 0x0424, BNX2_FL_NOT_5709, 0x00000000, 0x00000000 },
		{ 0x0428, BNX2_FL_NOT_5709, 0x00000000, 0x00000001 },
		{ 0x0450, BNX2_FL_NOT_5709, 0x00000000, 0x0000ffff },
		{ 0x0454, BNX2_FL_NOT_5709, 0x00000000, 0xffffffff },
		{ 0x0458, BNX2_FL_NOT_5709, 0x00000000, 0xffffffff },

		{ 0x0808, BNX2_FL_NOT_5709, 0x00000000, 0xffffffff },
		{ 0x0854, BNX2_FL_NOT_5709, 0x00000000, 0xffffffff },
		{ 0x0868, BNX2_FL_NOT_5709, 0x00000000, 0x77777777 },
		{ 0x086c, BNX2_FL_NOT_5709, 0x00000000, 0x77777777 },
		{ 0x0870, BNX2_FL_NOT_5709, 0x00000000, 0x77777777 },
		{ 0x0874, BNX2_FL_NOT_5709, 0x00000000, 0x77777777 },

		{ 0x0c00, BNX2_FL_NOT_5709, 0x00000000, 0x00000001 },
		{ 0x0c04, BNX2_FL_NOT_5709, 0x00000000, 0x03ff0001 },
		{ 0x0c08, BNX2_FL_NOT_5709,  0x0f0ff073, 0x00000000 },

		{ 0x1000, 0, 0x00000000, 0x00000001 },
		{ 0x1004, BNX2_FL_NOT_5709, 0x00000000, 0x000f0001 },

		{ 0x1408, 0, 0x01c00800, 0x00000000 },
		{ 0x149c, 0, 0x8000ffff, 0x00000000 },
		{ 0x14a8, 0, 0x00000000, 0x000001ff },
		{ 0x14ac, 0, 0x0fffffff, 0x10000000 },
		{ 0x14b0, 0, 0x00000002, 0x00000001 },
		{ 0x14b8, 0, 0x00000000, 0x00000000 },
		{ 0x14c0, 0, 0x00000000, 0x00000009 },
		{ 0x14c4, 0, 0x00003fff, 0x00000000 },
		{ 0x14cc, 0, 0x00000000, 0x00000001 },
		{ 0x14d0, 0, 0xffffffff, 0x00000000 },

		{ 0x1800, 0, 0x00000000, 0x00000001 },
		{ 0x1804, 0, 0x00000000, 0x00000003 },

		{ 0x2800, 0, 0x00000000, 0x00000001 },
		{ 0x2804, 0, 0x00000000, 0x00003f01 },
		{ 0x2808, 0, 0x0f3f3f03, 0x00000000 },
		{ 0x2810, 0, 0xffff0000, 0x00000000 },
		{ 0x2814, 0, 0xffff0000, 0x00000000 },
		{ 0x2818, 0, 0xffff0000, 0x00000000 },
		{ 0x281c, 0, 0xffff0000, 0x00000000 },
		{ 0x2834, 0, 0xffffffff, 0x00000000 },
		{ 0x2840, 0, 0x00000000, 0xffffffff },
		{ 0x2844, 0, 0x00000000, 0xffffffff },
		{ 0x2848, 0, 0xffffffff, 0x00000000 },
		{ 0x284c, 0, 0xf800f800, 0x07ff07ff },

		{ 0x2c00, 0, 0x00000000, 0x00000011 },
		{ 0x2c04, 0, 0x00000000, 0x00030007 },

		{ 0x3c00, 0, 0x00000000, 0x00000001 },
		{ 0x3c04, 0, 0x00000000, 0x00070000 },
		{ 0x3c08, 0, 0x00007f71, 0x07f00000 },
		{ 0x3c0c, 0, 0x1f3ffffc, 0x00000000 },
		{ 0x3c10, 0, 0xffffffff, 0x00000000 },
		{ 0x3c14, 0, 0x00000000, 0xffffffff },
		{ 0x3c18, 0, 0x00000000, 0xffffffff },
		{ 0x3c1c, 0, 0xfffff000, 0x00000000 },
		{ 0x3c20, 0, 0xffffff00, 0x00000000 },

		{ 0x5004, 0, 0x00000000, 0x0000007f },
		{ 0x5008, 0, 0x0f0007ff, 0x00000000 },

		{ 0x5c00, 0, 0x00000000, 0x00000001 },
		{ 0x5c04, 0, 0x00000000, 0x0003000f },
		{ 0x5c08, 0, 0x00000003, 0x00000000 },
		{ 0x5c0c, 0, 0x0000fff8, 0x00000000 },
		{ 0x5c10, 0, 0x00000000, 0xffffffff },
		{ 0x5c80, 0, 0x00000000, 0x0f7113f1 },
		{ 0x5c84, 0, 0x00000000, 0x0000f333 },
		{ 0x5c88, 0, 0x00000000, 0x00077373 },
		{ 0x5c8c, 0, 0x00000000, 0x0007f737 },

		{ 0x6808, 0, 0x0000ff7f, 0x00000000 },
		{ 0x680c, 0, 0xffffffff, 0x00000000 },
		{ 0x6810, 0, 0xffffffff, 0x00000000 },
		{ 0x6814, 0, 0xffffffff, 0x00000000 },
		{ 0x6818, 0, 0xffffffff, 0x00000000 },
		{ 0x681c, 0, 0xffffffff, 0x00000000 },
		{ 0x6820, 0, 0x00ff00ff, 0x00000000 },
		{ 0x6824, 0, 0x00ff00ff, 0x00000000 },
		{ 0x6828, 0, 0x00ff00ff, 0x00000000 },
		{ 0x682c, 0, 0x03ff03ff, 0x00000000 },
		{ 0x6830, 0, 0x03ff03ff, 0x00000000 },
		{ 0x6834, 0, 0x03ff03ff, 0x00000000 },
		{ 0x6838, 0, 0x03ff03ff, 0x00000000 },
		{ 0x683c, 0, 0x0000ffff, 0x00000000 },
		{ 0x6840, 0, 0x00000ff0, 0x00000000 },
		{ 0x6844, 0, 0x00ffff00, 0x00000000 },
		{ 0x684c, 0, 0xffffffff, 0x00000000 },
		{ 0x6850, 0, 0x7f7f7f7f, 0x00000000 },
		{ 0x6854, 0, 0x7f7f7f7f, 0x00000000 },
		{ 0x6858, 0, 0x7f7f7f7f, 0x00000000 },
		{ 0x685c, 0, 0x7f7f7f7f, 0x00000000 },
		{ 0x6908, 0, 0x00000000, 0x0001ff0f },
		{ 0x690c, 0, 0x00000000, 0x0ffe00f0 },

		{ 0xffff, 0, 0x00000000, 0x00000000 },
	};

	ret = 0;
	is_5709 = 0;
	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		is_5709 = 1;

	for (i = 0; reg_tbl[i].offset != 0xffff; i++) {
		u32 offset, rw_mask, ro_mask, save_val, val;
		u16 flags = reg_tbl[i].flags;

		if (is_5709 && (flags & BNX2_FL_NOT_5709))
			continue;

		offset = (u32) reg_tbl[i].offset;
		rw_mask = reg_tbl[i].rw_mask;
		ro_mask = reg_tbl[i].ro_mask;

		save_val = readl(bp->regview + offset);

		writel(0, bp->regview + offset);

		val = readl(bp->regview + offset);
		if ((val & rw_mask) != 0) {
			goto reg_test_err;
		}

		if ((val & ro_mask) != (save_val & ro_mask)) {
			goto reg_test_err;
		}

		writel(0xffffffff, bp->regview + offset);

		val = readl(bp->regview + offset);
		if ((val & rw_mask) != rw_mask) {
			goto reg_test_err;
		}

		if ((val & ro_mask) != (save_val & ro_mask)) {
			goto reg_test_err;
		}

		writel(save_val, bp->regview + offset);
		continue;

reg_test_err:
		writel(save_val, bp->regview + offset);
		ret = -ENODEV;
		break;
	}
	return ret;
}

static int
bnx2_do_mem_test(struct bnx2 *bp, u32 start, u32 size)
{
	static const u32 test_pattern[] = { 0x00000000, 0xffffffff, 0x55555555,
		0xaaaaaaaa , 0xaa55aa55, 0x55aa55aa };
	int i;

	for (i = 0; i < sizeof(test_pattern) / 4; i++) {
		u32 offset;

		for (offset = 0; offset < size; offset += 4) {

			bnx2_reg_wr_ind(bp, start + offset, test_pattern[i]);

			if (bnx2_reg_rd_ind(bp, start + offset) !=
				test_pattern[i]) {
				return -ENODEV;
			}
		}
	}
	return 0;
}

static int
bnx2_test_memory(struct bnx2 *bp)
{
	int ret = 0;
	int i;
	static struct mem_entry {
		u32   offset;
		u32   len;
	} mem_tbl_5706[] = {
		{ 0x60000,  0x4000 },
		{ 0xa0000,  0x3000 },
		{ 0xe0000,  0x4000 },
		{ 0x120000, 0x4000 },
		{ 0x1a0000, 0x4000 },
		{ 0x160000, 0x4000 },
		{ 0xffffffff, 0    },
	},
	mem_tbl_5709[] = {
		{ 0x60000,  0x4000 },
		{ 0xa0000,  0x3000 },
		{ 0xe0000,  0x4000 },
		{ 0x120000, 0x4000 },
		{ 0x1a0000, 0x4000 },
		{ 0xffffffff, 0    },
	};
	struct mem_entry *mem_tbl;

	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		mem_tbl = mem_tbl_5709;
	else
		mem_tbl = mem_tbl_5706;

	for (i = 0; mem_tbl[i].offset != 0xffffffff; i++) {
		if ((ret = bnx2_do_mem_test(bp, mem_tbl[i].offset,
			mem_tbl[i].len)) != 0) {
			return ret;
		}
	}

	return ret;
}

#define BNX2_MAC_LOOPBACK	0
#define BNX2_PHY_LOOPBACK	1

static int
bnx2_run_loopback(struct bnx2 *bp, int loopback_mode)
{
	unsigned int pkt_size, num_pkts, i;
	struct sk_buff *skb, *rx_skb;
	unsigned char *packet;
	u16 rx_start_idx, rx_idx;
	dma_addr_t map;
	struct tx_bd *txbd;
	struct sw_bd *rx_buf;
	struct l2_fhdr *rx_hdr;
	int ret = -ENODEV;
	struct bnx2_napi *bnapi = &bp->bnx2_napi[0], *tx_napi;
	struct bnx2_tx_ring_info *txr = &bnapi->tx_ring;
	struct bnx2_rx_ring_info *rxr = &bnapi->rx_ring;

	tx_napi = bnapi;

	txr = &tx_napi->tx_ring;
	rxr = &bnapi->rx_ring;
	if (loopback_mode == BNX2_MAC_LOOPBACK) {
		bp->loopback = MAC_LOOPBACK;
		bnx2_set_mac_loopback(bp);
	}
	else if (loopback_mode == BNX2_PHY_LOOPBACK) {
		if (bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP)
			return 0;

		bp->loopback = PHY_LOOPBACK;
		bnx2_set_phy_loopback(bp);
	}
	else
		return -EINVAL;

	pkt_size = min(bp->dev->mtu + ETH_HLEN, bp->rx_jumbo_thresh - 4);
	skb = netdev_alloc_skb(bp->dev, pkt_size);
	if (!skb)
		return -ENOMEM;
	packet = skb_put(skb, pkt_size);
	memcpy(packet, bp->dev->dev_addr, 6);
	memset(packet + 6, 0x0, 8);
	for (i = 14; i < pkt_size; i++)
		packet[i] = (unsigned char) (i & 0xff);

	if (skb_dma_map(&bp->pdev->dev, skb, DMA_TO_DEVICE)) {
		dev_kfree_skb(skb);
		return -EIO;
	}
	map = skb_shinfo(skb)->dma_maps[0];

	REG_WR(bp, BNX2_HC_COMMAND,
	       bp->hc_cmd | BNX2_HC_COMMAND_COAL_NOW_WO_INT);

	REG_RD(bp, BNX2_HC_COMMAND);

	udelay(5);
	rx_start_idx = bnx2_get_hw_rx_cons(bnapi);

	num_pkts = 0;

	txbd = &txr->tx_desc_ring[TX_RING_IDX(txr->tx_prod)];

	txbd->tx_bd_haddr_hi = (u64) map >> 32;
	txbd->tx_bd_haddr_lo = (u64) map & 0xffffffff;
	txbd->tx_bd_mss_nbytes = pkt_size;
	txbd->tx_bd_vlan_tag_flags = TX_BD_FLAGS_START | TX_BD_FLAGS_END;

	num_pkts++;
	txr->tx_prod = NEXT_TX_BD(txr->tx_prod);
	txr->tx_prod_bseq += pkt_size;

	REG_WR16(bp, txr->tx_bidx_addr, txr->tx_prod);
	REG_WR(bp, txr->tx_bseq_addr, txr->tx_prod_bseq);

	udelay(100);

	REG_WR(bp, BNX2_HC_COMMAND,
	       bp->hc_cmd | BNX2_HC_COMMAND_COAL_NOW_WO_INT);

	REG_RD(bp, BNX2_HC_COMMAND);

	udelay(5);

	skb_dma_unmap(&bp->pdev->dev, skb, DMA_TO_DEVICE);
	dev_kfree_skb(skb);

	if (bnx2_get_hw_tx_cons(tx_napi) != txr->tx_prod)
		goto loopback_test_done;

	rx_idx = bnx2_get_hw_rx_cons(bnapi);
	if (rx_idx != rx_start_idx + num_pkts) {
		goto loopback_test_done;
	}

	rx_buf = &rxr->rx_buf_ring[rx_start_idx];
	rx_skb = rx_buf->skb;

	rx_hdr = (struct l2_fhdr *) rx_skb->data;
	skb_reserve(rx_skb, BNX2_RX_OFFSET);

	pci_dma_sync_single_for_cpu(bp->pdev,
		pci_unmap_addr(rx_buf, mapping),
		bp->rx_buf_size, PCI_DMA_FROMDEVICE);

	if (rx_hdr->l2_fhdr_status &
		(L2_FHDR_ERRORS_BAD_CRC |
		L2_FHDR_ERRORS_PHY_DECODE |
		L2_FHDR_ERRORS_ALIGNMENT |
		L2_FHDR_ERRORS_TOO_SHORT |
		L2_FHDR_ERRORS_GIANT_FRAME)) {

		goto loopback_test_done;
	}

	if ((rx_hdr->l2_fhdr_pkt_len - 4) != pkt_size) {
		goto loopback_test_done;
	}

	for (i = 14; i < pkt_size; i++) {
		if (*(rx_skb->data + i) != (unsigned char) (i & 0xff)) {
			goto loopback_test_done;
		}
	}

	ret = 0;

loopback_test_done:
	bp->loopback = 0;
	return ret;
}

#define BNX2_MAC_LOOPBACK_FAILED	1
#define BNX2_PHY_LOOPBACK_FAILED	2
#define BNX2_LOOPBACK_FAILED		(BNX2_MAC_LOOPBACK_FAILED |	\
					 BNX2_PHY_LOOPBACK_FAILED)

static int
bnx2_test_loopback(struct bnx2 *bp)
{
	int rc = 0;

	if (!netif_running(bp->dev))
		return BNX2_LOOPBACK_FAILED;

	bnx2_reset_nic(bp, BNX2_DRV_MSG_CODE_RESET);
	spin_lock_bh(&bp->phy_lock);
	bnx2_init_phy(bp, 1);
	spin_unlock_bh(&bp->phy_lock);
	if (bnx2_run_loopback(bp, BNX2_MAC_LOOPBACK))
		rc |= BNX2_MAC_LOOPBACK_FAILED;
	if (bnx2_run_loopback(bp, BNX2_PHY_LOOPBACK))
		rc |= BNX2_PHY_LOOPBACK_FAILED;
	return rc;
}

#define NVRAM_SIZE 0x200
#define CRC32_RESIDUAL 0xdebb20e3

static int
bnx2_test_nvram(struct bnx2 *bp)
{
	__be32 buf[NVRAM_SIZE / 4];
	u8 *data = (u8 *) buf;
	int rc = 0;
	u32 magic, csum;

	if ((rc = bnx2_nvram_read(bp, 0, data, 4)) != 0)
		goto test_nvram_done;

        magic = be32_to_cpu(buf[0]);
	if (magic != 0x669955aa) {
		rc = -ENODEV;
		goto test_nvram_done;
	}

	if ((rc = bnx2_nvram_read(bp, 0x100, data, NVRAM_SIZE)) != 0)
		goto test_nvram_done;

	csum = ether_crc_le(0x100, data);
	if (csum != CRC32_RESIDUAL) {
		rc = -ENODEV;
		goto test_nvram_done;
	}

	csum = ether_crc_le(0x100, data + 0x100);
	if (csum != CRC32_RESIDUAL) {
		rc = -ENODEV;
	}

test_nvram_done:
	return rc;
}

static int
bnx2_test_link(struct bnx2 *bp)
{
	u32 bmsr;

	if (!netif_running(bp->dev))
		return -ENODEV;

	if (bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP) {
		if (bp->link_up)
			return 0;
		return -ENODEV;
	}
	spin_lock_bh(&bp->phy_lock);
	bnx2_enable_bmsr1(bp);
	bnx2_read_phy(bp, bp->mii_bmsr1, &bmsr);
	bnx2_read_phy(bp, bp->mii_bmsr1, &bmsr);
	bnx2_disable_bmsr1(bp);
	spin_unlock_bh(&bp->phy_lock);

	if (bmsr & BMSR_LSTATUS) {
		return 0;
	}
	return -ENODEV;
}

static int
bnx2_test_intr(struct bnx2 *bp)
{
	int i;
	u16 status_idx;

	if (!netif_running(bp->dev))
		return -ENODEV;

	status_idx = REG_RD(bp, BNX2_PCICFG_INT_ACK_CMD) & 0xffff;

	/* This register is not touched during run-time. */
	REG_WR(bp, BNX2_HC_COMMAND, bp->hc_cmd | BNX2_HC_COMMAND_COAL_NOW);
	REG_RD(bp, BNX2_HC_COMMAND);

	for (i = 0; i < 10; i++) {
		if ((REG_RD(bp, BNX2_PCICFG_INT_ACK_CMD) & 0xffff) !=
			status_idx) {

			break;
		}

		msleep_interruptible(10);
	}
	if (i < 10)
		return 0;

	return -ENODEV;
}

/* Determining link for parallel detection. */
static int
bnx2_5706_serdes_has_link(struct bnx2 *bp)
{
	u32 mode_ctl, an_dbg, exp;

	if (bp->phy_flags & BNX2_PHY_FLAG_NO_PARALLEL)
		return 0;

	bnx2_write_phy(bp, MII_BNX2_MISC_SHADOW, MISC_SHDW_MODE_CTL);
	bnx2_read_phy(bp, MII_BNX2_MISC_SHADOW, &mode_ctl);

	if (!(mode_ctl & MISC_SHDW_MODE_CTL_SIG_DET))
		return 0;

	bnx2_write_phy(bp, MII_BNX2_MISC_SHADOW, MISC_SHDW_AN_DBG);
	bnx2_read_phy(bp, MII_BNX2_MISC_SHADOW, &an_dbg);
	bnx2_read_phy(bp, MII_BNX2_MISC_SHADOW, &an_dbg);

	if (an_dbg & (MISC_SHDW_AN_DBG_NOSYNC | MISC_SHDW_AN_DBG_RUDI_INVALID))
		return 0;

	bnx2_write_phy(bp, MII_BNX2_DSP_ADDRESS, MII_EXPAND_REG1);
	bnx2_read_phy(bp, MII_BNX2_DSP_RW_PORT, &exp);
	bnx2_read_phy(bp, MII_BNX2_DSP_RW_PORT, &exp);

	if (exp & MII_EXPAND_REG1_RUDI_C)	/* receiving CONFIG */
		return 0;

	return 1;
}

static void
bnx2_5706_serdes_timer(struct bnx2 *bp)
{
	int check_link = 1;

	spin_lock(&bp->phy_lock);
	if (bp->serdes_an_pending) {
		bp->serdes_an_pending--;
		check_link = 0;
	} else if ((bp->link_up == 0) && (bp->autoneg & AUTONEG_SPEED)) {
		u32 bmcr;

		bp->current_interval = BNX2_TIMER_INTERVAL;

		bnx2_read_phy(bp, bp->mii_bmcr, &bmcr);

		if (bmcr & BMCR_ANENABLE) {
			if (bnx2_5706_serdes_has_link(bp)) {
				bmcr &= ~BMCR_ANENABLE;
				bmcr |= BMCR_SPEED1000 | BMCR_FULLDPLX;
				bnx2_write_phy(bp, bp->mii_bmcr, bmcr);
				bp->phy_flags |= BNX2_PHY_FLAG_PARALLEL_DETECT;
			}
		}
	}
	else if ((bp->link_up) && (bp->autoneg & AUTONEG_SPEED) &&
		 (bp->phy_flags & BNX2_PHY_FLAG_PARALLEL_DETECT)) {
		u32 phy2;

		bnx2_write_phy(bp, 0x17, 0x0f01);
		bnx2_read_phy(bp, 0x15, &phy2);
		if (phy2 & 0x20) {
			u32 bmcr;

			bnx2_read_phy(bp, bp->mii_bmcr, &bmcr);
			bmcr |= BMCR_ANENABLE;
			bnx2_write_phy(bp, bp->mii_bmcr, bmcr);

			bp->phy_flags &= ~BNX2_PHY_FLAG_PARALLEL_DETECT;
		}
	} else
		bp->current_interval = BNX2_TIMER_INTERVAL;

	if (check_link) {
		u32 val;

		bnx2_write_phy(bp, MII_BNX2_MISC_SHADOW, MISC_SHDW_AN_DBG);
		bnx2_read_phy(bp, MII_BNX2_MISC_SHADOW, &val);
		bnx2_read_phy(bp, MII_BNX2_MISC_SHADOW, &val);

		if (bp->link_up && (val & MISC_SHDW_AN_DBG_NOSYNC)) {
			if (!(bp->phy_flags & BNX2_PHY_FLAG_FORCED_DOWN)) {
				bnx2_5706s_force_link_dn(bp, 1);
				bp->phy_flags |= BNX2_PHY_FLAG_FORCED_DOWN;
			} else
				bnx2_set_link(bp);
		} else if (!bp->link_up && !(val & MISC_SHDW_AN_DBG_NOSYNC))
			bnx2_set_link(bp);
	}
	spin_unlock(&bp->phy_lock);
}

static void
bnx2_5708_serdes_timer(struct bnx2 *bp)
{
	if (bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP)
		return;

	if ((bp->phy_flags & BNX2_PHY_FLAG_2_5G_CAPABLE) == 0) {
		bp->serdes_an_pending = 0;
		return;
	}

	spin_lock(&bp->phy_lock);
	if (bp->serdes_an_pending)
		bp->serdes_an_pending--;
	else if ((bp->link_up == 0) && (bp->autoneg & AUTONEG_SPEED)) {
		u32 bmcr;

		bnx2_read_phy(bp, bp->mii_bmcr, &bmcr);
		if (bmcr & BMCR_ANENABLE) {
			bnx2_enable_forced_2g5(bp);
			bp->current_interval = SERDES_FORCED_TIMEOUT;
		} else {
			bnx2_disable_forced_2g5(bp);
			bp->serdes_an_pending = 2;
			bp->current_interval = BNX2_TIMER_INTERVAL;
		}

	} else
		bp->current_interval = BNX2_TIMER_INTERVAL;

	spin_unlock(&bp->phy_lock);
}

static void
bnx2_timer(unsigned long data)
{
	struct bnx2 *bp = (struct bnx2 *) data;

	if (!netif_running(bp->dev))
		return;

	if (atomic_read(&bp->intr_sem) != 0)
		goto bnx2_restart_timer;

	bnx2_send_heart_beat(bp);

	bp->stats_blk->stat_FwRxDrop =
		bnx2_reg_rd_ind(bp, BNX2_FW_RX_DROP_COUNT);

	/* workaround occasional corrupted counters */
	if (CHIP_NUM(bp) == CHIP_NUM_5708 && bp->stats_ticks)
		REG_WR(bp, BNX2_HC_COMMAND, bp->hc_cmd |
					    BNX2_HC_COMMAND_STATS_NOW);

	if (bp->phy_flags & BNX2_PHY_FLAG_SERDES) {
		if (CHIP_NUM(bp) == CHIP_NUM_5706)
			bnx2_5706_serdes_timer(bp);
		else
			bnx2_5708_serdes_timer(bp);
	}

bnx2_restart_timer:
	mod_timer(&bp->timer, jiffies + bp->current_interval);
}

static int
bnx2_request_irq(struct bnx2 *bp)
{
	unsigned long flags;
	struct bnx2_irq *irq;
	int rc = 0, i;

	if (bp->flags & BNX2_FLAG_USING_MSI_OR_MSIX)
		flags = 0;
	else
		flags = IRQF_SHARED;

	for (i = 0; i < bp->irq_nvecs; i++) {
		irq = &bp->irq_tbl[i];
		rc = request_irq(irq->vector, irq->handler, flags, irq->name,
				 &bp->bnx2_napi[i]);
		if (rc)
			break;
		irq->requested = 1;
	}
	return rc;
}

static void
bnx2_free_irq(struct bnx2 *bp)
{
	struct bnx2_irq *irq;
	int i;

	for (i = 0; i < bp->irq_nvecs; i++) {
		irq = &bp->irq_tbl[i];
		if (irq->requested)
			free_irq(irq->vector, &bp->bnx2_napi[i]);
		irq->requested = 0;
	}
	if (bp->flags & BNX2_FLAG_USING_MSI)
		pci_disable_msi(bp->pdev);
	else if (bp->flags & BNX2_FLAG_USING_MSIX)
		pci_disable_msix(bp->pdev);

	bp->flags &= ~(BNX2_FLAG_USING_MSI_OR_MSIX | BNX2_FLAG_ONE_SHOT_MSI);
}

static void
bnx2_enable_msix(struct bnx2 *bp, int msix_vecs)
{
	int i, rc;
	struct msix_entry msix_ent[BNX2_MAX_MSIX_VEC];

	bnx2_setup_msix_tbl(bp);
	REG_WR(bp, BNX2_PCI_MSIX_CONTROL, BNX2_MAX_MSIX_HW_VEC - 1);
	REG_WR(bp, BNX2_PCI_MSIX_TBL_OFF_BIR, BNX2_PCI_GRC_WINDOW2_BASE);
	REG_WR(bp, BNX2_PCI_MSIX_PBA_OFF_BIT, BNX2_PCI_GRC_WINDOW3_BASE);

	for (i = 0; i < BNX2_MAX_MSIX_VEC; i++) {
		msix_ent[i].entry = i;
		msix_ent[i].vector = 0;

		strcpy(bp->irq_tbl[i].name, bp->dev->name);
		bp->irq_tbl[i].handler = bnx2_msi_1shot;
	}

	rc = pci_enable_msix(bp->pdev, msix_ent, BNX2_MAX_MSIX_VEC);
	if (rc != 0)
		return;

	bp->irq_nvecs = msix_vecs;
	bp->flags |= BNX2_FLAG_USING_MSIX | BNX2_FLAG_ONE_SHOT_MSI;
	for (i = 0; i < BNX2_MAX_MSIX_VEC; i++)
		bp->irq_tbl[i].vector = msix_ent[i].vector;
}

static void
bnx2_setup_int_mode(struct bnx2 *bp, int dis_msi)
{
	int cpus = num_online_cpus();
	int msix_vecs = min(cpus + 1, RX_MAX_RINGS);

	bp->irq_tbl[0].handler = bnx2_interrupt;
	strcpy(bp->irq_tbl[0].name, bp->dev->name);
	bp->irq_nvecs = 1;
	bp->irq_tbl[0].vector = bp->pdev->irq;

	if ((bp->flags & BNX2_FLAG_MSIX_CAP) && !dis_msi && cpus > 1)
		bnx2_enable_msix(bp, msix_vecs);

	if ((bp->flags & BNX2_FLAG_MSI_CAP) && !dis_msi &&
	    !(bp->flags & BNX2_FLAG_USING_MSIX)) {
		if (pci_enable_msi(bp->pdev) == 0) {
			bp->flags |= BNX2_FLAG_USING_MSI;
			if (CHIP_NUM(bp) == CHIP_NUM_5709) {
				bp->flags |= BNX2_FLAG_ONE_SHOT_MSI;
				bp->irq_tbl[0].handler = bnx2_msi_1shot;
			} else
				bp->irq_tbl[0].handler = bnx2_msi;

			bp->irq_tbl[0].vector = bp->pdev->irq;
		}
	}

	bp->num_tx_rings = rounddown_pow_of_two(bp->irq_nvecs);
	bp->dev->real_num_tx_queues = bp->num_tx_rings;

	bp->num_rx_rings = bp->irq_nvecs;
}

/* Called with rtnl_lock */
static int
bnx2_open(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);
	int rc;

	netif_carrier_off(dev);

	bnx2_set_power_state(bp, PCI_D0);
	bnx2_disable_int(bp);

	bnx2_setup_int_mode(bp, disable_msi);
	bnx2_napi_enable(bp);
	rc = bnx2_alloc_mem(bp);
	if (rc)
		goto open_err;

	rc = bnx2_request_irq(bp);
	if (rc)
		goto open_err;

	rc = bnx2_init_nic(bp, 1);
	if (rc)
		goto open_err;

	mod_timer(&bp->timer, jiffies + bp->current_interval);

	atomic_set(&bp->intr_sem, 0);

	bnx2_enable_int(bp);

	if (bp->flags & BNX2_FLAG_USING_MSI) {
		/* Test MSI to make sure it is working
		 * If MSI test fails, go back to INTx mode
		 */
		if (bnx2_test_intr(bp) != 0) {
			printk(KERN_WARNING PFX "%s: No interrupt was generated"
			       " using MSI, switching to INTx mode. Please"
			       " report this failure to the PCI maintainer"
			       " and include system chipset information.\n",
			       bp->dev->name);

			bnx2_disable_int(bp);
			bnx2_free_irq(bp);

			bnx2_setup_int_mode(bp, 1);

			rc = bnx2_init_nic(bp, 0);

			if (!rc)
				rc = bnx2_request_irq(bp);

			if (rc) {
				del_timer_sync(&bp->timer);
				goto open_err;
			}
			bnx2_enable_int(bp);
		}
	}
	if (bp->flags & BNX2_FLAG_USING_MSI)
		printk(KERN_INFO PFX "%s: using MSI\n", dev->name);
	else if (bp->flags & BNX2_FLAG_USING_MSIX)
		printk(KERN_INFO PFX "%s: using MSIX\n", dev->name);

	netif_tx_start_all_queues(dev);

	return 0;

open_err:
	bnx2_napi_disable(bp);
	bnx2_free_skbs(bp);
	bnx2_free_irq(bp);
	bnx2_free_mem(bp);
	return rc;
}

static void
bnx2_reset_task(struct work_struct *work)
{
	struct bnx2 *bp = container_of(work, struct bnx2, reset_task);

	if (!netif_running(bp->dev))
		return;

	bnx2_netif_stop(bp);

	bnx2_init_nic(bp, 1);

	atomic_set(&bp->intr_sem, 1);
	bnx2_netif_start(bp);
}

static void
bnx2_tx_timeout(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);

	/* This allows the netif to be shutdown gracefully before resetting */
	schedule_work(&bp->reset_task);
}

#ifdef BCM_VLAN
/* Called with rtnl_lock */
static void
bnx2_vlan_rx_register(struct net_device *dev, struct vlan_group *vlgrp)
{
	struct bnx2 *bp = netdev_priv(dev);

	bnx2_netif_stop(bp);

	bp->vlgrp = vlgrp;
	bnx2_set_rx_mode(dev);
	if (bp->flags & BNX2_FLAG_CAN_KEEP_VLAN)
		bnx2_fw_sync(bp, BNX2_DRV_MSG_CODE_KEEP_VLAN_UPDATE, 0, 1);

	bnx2_netif_start(bp);
}
#endif

/* Called with netif_tx_lock.
 * bnx2_tx_int() runs without netif_tx_lock unless it needs to call
 * netif_wake_queue().
 */
static int
bnx2_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);
	dma_addr_t mapping;
	struct tx_bd *txbd;
	struct sw_tx_bd *tx_buf;
	u32 len, vlan_tag_flags, last_frag, mss;
	u16 prod, ring_prod;
	int i;
	struct bnx2_napi *bnapi;
	struct bnx2_tx_ring_info *txr;
	struct netdev_queue *txq;
	struct skb_shared_info *sp;

	/*  Determine which tx ring we will be placed on */
	i = skb_get_queue_mapping(skb);
	bnapi = &bp->bnx2_napi[i];
	txr = &bnapi->tx_ring;
	txq = netdev_get_tx_queue(dev, i);

	if (unlikely(bnx2_tx_avail(bp, txr) <
	    (skb_shinfo(skb)->nr_frags + 1))) {
		netif_tx_stop_queue(txq);
		printk(KERN_ERR PFX "%s: BUG! Tx ring full when queue awake!\n",
			dev->name);

		return NETDEV_TX_BUSY;
	}
	len = skb_headlen(skb);
	prod = txr->tx_prod;
	ring_prod = TX_RING_IDX(prod);

	vlan_tag_flags = 0;
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		vlan_tag_flags |= TX_BD_FLAGS_TCP_UDP_CKSUM;
	}

#ifdef BCM_VLAN
	if (bp->vlgrp && vlan_tx_tag_present(skb)) {
		vlan_tag_flags |=
			(TX_BD_FLAGS_VLAN_TAG | (vlan_tx_tag_get(skb) << 16));
	}
#endif
	if ((mss = skb_shinfo(skb)->gso_size)) {
		u32 tcp_opt_len;
		struct iphdr *iph;

		vlan_tag_flags |= TX_BD_FLAGS_SW_LSO;

		tcp_opt_len = tcp_optlen(skb);

		if (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6) {
			u32 tcp_off = skb_transport_offset(skb) -
				      sizeof(struct ipv6hdr) - ETH_HLEN;

			vlan_tag_flags |= ((tcp_opt_len >> 2) << 8) |
					  TX_BD_FLAGS_SW_FLAGS;
			if (likely(tcp_off == 0))
				vlan_tag_flags &= ~TX_BD_FLAGS_TCP6_OFF0_MSK;
			else {
				tcp_off >>= 3;
				vlan_tag_flags |= ((tcp_off & 0x3) <<
						   TX_BD_FLAGS_TCP6_OFF0_SHL) |
						  ((tcp_off & 0x10) <<
						   TX_BD_FLAGS_TCP6_OFF4_SHL);
				mss |= (tcp_off & 0xc) << TX_BD_TCP6_OFF2_SHL;
			}
		} else {
			iph = ip_hdr(skb);
			if (tcp_opt_len || (iph->ihl > 5)) {
				vlan_tag_flags |= ((iph->ihl - 5) +
						   (tcp_opt_len >> 2)) << 8;
			}
		}
	} else
		mss = 0;

	if (skb_dma_map(&bp->pdev->dev, skb, DMA_TO_DEVICE)) {
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	sp = skb_shinfo(skb);
	mapping = sp->dma_maps[0];

	tx_buf = &txr->tx_buf_ring[ring_prod];
	tx_buf->skb = skb;

	txbd = &txr->tx_desc_ring[ring_prod];

	txbd->tx_bd_haddr_hi = (u64) mapping >> 32;
	txbd->tx_bd_haddr_lo = (u64) mapping & 0xffffffff;
	txbd->tx_bd_mss_nbytes = len | (mss << 16);
	txbd->tx_bd_vlan_tag_flags = vlan_tag_flags | TX_BD_FLAGS_START;

	last_frag = skb_shinfo(skb)->nr_frags;

	for (i = 0; i < last_frag; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		prod = NEXT_TX_BD(prod);
		ring_prod = TX_RING_IDX(prod);
		txbd = &txr->tx_desc_ring[ring_prod];

		len = frag->size;
		mapping = sp->dma_maps[i + 1];

		txbd->tx_bd_haddr_hi = (u64) mapping >> 32;
		txbd->tx_bd_haddr_lo = (u64) mapping & 0xffffffff;
		txbd->tx_bd_mss_nbytes = len | (mss << 16);
		txbd->tx_bd_vlan_tag_flags = vlan_tag_flags;

	}
	txbd->tx_bd_vlan_tag_flags |= TX_BD_FLAGS_END;

	prod = NEXT_TX_BD(prod);
	txr->tx_prod_bseq += skb->len;

	REG_WR16(bp, txr->tx_bidx_addr, prod);
	REG_WR(bp, txr->tx_bseq_addr, txr->tx_prod_bseq);

	mmiowb();

	txr->tx_prod = prod;
	dev->trans_start = jiffies;

	if (unlikely(bnx2_tx_avail(bp, txr) <= MAX_SKB_FRAGS)) {
		netif_tx_stop_queue(txq);
		if (bnx2_tx_avail(bp, txr) > bp->tx_wake_thresh)
			netif_tx_wake_queue(txq);
	}

	return NETDEV_TX_OK;
}

/* Called with rtnl_lock */
static int
bnx2_close(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);

	cancel_work_sync(&bp->reset_task);

	bnx2_disable_int_sync(bp);
	bnx2_napi_disable(bp);
	del_timer_sync(&bp->timer);
	bnx2_shutdown_chip(bp);
	bnx2_free_irq(bp);
	bnx2_free_skbs(bp);
	bnx2_free_mem(bp);
	bp->link_up = 0;
	netif_carrier_off(bp->dev);
	bnx2_set_power_state(bp, PCI_D3hot);
	return 0;
}

#define GET_NET_STATS64(ctr)					\
	(unsigned long) ((unsigned long) (ctr##_hi) << 32) +	\
	(unsigned long) (ctr##_lo)

#define GET_NET_STATS32(ctr)		\
	(ctr##_lo)

#if (BITS_PER_LONG == 64)
#define GET_NET_STATS	GET_NET_STATS64
#else
#define GET_NET_STATS	GET_NET_STATS32
#endif

static struct net_device_stats *
bnx2_get_stats(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);
	struct statistics_block *stats_blk = bp->stats_blk;
	struct net_device_stats *net_stats = &bp->net_stats;

	if (bp->stats_blk == NULL) {
		return net_stats;
	}
	net_stats->rx_packets =
		GET_NET_STATS(stats_blk->stat_IfHCInUcastPkts) +
		GET_NET_STATS(stats_blk->stat_IfHCInMulticastPkts) +
		GET_NET_STATS(stats_blk->stat_IfHCInBroadcastPkts);

	net_stats->tx_packets =
		GET_NET_STATS(stats_blk->stat_IfHCOutUcastPkts) +
		GET_NET_STATS(stats_blk->stat_IfHCOutMulticastPkts) +
		GET_NET_STATS(stats_blk->stat_IfHCOutBroadcastPkts);

	net_stats->rx_bytes =
		GET_NET_STATS(stats_blk->stat_IfHCInOctets);

	net_stats->tx_bytes =
		GET_NET_STATS(stats_blk->stat_IfHCOutOctets);

	net_stats->multicast =
		GET_NET_STATS(stats_blk->stat_IfHCOutMulticastPkts);

	net_stats->collisions =
		(unsigned long) stats_blk->stat_EtherStatsCollisions;

	net_stats->rx_length_errors =
		(unsigned long) (stats_blk->stat_EtherStatsUndersizePkts +
		stats_blk->stat_EtherStatsOverrsizePkts);

	net_stats->rx_over_errors =
		(unsigned long) stats_blk->stat_IfInMBUFDiscards;

	net_stats->rx_frame_errors =
		(unsigned long) stats_blk->stat_Dot3StatsAlignmentErrors;

	net_stats->rx_crc_errors =
		(unsigned long) stats_blk->stat_Dot3StatsFCSErrors;

	net_stats->rx_errors = net_stats->rx_length_errors +
		net_stats->rx_over_errors + net_stats->rx_frame_errors +
		net_stats->rx_crc_errors;

	net_stats->tx_aborted_errors =
    		(unsigned long) (stats_blk->stat_Dot3StatsExcessiveCollisions +
		stats_blk->stat_Dot3StatsLateCollisions);

	if ((CHIP_NUM(bp) == CHIP_NUM_5706) ||
	    (CHIP_ID(bp) == CHIP_ID_5708_A0))
		net_stats->tx_carrier_errors = 0;
	else {
		net_stats->tx_carrier_errors =
			(unsigned long)
			stats_blk->stat_Dot3StatsCarrierSenseErrors;
	}

	net_stats->tx_errors =
    		(unsigned long)
		stats_blk->stat_emac_tx_stat_dot3statsinternalmactransmiterrors
		+
		net_stats->tx_aborted_errors +
		net_stats->tx_carrier_errors;

	net_stats->rx_missed_errors =
		(unsigned long) (stats_blk->stat_IfInMBUFDiscards +
		stats_blk->stat_FwRxDrop);

	return net_stats;
}

/* All ethtool functions called with rtnl_lock */

static int
bnx2_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct bnx2 *bp = netdev_priv(dev);
	int support_serdes = 0, support_copper = 0;

	cmd->supported = SUPPORTED_Autoneg;
	if (bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP) {
		support_serdes = 1;
		support_copper = 1;
	} else if (bp->phy_port == PORT_FIBRE)
		support_serdes = 1;
	else
		support_copper = 1;

	if (support_serdes) {
		cmd->supported |= SUPPORTED_1000baseT_Full |
			SUPPORTED_FIBRE;
		if (bp->phy_flags & BNX2_PHY_FLAG_2_5G_CAPABLE)
			cmd->supported |= SUPPORTED_2500baseX_Full;

	}
	if (support_copper) {
		cmd->supported |= SUPPORTED_10baseT_Half |
			SUPPORTED_10baseT_Full |
			SUPPORTED_100baseT_Half |
			SUPPORTED_100baseT_Full |
			SUPPORTED_1000baseT_Full |
			SUPPORTED_TP;

	}

	spin_lock_bh(&bp->phy_lock);
	cmd->port = bp->phy_port;
	cmd->advertising = bp->advertising;

	if (bp->autoneg & AUTONEG_SPEED) {
		cmd->autoneg = AUTONEG_ENABLE;
	}
	else {
		cmd->autoneg = AUTONEG_DISABLE;
	}

	if (netif_carrier_ok(dev)) {
		cmd->speed = bp->line_speed;
		cmd->duplex = bp->duplex;
	}
	else {
		cmd->speed = -1;
		cmd->duplex = -1;
	}
	spin_unlock_bh(&bp->phy_lock);

	cmd->transceiver = XCVR_INTERNAL;
	cmd->phy_address = bp->phy_addr;

	return 0;
}

static int
bnx2_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct bnx2 *bp = netdev_priv(dev);
	u8 autoneg = bp->autoneg;
	u8 req_duplex = bp->req_duplex;
	u16 req_line_speed = bp->req_line_speed;
	u32 advertising = bp->advertising;
	int err = -EINVAL;

	spin_lock_bh(&bp->phy_lock);

	if (cmd->port != PORT_TP && cmd->port != PORT_FIBRE)
		goto err_out_unlock;

	if (cmd->port != bp->phy_port &&
	    !(bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP))
		goto err_out_unlock;

	/* If device is down, we can store the settings only if the user
	 * is setting the currently active port.
	 */
	if (!netif_running(dev) && cmd->port != bp->phy_port)
		goto err_out_unlock;

	if (cmd->autoneg == AUTONEG_ENABLE) {
		autoneg |= AUTONEG_SPEED;

		cmd->advertising &= ETHTOOL_ALL_COPPER_SPEED;

		/* allow advertising 1 speed */
		if ((cmd->advertising == ADVERTISED_10baseT_Half) ||
			(cmd->advertising == ADVERTISED_10baseT_Full) ||
			(cmd->advertising == ADVERTISED_100baseT_Half) ||
			(cmd->advertising == ADVERTISED_100baseT_Full)) {

			if (cmd->port == PORT_FIBRE)
				goto err_out_unlock;

			advertising = cmd->advertising;

		} else if (cmd->advertising == ADVERTISED_2500baseX_Full) {
			if (!(bp->phy_flags & BNX2_PHY_FLAG_2_5G_CAPABLE) ||
			    (cmd->port == PORT_TP))
				goto err_out_unlock;
		} else if (cmd->advertising == ADVERTISED_1000baseT_Full)
			advertising = cmd->advertising;
		else if (cmd->advertising == ADVERTISED_1000baseT_Half)
			goto err_out_unlock;
		else {
			if (cmd->port == PORT_FIBRE)
				advertising = ETHTOOL_ALL_FIBRE_SPEED;
			else
				advertising = ETHTOOL_ALL_COPPER_SPEED;
		}
		advertising |= ADVERTISED_Autoneg;
	}
	else {
		if (cmd->port == PORT_FIBRE) {
			if ((cmd->speed != SPEED_1000 &&
			     cmd->speed != SPEED_2500) ||
			    (cmd->duplex != DUPLEX_FULL))
				goto err_out_unlock;

			if (cmd->speed == SPEED_2500 &&
			    !(bp->phy_flags & BNX2_PHY_FLAG_2_5G_CAPABLE))
				goto err_out_unlock;
		}
		else if (cmd->speed == SPEED_1000 || cmd->speed == SPEED_2500)
			goto err_out_unlock;

		autoneg &= ~AUTONEG_SPEED;
		req_line_speed = cmd->speed;
		req_duplex = cmd->duplex;
		advertising = 0;
	}

	bp->autoneg = autoneg;
	bp->advertising = advertising;
	bp->req_line_speed = req_line_speed;
	bp->req_duplex = req_duplex;

	err = 0;
	/* If device is down, the new settings will be picked up when it is
	 * brought up.
	 */
	if (netif_running(dev))
		err = bnx2_setup_phy(bp, cmd->port);

err_out_unlock:
	spin_unlock_bh(&bp->phy_lock);

	return err;
}

static void
bnx2_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct bnx2 *bp = netdev_priv(dev);

	strcpy(info->driver, DRV_MODULE_NAME);
	strcpy(info->version, DRV_MODULE_VERSION);
	strcpy(info->bus_info, pci_name(bp->pdev));
	strcpy(info->fw_version, bp->fw_version);
}

#define BNX2_REGDUMP_LEN		(32 * 1024)

static int
bnx2_get_regs_len(struct net_device *dev)
{
	return BNX2_REGDUMP_LEN;
}

static void
bnx2_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *_p)
{
	u32 *p = _p, i, offset;
	u8 *orig_p = _p;
	struct bnx2 *bp = netdev_priv(dev);
	u32 reg_boundaries[] = { 0x0000, 0x0098, 0x0400, 0x045c,
				 0x0800, 0x0880, 0x0c00, 0x0c10,
				 0x0c30, 0x0d08, 0x1000, 0x101c,
				 0x1040, 0x1048, 0x1080, 0x10a4,
				 0x1400, 0x1490, 0x1498, 0x14f0,
				 0x1500, 0x155c, 0x1580, 0x15dc,
				 0x1600, 0x1658, 0x1680, 0x16d8,
				 0x1800, 0x1820, 0x1840, 0x1854,
				 0x1880, 0x1894, 0x1900, 0x1984,
				 0x1c00, 0x1c0c, 0x1c40, 0x1c54,
				 0x1c80, 0x1c94, 0x1d00, 0x1d84,
				 0x2000, 0x2030, 0x23c0, 0x2400,
				 0x2800, 0x2820, 0x2830, 0x2850,
				 0x2b40, 0x2c10, 0x2fc0, 0x3058,
				 0x3c00, 0x3c94, 0x4000, 0x4010,
				 0x4080, 0x4090, 0x43c0, 0x4458,
				 0x4c00, 0x4c18, 0x4c40, 0x4c54,
				 0x4fc0, 0x5010, 0x53c0, 0x5444,
				 0x5c00, 0x5c18, 0x5c80, 0x5c90,
				 0x5fc0, 0x6000, 0x6400, 0x6428,
				 0x6800, 0x6848, 0x684c, 0x6860,
				 0x6888, 0x6910, 0x8000 };

	regs->version = 0;

	memset(p, 0, BNX2_REGDUMP_LEN);

	if (!netif_running(bp->dev))
		return;

	i = 0;
	offset = reg_boundaries[0];
	p += offset;
	while (offset < BNX2_REGDUMP_LEN) {
		*p++ = REG_RD(bp, offset);
		offset += 4;
		if (offset == reg_boundaries[i + 1]) {
			offset = reg_boundaries[i + 2];
			p = (u32 *) (orig_p + offset);
			i += 2;
		}
	}
}

static void
bnx2_get_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bnx2 *bp = netdev_priv(dev);

	if (bp->flags & BNX2_FLAG_NO_WOL) {
		wol->supported = 0;
		wol->wolopts = 0;
	}
	else {
		wol->supported = WAKE_MAGIC;
		if (bp->wol)
			wol->wolopts = WAKE_MAGIC;
		else
			wol->wolopts = 0;
	}
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}

static int
bnx2_set_wol(struct net_device *dev, struct ethtool_wolinfo *wol)
{
	struct bnx2 *bp = netdev_priv(dev);

	if (wol->wolopts & ~WAKE_MAGIC)
		return -EINVAL;

	if (wol->wolopts & WAKE_MAGIC) {
		if (bp->flags & BNX2_FLAG_NO_WOL)
			return -EINVAL;

		bp->wol = 1;
	}
	else {
		bp->wol = 0;
	}
	return 0;
}

static int
bnx2_nway_reset(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);
	u32 bmcr;

	if (!netif_running(dev))
		return -EAGAIN;

	if (!(bp->autoneg & AUTONEG_SPEED)) {
		return -EINVAL;
	}

	spin_lock_bh(&bp->phy_lock);

	if (bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP) {
		int rc;

		rc = bnx2_setup_remote_phy(bp, bp->phy_port);
		spin_unlock_bh(&bp->phy_lock);
		return rc;
	}

	/* Force a link down visible on the other side */
	if (bp->phy_flags & BNX2_PHY_FLAG_SERDES) {
		bnx2_write_phy(bp, bp->mii_bmcr, BMCR_LOOPBACK);
		spin_unlock_bh(&bp->phy_lock);

		msleep(20);

		spin_lock_bh(&bp->phy_lock);

		bp->current_interval = SERDES_AN_TIMEOUT;
		bp->serdes_an_pending = 1;
		mod_timer(&bp->timer, jiffies + bp->current_interval);
	}

	bnx2_read_phy(bp, bp->mii_bmcr, &bmcr);
	bmcr &= ~BMCR_LOOPBACK;
	bnx2_write_phy(bp, bp->mii_bmcr, bmcr | BMCR_ANRESTART | BMCR_ANENABLE);

	spin_unlock_bh(&bp->phy_lock);

	return 0;
}

static int
bnx2_get_eeprom_len(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);

	if (bp->flash_info == NULL)
		return 0;

	return (int) bp->flash_size;
}

static int
bnx2_get_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom,
		u8 *eebuf)
{
	struct bnx2 *bp = netdev_priv(dev);
	int rc;

	if (!netif_running(dev))
		return -EAGAIN;

	/* parameters already validated in ethtool_get_eeprom */

	rc = bnx2_nvram_read(bp, eeprom->offset, eebuf, eeprom->len);

	return rc;
}

static int
bnx2_set_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom,
		u8 *eebuf)
{
	struct bnx2 *bp = netdev_priv(dev);
	int rc;

	if (!netif_running(dev))
		return -EAGAIN;

	/* parameters already validated in ethtool_set_eeprom */

	rc = bnx2_nvram_write(bp, eeprom->offset, eebuf, eeprom->len);

	return rc;
}

static int
bnx2_get_coalesce(struct net_device *dev, struct ethtool_coalesce *coal)
{
	struct bnx2 *bp = netdev_priv(dev);

	memset(coal, 0, sizeof(struct ethtool_coalesce));

	coal->rx_coalesce_usecs = bp->rx_ticks;
	coal->rx_max_coalesced_frames = bp->rx_quick_cons_trip;
	coal->rx_coalesce_usecs_irq = bp->rx_ticks_int;
	coal->rx_max_coalesced_frames_irq = bp->rx_quick_cons_trip_int;

	coal->tx_coalesce_usecs = bp->tx_ticks;
	coal->tx_max_coalesced_frames = bp->tx_quick_cons_trip;
	coal->tx_coalesce_usecs_irq = bp->tx_ticks_int;
	coal->tx_max_coalesced_frames_irq = bp->tx_quick_cons_trip_int;

	coal->stats_block_coalesce_usecs = bp->stats_ticks;

	return 0;
}

static int
bnx2_set_coalesce(struct net_device *dev, struct ethtool_coalesce *coal)
{
	struct bnx2 *bp = netdev_priv(dev);

	bp->rx_ticks = (u16) coal->rx_coalesce_usecs;
	if (bp->rx_ticks > 0x3ff) bp->rx_ticks = 0x3ff;

	bp->rx_quick_cons_trip = (u16) coal->rx_max_coalesced_frames;
	if (bp->rx_quick_cons_trip > 0xff) bp->rx_quick_cons_trip = 0xff;

	bp->rx_ticks_int = (u16) coal->rx_coalesce_usecs_irq;
	if (bp->rx_ticks_int > 0x3ff) bp->rx_ticks_int = 0x3ff;

	bp->rx_quick_cons_trip_int = (u16) coal->rx_max_coalesced_frames_irq;
	if (bp->rx_quick_cons_trip_int > 0xff)
		bp->rx_quick_cons_trip_int = 0xff;

	bp->tx_ticks = (u16) coal->tx_coalesce_usecs;
	if (bp->tx_ticks > 0x3ff) bp->tx_ticks = 0x3ff;

	bp->tx_quick_cons_trip = (u16) coal->tx_max_coalesced_frames;
	if (bp->tx_quick_cons_trip > 0xff) bp->tx_quick_cons_trip = 0xff;

	bp->tx_ticks_int = (u16) coal->tx_coalesce_usecs_irq;
	if (bp->tx_ticks_int > 0x3ff) bp->tx_ticks_int = 0x3ff;

	bp->tx_quick_cons_trip_int = (u16) coal->tx_max_coalesced_frames_irq;
	if (bp->tx_quick_cons_trip_int > 0xff) bp->tx_quick_cons_trip_int =
		0xff;

	bp->stats_ticks = coal->stats_block_coalesce_usecs;
	if (CHIP_NUM(bp) == CHIP_NUM_5708) {
		if (bp->stats_ticks != 0 && bp->stats_ticks != USEC_PER_SEC)
			bp->stats_ticks = USEC_PER_SEC;
	}
	if (bp->stats_ticks > BNX2_HC_STATS_TICKS_HC_STAT_TICKS)
		bp->stats_ticks = BNX2_HC_STATS_TICKS_HC_STAT_TICKS;
	bp->stats_ticks &= BNX2_HC_STATS_TICKS_HC_STAT_TICKS;

	if (netif_running(bp->dev)) {
		bnx2_netif_stop(bp);
		bnx2_init_nic(bp, 0);
		bnx2_netif_start(bp);
	}

	return 0;
}

static void
bnx2_get_ringparam(struct net_device *dev, struct ethtool_ringparam *ering)
{
	struct bnx2 *bp = netdev_priv(dev);

	ering->rx_max_pending = MAX_TOTAL_RX_DESC_CNT;
	ering->rx_mini_max_pending = 0;
	ering->rx_jumbo_max_pending = MAX_TOTAL_RX_PG_DESC_CNT;

	ering->rx_pending = bp->rx_ring_size;
	ering->rx_mini_pending = 0;
	ering->rx_jumbo_pending = bp->rx_pg_ring_size;

	ering->tx_max_pending = MAX_TX_DESC_CNT;
	ering->tx_pending = bp->tx_ring_size;
}

static int
bnx2_change_ring_size(struct bnx2 *bp, u32 rx, u32 tx)
{
	if (netif_running(bp->dev)) {
		bnx2_netif_stop(bp);
		bnx2_reset_chip(bp, BNX2_DRV_MSG_CODE_RESET);
		bnx2_free_skbs(bp);
		bnx2_free_mem(bp);
	}

	bnx2_set_rx_ring_size(bp, rx);
	bp->tx_ring_size = tx;

	if (netif_running(bp->dev)) {
		int rc;

		rc = bnx2_alloc_mem(bp);
		if (rc)
			return rc;
		bnx2_init_nic(bp, 0);
		bnx2_netif_start(bp);
	}
	return 0;
}

static int
bnx2_set_ringparam(struct net_device *dev, struct ethtool_ringparam *ering)
{
	struct bnx2 *bp = netdev_priv(dev);
	int rc;

	if ((ering->rx_pending > MAX_TOTAL_RX_DESC_CNT) ||
		(ering->tx_pending > MAX_TX_DESC_CNT) ||
		(ering->tx_pending <= MAX_SKB_FRAGS)) {

		return -EINVAL;
	}
	rc = bnx2_change_ring_size(bp, ering->rx_pending, ering->tx_pending);
	return rc;
}

static void
bnx2_get_pauseparam(struct net_device *dev, struct ethtool_pauseparam *epause)
{
	struct bnx2 *bp = netdev_priv(dev);

	epause->autoneg = ((bp->autoneg & AUTONEG_FLOW_CTRL) != 0);
	epause->rx_pause = ((bp->flow_ctrl & FLOW_CTRL_RX) != 0);
	epause->tx_pause = ((bp->flow_ctrl & FLOW_CTRL_TX) != 0);
}

static int
bnx2_set_pauseparam(struct net_device *dev, struct ethtool_pauseparam *epause)
{
	struct bnx2 *bp = netdev_priv(dev);

	bp->req_flow_ctrl = 0;
	if (epause->rx_pause)
		bp->req_flow_ctrl |= FLOW_CTRL_RX;
	if (epause->tx_pause)
		bp->req_flow_ctrl |= FLOW_CTRL_TX;

	if (epause->autoneg) {
		bp->autoneg |= AUTONEG_FLOW_CTRL;
	}
	else {
		bp->autoneg &= ~AUTONEG_FLOW_CTRL;
	}

	if (netif_running(dev)) {
		spin_lock_bh(&bp->phy_lock);
		bnx2_setup_phy(bp, bp->phy_port);
		spin_unlock_bh(&bp->phy_lock);
	}

	return 0;
}

static u32
bnx2_get_rx_csum(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);

	return bp->rx_csum;
}

static int
bnx2_set_rx_csum(struct net_device *dev, u32 data)
{
	struct bnx2 *bp = netdev_priv(dev);

	bp->rx_csum = data;
	return 0;
}

static int
bnx2_set_tso(struct net_device *dev, u32 data)
{
	struct bnx2 *bp = netdev_priv(dev);

	if (data) {
		dev->features |= NETIF_F_TSO | NETIF_F_TSO_ECN;
		if (CHIP_NUM(bp) == CHIP_NUM_5709)
			dev->features |= NETIF_F_TSO6;
	} else
		dev->features &= ~(NETIF_F_TSO | NETIF_F_TSO6 |
				   NETIF_F_TSO_ECN);
	return 0;
}

#define BNX2_NUM_STATS 46

static struct {
	char string[ETH_GSTRING_LEN];
} bnx2_stats_str_arr[BNX2_NUM_STATS] = {
	{ "rx_bytes" },
	{ "rx_error_bytes" },
	{ "tx_bytes" },
	{ "tx_error_bytes" },
	{ "rx_ucast_packets" },
	{ "rx_mcast_packets" },
	{ "rx_bcast_packets" },
	{ "tx_ucast_packets" },
	{ "tx_mcast_packets" },
	{ "tx_bcast_packets" },
	{ "tx_mac_errors" },
	{ "tx_carrier_errors" },
	{ "rx_crc_errors" },
	{ "rx_align_errors" },
	{ "tx_single_collisions" },
	{ "tx_multi_collisions" },
	{ "tx_deferred" },
	{ "tx_excess_collisions" },
	{ "tx_late_collisions" },
	{ "tx_total_collisions" },
	{ "rx_fragments" },
	{ "rx_jabbers" },
	{ "rx_undersize_packets" },
	{ "rx_oversize_packets" },
	{ "rx_64_byte_packets" },
	{ "rx_65_to_127_byte_packets" },
	{ "rx_128_to_255_byte_packets" },
	{ "rx_256_to_511_byte_packets" },
	{ "rx_512_to_1023_byte_packets" },
	{ "rx_1024_to_1522_byte_packets" },
	{ "rx_1523_to_9022_byte_packets" },
	{ "tx_64_byte_packets" },
	{ "tx_65_to_127_byte_packets" },
	{ "tx_128_to_255_byte_packets" },
	{ "tx_256_to_511_byte_packets" },
	{ "tx_512_to_1023_byte_packets" },
	{ "tx_1024_to_1522_byte_packets" },
	{ "tx_1523_to_9022_byte_packets" },
	{ "rx_xon_frames" },
	{ "rx_xoff_frames" },
	{ "tx_xon_frames" },
	{ "tx_xoff_frames" },
	{ "rx_mac_ctrl_frames" },
	{ "rx_filtered_packets" },
	{ "rx_discards" },
	{ "rx_fw_discards" },
};

#define STATS_OFFSET32(offset_name) (offsetof(struct statistics_block, offset_name) / 4)

static const unsigned long bnx2_stats_offset_arr[BNX2_NUM_STATS] = {
    STATS_OFFSET32(stat_IfHCInOctets_hi),
    STATS_OFFSET32(stat_IfHCInBadOctets_hi),
    STATS_OFFSET32(stat_IfHCOutOctets_hi),
    STATS_OFFSET32(stat_IfHCOutBadOctets_hi),
    STATS_OFFSET32(stat_IfHCInUcastPkts_hi),
    STATS_OFFSET32(stat_IfHCInMulticastPkts_hi),
    STATS_OFFSET32(stat_IfHCInBroadcastPkts_hi),
    STATS_OFFSET32(stat_IfHCOutUcastPkts_hi),
    STATS_OFFSET32(stat_IfHCOutMulticastPkts_hi),
    STATS_OFFSET32(stat_IfHCOutBroadcastPkts_hi),
    STATS_OFFSET32(stat_emac_tx_stat_dot3statsinternalmactransmiterrors),
    STATS_OFFSET32(stat_Dot3StatsCarrierSenseErrors),
    STATS_OFFSET32(stat_Dot3StatsFCSErrors),
    STATS_OFFSET32(stat_Dot3StatsAlignmentErrors),
    STATS_OFFSET32(stat_Dot3StatsSingleCollisionFrames),
    STATS_OFFSET32(stat_Dot3StatsMultipleCollisionFrames),
    STATS_OFFSET32(stat_Dot3StatsDeferredTransmissions),
    STATS_OFFSET32(stat_Dot3StatsExcessiveCollisions),
    STATS_OFFSET32(stat_Dot3StatsLateCollisions),
    STATS_OFFSET32(stat_EtherStatsCollisions),
    STATS_OFFSET32(stat_EtherStatsFragments),
    STATS_OFFSET32(stat_EtherStatsJabbers),
    STATS_OFFSET32(stat_EtherStatsUndersizePkts),
    STATS_OFFSET32(stat_EtherStatsOverrsizePkts),
    STATS_OFFSET32(stat_EtherStatsPktsRx64Octets),
    STATS_OFFSET32(stat_EtherStatsPktsRx65Octetsto127Octets),
    STATS_OFFSET32(stat_EtherStatsPktsRx128Octetsto255Octets),
    STATS_OFFSET32(stat_EtherStatsPktsRx256Octetsto511Octets),
    STATS_OFFSET32(stat_EtherStatsPktsRx512Octetsto1023Octets),
    STATS_OFFSET32(stat_EtherStatsPktsRx1024Octetsto1522Octets),
    STATS_OFFSET32(stat_EtherStatsPktsRx1523Octetsto9022Octets),
    STATS_OFFSET32(stat_EtherStatsPktsTx64Octets),
    STATS_OFFSET32(stat_EtherStatsPktsTx65Octetsto127Octets),
    STATS_OFFSET32(stat_EtherStatsPktsTx128Octetsto255Octets),
    STATS_OFFSET32(stat_EtherStatsPktsTx256Octetsto511Octets),
    STATS_OFFSET32(stat_EtherStatsPktsTx512Octetsto1023Octets),
    STATS_OFFSET32(stat_EtherStatsPktsTx1024Octetsto1522Octets),
    STATS_OFFSET32(stat_EtherStatsPktsTx1523Octetsto9022Octets),
    STATS_OFFSET32(stat_XonPauseFramesReceived),
    STATS_OFFSET32(stat_XoffPauseFramesReceived),
    STATS_OFFSET32(stat_OutXonSent),
    STATS_OFFSET32(stat_OutXoffSent),
    STATS_OFFSET32(stat_MacControlFramesReceived),
    STATS_OFFSET32(stat_IfInFramesL2FilterDiscards),
    STATS_OFFSET32(stat_IfInMBUFDiscards),
    STATS_OFFSET32(stat_FwRxDrop),
};

/* stat_IfHCInBadOctets and stat_Dot3StatsCarrierSenseErrors are
 * skipped because of errata.
 */
static u8 bnx2_5706_stats_len_arr[BNX2_NUM_STATS] = {
	8,0,8,8,8,8,8,8,8,8,
	4,0,4,4,4,4,4,4,4,4,
	4,4,4,4,4,4,4,4,4,4,
	4,4,4,4,4,4,4,4,4,4,
	4,4,4,4,4,4,
};

static u8 bnx2_5708_stats_len_arr[BNX2_NUM_STATS] = {
	8,0,8,8,8,8,8,8,8,8,
	4,4,4,4,4,4,4,4,4,4,
	4,4,4,4,4,4,4,4,4,4,
	4,4,4,4,4,4,4,4,4,4,
	4,4,4,4,4,4,
};

#define BNX2_NUM_TESTS 6

static struct {
	char string[ETH_GSTRING_LEN];
} bnx2_tests_str_arr[BNX2_NUM_TESTS] = {
	{ "register_test (offline)" },
	{ "memory_test (offline)" },
	{ "loopback_test (offline)" },
	{ "nvram_test (online)" },
	{ "interrupt_test (online)" },
	{ "link_test (online)" },
};

static int
bnx2_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return BNX2_NUM_TESTS;
	case ETH_SS_STATS:
		return BNX2_NUM_STATS;
	default:
		return -EOPNOTSUPP;
	}
}

static void
bnx2_self_test(struct net_device *dev, struct ethtool_test *etest, u64 *buf)
{
	struct bnx2 *bp = netdev_priv(dev);

	bnx2_set_power_state(bp, PCI_D0);

	memset(buf, 0, sizeof(u64) * BNX2_NUM_TESTS);
	if (etest->flags & ETH_TEST_FL_OFFLINE) {
		int i;

		bnx2_netif_stop(bp);
		bnx2_reset_chip(bp, BNX2_DRV_MSG_CODE_DIAG);
		bnx2_free_skbs(bp);

		if (bnx2_test_registers(bp) != 0) {
			buf[0] = 1;
			etest->flags |= ETH_TEST_FL_FAILED;
		}
		if (bnx2_test_memory(bp) != 0) {
			buf[1] = 1;
			etest->flags |= ETH_TEST_FL_FAILED;
		}
		if ((buf[2] = bnx2_test_loopback(bp)) != 0)
			etest->flags |= ETH_TEST_FL_FAILED;

		if (!netif_running(bp->dev))
			bnx2_shutdown_chip(bp);
		else {
			bnx2_init_nic(bp, 1);
			bnx2_netif_start(bp);
		}

		/* wait for link up */
		for (i = 0; i < 7; i++) {
			if (bp->link_up)
				break;
			msleep_interruptible(1000);
		}
	}

	if (bnx2_test_nvram(bp) != 0) {
		buf[3] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}
	if (bnx2_test_intr(bp) != 0) {
		buf[4] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;
	}

	if (bnx2_test_link(bp) != 0) {
		buf[5] = 1;
		etest->flags |= ETH_TEST_FL_FAILED;

	}
	if (!netif_running(bp->dev))
		bnx2_set_power_state(bp, PCI_D3hot);
}

static void
bnx2_get_strings(struct net_device *dev, u32 stringset, u8 *buf)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(buf, bnx2_stats_str_arr,
			sizeof(bnx2_stats_str_arr));
		break;
	case ETH_SS_TEST:
		memcpy(buf, bnx2_tests_str_arr,
			sizeof(bnx2_tests_str_arr));
		break;
	}
}

static void
bnx2_get_ethtool_stats(struct net_device *dev,
		struct ethtool_stats *stats, u64 *buf)
{
	struct bnx2 *bp = netdev_priv(dev);
	int i;
	u32 *hw_stats = (u32 *) bp->stats_blk;
	u8 *stats_len_arr = NULL;

	if (hw_stats == NULL) {
		memset(buf, 0, sizeof(u64) * BNX2_NUM_STATS);
		return;
	}

	if ((CHIP_ID(bp) == CHIP_ID_5706_A0) ||
	    (CHIP_ID(bp) == CHIP_ID_5706_A1) ||
	    (CHIP_ID(bp) == CHIP_ID_5706_A2) ||
	    (CHIP_ID(bp) == CHIP_ID_5708_A0))
		stats_len_arr = bnx2_5706_stats_len_arr;
	else
		stats_len_arr = bnx2_5708_stats_len_arr;

	for (i = 0; i < BNX2_NUM_STATS; i++) {
		if (stats_len_arr[i] == 0) {
			/* skip this counter */
			buf[i] = 0;
			continue;
		}
		if (stats_len_arr[i] == 4) {
			/* 4-byte counter */
			buf[i] = (u64)
				*(hw_stats + bnx2_stats_offset_arr[i]);
			continue;
		}
		/* 8-byte counter */
		buf[i] = (((u64) *(hw_stats +
					bnx2_stats_offset_arr[i])) << 32) +
				*(hw_stats + bnx2_stats_offset_arr[i] + 1);
	}
}

static int
bnx2_phys_id(struct net_device *dev, u32 data)
{
	struct bnx2 *bp = netdev_priv(dev);
	int i;
	u32 save;

	bnx2_set_power_state(bp, PCI_D0);

	if (data == 0)
		data = 2;

	save = REG_RD(bp, BNX2_MISC_CFG);
	REG_WR(bp, BNX2_MISC_CFG, BNX2_MISC_CFG_LEDMODE_MAC);

	for (i = 0; i < (data * 2); i++) {
		if ((i % 2) == 0) {
			REG_WR(bp, BNX2_EMAC_LED, BNX2_EMAC_LED_OVERRIDE);
		}
		else {
			REG_WR(bp, BNX2_EMAC_LED, BNX2_EMAC_LED_OVERRIDE |
				BNX2_EMAC_LED_1000MB_OVERRIDE |
				BNX2_EMAC_LED_100MB_OVERRIDE |
				BNX2_EMAC_LED_10MB_OVERRIDE |
				BNX2_EMAC_LED_TRAFFIC_OVERRIDE |
				BNX2_EMAC_LED_TRAFFIC);
		}
		msleep_interruptible(500);
		if (signal_pending(current))
			break;
	}
	REG_WR(bp, BNX2_EMAC_LED, 0);
	REG_WR(bp, BNX2_MISC_CFG, save);

	if (!netif_running(dev))
		bnx2_set_power_state(bp, PCI_D3hot);

	return 0;
}

static int
bnx2_set_tx_csum(struct net_device *dev, u32 data)
{
	struct bnx2 *bp = netdev_priv(dev);

	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		return (ethtool_op_set_tx_ipv6_csum(dev, data));
	else
		return (ethtool_op_set_tx_csum(dev, data));
}

static const struct ethtool_ops bnx2_ethtool_ops = {
	.get_settings		= bnx2_get_settings,
	.set_settings		= bnx2_set_settings,
	.get_drvinfo		= bnx2_get_drvinfo,
	.get_regs_len		= bnx2_get_regs_len,
	.get_regs		= bnx2_get_regs,
	.get_wol		= bnx2_get_wol,
	.set_wol		= bnx2_set_wol,
	.nway_reset		= bnx2_nway_reset,
	.get_link		= ethtool_op_get_link,
	.get_eeprom_len		= bnx2_get_eeprom_len,
	.get_eeprom		= bnx2_get_eeprom,
	.set_eeprom		= bnx2_set_eeprom,
	.get_coalesce		= bnx2_get_coalesce,
	.set_coalesce		= bnx2_set_coalesce,
	.get_ringparam		= bnx2_get_ringparam,
	.set_ringparam		= bnx2_set_ringparam,
	.get_pauseparam		= bnx2_get_pauseparam,
	.set_pauseparam		= bnx2_set_pauseparam,
	.get_rx_csum		= bnx2_get_rx_csum,
	.set_rx_csum		= bnx2_set_rx_csum,
	.set_tx_csum		= bnx2_set_tx_csum,
	.set_sg			= ethtool_op_set_sg,
	.set_tso		= bnx2_set_tso,
	.self_test		= bnx2_self_test,
	.get_strings		= bnx2_get_strings,
	.phys_id		= bnx2_phys_id,
	.get_ethtool_stats	= bnx2_get_ethtool_stats,
	.get_sset_count		= bnx2_get_sset_count,
};

/* Called with rtnl_lock */
static int
bnx2_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct mii_ioctl_data *data = if_mii(ifr);
	struct bnx2 *bp = netdev_priv(dev);
	int err;

	switch(cmd) {
	case SIOCGMIIPHY:
		data->phy_id = bp->phy_addr;

		/* fallthru */
	case SIOCGMIIREG: {
		u32 mii_regval;

		if (bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP)
			return -EOPNOTSUPP;

		if (!netif_running(dev))
			return -EAGAIN;

		spin_lock_bh(&bp->phy_lock);
		err = bnx2_read_phy(bp, data->reg_num & 0x1f, &mii_regval);
		spin_unlock_bh(&bp->phy_lock);

		data->val_out = mii_regval;

		return err;
	}

	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;

		if (bp->phy_flags & BNX2_PHY_FLAG_REMOTE_PHY_CAP)
			return -EOPNOTSUPP;

		if (!netif_running(dev))
			return -EAGAIN;

		spin_lock_bh(&bp->phy_lock);
		err = bnx2_write_phy(bp, data->reg_num & 0x1f, data->val_in);
		spin_unlock_bh(&bp->phy_lock);

		return err;

	default:
		/* do nothing */
		break;
	}
	return -EOPNOTSUPP;
}

/* Called with rtnl_lock */
static int
bnx2_change_mac_addr(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct bnx2 *bp = netdev_priv(dev);

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	if (netif_running(dev))
		bnx2_set_mac_addr(bp, bp->dev->dev_addr, 0);

	return 0;
}

/* Called with rtnl_lock */
static int
bnx2_change_mtu(struct net_device *dev, int new_mtu)
{
	struct bnx2 *bp = netdev_priv(dev);

	if (((new_mtu + ETH_HLEN) > MAX_ETHERNET_JUMBO_PACKET_SIZE) ||
		((new_mtu + ETH_HLEN) < MIN_ETHERNET_PACKET_SIZE))
		return -EINVAL;

	dev->mtu = new_mtu;
	return (bnx2_change_ring_size(bp, bp->rx_ring_size, bp->tx_ring_size));
}

#if defined(HAVE_POLL_CONTROLLER) || defined(CONFIG_NET_POLL_CONTROLLER)
static void
poll_bnx2(struct net_device *dev)
{
	struct bnx2 *bp = netdev_priv(dev);

	disable_irq(bp->pdev->irq);
	bnx2_interrupt(bp->pdev->irq, dev);
	enable_irq(bp->pdev->irq);
}
#endif

static void __devinit
bnx2_get_5709_media(struct bnx2 *bp)
{
	u32 val = REG_RD(bp, BNX2_MISC_DUAL_MEDIA_CTRL);
	u32 bond_id = val & BNX2_MISC_DUAL_MEDIA_CTRL_BOND_ID;
	u32 strap;

	if (bond_id == BNX2_MISC_DUAL_MEDIA_CTRL_BOND_ID_C)
		return;
	else if (bond_id == BNX2_MISC_DUAL_MEDIA_CTRL_BOND_ID_S) {
		bp->phy_flags |= BNX2_PHY_FLAG_SERDES;
		return;
	}

	if (val & BNX2_MISC_DUAL_MEDIA_CTRL_STRAP_OVERRIDE)
		strap = (val & BNX2_MISC_DUAL_MEDIA_CTRL_PHY_CTRL) >> 21;
	else
		strap = (val & BNX2_MISC_DUAL_MEDIA_CTRL_PHY_CTRL_STRAP) >> 8;

	if (PCI_FUNC(bp->pdev->devfn) == 0) {
		switch (strap) {
		case 0x4:
		case 0x5:
		case 0x6:
			bp->phy_flags |= BNX2_PHY_FLAG_SERDES;
			return;
		}
	} else {
		switch (strap) {
		case 0x1:
		case 0x2:
		case 0x4:
			bp->phy_flags |= BNX2_PHY_FLAG_SERDES;
			return;
		}
	}
}

static void __devinit
bnx2_get_pci_speed(struct bnx2 *bp)
{
	u32 reg;

	reg = REG_RD(bp, BNX2_PCICFG_MISC_STATUS);
	if (reg & BNX2_PCICFG_MISC_STATUS_PCIX_DET) {
		u32 clkreg;

		bp->flags |= BNX2_FLAG_PCIX;

		clkreg = REG_RD(bp, BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS);

		clkreg &= BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET;
		switch (clkreg) {
		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_133MHZ:
			bp->bus_speed_mhz = 133;
			break;

		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_95MHZ:
			bp->bus_speed_mhz = 100;
			break;

		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_66MHZ:
		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_80MHZ:
			bp->bus_speed_mhz = 66;
			break;

		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_48MHZ:
		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_55MHZ:
			bp->bus_speed_mhz = 50;
			break;

		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_LOW:
		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_32MHZ:
		case BNX2_PCICFG_PCI_CLOCK_CONTROL_BITS_PCI_CLK_SPD_DET_38MHZ:
			bp->bus_speed_mhz = 33;
			break;
		}
	}
	else {
		if (reg & BNX2_PCICFG_MISC_STATUS_M66EN)
			bp->bus_speed_mhz = 66;
		else
			bp->bus_speed_mhz = 33;
	}

	if (reg & BNX2_PCICFG_MISC_STATUS_32BIT_DET)
		bp->flags |= BNX2_FLAG_PCI_32BIT;

}

static int __devinit
bnx2_init_board(struct pci_dev *pdev, struct net_device *dev)
{
	struct bnx2 *bp;
	unsigned long mem_len;
	int rc, i, j;
	u32 reg;
	u64 dma_mask, persist_dma_mask;

	SET_NETDEV_DEV(dev, &pdev->dev);
	bp = netdev_priv(dev);

	bp->flags = 0;
	bp->phy_flags = 0;

	/* enable device (incl. PCI PM wakeup), and bus-mastering */
	rc = pci_enable_device(pdev);
	if (rc) {
		dev_err(&pdev->dev, "Cannot enable PCI device, aborting.\n");
		goto err_out;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		dev_err(&pdev->dev,
			"Cannot find PCI device base address, aborting.\n");
		rc = -ENODEV;
		goto err_out_disable;
	}

	rc = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (rc) {
		dev_err(&pdev->dev, "Cannot obtain PCI resources, aborting.\n");
		goto err_out_disable;
	}

	pci_set_master(pdev);
	pci_save_state(pdev);

	bp->pm_cap = pci_find_capability(pdev, PCI_CAP_ID_PM);
	if (bp->pm_cap == 0) {
		dev_err(&pdev->dev,
			"Cannot find power management capability, aborting.\n");
		rc = -EIO;
		goto err_out_release;
	}

	bp->dev = dev;
	bp->pdev = pdev;

	spin_lock_init(&bp->phy_lock);
	spin_lock_init(&bp->indirect_lock);
	INIT_WORK(&bp->reset_task, bnx2_reset_task);

	dev->base_addr = dev->mem_start = pci_resource_start(pdev, 0);
	mem_len = MB_GET_CID_ADDR(TX_TSS_CID + TX_MAX_TSS_RINGS);
	dev->mem_end = dev->mem_start + mem_len;
	dev->irq = pdev->irq;

	bp->regview = ioremap_nocache(dev->base_addr, mem_len);

	if (!bp->regview) {
		dev_err(&pdev->dev, "Cannot map register space, aborting.\n");
		rc = -ENOMEM;
		goto err_out_release;
	}

	/* Configure byte swap and enable write to the reg_window registers.
	 * Rely on CPU to do target byte swapping on big endian systems
	 * The chip's target access swapping will not swap all accesses
	 */
	pci_write_config_dword(bp->pdev, BNX2_PCICFG_MISC_CONFIG,
			       BNX2_PCICFG_MISC_CONFIG_REG_WINDOW_ENA |
			       BNX2_PCICFG_MISC_CONFIG_TARGET_MB_WORD_SWAP);

	bnx2_set_power_state(bp, PCI_D0);

	bp->chip_id = REG_RD(bp, BNX2_MISC_ID);

	if (CHIP_NUM(bp) == CHIP_NUM_5709) {
		if (pci_find_capability(pdev, PCI_CAP_ID_EXP) == 0) {
			dev_err(&pdev->dev,
				"Cannot find PCIE capability, aborting.\n");
			rc = -EIO;
			goto err_out_unmap;
		}
		bp->flags |= BNX2_FLAG_PCIE;
		if (CHIP_REV(bp) == CHIP_REV_Ax)
			bp->flags |= BNX2_FLAG_JUMBO_BROKEN;
	} else {
		bp->pcix_cap = pci_find_capability(pdev, PCI_CAP_ID_PCIX);
		if (bp->pcix_cap == 0) {
			dev_err(&pdev->dev,
				"Cannot find PCIX capability, aborting.\n");
			rc = -EIO;
			goto err_out_unmap;
		}
	}

	if (CHIP_NUM(bp) == CHIP_NUM_5709 && CHIP_REV(bp) != CHIP_REV_Ax) {
		if (pci_find_capability(pdev, PCI_CAP_ID_MSIX))
			bp->flags |= BNX2_FLAG_MSIX_CAP;
	}

	if (CHIP_ID(bp) != CHIP_ID_5706_A0 && CHIP_ID(bp) != CHIP_ID_5706_A1) {
		if (pci_find_capability(pdev, PCI_CAP_ID_MSI))
			bp->flags |= BNX2_FLAG_MSI_CAP;
	}

	/* 5708 cannot support DMA addresses > 40-bit.  */
	if (CHIP_NUM(bp) == CHIP_NUM_5708)
		persist_dma_mask = dma_mask = DMA_40BIT_MASK;
	else
		persist_dma_mask = dma_mask = DMA_64BIT_MASK;

	/* Configure DMA attributes. */
	if (pci_set_dma_mask(pdev, dma_mask) == 0) {
		dev->features |= NETIF_F_HIGHDMA;
		rc = pci_set_consistent_dma_mask(pdev, persist_dma_mask);
		if (rc) {
			dev_err(&pdev->dev,
				"pci_set_consistent_dma_mask failed, aborting.\n");
			goto err_out_unmap;
		}
	} else if ((rc = pci_set_dma_mask(pdev, DMA_32BIT_MASK)) != 0) {
		dev_err(&pdev->dev, "System does not support DMA, aborting.\n");
		goto err_out_unmap;
	}

	if (!(bp->flags & BNX2_FLAG_PCIE))
		bnx2_get_pci_speed(bp);

	/* 5706A0 may falsely detect SERR and PERR. */
	if (CHIP_ID(bp) == CHIP_ID_5706_A0) {
		reg = REG_RD(bp, PCI_COMMAND);
		reg &= ~(PCI_COMMAND_SERR | PCI_COMMAND_PARITY);
		REG_WR(bp, PCI_COMMAND, reg);
	}
	else if ((CHIP_ID(bp) == CHIP_ID_5706_A1) &&
		!(bp->flags & BNX2_FLAG_PCIX)) {

		dev_err(&pdev->dev,
			"5706 A1 can only be used in a PCIX bus, aborting.\n");
		goto err_out_unmap;
	}

	bnx2_init_nvram(bp);

	reg = bnx2_reg_rd_ind(bp, BNX2_SHM_HDR_SIGNATURE);

	if ((reg & BNX2_SHM_HDR_SIGNATURE_SIG_MASK) ==
	    BNX2_SHM_HDR_SIGNATURE_SIG) {
		u32 off = PCI_FUNC(pdev->devfn) << 2;

		bp->shmem_base = bnx2_reg_rd_ind(bp, BNX2_SHM_HDR_ADDR_0 + off);
	} else
		bp->shmem_base = HOST_VIEW_SHMEM_BASE;

	/* Get the permanent MAC address.  First we need to make sure the
	 * firmware is actually running.
	 */
	reg = bnx2_shmem_rd(bp, BNX2_DEV_INFO_SIGNATURE);

	if ((reg & BNX2_DEV_INFO_SIGNATURE_MAGIC_MASK) !=
	    BNX2_DEV_INFO_SIGNATURE_MAGIC) {
		dev_err(&pdev->dev, "Firmware not running, aborting.\n");
		rc = -ENODEV;
		goto err_out_unmap;
	}

	reg = bnx2_shmem_rd(bp, BNX2_DEV_INFO_BC_REV);
	for (i = 0, j = 0; i < 3; i++) {
		u8 num, k, skip0;

		num = (u8) (reg >> (24 - (i * 8)));
		for (k = 100, skip0 = 1; k >= 1; num %= k, k /= 10) {
			if (num >= k || !skip0 || k == 1) {
				bp->fw_version[j++] = (num / k) + '0';
				skip0 = 0;
			}
		}
		if (i != 2)
			bp->fw_version[j++] = '.';
	}
	reg = bnx2_shmem_rd(bp, BNX2_PORT_FEATURE);
	if (reg & BNX2_PORT_FEATURE_WOL_ENABLED)
		bp->wol = 1;

	if (reg & BNX2_PORT_FEATURE_ASF_ENABLED) {
		bp->flags |= BNX2_FLAG_ASF_ENABLE;

		for (i = 0; i < 30; i++) {
			reg = bnx2_shmem_rd(bp, BNX2_BC_STATE_CONDITION);
			if (reg & BNX2_CONDITION_MFW_RUN_MASK)
				break;
			msleep(10);
		}
	}
	reg = bnx2_shmem_rd(bp, BNX2_BC_STATE_CONDITION);
	reg &= BNX2_CONDITION_MFW_RUN_MASK;
	if (reg != BNX2_CONDITION_MFW_RUN_UNKNOWN &&
	    reg != BNX2_CONDITION_MFW_RUN_NONE) {
		u32 addr = bnx2_shmem_rd(bp, BNX2_MFW_VER_PTR);

		bp->fw_version[j++] = ' ';
		for (i = 0; i < 3; i++) {
			reg = bnx2_reg_rd_ind(bp, addr + i * 4);
			reg = swab32(reg);
			memcpy(&bp->fw_version[j], &reg, 4);
			j += 4;
		}
	}

	reg = bnx2_shmem_rd(bp, BNX2_PORT_HW_CFG_MAC_UPPER);
	bp->mac_addr[0] = (u8) (reg >> 8);
	bp->mac_addr[1] = (u8) reg;

	reg = bnx2_shmem_rd(bp, BNX2_PORT_HW_CFG_MAC_LOWER);
	bp->mac_addr[2] = (u8) (reg >> 24);
	bp->mac_addr[3] = (u8) (reg >> 16);
	bp->mac_addr[4] = (u8) (reg >> 8);
	bp->mac_addr[5] = (u8) reg;

	bp->tx_ring_size = MAX_TX_DESC_CNT;
	bnx2_set_rx_ring_size(bp, 255);

	bp->rx_csum = 1;

	bp->tx_quick_cons_trip_int = 20;
	bp->tx_quick_cons_trip = 20;
	bp->tx_ticks_int = 80;
	bp->tx_ticks = 80;

	bp->rx_quick_cons_trip_int = 6;
	bp->rx_quick_cons_trip = 6;
	bp->rx_ticks_int = 18;
	bp->rx_ticks = 18;

	bp->stats_ticks = USEC_PER_SEC & BNX2_HC_STATS_TICKS_HC_STAT_TICKS;

	bp->current_interval = BNX2_TIMER_INTERVAL;

	bp->phy_addr = 1;

	/* Disable WOL support if we are running on a SERDES chip. */
	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		bnx2_get_5709_media(bp);
	else if (CHIP_BOND_ID(bp) & CHIP_BOND_ID_SERDES_BIT)
		bp->phy_flags |= BNX2_PHY_FLAG_SERDES;

	bp->phy_port = PORT_TP;
	if (bp->phy_flags & BNX2_PHY_FLAG_SERDES) {
		bp->phy_port = PORT_FIBRE;
		reg = bnx2_shmem_rd(bp, BNX2_SHARED_HW_CFG_CONFIG);
		if (!(reg & BNX2_SHARED_HW_CFG_GIG_LINK_ON_VAUX)) {
			bp->flags |= BNX2_FLAG_NO_WOL;
			bp->wol = 0;
		}
		if (CHIP_NUM(bp) == CHIP_NUM_5706) {
			/* Don't do parallel detect on this board because of
			 * some board problems.  The link will not go down
			 * if we do parallel detect.
			 */
			if (pdev->subsystem_vendor == PCI_VENDOR_ID_HP &&
			    pdev->subsystem_device == 0x310c)
				bp->phy_flags |= BNX2_PHY_FLAG_NO_PARALLEL;
		} else {
			bp->phy_addr = 2;
			if (reg & BNX2_SHARED_HW_CFG_PHY_2_5G)
				bp->phy_flags |= BNX2_PHY_FLAG_2_5G_CAPABLE;
		}
	} else if (CHIP_NUM(bp) == CHIP_NUM_5706 ||
		   CHIP_NUM(bp) == CHIP_NUM_5708)
		bp->phy_flags |= BNX2_PHY_FLAG_CRC_FIX;
	else if (CHIP_NUM(bp) == CHIP_NUM_5709 &&
		 (CHIP_REV(bp) == CHIP_REV_Ax ||
		  CHIP_REV(bp) == CHIP_REV_Bx))
		bp->phy_flags |= BNX2_PHY_FLAG_DIS_EARLY_DAC;

	bnx2_init_fw_cap(bp);

	if ((CHIP_ID(bp) == CHIP_ID_5708_A0) ||
	    (CHIP_ID(bp) == CHIP_ID_5708_B0) ||
	    (CHIP_ID(bp) == CHIP_ID_5708_B1)) {
		bp->flags |= BNX2_FLAG_NO_WOL;
		bp->wol = 0;
	}

	if (CHIP_ID(bp) == CHIP_ID_5706_A0) {
		bp->tx_quick_cons_trip_int =
			bp->tx_quick_cons_trip;
		bp->tx_ticks_int = bp->tx_ticks;
		bp->rx_quick_cons_trip_int =
			bp->rx_quick_cons_trip;
		bp->rx_ticks_int = bp->rx_ticks;
		bp->comp_prod_trip_int = bp->comp_prod_trip;
		bp->com_ticks_int = bp->com_ticks;
		bp->cmd_ticks_int = bp->cmd_ticks;
	}

	/* Disable MSI on 5706 if AMD 8132 bridge is found.
	 *
	 * MSI is defined to be 32-bit write.  The 5706 does 64-bit MSI writes
	 * with byte enables disabled on the unused 32-bit word.  This is legal
	 * but causes problems on the AMD 8132 which will eventually stop
	 * responding after a while.
	 *
	 * AMD believes this incompatibility is unique to the 5706, and
	 * prefers to locally disable MSI rather than globally disabling it.
	 */
	if (CHIP_NUM(bp) == CHIP_NUM_5706 && disable_msi == 0) {
		struct pci_dev *amd_8132 = NULL;

		while ((amd_8132 = pci_get_device(PCI_VENDOR_ID_AMD,
						  PCI_DEVICE_ID_AMD_8132_BRIDGE,
						  amd_8132))) {

			if (amd_8132->revision >= 0x10 &&
			    amd_8132->revision <= 0x13) {
				disable_msi = 1;
				pci_dev_put(amd_8132);
				break;
			}
		}
	}

	bnx2_set_default_link(bp);
	bp->req_flow_ctrl = FLOW_CTRL_RX | FLOW_CTRL_TX;

	init_timer(&bp->timer);
	bp->timer.expires = RUN_AT(BNX2_TIMER_INTERVAL);
	bp->timer.data = (unsigned long) bp;
	bp->timer.function = bnx2_timer;

	return 0;

err_out_unmap:
	if (bp->regview) {
		iounmap(bp->regview);
		bp->regview = NULL;
	}

err_out_release:
	pci_release_regions(pdev);

err_out_disable:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

err_out:
	return rc;
}

static char * __devinit
bnx2_bus_string(struct bnx2 *bp, char *str)
{
	char *s = str;

	if (bp->flags & BNX2_FLAG_PCIE) {
		s += sprintf(s, "PCI Express");
	} else {
		s += sprintf(s, "PCI");
		if (bp->flags & BNX2_FLAG_PCIX)
			s += sprintf(s, "-X");
		if (bp->flags & BNX2_FLAG_PCI_32BIT)
			s += sprintf(s, " 32-bit");
		else
			s += sprintf(s, " 64-bit");
		s += sprintf(s, " %dMHz", bp->bus_speed_mhz);
	}
	return str;
}

static void __devinit
bnx2_init_napi(struct bnx2 *bp)
{
	int i;

	for (i = 0; i < BNX2_MAX_MSIX_VEC; i++) {
		struct bnx2_napi *bnapi = &bp->bnx2_napi[i];
		int (*poll)(struct napi_struct *, int);

		if (i == 0)
			poll = bnx2_poll;
		else
			poll = bnx2_poll_msix;

		netif_napi_add(bp->dev, &bp->bnx2_napi[i].napi, poll, 64);
		bnapi->bp = bp;
	}
}

static int __devinit
bnx2_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int version_printed = 0;
	struct net_device *dev = NULL;
	struct bnx2 *bp;
	int rc;
	char str[40];
	DECLARE_MAC_BUF(mac);

	if (version_printed++ == 0)
		printk(KERN_INFO "%s", version);

	/* dev zeroed in init_etherdev */
	dev = alloc_etherdev_mq(sizeof(*bp), TX_MAX_RINGS);

	if (!dev)
		return -ENOMEM;

	rc = bnx2_init_board(pdev, dev);
	if (rc < 0) {
		free_netdev(dev);
		return rc;
	}

	dev->open = bnx2_open;
	dev->hard_start_xmit = bnx2_start_xmit;
	dev->stop = bnx2_close;
	dev->get_stats = bnx2_get_stats;
	dev->set_rx_mode = bnx2_set_rx_mode;
	dev->do_ioctl = bnx2_ioctl;
	dev->set_mac_address = bnx2_change_mac_addr;
	dev->change_mtu = bnx2_change_mtu;
	dev->tx_timeout = bnx2_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
#ifdef BCM_VLAN
	dev->vlan_rx_register = bnx2_vlan_rx_register;
#endif
	dev->ethtool_ops = &bnx2_ethtool_ops;

	bp = netdev_priv(dev);
	bnx2_init_napi(bp);

#if defined(HAVE_POLL_CONTROLLER) || defined(CONFIG_NET_POLL_CONTROLLER)
	dev->poll_controller = poll_bnx2;
#endif

	pci_set_drvdata(pdev, dev);

	memcpy(dev->dev_addr, bp->mac_addr, 6);
	memcpy(dev->perm_addr, bp->mac_addr, 6);

	dev->features |= NETIF_F_IP_CSUM | NETIF_F_SG;
	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		dev->features |= NETIF_F_IPV6_CSUM;

#ifdef BCM_VLAN
	dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
#endif
	dev->features |= NETIF_F_TSO | NETIF_F_TSO_ECN;
	if (CHIP_NUM(bp) == CHIP_NUM_5709)
		dev->features |= NETIF_F_TSO6;

	if ((rc = register_netdev(dev))) {
		dev_err(&pdev->dev, "Cannot register net device\n");
		if (bp->regview)
			iounmap(bp->regview);
		pci_release_regions(pdev);
		pci_disable_device(pdev);
		pci_set_drvdata(pdev, NULL);
		free_netdev(dev);
		return rc;
	}

	printk(KERN_INFO "%s: %s (%c%d) %s found at mem %lx, "
		"IRQ %d, node addr %s\n",
		dev->name,
		board_info[ent->driver_data].name,
		((CHIP_ID(bp) & 0xf000) >> 12) + 'A',
		((CHIP_ID(bp) & 0x0ff0) >> 4),
		bnx2_bus_string(bp, str),
		dev->base_addr,
		bp->pdev->irq, print_mac(mac, dev->dev_addr));

	return 0;
}

static void __devexit
bnx2_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2 *bp = netdev_priv(dev);

	flush_scheduled_work();

	unregister_netdev(dev);

	if (bp->regview)
		iounmap(bp->regview);

	free_netdev(dev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static int
bnx2_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2 *bp = netdev_priv(dev);

	/* PCI register 4 needs to be saved whether netif_running() or not.
	 * MSI address and data need to be saved if using MSI and
	 * netif_running().
	 */
	pci_save_state(pdev);
	if (!netif_running(dev))
		return 0;

	flush_scheduled_work();
	bnx2_netif_stop(bp);
	netif_device_detach(dev);
	del_timer_sync(&bp->timer);
	bnx2_shutdown_chip(bp);
	bnx2_free_skbs(bp);
	bnx2_set_power_state(bp, pci_choose_state(pdev, state));
	return 0;
}

static int
bnx2_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2 *bp = netdev_priv(dev);

	pci_restore_state(pdev);
	if (!netif_running(dev))
		return 0;

	bnx2_set_power_state(bp, PCI_D0);
	netif_device_attach(dev);
	bnx2_init_nic(bp, 1);
	bnx2_netif_start(bp);
	return 0;
}

/**
 * bnx2_io_error_detected - called when PCI error is detected
 * @pdev: Pointer to PCI device
 * @state: The current pci connection state
 *
 * This function is called after a PCI bus error affecting
 * this device has been detected.
 */
static pci_ers_result_t bnx2_io_error_detected(struct pci_dev *pdev,
					       pci_channel_state_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2 *bp = netdev_priv(dev);

	rtnl_lock();
	netif_device_detach(dev);

	if (netif_running(dev)) {
		bnx2_netif_stop(bp);
		del_timer_sync(&bp->timer);
		bnx2_reset_nic(bp, BNX2_DRV_MSG_CODE_RESET);
	}

	pci_disable_device(pdev);
	rtnl_unlock();

	/* Request a slot slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * bnx2_io_slot_reset - called after the pci bus has been reset.
 * @pdev: Pointer to PCI device
 *
 * Restart the card from scratch, as if from a cold-boot.
 */
static pci_ers_result_t bnx2_io_slot_reset(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2 *bp = netdev_priv(dev);

	rtnl_lock();
	if (pci_enable_device(pdev)) {
		dev_err(&pdev->dev,
			"Cannot re-enable PCI device after reset.\n");
		rtnl_unlock();
		return PCI_ERS_RESULT_DISCONNECT;
	}
	pci_set_master(pdev);
	pci_restore_state(pdev);

	if (netif_running(dev)) {
		bnx2_set_power_state(bp, PCI_D0);
		bnx2_init_nic(bp, 1);
	}

	rtnl_unlock();
	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * bnx2_io_resume - called when traffic can start flowing again.
 * @pdev: Pointer to PCI device
 *
 * This callback is called when the error recovery driver tells us that
 * its OK to resume normal operation.
 */
static void bnx2_io_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct bnx2 *bp = netdev_priv(dev);

	rtnl_lock();
	if (netif_running(dev))
		bnx2_netif_start(bp);

	netif_device_attach(dev);
	rtnl_unlock();
}

static struct pci_error_handlers bnx2_err_handler = {
	.error_detected	= bnx2_io_error_detected,
	.slot_reset	= bnx2_io_slot_reset,
	.resume		= bnx2_io_resume,
};

static struct pci_driver bnx2_pci_driver = {
	.name		= DRV_MODULE_NAME,
	.id_table	= bnx2_pci_tbl,
	.probe		= bnx2_init_one,
	.remove		= __devexit_p(bnx2_remove_one),
	.suspend	= bnx2_suspend,
	.resume		= bnx2_resume,
	.err_handler	= &bnx2_err_handler,
};

static int __init bnx2_init(void)
{
	return pci_register_driver(&bnx2_pci_driver);
}

static void __exit bnx2_cleanup(void)
{
	pci_unregister_driver(&bnx2_pci_driver);
}

module_init(bnx2_init);
module_exit(bnx2_cleanup);



