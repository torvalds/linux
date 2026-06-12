// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2023 MediaTek Inc. */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "mt7925.h"
#include "mac.h"
#include "mcu.h"
#include "regd.h"
#include "../dma.h"

static const struct pci_device_id mt7925_pci_device_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7925),
		.driver_data = (kernel_ulong_t)MT7925_FIRMWARE_WM },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x0717),
		.driver_data = (kernel_ulong_t)MT7925_FIRMWARE_WM },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x7927),
		.driver_data = (kernel_ulong_t)MT7927_FIRMWARE_WM },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x6639),
		.driver_data = (kernel_ulong_t)MT7927_FIRMWARE_WM },
	{ PCI_DEVICE(PCI_VENDOR_ID_MEDIATEK, 0x0738),
		.driver_data = (kernel_ulong_t)MT7927_FIRMWARE_WM },
	{ },
};

static bool mt7925_disable_aspm;
module_param_named(disable_aspm, mt7925_disable_aspm, bool, 0644);
MODULE_PARM_DESC(disable_aspm, "disable PCI ASPM support");

static int mt7925e_init_reset(struct mt792x_dev *dev)
{
	return mt792x_wpdma_reset(dev, true);
}

static void mt7925e_unregister_device(struct mt792x_dev *dev)
{
	int i;
	struct mt76_connac_pm *pm = &dev->pm;
	struct ieee80211_hw *hw = mt76_hw(dev);

	if (dev->phy.chip_cap & MT792x_CHIP_CAP_WF_RF_PIN_CTRL_EVT_EN)
		wiphy_rfkill_stop_polling(hw->wiphy);

	cancel_work_sync(&dev->init_work);
	mt76_unregister_device(&dev->mt76);
	mt76_for_each_q_rx(&dev->mt76, i)
		napi_disable(&dev->mt76.napi[i]);
	cancel_delayed_work_sync(&pm->ps_work);
	cancel_work_sync(&pm->wake_work);
	cancel_work_sync(&dev->reset_work);

	mt7925_tx_token_put(dev);
	__mt792x_mcu_drv_pmctrl(dev);
	mt792x_dma_cleanup(dev);
	mt792x_wfsys_reset(dev);
	skb_queue_purge(&dev->mt76.mcu.res_q);

	tasklet_disable(&dev->mt76.irq_tasklet);
}

static void mt7925_reg_remap_restore(struct mt792x_dev *dev)
{
	/* remap to ori status */
	if (unlikely(dev->backup_l1)) {
		dev->bus_ops->wr(&dev->mt76, MT_HIF_REMAP_L1, dev->backup_l1);
		dev->backup_l1 = 0;
	}

	if (dev->backup_l2) {
		dev->bus_ops->wr(&dev->mt76, MT_HIF_REMAP_L2, dev->backup_l2);
		dev->backup_l2 = 0;
	}
}

static u32 mt7925_reg_map_l1(struct mt792x_dev *dev, u32 addr)
{
	u32 offset = FIELD_GET(MT_HIF_REMAP_L1_OFFSET, addr);
	u32 base = FIELD_GET(MT_HIF_REMAP_L1_BASE, addr);

	dev->backup_l1 = dev->bus_ops->rr(&dev->mt76, MT_HIF_REMAP_L1);

	dev->bus_ops->rmw(&dev->mt76, MT_HIF_REMAP_L1,
			  MT_HIF_REMAP_L1_MASK,
			  FIELD_PREP(MT_HIF_REMAP_L1_MASK, base));

	/* use read to push write */
	dev->bus_ops->rr(&dev->mt76, MT_HIF_REMAP_L1);

	return MT_HIF_REMAP_BASE_L1 + offset;
}

static u32 mt7925_reg_map_l2(struct mt792x_dev *dev, u32 addr)
{
	u32 base = FIELD_GET(MT_HIF_REMAP_L1_BASE, MT_HIF_REMAP_BASE_L2);

	dev->backup_l2 = dev->bus_ops->rr(&dev->mt76, MT_HIF_REMAP_L1);

	dev->bus_ops->rmw(&dev->mt76, MT_HIF_REMAP_L1,
			  MT_HIF_REMAP_L1_MASK,
			  FIELD_PREP(MT_HIF_REMAP_L1_MASK, base));

	dev->bus_ops->wr(&dev->mt76, MT_HIF_REMAP_L2, addr);
	/* use read to push write */
	dev->bus_ops->rr(&dev->mt76, MT_HIF_REMAP_L1);

	return MT_HIF_REMAP_BASE_L1;
}

