/*
	Copyright (C) 2009 Ivo van Doorn <IvDoorn@gmail.com>
	Copyright (C) 2009 Alban Browaeys <prahal@yahoo.com>
	Copyright (C) 2009 Felix Fietkau <nbd@openwrt.org>
	Copyright (C) 2009 Luis Correia <luis.f.correia@gmail.com>
	Copyright (C) 2009 Mattias Nissler <mattias.nissler@gmx.de>
	Copyright (C) 2009 Mark Asselstine <asselsm@gmail.com>
	Copyright (C) 2009 Xose Vazquez Perez <xose.vazquez@gmail.com>
	Copyright (C) 2009 Bart Zolnierkiewicz <bzolnier@gmail.com>
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2800pci
	Abstract: rt2800pci device specific routines.
	Supported chipsets: RT2800E & RT2800ED.
 */

#include <linux/crc-ccitt.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/eeprom_93cx6.h>

#include "rt2x00.h"
#include "rt2x00pci.h"
#include "rt2x00soc.h"
#include "rt2800lib.h"
#include "rt2800.h"
#include "rt2800pci.h"

/*
 * Allow hardware encryption to be disabled.
 */
static int modparam_nohwcrypt = 1;
module_param_named(nohwcrypt, modparam_nohwcrypt, bool, S_IRUGO);
MODULE_PARM_DESC(nohwcrypt, "Disable hardware encryption.");

static void rt2800pci_mcu_status(struct rt2x00_dev *rt2x00dev, const u8 token)
{
	unsigned int i;
	u32 reg;

	/*
	 * SOC devices don't support MCU requests.
	 */
	if (rt2x00_is_soc(rt2x00dev))
		return;

	for (i = 0; i < 200; i++) {
		rt2800_register_read(rt2x00dev, H2M_MAILBOX_CID, &reg);

		if ((rt2x00_get_field32(reg, H2M_MAILBOX_CID_CMD0) == token) ||
		    (rt2x00_get_field32(reg, H2M_MAILBOX_CID_CMD1) == token) ||
		    (rt2x00_get_field32(reg, H2M_MAILBOX_CID_CMD2) == token) ||
		    (rt2x00_get_field32(reg, H2M_MAILBOX_CID_CMD3) == token))
			break;

		udelay(REGISTER_BUSY_DELAY);
	}

	if (i == 200)
		ERROR(rt2x00dev, "MCU request failed, no response from hardware\n");

	rt2800_register_write(rt2x00dev, H2M_MAILBOX_STATUS, ~0);
	rt2800_register_write(rt2x00dev, H2M_MAILBOX_CID, ~0);
}

#ifdef CONFIG_RT2800PCI_SOC
static void rt2800pci_read_eeprom_soc(struct rt2x00_dev *rt2x00dev)
{
	u32 *base_addr = (u32 *) KSEG1ADDR(0x1F040000); /* XXX for RT3052 */

	memcpy_fromio(rt2x00dev->eeprom, base_addr, EEPROM_SIZE);
}
#else
static inline void rt2800pci_read_eeprom_soc(struct rt2x00_dev *rt2x00dev)
{
}
#endif /* CONFIG_RT2800PCI_SOC */

#ifdef CONFIG_RT2800PCI_PCI
static void rt2800pci_eepromregister_read(struct eeprom_93cx6 *eeprom)
{
	struct rt2x00_dev *rt2x00dev = eeprom->data;
	u32 reg;

	rt2800_register_read(rt2x00dev, E2PROM_CSR, &reg);

	eeprom->reg_data_in = !!rt2x00_get_field32(reg, E2PROM_CSR_DATA_IN);
	eeprom->reg_data_out = !!rt2x00_get_field32(reg, E2PROM_CSR_DATA_OUT);
	eeprom->reg_data_clock =
	    !!rt2x00_get_field32(reg, E2PROM_CSR_DATA_CLOCK);
	eeprom->reg_chip_select =
	    !!rt2x00_get_field32(reg, E2PROM_CSR_CHIP_SELECT);
}

static void rt2800pci_eepromregister_write(struct eeprom_93cx6 *eeprom)
{
	struct rt2x00_dev *rt2x00dev = eeprom->data;
	u32 reg = 0;

	rt2x00_set_field32(&reg, E2PROM_CSR_DATA_IN, !!eeprom->reg_data_in);
	rt2x00_set_field32(&reg, E2PROM_CSR_DATA_OUT, !!eeprom->reg_data_out);
	rt2x00_set_field32(&reg, E2PROM_CSR_DATA_CLOCK,
			   !!eeprom->reg_data_clock);
	rt2x00_set_field32(&reg, E2PROM_CSR_CHIP_SELECT,
			   !!eeprom->reg_chip_select);

	rt2800_register_write(rt2x00dev, E2PROM_CSR, reg);
}

static void rt2800pci_read_eeprom_pci(struct rt2x00_dev *rt2x00dev)
{
	struct eeprom_93cx6 eeprom;
	u32 reg;

	rt2800_register_read(rt2x00dev, E2PROM_CSR, &reg);

	eeprom.data = rt2x00dev;
	eeprom.register_read = rt2800pci_eepromregister_read;
	eeprom.register_write = rt2800pci_eepromregister_write;
	eeprom.width = !rt2x00_get_field32(reg, E2PROM_CSR_TYPE) ?
	    PCI_EEPROM_WIDTH_93C46 : PCI_EEPROM_WIDTH_93C66;
	eeprom.reg_data_in = 0;
	eeprom.reg_data_out = 0;
	eeprom.reg_data_clock = 0;
	eeprom.reg_chip_select = 0;

	eeprom_93cx6_multiread(&eeprom, EEPROM_BASE, rt2x00dev->eeprom,
			       EEPROM_SIZE / sizeof(u16));
}

static int rt2800pci_efuse_detect(struct rt2x00_dev *rt2x00dev)
{
	return rt2800_efuse_detect(rt2x00dev);
}

static inline void rt2800pci_read_eeprom_efuse(struct rt2x00_dev *rt2x00dev)
{
	rt2800_read_eeprom_efuse(rt2x00dev);
}
#else
static inline void rt2800pci_read_eeprom_pci(struct rt2x00_dev *rt2x00dev)
{
}

static inline int rt2800pci_efuse_detect(struct rt2x00_dev *rt2x00dev)
{
	return 0;
}

static inline void rt2800pci_read_eeprom_efuse(struct rt2x00_dev *rt2x00dev)
{
}
#endif /* CONFIG_RT2800PCI_PCI */

/*
 * Firmware functions
 */
static char *rt2800pci_get_firmware_name(struct rt2x00_dev *rt2x00dev)
{
	return FIRMWARE_RT2860;
}

static int rt2800pci_check_firmware(struct rt2x00_dev *rt2x00dev,
				    const u8 *data, const size_t len)
{
	u16 fw_crc;
	u16 crc;

	/*
	 * Only support 8kb firmware files.
	 */
	if (len != 8192)
		return FW_BAD_LENGTH;

	/*
	 * The last 2 bytes in the firmware array are the crc checksum itself,
	 * this means that we should never pass those 2 bytes to the crc
	 * algorithm.
	 */
	fw_crc = (data[len - 2] << 8 | data[len - 1]);

	/*
	 * Use the crc ccitt algorithm.
	 * This will return the same value as the legacy driver which
	 * used bit ordering reversion on the both the firmware bytes
	 * before input input as well as on the final output.
	 * Obviously using crc ccitt directly is much more efficient.
	 */
	crc = crc_ccitt(~0, data, len - 2);

	/*
	 * There is a small difference between the crc-itu-t + bitrev and
	 * the crc-ccitt crc calculation. In the latter method the 2 bytes
	 * will be swapped, use swab16 to convert the crc to the correct
	 * value.
	 */
	crc = swab16(crc);

	return (fw_crc == crc) ? FW_OK : FW_BAD_CRC;
}

static int rt2800pci_load_firmware(struct rt2x00_dev *rt2x00dev,
				   const u8 *data, const size_t len)
{
	unsigned int i;
	u32 reg;

	/*
	 * Wait for stable hardware.
	 */
	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2800_register_read(rt2x00dev, MAC_CSR0, &reg);
		if (reg && reg != ~0)
			break;
		msleep(1);
	}

	if (i == REGISTER_BUSY_COUNT) {
		ERROR(rt2x00dev, "Unstable hardware.\n");
		return -EBUSY;
	}

	rt2800_register_write(rt2x00dev, PWR_PIN_CFG, 0x00000002);
	rt2800_register_write(rt2x00dev, AUTOWAKEUP_CFG, 0x00000000);

	/*
	 * Disable DMA, will be reenabled later when enabling
	 * the radio.
	 */
	rt2800_register_read(rt2x00dev, WPDMA_GLO_CFG, &reg);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_TX_DMA, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_DMA_BUSY, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_RX_DMA, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_RX_DMA_BUSY, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_WRITEBACK_DONE, 1);
	rt2800_register_write(rt2x00dev, WPDMA_GLO_CFG, reg);

	/*
	 * enable Host program ram write selection
	 */
	reg = 0;
	rt2x00_set_field32(&reg, PBF_SYS_CTRL_HOST_RAM_WRITE, 1);
	rt2800_register_write(rt2x00dev, PBF_SYS_CTRL, reg);

	/*
	 * Write firmware to device.
	 */
	rt2800_register_multiwrite(rt2x00dev, FIRMWARE_IMAGE_BASE,
				      data, len);

	rt2800_register_write(rt2x00dev, PBF_SYS_CTRL, 0x00000);
	rt2800_register_write(rt2x00dev, PBF_SYS_CTRL, 0x00001);

	/*
	 * Wait for device to stabilize.
	 */
	for (i = 0; i < REGISTER_BUSY_COUNT; i++) {
		rt2800_register_read(rt2x00dev, PBF_SYS_CTRL, &reg);
		if (rt2x00_get_field32(reg, PBF_SYS_CTRL_READY))
			break;
		msleep(1);
	}

	if (i == REGISTER_BUSY_COUNT) {
		ERROR(rt2x00dev, "PBF system register not ready.\n");
		return -EBUSY;
	}

	/*
	 * Disable interrupts
	 */
	rt2x00dev->ops->lib->set_device_state(rt2x00dev, STATE_RADIO_IRQ_OFF);

	/*
	 * Initialize BBP R/W access agent
	 */
	rt2800_register_write(rt2x00dev, H2M_BBP_AGENT, 0);
	rt2800_register_write(rt2x00dev, H2M_MAILBOX_CSR, 0);

	return 0;
}

/*
 * Initialization functions.
 */
static bool rt2800pci_get_entry_state(struct queue_entry *entry)
{
	struct queue_entry_priv_pci *entry_priv = entry->priv_data;
	u32 word;

	if (entry->queue->qid == QID_RX) {
		rt2x00_desc_read(entry_priv->desc, 1, &word);

		return (!rt2x00_get_field32(word, RXD_W1_DMA_DONE));
	} else {
		rt2x00_desc_read(entry_priv->desc, 1, &word);

		return (!rt2x00_get_field32(word, TXD_W1_DMA_DONE));
	}
}