static u32 __mt7925_reg_addr(struct mt792x_dev *dev, u32 addr)
{
	static const struct mt76_connac_reg_map default_fixed_map[] = {
		{ 0x830c0000, 0x000000, 0x0001000 }, /* WF_MCU_BUS_CR_REMAP */
		{ 0x54000000, 0x002000, 0x0001000 }, /* WFDMA PCIE0 MCU DMA0 */
		{ 0x55000000, 0x003000, 0x0001000 }, /* WFDMA PCIE0 MCU DMA1 */
		{ 0x56000000, 0x004000, 0x0001000 }, /* WFDMA reserved */
		{ 0x57000000, 0x005000, 0x0001000 }, /* WFDMA MCU wrap CR */
		{ 0x58000000, 0x006000, 0x0001000 }, /* WFDMA PCIE1 MCU DMA0 (MEM_DMA) */
		{ 0x59000000, 0x007000, 0x0001000 }, /* WFDMA PCIE1 MCU DMA1 */
		{ 0x820c0000, 0x008000, 0x0004000 }, /* WF_UMAC_TOP (PLE) */
		{ 0x820c8000, 0x00c000, 0x0002000 }, /* WF_UMAC_TOP (PSE) */
		{ 0x820cc000, 0x00e000, 0x0002000 }, /* WF_UMAC_TOP (PP) */
		{ 0x74030000, 0x010000, 0x0001000 }, /* PCIe MAC */
		{ 0x820e0000, 0x020000, 0x0000400 }, /* WF_LMAC_TOP BN0 (WF_CFG) */
		{ 0x820e1000, 0x020400, 0x0000200 }, /* WF_LMAC_TOP BN0 (WF_TRB) */
		{ 0x820e2000, 0x020800, 0x0000400 }, /* WF_LMAC_TOP BN0 (WF_AGG) */
		{ 0x820e3000, 0x020c00, 0x0000400 }, /* WF_LMAC_TOP BN0 (WF_ARB) */
		{ 0x820e4000, 0x021000, 0x0000400 }, /* WF_LMAC_TOP BN0 (WF_TMAC) */
		{ 0x820e5000, 0x021400, 0x0000800 }, /* WF_LMAC_TOP BN0 (WF_RMAC) */
		{ 0x820ce000, 0x021c00, 0x0000200 }, /* WF_LMAC_TOP (WF_SEC) */
		{ 0x820e7000, 0x021e00, 0x0000200 }, /* WF_LMAC_TOP BN0 (WF_DMA) */
		{ 0x820cf000, 0x022000, 0x0001000 }, /* WF_LMAC_TOP (WF_PF) */
		{ 0x820e9000, 0x023400, 0x0000200 }, /* WF_LMAC_TOP BN0 (WF_WTBLOFF) */
		{ 0x820ea000, 0x024000, 0x0000200 }, /* WF_LMAC_TOP BN0 (WF_ETBF) */
		{ 0x820eb000, 0x024200, 0x0000400 }, /* WF_LMAC_TOP BN0 (WF_LPON) */
		{ 0x820ec000, 0x024600, 0x0000200 }, /* WF_LMAC_TOP BN0 (WF_INT) */
		{ 0x820ed000, 0x024800, 0x0000800 }, /* WF_LMAC_TOP BN0 (WF_MIB) */
		{ 0x820ca000, 0x026000, 0x0002000 }, /* WF_LMAC_TOP BN0 (WF_MUCOP) */
		{ 0x820d0000, 0x030000, 0x0010000 }, /* WF_LMAC_TOP (WF_WTBLON) */
		{ 0x40000000, 0x070000, 0x0010000 }, /* WF_UMAC_SYSRAM */
		{ 0x00400000, 0x080000, 0x0010000 }, /* WF_MCU_SYSRAM */
		{ 0x00410000, 0x090000, 0x0010000 }, /* WF_MCU_SYSRAM (configure register) */
		{ 0x820f0000, 0x0a0000, 0x0000400 }, /* WF_LMAC_TOP BN1 (WF_CFG) */
		{ 0x820f1000, 0x0a0600, 0x0000200 }, /* WF_LMAC_TOP BN1 (WF_TRB) */
		{ 0x820f2000, 0x0a0800, 0x0000400 }, /* WF_LMAC_TOP BN1 (WF_AGG) */
		{ 0x820f3000, 0x0a0c00, 0x0000400 }, /* WF_LMAC_TOP BN1 (WF_ARB) */
		{ 0x820f4000, 0x0a1000, 0x0000400 }, /* WF_LMAC_TOP BN1 (WF_TMAC) */
		{ 0x820f5000, 0x0a1400, 0x0000800 }, /* WF_LMAC_TOP BN1 (WF_RMAC) */
		{ 0x820f7000, 0x0a1e00, 0x0000200 }, /* WF_LMAC_TOP BN1 (WF_DMA) */
		{ 0x820f9000, 0x0a3400, 0x0000200 }, /* WF_LMAC_TOP BN1 (WF_WTBLOFF) */
		{ 0x820fa000, 0x0a4000, 0x0000200 }, /* WF_LMAC_TOP BN1 (WF_ETBF) */
		{ 0x820fb000, 0x0a4200, 0x0000400 }, /* WF_LMAC_TOP BN1 (WF_LPON) */
		{ 0x820fc000, 0x0a4600, 0x0000200 }, /* WF_LMAC_TOP BN1 (WF_INT) */
		{ 0x820fd000, 0x0a4800, 0x0000800 }, /* WF_LMAC_TOP BN1 (WF_MIB) */
		{ 0x820c4000, 0x0a8000, 0x0004000 }, /* WF_LMAC_TOP BN1 (WF_MUCOP) */
		{ 0x820b0000, 0x0ae000, 0x0001000 }, /* [APB2] WFSYS_ON */
		{ 0x80020000, 0x0b0000, 0x0010000 }, /* WF_TOP_MISC_OFF */
		{ 0x81020000, 0x0c0000, 0x0010000 }, /* WF_TOP_MISC_ON */
		{ 0x7c020000, 0x0d0000, 0x0010000 }, /* CONN_INFRA, wfdma */
		{ 0x7c060000, 0x0e0000, 0x0010000 }, /* CONN_INFRA, conn_host_csr_top */
		{ 0x7c000000, 0x0f0000, 0x0010000 }, /* CONN_INFRA */
		{ 0x70020000, 0x1f0000, 0x0010000 }, /* Reserved for CBTOP, can't switch */
		{ 0x7c500000, 0x060000, 0x200000 },  /* remap */
		{ 0x0, 0x0, 0x0 } /* End */
	};
	/* The remap table was ordered from highest to lowest frequency
	 * to improve lookup efficiency.
	 */
	static const struct mt76_connac_reg_map mt7928_fixed_map[] = {
		{0x54000000, 0x002000, 0x01000}, /* WFDMA_0 (PCIE0 MCU DMA0) */
		{0x55000000, 0x003000, 0x01000}, /* WFDMA_1 (PCIE0 MCU DMA1) */
		{0x57000000, 0x005000, 0x01000}, /* WFDMA_3 (MCU wrap CR) */
		{0x58000000, 0x006000, 0x01000}, /* WFDMA_4 (PCIE1 MCU DMA0) */
		{0x59000000, 0x007000, 0x01000}, /* WFDMA_5 (PCIE1 MCU DMA1) */
		{0x56000000, 0x004000, 0x01000}, /* WFDMA_2 (Reserved) */
		{0x820D0000, 0x030000, 0x10000}, /* WF_LMAC_TOP (WF_WTBLON) */
		{0x820C4000, 0x0A8000, 0x04000}, /* WF_LMAC_TOP (WF_UWTBL) */
		{0x820C0000, 0x008000, 0x04000}, /* WF_UMAC_TOP (PLE) */
		{0x820C8000, 0x00C000, 0x02000}, /* WF_UMAC_TOP (PSE) */
		{0x820CC000, 0x00E000, 0x02000}, /* WF_UMAC_TOP (PP) */
		{0x820F0000, 0x0A0000, 0x00400}, /* WF_LMAC_TOP (WF_CFG) */
		{0x820F1000, 0x0A0600, 0x00200}, /* WF_LMAC_TOP (WF_TRB) */
		{0x820F2000, 0x0A0800, 0x00400}, /* WF_LMAC_TOP (WF_AGG) */
		{0x820F3000, 0x0A0C00, 0x00400}, /* WF_LMAC_TOP (WF_ARB) */
		{0x820F4000, 0x0A1000, 0x00400}, /* WF_LMAC_TOP (WF_TMAC) */
		{0x820F5000, 0x0A1400, 0x00800}, /* WF_LMAC_TOP (WF_RMAC) */
		{0x820F7000, 0x0A1E00, 0x00200}, /* WF_LMAC_TOP (WF_DMA) */
		{0x820F9000, 0x0A3400, 0x00200}, /* WF_LMAC_TOP (WF_WTBLOFF) */
		{0x820FA000, 0x0A4000, 0x00200}, /* WF_LMAC_TOP (WF_ETBF) */
		{0x820FB000, 0x0A4200, 0x00400}, /* WF_LMAC_TOP (WF_LPON) */
		{0x820FC000, 0x0A4600, 0x00200}, /* WF_LMAC_TOP (WF_INT) */
		{0x820FD000, 0x0A4800, 0x00800}, /* WF_LMAC_TOP (WF_MIB) */
		{0x820E0000, 0x020000, 0x00400}, /* WF_LMAC_TOP (WF_CFG) */
		{0x820E1000, 0x020400, 0x00200}, /* WF_LMAC_TOP (WF_TRB) */
		{0x820E2000, 0x020800, 0x00400}, /* WF_LMAC_TOP (WF_AGG) */
		{0x820E3000, 0x020C00, 0x00400}, /* WF_LMAC_TOP (WF_ARB) */
		{0x820E4000, 0x021000, 0x00400}, /* WF_LMAC_TOP (WF_TMAC) */
		{0x820E5000, 0x021400, 0x00800}, /* WF_LMAC_TOP (WF_RMAC) */
		{0x820CE000, 0x021C00, 0x00200}, /* WF_LMAC_TOP (WF_SEC) */
		{0x820E7000, 0x021E00, 0x00200}, /* WF_LMAC_TOP (WF_DMA) */
		{0x820CF000, 0x022000, 0x01000}, /* WF_LMAC_TOP (WF_PF) */
		{0x820E9000, 0x023400, 0x00200}, /* WF_LMAC_TOP (WF_WTBLOFF) */
		{0x820EA000, 0x024000, 0x00200}, /* WF_LMAC_TOP (WF_ETBF) */
		{0x820EB000, 0x024200, 0x00400}, /* WF_LMAC_TOP (WF_LPON) */
		{0x820EC000, 0x024600, 0x00200}, /* WF_LMAC_TOP (WF_INT) */
		{0x820ED000, 0x024800, 0x00800}, /* WF_LMAC_TOP (WF_MIB) */
		{0x820CA000, 0x026000, 0x02000}, /* WF_LMAC_TOP (WF_MUCOP) */
		{0x7C500000, 0x060000, 0x200000}, /* remap */
		{0x7C000000, 0x0F0000, 0x10000}, /* CONN_INFRA (off2on) */
		{0x7C060000, 0x0E0000, 0x10000}, /* remap MT_CONN_ON_LPCTL and MT_CONN_ON_MISC */
		{0x20060000, 0x0E0000, 0x10000}, /* CONN_INFRA conn_host_csr_top */
		{0x7C010000, 0x100000, 0x10000}, /* CONN_INFRA (gpio clkgen cfg) */
		{0x7C050000, 0x1A0000, 0x10000}, /* CONN_INFRA SYSRAM */
		{0x7C080000, 0x190000, 0x10000}, /* CONN_INFRA (coex, pta) */
		{0x7C070000, 0x180000, 0x10000}, /* CONN_INFRA Semaphore */
		{0x7C040000, 0x170000, 0x10000}, /* CONN_INFRA (bus, afe) */
		{0x7C026000, 0x0D6000, 0x0019C}, /* remap DMASHL TOP */
		{0x20020000, 0x0D0000, 0x0C000}, /* CONN_INFRA wf_dma_host_side_cr */
		{0x200B0000, 0x050000, 0x10000}, /* CONN_INFRA conn_von_sysram */
		{0x20090000, 0x150000, 0x08000}, /* CONN_INFRA von_connsys_s0-s7 */
		{0x7C098000, 0x158000, 0x08000}, /* CONN_INFRA von_connsys_hclk_s0-s7 */
		{0x20030000, 0x160000, 0x10000}, /* CONN_INFRA CCIF */
		{0x70000000, 0x1E0000, 0x10000}, /* CONN_INFRA CONN2AP */
		{0x830C0000, 0x000000, 0x01000}, /* WF_MCU_BUS_CR_REMAP */
		{0x81020000, 0x0C0000, 0x10000}, /* WF_TOP_MISC_ON */
		{0x80020000, 0x0B0000, 0x10000}, /* WF_TOP_MISC_OFF */
		{0x81040000, 0x120000, 0x01000}, /* WF_MCU_CFG_ON */
		{0x00400000, 0x080000, 0x10000}, /* WF_MCU_SYSRAM */
		{0x00410000, 0x090000, 0x10000}, /* WF_MCU_SYSRAM (Common driver) */
		{0x88000000, 0x140000, 0x10000}, /* WF_MCU_CFG_LS */
		{0x80010000, 0x124000, 0x01000}, /* WF_AXIDMA */
		{0x81050000, 0x121000, 0x01000}, /* WF_MCU_EINT */
		{0x81060000, 0x122000, 0x01000}, /* WF_MCU_GPT */
		{0x81070000, 0x123000, 0x01000}, /* WF_MCU_WDT */
		{0x830A0000, 0x040000, 0x10000}, /* WF_PHY_MAP0 */
		{0x83090000, 0x060000, 0x10000}, /* WF_PHY_MAP2 */
		{0x83000000, 0x110000, 0x10000}, /* WF_PHY_MAP3 */
		{0x83010000, 0x130000, 0x10000}, /* WF_PHY_MAP4 */
		{0x81030000, 0x0AE000, 0x00100}, /* WFSYS_AON */
		{0x81031000, 0x0AE100, 0x00100}, /* WFSYS_AON */
		{0x81032000, 0x0AE200, 0x00100}, /* WFSYS_AON */
		{0x81033000, 0x0AE300, 0x00100}, /* WFSYS_AON */
		{0x81034000, 0x0AE400, 0x00100}, /* WFSYS_AON */
		{0xE0400000, 0x070000, 0x10000}, /* WF_UMCA_SYSRAM */
		{0x70010000, 0x1C0000, 0x10000}, /* CB Infra1 */
		{0x70020000, 0x1F0000, 0x10000}, /* Reserved for CBTOP, can't switch */
		{0x74040000, 0x1D0000, 0x10000}, /* CB PCIe (cbtop remap) */
		{0x18010000, 0x100000, 0x10000}, /* remap MT_HW_EMI_CTRL */
		{0x00000000, 0x000000, 0x00000}, /* END */
	};
	const struct mt76_connac_reg_map *fixed_map;
	size_t array_size;
	u32 i;

	if (addr < 0x200000)
		return addr;

	mt7925_reg_remap_restore(dev);

	if (is_mt7928(&dev->mt76)) {
		fixed_map = mt7928_fixed_map;
		array_size = ARRAY_SIZE(mt7928_fixed_map);
	} else {
		fixed_map = default_fixed_map;
		array_size = ARRAY_SIZE(default_fixed_map);
	}

	for (i = 0; i < array_size; i++) {
		u32 ofs;

		if (addr < fixed_map[i].phys)
			continue;

		ofs = addr - fixed_map[i].phys;
		if (ofs > fixed_map[i].size)
			continue;

		return fixed_map[i].maps + ofs;
	}

	if ((addr >= 0x18000000 && addr < 0x18c00000) ||
	    (addr >= 0x70000000 && addr < 0x78000000) ||
	    (addr >= 0x7c000000 && addr < 0x7c400000))
		return mt7925_reg_map_l1(dev, addr);

	return mt7925_reg_map_l2(dev, addr);
}