static void rt2800pci_clear_entry(struct queue_entry *entry)
{
	struct queue_entry_priv_pci *entry_priv = entry->priv_data;
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	u32 word;

	if (entry->queue->qid == QID_RX) {
		rt2x00_desc_read(entry_priv->desc, 0, &word);
		rt2x00_set_field32(&word, RXD_W0_SDP0, skbdesc->skb_dma);
		rt2x00_desc_write(entry_priv->desc, 0, word);

		rt2x00_desc_read(entry_priv->desc, 1, &word);
		rt2x00_set_field32(&word, RXD_W1_DMA_DONE, 0);
		rt2x00_desc_write(entry_priv->desc, 1, word);
	} else {
		rt2x00_desc_read(entry_priv->desc, 1, &word);
		rt2x00_set_field32(&word, TXD_W1_DMA_DONE, 1);
		rt2x00_desc_write(entry_priv->desc, 1, word);
	}
}

static int rt2800pci_init_queues(struct rt2x00_dev *rt2x00dev)
{
	struct queue_entry_priv_pci *entry_priv;
	u32 reg;

	/*
	 * Initialize registers.
	 */
	entry_priv = rt2x00dev->tx[0].entries[0].priv_data;
	rt2800_register_write(rt2x00dev, TX_BASE_PTR0, entry_priv->desc_dma);
	rt2800_register_write(rt2x00dev, TX_MAX_CNT0, rt2x00dev->tx[0].limit);
	rt2800_register_write(rt2x00dev, TX_CTX_IDX0, 0);
	rt2800_register_write(rt2x00dev, TX_DTX_IDX0, 0);

	entry_priv = rt2x00dev->tx[1].entries[0].priv_data;
	rt2800_register_write(rt2x00dev, TX_BASE_PTR1, entry_priv->desc_dma);
	rt2800_register_write(rt2x00dev, TX_MAX_CNT1, rt2x00dev->tx[1].limit);
	rt2800_register_write(rt2x00dev, TX_CTX_IDX1, 0);
	rt2800_register_write(rt2x00dev, TX_DTX_IDX1, 0);

	entry_priv = rt2x00dev->tx[2].entries[0].priv_data;
	rt2800_register_write(rt2x00dev, TX_BASE_PTR2, entry_priv->desc_dma);
	rt2800_register_write(rt2x00dev, TX_MAX_CNT2, rt2x00dev->tx[2].limit);
	rt2800_register_write(rt2x00dev, TX_CTX_IDX2, 0);
	rt2800_register_write(rt2x00dev, TX_DTX_IDX2, 0);

	entry_priv = rt2x00dev->tx[3].entries[0].priv_data;
	rt2800_register_write(rt2x00dev, TX_BASE_PTR3, entry_priv->desc_dma);
	rt2800_register_write(rt2x00dev, TX_MAX_CNT3, rt2x00dev->tx[3].limit);
	rt2800_register_write(rt2x00dev, TX_CTX_IDX3, 0);
	rt2800_register_write(rt2x00dev, TX_DTX_IDX3, 0);

	entry_priv = rt2x00dev->rx->entries[0].priv_data;
	rt2800_register_write(rt2x00dev, RX_BASE_PTR, entry_priv->desc_dma);
	rt2800_register_write(rt2x00dev, RX_MAX_CNT, rt2x00dev->rx[0].limit);
	rt2800_register_write(rt2x00dev, RX_CRX_IDX, rt2x00dev->rx[0].limit - 1);
	rt2800_register_write(rt2x00dev, RX_DRX_IDX, 0);

	/*
	 * Enable global DMA configuration
	 */
	rt2800_register_read(rt2x00dev, WPDMA_GLO_CFG, &reg);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_TX_DMA, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_RX_DMA, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_WRITEBACK_DONE, 1);
	rt2800_register_write(rt2x00dev, WPDMA_GLO_CFG, reg);

	rt2800_register_write(rt2x00dev, DELAY_INT_CFG, 0);

	return 0;
}

/*
 * Device state switch handlers.
 */
static void rt2800pci_toggle_rx(struct rt2x00_dev *rt2x00dev,
				enum dev_state state)
{
	u32 reg;

	rt2800_register_read(rt2x00dev, MAC_SYS_CTRL, &reg);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_RX,
			   (state == STATE_RADIO_RX_ON) ||
			   (state == STATE_RADIO_RX_ON_LINK));
	rt2800_register_write(rt2x00dev, MAC_SYS_CTRL, reg);
}

static void rt2800pci_toggle_irq(struct rt2x00_dev *rt2x00dev,
				 enum dev_state state)
{
	int mask = (state == STATE_RADIO_IRQ_ON);
	u32 reg;

	/*
	 * When interrupts are being enabled, the interrupt registers
	 * should clear the register to assure a clean state.
	 */
	if (state == STATE_RADIO_IRQ_ON) {
		rt2800_register_read(rt2x00dev, INT_SOURCE_CSR, &reg);
		rt2800_register_write(rt2x00dev, INT_SOURCE_CSR, reg);
	}

	rt2800_register_read(rt2x00dev, INT_MASK_CSR, &reg);
	rt2x00_set_field32(&reg, INT_MASK_CSR_RXDELAYINT, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_TXDELAYINT, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_RX_DONE, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_AC0_DMA_DONE, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_AC1_DMA_DONE, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_AC2_DMA_DONE, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_AC3_DMA_DONE, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_HCCA_DMA_DONE, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_MGMT_DMA_DONE, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_MCU_COMMAND, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_RXTX_COHERENT, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_TBTT, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_PRE_TBTT, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_TX_FIFO_STATUS, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_AUTO_WAKEUP, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_GPTIMER, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_RX_COHERENT, mask);
	rt2x00_set_field32(&reg, INT_MASK_CSR_TX_COHERENT, mask);
	rt2800_register_write(rt2x00dev, INT_MASK_CSR, reg);
}

static int rt2800pci_enable_radio(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;
	u16 word;

	/*
	 * Initialize all registers.
	 */
	if (unlikely(rt2800_wait_wpdma_ready(rt2x00dev) ||
		     rt2800pci_init_queues(rt2x00dev) ||
		     rt2800_init_registers(rt2x00dev) ||
		     rt2800_wait_wpdma_ready(rt2x00dev) ||
		     rt2800_init_bbp(rt2x00dev) ||
		     rt2800_init_rfcsr(rt2x00dev)))
		return -EIO;

	/*
	 * Send signal to firmware during boot time.
	 */
	rt2800_mcu_request(rt2x00dev, MCU_BOOT_SIGNAL, 0xff, 0, 0);

	/*
	 * Enable RX.
	 */
	rt2800_register_read(rt2x00dev, MAC_SYS_CTRL, &reg);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_TX, 1);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_RX, 0);
	rt2800_register_write(rt2x00dev, MAC_SYS_CTRL, reg);

	rt2800_register_read(rt2x00dev, WPDMA_GLO_CFG, &reg);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_TX_DMA, 1);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_RX_DMA, 1);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_WP_DMA_BURST_SIZE, 2);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_WRITEBACK_DONE, 1);
	rt2800_register_write(rt2x00dev, WPDMA_GLO_CFG, reg);

	rt2800_register_read(rt2x00dev, MAC_SYS_CTRL, &reg);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_TX, 1);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_RX, 1);
	rt2800_register_write(rt2x00dev, MAC_SYS_CTRL, reg);

	/*
	 * Initialize LED control
	 */
	rt2x00_eeprom_read(rt2x00dev, EEPROM_LED1, &word);
	rt2800_mcu_request(rt2x00dev, MCU_LED_1, 0xff,
			      word & 0xff, (word >> 8) & 0xff);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_LED2, &word);
	rt2800_mcu_request(rt2x00dev, MCU_LED_2, 0xff,
			      word & 0xff, (word >> 8) & 0xff);

	rt2x00_eeprom_read(rt2x00dev, EEPROM_LED3, &word);
	rt2800_mcu_request(rt2x00dev, MCU_LED_3, 0xff,
			      word & 0xff, (word >> 8) & 0xff);

	return 0;
}

static void rt2800pci_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	rt2800_register_read(rt2x00dev, WPDMA_GLO_CFG, &reg);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_TX_DMA, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_DMA_BUSY, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_RX_DMA, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_RX_DMA_BUSY, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_WRITEBACK_DONE, 1);
	rt2800_register_write(rt2x00dev, WPDMA_GLO_CFG, reg);

	rt2800_register_write(rt2x00dev, MAC_SYS_CTRL, 0);
	rt2800_register_write(rt2x00dev, PWR_PIN_CFG, 0);
	rt2800_register_write(rt2x00dev, TX_PIN_CFG, 0);

	rt2800_register_write(rt2x00dev, PBF_SYS_CTRL, 0x00001280);

	rt2800_register_read(rt2x00dev, WPDMA_RST_IDX, &reg);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX0, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX1, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX2, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX3, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX4, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX5, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DRX_IDX0, 1);
	rt2800_register_write(rt2x00dev, WPDMA_RST_IDX, reg);

	rt2800_register_write(rt2x00dev, PBF_SYS_CTRL, 0x00000e1f);
	rt2800_register_write(rt2x00dev, PBF_SYS_CTRL, 0x00000e00);

	/* Wait for DMA, ignore error */
	rt2800_wait_wpdma_ready(rt2x00dev);
}

static int rt2800pci_set_state(struct rt2x00_dev *rt2x00dev,
			       enum dev_state state)
{
	/*
	 * Always put the device to sleep (even when we intend to wakeup!)
	 * if the device is booting and wasn't asleep it will return
	 * failure when attempting to wakeup.
	 */
	rt2800_mcu_request(rt2x00dev, MCU_SLEEP, 0xff, 0, 2);

	if (state == STATE_AWAKE) {
		rt2800_mcu_request(rt2x00dev, MCU_WAKEUP, TOKEN_WAKUP, 0, 0);
		rt2800pci_mcu_status(rt2x00dev, TOKEN_WAKUP);
	}

	return 0;
}

static int rt2800pci_set_device_state(struct rt2x00_dev *rt2x00dev,
				      enum dev_state state)
{
	int retval = 0;

	switch (state) {
	case STATE_RADIO_ON:
		/*
		 * Before the radio can be enabled, the device first has
		 * to be woken up. After that it needs a bit of time
		 * to be fully awake and then the radio can be enabled.
		 */
		rt2800pci_set_state(rt2x00dev, STATE_AWAKE);
		msleep(1);
		retval = rt2800pci_enable_radio(rt2x00dev);
		break;
	case STATE_RADIO_OFF:
		/*
		 * After the radio has been disabled, the device should
		 * be put to sleep for powersaving.
		 */
		rt2800pci_disable_radio(rt2x00dev);
		rt2800pci_set_state(rt2x00dev, STATE_SLEEP);
		break;
	case STATE_RADIO_RX_ON:
	case STATE_RADIO_RX_ON_LINK:
	case STATE_RADIO_RX_OFF:
	case STATE_RADIO_RX_OFF_LINK:
		rt2800pci_toggle_rx(rt2x00dev, state);
		break;
	case STATE_RADIO_IRQ_ON:
	case STATE_RADIO_IRQ_OFF:
		rt2800pci_toggle_irq(rt2x00dev, state);
		break;
	case STATE_DEEP_SLEEP:
	case STATE_SLEEP:
	case STATE_STANDBY:
	case STATE_AWAKE:
		retval = rt2800pci_set_state(rt2x00dev, state);
		break;
	default:
		retval = -ENOTSUPP;
		break;
	}