static u32 mt7925_rr(struct mt76_dev *mdev, u32 offset)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	u32 addr = __mt7925_reg_addr(dev, offset);

	return dev->bus_ops->rr(mdev, addr);
}

static void mt7925_wr(struct mt76_dev *mdev, u32 offset, u32 val)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	u32 addr = __mt7925_reg_addr(dev, offset);

	dev->bus_ops->wr(mdev, addr, val);
}

static u32 mt7925_rmw(struct mt76_dev *mdev, u32 offset, u32 mask, u32 val)
{
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	u32 addr = __mt7925_reg_addr(dev, offset);

	return dev->bus_ops->rmw(mdev, addr, mask, val);
}

static const struct mt792x_dma_layout mt7925_dma_layout = {
	.tx_data0 = mt792x_dma_ring(MT7925_TXQ_BAND0,
				    MT7925_TX_RING_SIZE,
				    MT_TX_RING_BASE),
	.tx_mcu = mt792x_dma_ring(MT7925_TXQ_MCU_WM,
				  MT7925_TX_MCU_RING_SIZE,
				  MT_TX_RING_BASE),
	.tx_fwdl = mt792x_dma_ring(MT7925_TXQ_FWDL,
				   MT7925_TX_FWDL_RING_SIZE,
				   MT_TX_RING_BASE),
	.rx_mcu = mt792x_dma_ring(MT7925_RXQ_MCU_WM,
				  MT7925_RX_MCU_RING_SIZE,
				  MT_RX_EVENT_RING_BASE),
	.rx_data = mt792x_dma_ring(MT7925_RXQ_BAND0,
				   MT7925_RX_RING_SIZE,
				   MT_RX_DATA_RING_BASE),
};