	if (unlikely(retval))
		ERROR(rt2x00dev, "Device failed to enter state %d (%d).\n",
		      state, retval);

	return retval;
}

/*
 * TX descriptor initialization
 */
static int rt2800pci_write_tx_data(struct queue_entry* entry,
				   struct txentry_desc *txdesc)
{
	int ret;

	ret = rt2x00pci_write_tx_data(entry, txdesc);
	if (ret)
		return ret;

	rt2800_write_txwi(entry->skb, txdesc);

	return 0;
}


static void rt2800pci_write_tx_desc(struct rt2x00_dev *rt2x00dev,
				    struct sk_buff *skb,
				    struct txentry_desc *txdesc)
{
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(skb);
	struct queue_entry_priv_pci *entry_priv = skbdesc->entry->priv_data;
	__le32 *txd = entry_priv->desc;
	u32 word;

	/*
	 * The buffers pointed by SD_PTR0/SD_LEN0 and SD_PTR1/SD_LEN1
	 * must contains a TXWI structure + 802.11 header + padding + 802.11
	 * data. We choose to have SD_PTR0/SD_LEN0 only contains TXWI and
	 * SD_PTR1/SD_LEN1 contains 802.11 header + padding + 802.11
	 * data. It means that LAST_SEC0 is always 0.
	 */

	/*
	 * Initialize TX descriptor
	 */
	rt2x00_desc_read(txd, 0, &word);
	rt2x00_set_field32(&word, TXD_W0_SD_PTR0, skbdesc->skb_dma);
	rt2x00_desc_write(txd, 0, word);

	rt2x00_desc_read(txd, 1, &word);
	rt2x00_set_field32(&word, TXD_W1_SD_LEN1, skb->len);
	rt2x00_set_field32(&word, TXD_W1_LAST_SEC1,
			   !test_bit(ENTRY_TXD_MORE_FRAG, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W1_BURST,
			   test_bit(ENTRY_TXD_BURST, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W1_SD_LEN0, TXWI_DESC_SIZE);
	rt2x00_set_field32(&word, TXD_W1_LAST_SEC0, 0);
	rt2x00_set_field32(&word, TXD_W1_DMA_DONE, 0);
	rt2x00_desc_write(txd, 1, word);

	rt2x00_desc_read(txd, 2, &word);
	rt2x00_set_field32(&word, TXD_W2_SD_PTR1,
			   skbdesc->skb_dma + TXWI_DESC_SIZE);
	rt2x00_desc_write(txd, 2, word);

	rt2x00_desc_read(txd, 3, &word);
	rt2x00_set_field32(&word, TXD_W3_WIV,
			   !test_bit(ENTRY_TXD_ENCRYPT_IV, &txdesc->flags));
	rt2x00_set_field32(&word, TXD_W3_QSEL, 2);
	rt2x00_desc_write(txd, 3, word);

	/*
	 * Register descriptor details in skb frame descriptor.
	 */
	skbdesc->desc = txd;
	skbdesc->desc_len = TXD_DESC_SIZE;
}

/*
 * TX data initialization
 */
static void rt2800pci_write_beacon(struct queue_entry *entry,
				   struct txentry_desc *txdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	unsigned int beacon_base;
	u32 reg;

	/*
	 * Disable beaconing while we are reloading the beacon data,
	 * otherwise we might be sending out invalid data.
	 */
	rt2800_register_read(rt2x00dev, BCN_TIME_CFG, &reg);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_GEN, 0);
	rt2800_register_write(rt2x00dev, BCN_TIME_CFG, reg);

	/*
	 * Add the TXWI for the beacon to the skb.
	 */
	rt2800_write_txwi(entry->skb, txdesc);
	skb_push(entry->skb, TXWI_DESC_SIZE);

	/*
	 * Write entire beacon with TXWI to register.
	 */
	beacon_base = HW_BEACON_OFFSET(entry->entry_idx);
	rt2800_register_multiwrite(rt2x00dev, beacon_base,
				   entry->skb->data, entry->skb->len);

	/*
	 * Enable beaconing again.
	 */
	rt2x00_set_field32(&reg, BCN_TIME_CFG_TSF_TICKING, 1);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_TBTT_ENABLE, 1);
	rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_GEN, 1);
	rt2800_register_write(rt2x00dev, BCN_TIME_CFG, reg);

	/*
	 * Clean up beacon skb.
	 */
	dev_kfree_skb_any(entry->skb);
	entry->skb = NULL;
}

static void rt2800pci_kick_tx_queue(struct rt2x00_dev *rt2x00dev,
				    const enum data_queue_qid queue_idx)
{
	struct data_queue *queue;
	unsigned int idx, qidx = 0;

	if (queue_idx > QID_HCCA && queue_idx != QID_MGMT)
		return;

	queue = rt2x00queue_get_queue(rt2x00dev, queue_idx);
	idx = queue->index[Q_INDEX];

	if (queue_idx == QID_MGMT)
		qidx = 5;
	else
		qidx = queue_idx;

	rt2800_register_write(rt2x00dev, TX_CTX_IDX(qidx), idx);
}

static void rt2800pci_kill_tx_queue(struct rt2x00_dev *rt2x00dev,
				    const enum data_queue_qid qid)
{
	u32 reg;

	if (qid == QID_BEACON) {
		rt2800_register_write(rt2x00dev, BCN_TIME_CFG, 0);
		return;
	}

	rt2800_register_read(rt2x00dev, WPDMA_RST_IDX, &reg);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX0, (qid == QID_AC_BE));
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX1, (qid == QID_AC_BK));
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX2, (qid == QID_AC_VI));
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX3, (qid == QID_AC_VO));
	rt2800_register_write(rt2x00dev, WPDMA_RST_IDX, reg);
}

/*
 * RX control handlers
 */
static void rt2800pci_fill_rxdone(struct queue_entry *entry,
				  struct rxdone_entry_desc *rxdesc)
{
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	struct queue_entry_priv_pci *entry_priv = entry->priv_data;
	__le32 *rxd = entry_priv->desc;
	u32 word;

	rt2x00_desc_read(rxd, 3, &word);

	if (rt2x00_get_field32(word, RXD_W3_CRC_ERROR))
		rxdesc->flags |= RX_FLAG_FAILED_FCS_CRC;

	/*
	 * Unfortunately we don't know the cipher type used during
	 * decryption. This prevents us from correct providing
	 * correct statistics through debugfs.
	 */
	rxdesc->cipher_status = rt2x00_get_field32(word, RXD_W3_CIPHER_ERROR);

	if (rt2x00_get_field32(word, RXD_W3_DECRYPTED)) {
		/*
		 * Hardware has stripped IV/EIV data from 802.11 frame during
		 * decryption. Unfortunately the descriptor doesn't contain
		 * any fields with the EIV/IV data either, so they can't
		 * be restored by rt2x00lib.
		 */
		rxdesc->flags |= RX_FLAG_IV_STRIPPED;

		if (rxdesc->cipher_status == RX_CRYPTO_SUCCESS)
			rxdesc->flags |= RX_FLAG_DECRYPTED;
		else if (rxdesc->cipher_status == RX_CRYPTO_FAIL_MIC)
			rxdesc->flags |= RX_FLAG_MMIC_ERROR;
	}

	if (rt2x00_get_field32(word, RXD_W3_MY_BSS))
		rxdesc->dev_flags |= RXDONE_MY_BSS;

	if (rt2x00_get_field32(word, RXD_W3_L2PAD))
		rxdesc->dev_flags |= RXDONE_L2PAD;

	/*
	 * Process the RXWI structure that is at the start of the buffer.
	 */
	rt2800_process_rxwi(entry->skb, rxdesc);

	/*
	 * Set RX IDX in register to inform hardware that we have handled
	 * this entry and it is available for reuse again.
	 */
	rt2800_register_write(rt2x00dev, RX_CRX_IDX, entry->entry_idx);
}

/*
 * Interrupt functions.
 */
static void rt2800pci_txdone(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;
	struct queue_entry *entry;
	__le32 *txwi;
	struct txdone_entry_desc txdesc;
	u32 word;
	u32 reg;
	u32 old_reg;
	int wcid, ack, pid, tx_wcid, tx_ack, tx_pid;
	u16 mcs, real_mcs;

	/*
	 * During each loop we will compare the freshly read
	 * TX_STA_FIFO register value with the value read from
	 * the previous loop. If the 2 values are equal then
	 * we should stop processing because the chance it
	 * quite big that the device has been unplugged and
	 * we risk going into an endless loop.
	 */
	old_reg = 0;

	while (1) {
		rt2800_register_read(rt2x00dev, TX_STA_FIFO, &reg);
		if (!rt2x00_get_field32(reg, TX_STA_FIFO_VALID))
			break;

		if (old_reg == reg)
			break;
		old_reg = reg;

		wcid    = rt2x00_get_field32(reg, TX_STA_FIFO_WCID);
		ack     = rt2x00_get_field32(reg, TX_STA_FIFO_TX_ACK_REQUIRED);
		pid     = rt2x00_get_field32(reg, TX_STA_FIFO_PID_TYPE);

		/*
		 * Skip this entry when it contains an invalid
		 * queue identication number.
		 */
		if (pid <= 0 || pid > QID_RX)
			continue;

		queue = rt2x00queue_get_queue(rt2x00dev, pid - 1);
		if (unlikely(!queue))
			continue;

		/*
		 * Inside each queue, we process each entry in a chronological
		 * order. We first check that the queue is not empty.
		 */
		if (rt2x00queue_empty(queue))
			continue;
		entry = rt2x00queue_get_entry(queue, Q_INDEX_DONE);

		/* Check if we got a match by looking at WCID/ACK/PID
		 * fields */
		txwi = (__le32 *)(entry->skb->data -
				  rt2x00dev->ops->extra_tx_headroom);

		rt2x00_desc_read(txwi, 1, &word);
		tx_wcid = rt2x00_get_field32(word, TXWI_W1_WIRELESS_CLI_ID);
		tx_ack  = rt2x00_get_field32(word, TXWI_W1_ACK);
		tx_pid  = rt2x00_get_field32(word, TXWI_W1_PACKETID);

		if ((wcid != tx_wcid) || (ack != tx_ack) || (pid != tx_pid))
			WARNING(rt2x00dev, "invalid TX_STA_FIFO content\n");

		/*
		 * Obtain the status about this packet.
		 */
		txdesc.flags = 0;
		rt2x00_desc_read(txwi, 0, &word);
		mcs = rt2x00_get_field32(word, TXWI_W0_MCS);
		real_mcs = rt2x00_get_field32(reg, TX_STA_FIFO_MCS);

		/*
		 * Ralink has a retry mechanism using a global fallback
		 * table. We setup this fallback table to try the immediate
		 * lower rate for all rates. In the TX_STA_FIFO, the MCS field
		 * always contains the MCS used for the last transmission, be
		 * it successful or not.
		 */
		if (rt2x00_get_field32(reg, TX_STA_FIFO_TX_SUCCESS)) {
			/*
			 * Transmission succeeded. The number of retries is
			 * mcs - real_mcs
			 */
			__set_bit(TXDONE_SUCCESS, &txdesc.flags);
			txdesc.retry = ((mcs > real_mcs) ? mcs - real_mcs : 0);
		} else {
			/*
			 * Transmission failed. The number of retries is
			 * always 7 in this case (for a total number of 8
			 * frames sent).
			 */
			__set_bit(TXDONE_FAILURE, &txdesc.flags);
			txdesc.retry = 7;
		}

		__set_bit(TXDONE_FALLBACK, &txdesc.flags);


		rt2x00lib_txdone(entry, &txdesc);
	}
}