static const struct mt792x_dma_layout mt7927_dma_layout = {
	.tx_data0 = mt792x_dma_ring(MT7927_TXQ_BAND0,
				    MT7925_TX_RING_SIZE,
				    MT_TX_RING_BASE),
	.tx_mcu = mt792x_dma_ring(MT7927_TXQ_MCU_WM,
				  MT7925_TX_MCU_RING_SIZE,
				  MT_TX_RING_BASE),
	.tx_fwdl = mt792x_dma_ring(MT7927_TXQ_FWDL,
				   MT7925_TX_FWDL_RING_SIZE,
				   MT_TX_RING_BASE),
	.rx_mcu = mt792x_dma_ring(MT7927_RXQ_MCU_WM,
				  MT7925_RX_MCU_RING_SIZE,
				  MT_RX_EVENT_RING_BASE),
	.rx_data = mt792x_dma_ring(MT7927_RXQ_BAND0,
				   MT7925_RX_RING_SIZE,
				   MT_RX_DATA_RING_BASE),
};

static const struct mt792x_dma_layout mt7928_dma_layout = {
	.tx_data0 = mt792x_dma_ring(MT7925_TXQ_BAND0,
				    MT7925_TX_RING_SIZE,
				    MT_TX_RING_BASE),
	.tx_mcu = mt792x_dma_ring(MT7925_TXQ_MCU_WM,
				  MT7925_TX_MCU_RING_SIZE,
				  MT_TX_RING_BASE),
	.tx_fwdl = mt792x_dma_ring(MT7925_TXQ_FWDL,
				   MT7925_TX_FWDL_RING_SIZE,
				   MT_TX_RING_BASE),
	.tx_done = mt792x_dma_ring(MT7928_RXQ_MCU_WM2,
				   MT7928_RX_MCU_WA_RING_SIZE,
				   MT_RX_EVENT_RING_BASE),
	.rx_mcu = mt792x_dma_ring(MT7928_RXQ_MCU_WM,
				  MT7925_RX_MCU_RING_SIZE,
				  MT_RX_EVENT_RING_BASE),
	.rx_data = mt792x_dma_ring(MT7928_RXQ_BAND0,
				   MT7925_RX_RING_SIZE,
				   MT_RX_DATA_RING_BASE),
};

static int mt7927_dma_init(struct mt792x_dev *dev)
{
	int ret;

	ret = mt792x_dma_alloc_queues(dev, &mt7927_dma_layout);
	if (ret)
		return ret;

	ret = mt76_queue_alloc(dev, &dev->mt76.q_rx[MT_RXQ_MCU_WA],
			       MT7927_RXQ_DATA2,
			       MT7925_RX_MCU_RING_SIZE,
			       MT_RX_BUF_SIZE,
			       MT_RX_DATA_RING_BASE);
	if (ret)
		return ret;

	ret = mt76_init_queues(dev, mt792x_poll_rx);
	if (ret < 0)
		return ret;

	netif_napi_add_tx(dev->mt76.tx_napi_dev, &dev->mt76.tx_napi,
			  mt792x_poll_tx);
	napi_enable(&dev->mt76.tx_napi);

	return mt792x_dma_enable(dev);
}

static void mt7928_dma_shdl_lite_init(struct mt792x_dev *dev)
{
	u32 addr, idx, grp1_5_quota, grp15_quota;
	u32 q2group[8] = {
		0x04000000, /* AC00->G0,..., AC03->G4 */
		0x04010101, /* AC10->G1,..., AC13->G4 */
		0x04020202, /* AC20->G2,..., AC23->G4 */
		0x04030303, /* AC30->G3,..., AC33->G4 */
		0x00000005, /* ALTX->G5,BMC->G0,BCN->G0 */
		0x00000005, /* TGID=1 ALTX->G5 */
		0x00000000, /* NAF/NBCN/FIXFID -> G0 */
		0x00000005, /* TGID=2 ALTX->G5 */
	};

	/* RST */
	mt76_wr(dev, MT_DMASHDL_LITE_MAIN_CONTROL, MT_DMASHDL_LITE_MAIN_CONTROL_SW_RST);
	/* pse page size 0x10, ple page size 0x7e0 */
	mt76_wr(dev, MT_DMASHDL_LITE_PAGE_SIZE,
		FIELD_PREP(MT_DMASHDL_LITE_PSE_PAGE_SIZE_MASK, 0x10) |
		FIELD_PREP(MT_DMASHDL_LITE_PLE_PAGE_SIZE_MASK, 0x7e0));
	/* pse max page 8, ple max page 1 */
	mt76_wr(dev, MT_DMASHDL_LITE_PKT_MAX_SIZE,
		FIELD_PREP(MT_DMASHDL_LITE_PSE_PKT_MAX_SIZE_MASK, 8) |
		FIELD_PREP(MT_DMASHDL_LITE_PLE_PKT_MAX_SIZE_MASK, 1));
	/* SN/UDF check */
	mt76_wr(dev, MT_DMASHDL_LITE_GROUP_SN_CHK0, 0xffffffff);
	mt76_wr(dev, MT_DMASHDL_LITE_GROUP_SN_CHK1, 0xffffffff);
	mt76_wr(dev, MT_DMASHDL_LITE_GROUP_UDF_CHK0, 0xffffffff);
	mt76_wr(dev, MT_DMASHDL_LITE_GROUP_UDF_CHK1, 0xffffffff);
	/* q mapping */
	for (addr = MT_DMASHDL_LITE_Q_MAPPING0, idx = 0;
	     idx < ARRAY_SIZE(q2group);
	     idx++, addr += 4)
		mt76_wr(dev, addr, q2group[idx]);
	/* refill, set 0 to enable group 0,1,2,3,4,5 & 15 */
	mt76_wr(dev, MT_DMASHDL_LITE_GROUP_DISABLE0, 0xffff7fc0);
	mt76_wr(dev, MT_DMASHDL_LITE_GROUP_DISABLE1, 0xffffffff);
	/* max/min quota */
	grp1_5_quota = FIELD_PREP(MT_DMASHDL_LITE_GROUP_MAX_QUOTA_MASK, 0x3f0) |
		       FIELD_PREP(MT_DMASHDL_LITE_GROUP_MIN_QUOTA_MASK, 0x10);
	grp15_quota = FIELD_PREP(MT_DMASHDL_LITE_GROUP_MAX_QUOTA_MASK, 0x30);

	for (addr = MT_DMASHDL_LITE_GROUP0_QUOTA, idx = 0;
	     idx < DMASHDL_LITE_GROUP_NUM;
	     idx++, addr += 4)
		mt76_wr(dev, addr, (idx <= 5) ? grp1_5_quota :
			((idx == 15) ? grp15_quota : 0));
}