static void rt2800pci_wakeup(struct rt2x00_dev *rt2x00dev)
{
	struct ieee80211_conf conf = { .flags = 0 };
	struct rt2x00lib_conf libconf = { .conf = &conf };

	rt2800_config(rt2x00dev, &libconf, IEEE80211_CONF_CHANGE_PS);
}

static irqreturn_t rt2800pci_interrupt(int irq, void *dev_instance)
{
	struct rt2x00_dev *rt2x00dev = dev_instance;
	u32 reg;

	/* Read status and ACK all interrupts */
	rt2800_register_read(rt2x00dev, INT_SOURCE_CSR, &reg);
	rt2800_register_write(rt2x00dev, INT_SOURCE_CSR, reg);

	if (!reg)
		return IRQ_NONE;

	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return IRQ_HANDLED;

	/*
	 * 1 - Rx ring done interrupt.
	 */
	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_RX_DONE))
		rt2x00pci_rxdone(rt2x00dev);

	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_TX_FIFO_STATUS))
		rt2800pci_txdone(rt2x00dev);

	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_AUTO_WAKEUP))
		rt2800pci_wakeup(rt2x00dev);

	return IRQ_HANDLED;
}

/*
 * Device probe functions.
 */
static int rt2800pci_validate_eeprom(struct rt2x00_dev *rt2x00dev)
{
	/*
	 * Read EEPROM into buffer
	 */
	if (rt2x00_is_soc(rt2x00dev))
		rt2800pci_read_eeprom_soc(rt2x00dev);
	else if (rt2800pci_efuse_detect(rt2x00dev))
		rt2800pci_read_eeprom_efuse(rt2x00dev);
	else
		rt2800pci_read_eeprom_pci(rt2x00dev);

	return rt2800_validate_eeprom(rt2x00dev);
}

static const struct rt2800_ops rt2800pci_rt2800_ops = {
	.register_read		= rt2x00pci_register_read,
	.register_read_lock	= rt2x00pci_register_read, /* same for PCI */
	.register_write		= rt2x00pci_register_write,
	.register_write_lock	= rt2x00pci_register_write, /* same for PCI */

	.register_multiread	= rt2x00pci_register_multiread,
	.register_multiwrite	= rt2x00pci_register_multiwrite,

	.regbusy_read		= rt2x00pci_regbusy_read,
};

static int rt2800pci_probe_hw(struct rt2x00_dev *rt2x00dev)
{
	int retval;

	rt2x00dev->priv = (void *)&rt2800pci_rt2800_ops;

	/*
	 * Allocate eeprom data.
	 */
	retval = rt2800pci_validate_eeprom(rt2x00dev);
	if (retval)
		return retval;

	retval = rt2800_init_eeprom(rt2x00dev);
	if (retval)
		return retval;

	/*
	 * Initialize hw specifications.
	 */
	retval = rt2800_probe_hw_mode(rt2x00dev);
	if (retval)
		return retval;

	/*
	 * This device has multiple filters for control frames
	 * and has a separate filter for PS Poll frames.
	 */
	__set_bit(DRIVER_SUPPORT_CONTROL_FILTERS, &rt2x00dev->flags);
	__set_bit(DRIVER_SUPPORT_CONTROL_FILTER_PSPOLL, &rt2x00dev->flags);

	/*
	 * This device requires firmware.
	 */
	if (!rt2x00_is_soc(rt2x00dev))
		__set_bit(DRIVER_REQUIRE_FIRMWARE, &rt2x00dev->flags);
	__set_bit(DRIVER_REQUIRE_DMA, &rt2x00dev->flags);
	__set_bit(DRIVER_REQUIRE_L2PAD, &rt2x00dev->flags);
	if (!modparam_nohwcrypt)
		__set_bit(CONFIG_SUPPORT_HW_CRYPTO, &rt2x00dev->flags);

	/*
	 * Set the rssi offset.
	 */
	rt2x00dev->rssi_offset = DEFAULT_RSSI_OFFSET;

	return 0;
}

static const struct rt2x00lib_ops rt2800pci_rt2x00_ops = {
	.irq_handler		= rt2800pci_interrupt,
	.probe_hw		= rt2800pci_probe_hw,
	.get_firmware_name	= rt2800pci_get_firmware_name,
	.check_firmware		= rt2800pci_check_firmware,
	.load_firmware		= rt2800pci_load_firmware,
	.initialize		= rt2x00pci_initialize,
	.uninitialize		= rt2x00pci_uninitialize,
	.get_entry_state	= rt2800pci_get_entry_state,
	.clear_entry		= rt2800pci_clear_entry,
	.set_device_state	= rt2800pci_set_device_state,
	.rfkill_poll		= rt2800_rfkill_poll,
	.link_stats		= rt2800_link_stats,
	.reset_tuner		= rt2800_reset_tuner,
	.link_tuner		= rt2800_link_tuner,
	.write_tx_desc		= rt2800pci_write_tx_desc,
	.write_tx_data		= rt2800pci_write_tx_data,
	.write_beacon		= rt2800pci_write_beacon,
	.kick_tx_queue		= rt2800pci_kick_tx_queue,
	.kill_tx_queue		= rt2800pci_kill_tx_queue,
	.fill_rxdone		= rt2800pci_fill_rxdone,
	.config_shared_key	= rt2800_config_shared_key,
	.config_pairwise_key	= rt2800_config_pairwise_key,
	.config_filter		= rt2800_config_filter,
	.config_intf		= rt2800_config_intf,
	.config_erp		= rt2800_config_erp,
	.config_ant		= rt2800_config_ant,
	.config			= rt2800_config,
};