static int mt7925_dma_init(struct mt792x_dev *dev)
{
	int ret;
	const struct mt792x_dma_layout *layout =
		is_mt7928(&dev->mt76) ? &mt7928_dma_layout : &mt7925_dma_layout;

	ret = mt792x_dma_alloc_queues(dev, layout);
	if (ret)
		return ret;

	ret = mt76_init_queues(dev, mt792x_poll_rx);
	if (ret < 0)
		return ret;

	if (is_mt7928(&dev->mt76))
		mt7928_dma_shdl_lite_init(dev);

	netif_napi_add_tx(dev->mt76.tx_napi_dev, &dev->mt76.tx_napi,
			  mt792x_poll_tx);
	napi_enable(&dev->mt76.tx_napi);

	return mt792x_dma_enable(dev);
}

static const struct mt792x_pcie_reg mt7925_pcie_reg = {
	.imask = MT7925_PCIE_MAC_INT_ENABLE,
	.pm = MT7925_PCIE_MAC_PM,
};

static const struct mt792x_pcie_reg mt7928_pcie_reg = {
	.imask = MT7928_PCIE_MAC_INT_ENABLE,
	.pm = MT7928_PCIE_MAC_PM,
};

static const struct mt792x_irq_map mt7925_irq_map = {
	.host_irq_enable = MT_WFDMA0_HOST_INT_ENA,
	.tx = {
		.all_complete_mask = MT_INT_TX_DONE_ALL,
		.mcu_complete_mask = MT_INT_TX_DONE_MCU,
	},
	.rx = {
		.all_complete_mask = MT7925_INT_RX_DONE_ALL,
		.data_complete_mask = MT7925_INT_RX_DONE_DATA,
		.wm_complete_mask = MT7925_INT_RX_DONE_WM,
		.wm2_complete_mask = MT_INT_RX_DONE_WM2,
	},
};

static const struct mt792x_irq_map mt7927_irq_map = {
	.host_irq_enable = MT_WFDMA0_HOST_INT_ENA,
	.tx = {
		.all_complete_mask = MT_INT_TX_DONE_ALL,
		.mcu_complete_mask = MT_INT_TX_DONE_MCU,
	},
	.rx = {
		.all_complete_mask = MT7927_INT_RX_DONE_ALL,
		.data_complete_mask = MT7927_INT_RX_DONE_DATA,
		.wm_complete_mask = MT7927_INT_RX_DONE_WM,
		.wm2_complete_mask = MT7927_INT_RX_DONE_WM2,
	},
};

static const struct mt792x_irq_map mt7928_irq_map = {
	.host_irq_enable = MT_WFDMA0_HOST_INT_ENA,
	.tx = {
		.all_complete_mask = MT_INT_TX_DONE_ALL,
		.mcu_complete_mask = MT_INT_TX_DONE_MCU,
	},
	.rx = {
		.all_complete_mask = MT7928_INT_RX_DONE_ALL,
		.data_complete_mask = MT7928_INT_RX_DONE_DATA,
		.wm_complete_mask = MT7928_INT_RX_DONE_WM,
		.wm2_complete_mask = MT_INT_RX_DONE_WM2,
	},
};