static const struct data_queue_desc rt2800pci_queue_rx = {
	.entry_num		= RX_ENTRIES,
	.data_size		= AGGREGATION_SIZE,
	.desc_size		= RXD_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_pci),
};

static const struct data_queue_desc rt2800pci_queue_tx = {
	.entry_num		= TX_ENTRIES,
	.data_size		= AGGREGATION_SIZE,
	.desc_size		= TXD_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_pci),
};

static const struct data_queue_desc rt2800pci_queue_bcn = {
	.entry_num		= 8 * BEACON_ENTRIES,
	.data_size		= 0, /* No DMA required for beacons */
	.desc_size		= TXWI_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_pci),
};

static const struct rt2x00_ops rt2800pci_ops = {
	.name			= KBUILD_MODNAME,
	.max_sta_intf		= 1,
	.max_ap_intf		= 8,
	.eeprom_size		= EEPROM_SIZE,
	.rf_size		= RF_SIZE,
	.tx_queues		= NUM_TX_QUEUES,
	.extra_tx_headroom	= TXWI_DESC_SIZE,
	.rx			= &rt2800pci_queue_rx,
	.tx			= &rt2800pci_queue_tx,
	.bcn			= &rt2800pci_queue_bcn,
	.lib			= &rt2800pci_rt2x00_ops,
	.hw			= &rt2800_mac80211_ops,
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	.debugfs		= &rt2800_rt2x00debug,
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */
};

/*
 * RT2800pci module information.
 */
#ifdef CONFIG_RT2800PCI_PCI
static DEFINE_PCI_DEVICE_TABLE(rt2800pci_device_table) = {
	{ PCI_DEVICE(0x1814, 0x0601), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1814, 0x0681), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1814, 0x0701), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1814, 0x0781), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1432, 0x7708), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1432, 0x7727), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1432, 0x7728), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1432, 0x7738), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1432, 0x7748), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1432, 0x7758), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1432, 0x7768), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1a3b, 0x1059), PCI_DEVICE_DATA(&rt2800pci_ops) },
#ifdef CONFIG_RT2800PCI_RT30XX
	{ PCI_DEVICE(0x1814, 0x3090), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1814, 0x3091), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1814, 0x3092), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1462, 0x891a), PCI_DEVICE_DATA(&rt2800pci_ops) },
#endif
#ifdef CONFIG_RT2800PCI_RT35XX
	{ PCI_DEVICE(0x1814, 0x3060), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1814, 0x3062), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1814, 0x3562), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1814, 0x3592), PCI_DEVICE_DATA(&rt2800pci_ops) },
	{ PCI_DEVICE(0x1814, 0x3593), PCI_DEVICE_DATA(&rt2800pci_ops) },
#endif
	{ 0, }
};
#endif /* CONFIG_RT2800PCI_PCI */

MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("Ralink RT2800 PCI & PCMCIA Wireless LAN driver.");
MODULE_SUPPORTED_DEVICE("Ralink RT2860 PCI & PCMCIA chipset based cards");
#ifdef CONFIG_RT2800PCI_PCI
MODULE_FIRMWARE(FIRMWARE_RT2860);
MODULE_DEVICE_TABLE(pci, rt2800pci_device_table);
#endif /* CONFIG_RT2800PCI_PCI */
MODULE_LICENSE("GPL");

#ifdef CONFIG_RT2800PCI_SOC
static int rt2800soc_probe(struct platform_device *pdev)
{
	return rt2x00soc_probe(pdev, &rt2800pci_ops);
}

static struct platform_driver rt2800soc_driver = {
	.driver		= {
		.name		= "rt2800_wmac",
		.owner		= THIS_MODULE,
		.mod_name	= KBUILD_MODNAME,
	},
	.probe		= rt2800soc_probe,
	.remove		= __devexit_p(rt2x00soc_remove),
	.suspend	= rt2x00soc_suspend,
	.resume		= rt2x00soc_resume,
};
#endif /* CONFIG_RT2800PCI_SOC */

#ifdef CONFIG_RT2800PCI_PCI
static struct pci_driver rt2800pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= rt2800pci_device_table,
	.probe		= rt2x00pci_probe,
	.remove		= __devexit_p(rt2x00pci_remove),
	.suspend	= rt2x00pci_suspend,
	.resume		= rt2x00pci_resume,
};
#endif /* CONFIG_RT2800PCI_PCI */

static int __init rt2800pci_init(void)
{
	int ret = 0;

#ifdef CONFIG_RT2800PCI_SOC
	ret = platform_driver_register(&rt2800soc_driver);
	if (ret)
		return ret;
#endif
#ifdef CONFIG_RT2800PCI_PCI
	ret = pci_register_driver(&rt2800pci_driver);
	if (ret) {
#ifdef CONFIG_RT2800PCI_SOC
		platform_driver_unregister(&rt2800soc_driver);
#endif
		return ret;
	}
#endif

	return ret;
}

static void __exit rt2800pci_exit(void)
{
#ifdef CONFIG_RT2800PCI_PCI
	pci_unregister_driver(&rt2800pci_driver);
#endif
#ifdef CONFIG_RT2800PCI_SOC
	platform_driver_unregister(&rt2800soc_driver);
#endif
}

module_init(rt2800pci_init);
module_exit(rt2800pci_exit);