static int mt7925_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	static const struct mt76_driver_ops drv_ops = {
		/* txwi_size = txd size + txp size */
		.txwi_size = MT_TXD_SIZE + sizeof(struct mt76_connac_hw_txp),
		.drv_flags = MT_DRV_TXWI_NO_FREE | MT_DRV_HW_MGMT_TXQ |
			     MT_DRV_AMSDU_OFFLOAD,
		.survey_flags = SURVEY_INFO_TIME_TX |
				SURVEY_INFO_TIME_RX |
				SURVEY_INFO_TIME_BSS_RX,
		.token_size = MT7925_TOKEN_SIZE,
		.tx_prepare_skb = mt7925e_tx_prepare_skb,
		.tx_complete_skb = mt76_connac_tx_complete_skb,
		.rx_check = mt7925_rx_check,
		.rx_skb = mt7925_queue_rx_skb,
		.rx_poll_complete = mt792x_rx_poll_complete,
		.sta_add = mt7925_mac_sta_add,
		.sta_event = mt7925_mac_sta_event,
		.sta_remove = mt7925_mac_sta_remove,
		.update_survey = mt792x_update_channel,
	};
	static const struct mt792x_hif_ops mt7925_pcie_ops = {
		.init_reset = mt7925e_init_reset,
		.reset = mt7925e_mac_reset,
		.mcu_init = mt7925e_mcu_init,
		.drv_own = mt792xe_mcu_drv_pmctrl,
		.fw_own = mt792xe_mcu_fw_pmctrl,
	};
	bool is_mt7927_hw, is_mt7928_hw;
	struct mt76_bus_ops *bus_ops;
	struct ieee80211_ops *ops;
	struct mt792x_dev *dev;
	struct mt76_dev *mdev;
	u8 features;
	int ret;
	u16 cmd;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pcim_iomap_regions(pdev, BIT(0), pci_name(pdev));
	if (ret)
		return ret;

	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	if (!(cmd & PCI_COMMAND_MEMORY)) {
		cmd |= PCI_COMMAND_MEMORY;
		pci_write_config_word(pdev, PCI_COMMAND, cmd);
	}
	pci_set_master(pdev);

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		return ret;

	is_mt7927_hw = (pdev->device == 0x6639 || pdev->device == 0x7927 ||
			pdev->device == 0x0738);
	is_mt7928_hw = (pdev->device == 0x7928 || pdev->device == 0x7935);

	if (is_mt7928_hw)
		ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(34));
	else
		ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));

	if (ret)
		goto err_free_pci_vec;

	/* MT7927: ASPM L1 causes unreliable WFDMA register access */
	if (mt7925_disable_aspm || is_mt7927_hw)
		mt76_pci_disable_aspm(pdev);

	ops = mt792x_get_mac80211_ops(&pdev->dev, &mt7925_ops,
				      (void *)id->driver_data, &features);
	if (!ops) {
		ret = -ENOMEM;
		goto err_free_pci_vec;
	}

	mdev = mt76_alloc_device(&pdev->dev, sizeof(*dev), ops, &drv_ops);
	if (!mdev) {
		ret = -ENOMEM;
		goto err_free_pci_vec;
	}

	pci_set_drvdata(pdev, mdev);

	dev = container_of(mdev, struct mt792x_dev, mt76);
	dev->fw_features = features;
	dev->hif_ops = &mt7925_pcie_ops;
	dev->pcie_reg = &mt7925_pcie_reg;

	if (is_mt7928_hw) {
		dev->pcie_reg = &mt7928_pcie_reg;
		dev->irq_map = &mt7928_irq_map;
		mdev->rev = 0x7928 << 16;
	} else if (is_mt7927_hw) {
		dev->irq_map = &mt7927_irq_map;
	} else {
		dev->irq_map = &mt7925_irq_map;
	}

	mt76_mmio_init(&dev->mt76, pcim_iomap_table(pdev)[0]);
	tasklet_init(&mdev->irq_tasklet, mt792x_irq_tasklet, (unsigned long)dev);

	dev->phy.dev = dev;
	dev->phy.mt76 = &dev->mt76.phy;
	dev->mt76.phy.priv = &dev->phy;
	dev->bus_ops = dev->mt76.bus;
	bus_ops = devm_kmemdup(dev->mt76.dev, dev->bus_ops, sizeof(*bus_ops),
			       GFP_KERNEL);
	if (!bus_ops) {
		ret = -ENOMEM;
		goto err_free_dev;
	}

	bus_ops->rr = mt7925_rr;
	bus_ops->wr = mt7925_wr;
	bus_ops->rmw = mt7925_rmw;
	dev->mt76.bus = bus_ops;

	if (!mt7925_disable_aspm && mt76_pci_aspm_supported(pdev))
		dev->aspm_supported = true;

	ret = __mt792x_mcu_fw_pmctrl(dev);
	if (ret)
		goto err_free_dev;

	ret = __mt792xe_mcu_drv_pmctrl(dev);
	if (ret)
		goto err_free_dev;

	mdev->rev = (mt76_rr(dev, MT_HW_CHIPID) << 16) |
		    (mt76_rr(dev, MT_HW_REV) & 0xff);

	dev_info(mdev->dev, "ASIC revision: %04x\n", mdev->rev);

	if (is_mt7927_hw && mt76_chip(mdev) != 0x7927) {
		dev_info(mdev->dev,
			 "MT7927 raw CHIPID=0x%04x, forcing chip=0x7927\n",
			 mt76_chip(mdev));
		mdev->rev = (0x7927 << 16) | (mdev->rev & 0xff);
	}

	mt76_rmw_field(dev, MT_HW_EMI_CTL, MT_HW_EMI_CTL_SLPPROT_EN, 1);

	ret = mt792x_wfsys_reset(dev);
	if (ret)
		goto err_free_dev;

	mt76_wr(dev, dev->irq_map->host_irq_enable, 0);
	mt76_wr(dev, dev->pcie_reg->imask, 0xff);

	ret = devm_request_irq(mdev->dev, pdev->irq, mt792x_irq_handler,
			       IRQF_SHARED, KBUILD_MODNAME, dev);
	if (ret)
		goto err_free_dev;

	if (is_mt7927(&dev->mt76))
		ret = mt7927_dma_init(dev);
	else if (is_mt7925(&dev->mt76) || is_mt7928(&dev->mt76))
		ret = mt7925_dma_init(dev);
	else
		ret = -EINVAL;

	if (ret)
		goto err_free_irq;

	ret = mt7925_register_device(dev);
	if (ret)
		goto err_free_dma;

	return 0;

err_free_dma:
	mt792x_dma_cleanup(dev);
err_free_irq:
	devm_free_irq(&pdev->dev, pdev->irq, dev);
err_free_dev:
	mt76_free_device(&dev->mt76);
err_free_pci_vec:
	pci_free_irq_vectors(pdev);

	return ret;
}

static void mt7925_pci_remove(struct pci_dev *pdev)
{
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);

	mt7925e_unregister_device(dev);
	set_bit(MT76_REMOVED, &mdev->phy.state);
	devm_free_irq(&pdev->dev, pdev->irq, dev);
	mt76_free_device(&dev->mt76);
	pci_free_irq_vectors(pdev);
}

static int mt7925_pci_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	struct mt76_connac_pm *pm = &dev->pm;
	int i, err, ret;

	pm->suspended = true;
	dev->hif_resumed = false;
	flush_work(&dev->reset_work);
	cancel_delayed_work_sync(&pm->ps_work);
	cancel_work_sync(&pm->wake_work);

	mt7925_roc_abort_sync(dev);

	err = mt792x_mcu_drv_pmctrl(dev);
	if (err < 0)
		goto restore_suspend;

	wait_event_timeout(dev->wait,
			   !dev->regd_in_progress, 5 * HZ);

	/* always enable deep sleep during suspend to reduce
	 * power consumption
	 */
	mt7925_mcu_set_deep_sleep(dev, true);

	mt76_connac_mcu_set_hif_suspend(mdev, true, false);
	ret = wait_event_timeout(dev->wait,
				 dev->hif_idle, 3 * HZ);
	if (!ret) {
		err = -ETIMEDOUT;
		goto restore_suspend;
	}

	napi_disable(&mdev->tx_napi);
	mt76_worker_disable(&mdev->tx_worker);

	mt76_for_each_q_rx(mdev, i) {
		napi_disable(&mdev->napi[i]);
	}

	/* wait until dma is idle  */
	mt76_poll(dev, MT_WFDMA0_GLO_CFG,
		  MT_WFDMA0_GLO_CFG_TX_DMA_BUSY |
		  MT_WFDMA0_GLO_CFG_RX_DMA_BUSY, 0, 1000);

	/* put dma disabled */
	mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	/* disable interrupt */
	mt76_wr(dev, dev->irq_map->host_irq_enable, 0);
	mt76_wr(dev, dev->pcie_reg->imask, 0x0);

	synchronize_irq(pdev->irq);
	tasklet_kill(&mdev->irq_tasklet);

	err = mt792x_mcu_fw_pmctrl(dev);
	if (err)
		goto restore_napi;

	return 0;

restore_napi:
	mt76_for_each_q_rx(mdev, i) {
		napi_enable(&mdev->napi[i]);
	}
	napi_enable(&mdev->tx_napi);

	if (!pm->ds_enable)
		mt7925_mcu_set_deep_sleep(dev, false);

	mt76_connac_mcu_set_hif_suspend(mdev, false, false);
	ret = wait_event_timeout(dev->wait,
				 dev->hif_resumed, 3 * HZ);
	if (!ret)
		err = -ETIMEDOUT;
restore_suspend:
	pm->suspended = false;

	if (err < 0)
		mt792x_reset(&dev->mt76);

	return err;
}

static int _mt7925_pci_resume(struct device *device, bool restore)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct mt76_dev *mdev = pci_get_drvdata(pdev);
	struct mt792x_dev *dev = container_of(mdev, struct mt792x_dev, mt76);
	struct mt76_connac_pm *pm = &dev->pm;
	int i, err, ret;

	dev->hif_idle = false;
	err = mt792x_mcu_drv_pmctrl(dev);
	if (err < 0)
		goto failed;

	mt792x_wpdma_reinit_cond(dev);

	/* enable interrupt */
	mt76_wr(dev, dev->pcie_reg->imask, 0xff);
	mt76_connac_irq_enable(&dev->mt76,
			       dev->irq_map->tx.all_complete_mask |
			       dev->irq_map->rx.all_complete_mask |
			       MT_INT_MCU_CMD);
	mt76_set(dev, MT_MCU2HOST_SW_INT_ENA, MT_MCU_CMD_WAKE_RX_PCIE);

	/* put dma enabled */
	mt76_set(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	mt76_worker_enable(&mdev->tx_worker);

	mt76_for_each_q_rx(mdev, i) {
		napi_enable(&mdev->napi[i]);
	}
	napi_enable(&mdev->tx_napi);

	local_bh_disable();
	mt76_for_each_q_rx(mdev, i) {
		napi_schedule(&mdev->napi[i]);
	}
	napi_schedule(&mdev->tx_napi);
	local_bh_enable();

	if (restore)
		goto failed;

	mt76_connac_mcu_set_hif_suspend(mdev, false, false);
	ret = wait_event_timeout(dev->wait,
				 dev->hif_resumed, 3 * HZ);
	if (!ret) {
		err = -ETIMEDOUT;
		goto failed;
	}

	/* restore previous ds setting */
	if (!pm->ds_enable)
		mt7925_mcu_set_deep_sleep(dev, false);

	mt7925_mcu_regd_update(dev, mdev->alpha2, dev->country_ie_env);
failed:
	pm->suspended = false;

	if (err < 0 || restore)
		mt792x_reset(&dev->mt76);

	return err;
}

static void mt7925_pci_shutdown(struct pci_dev *pdev)
{
	mt7925_pci_remove(pdev);
}

static int mt7925_pci_resume(struct device *device)
{
	return _mt7925_pci_resume(device, false);
}

static int mt7925_pci_restore(struct device *device)
{
	return _mt7925_pci_resume(device, true);
}

static const struct dev_pm_ops mt7925_pm_ops = {
	.suspend = pm_sleep_ptr(mt7925_pci_suspend),
	.resume  = pm_sleep_ptr(mt7925_pci_resume),
	.freeze = pm_sleep_ptr(mt7925_pci_suspend),
	.thaw = pm_sleep_ptr(mt7925_pci_resume),
	.poweroff = pm_sleep_ptr(mt7925_pci_suspend),
	.restore = pm_sleep_ptr(mt7925_pci_restore),
};

static struct pci_driver mt7925_pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= mt7925_pci_device_table,
	.probe		= mt7925_pci_probe,
	.remove		= mt7925_pci_remove,
	.shutdown       = mt7925_pci_shutdown,
	.driver.pm	= pm_sleep_ptr(&mt7925_pm_ops),
};

module_pci_driver(mt7925_pci_driver);

MODULE_DEVICE_TABLE(pci, mt7925_pci_device_table);
MODULE_FIRMWARE(MT7925_FIRMWARE_WM);
MODULE_FIRMWARE(MT7925_ROM_PATCH);
MODULE_FIRMWARE(MT7927_FIRMWARE_WM);
MODULE_FIRMWARE(MT7927_ROM_PATCH);
MODULE_AUTHOR("Deren Wu <deren.wu@mediatek.com>");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_DESCRIPTION("MediaTek MT7925E (PCIe) wireless driver");
MODULE_LICENSE("Dual BSD/GPL");
