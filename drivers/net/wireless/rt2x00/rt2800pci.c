/*
	Copyright (C) 2009 - 2010 Ivo van Doorn <IvDoorn@gmail.com>
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
static int modparam_nohwcrypt = 0;
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
		rt2x00pci_register_read(rt2x00dev, H2M_MAILBOX_CID, &reg);

		if ((rt2x00_get_field32(reg, H2M_MAILBOX_CID_CMD0) == token) ||
		    (rt2x00_get_field32(reg, H2M_MAILBOX_CID_CMD1) == token) ||
		    (rt2x00_get_field32(reg, H2M_MAILBOX_CID_CMD2) == token) ||
		    (rt2x00_get_field32(reg, H2M_MAILBOX_CID_CMD3) == token))
			break;

		udelay(REGISTER_BUSY_DELAY);
	}

	if (i == 200)
		ERROR(rt2x00dev, "MCU request failed, no response from hardware\n");

	rt2x00pci_register_write(rt2x00dev, H2M_MAILBOX_STATUS, ~0);
	rt2x00pci_register_write(rt2x00dev, H2M_MAILBOX_CID, ~0);
}

#if defined(CONFIG_RALINK_RT288X) || defined(CONFIG_RALINK_RT305X)
static void rt2800pci_read_eeprom_soc(struct rt2x00_dev *rt2x00dev)
{
	void __iomem *base_addr = ioremap(0x1F040000, EEPROM_SIZE);

	memcpy_fromio(rt2x00dev->eeprom, base_addr, EEPROM_SIZE);

	iounmap(base_addr);
}
#else
static inline void rt2800pci_read_eeprom_soc(struct rt2x00_dev *rt2x00dev)
{
}
#endif /* CONFIG_RALINK_RT288X || CONFIG_RALINK_RT305X */

#ifdef CONFIG_PCI
static void rt2800pci_eepromregister_read(struct eeprom_93cx6 *eeprom)
{
	struct rt2x00_dev *rt2x00dev = eeprom->data;
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, E2PROM_CSR, &reg);

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

	rt2x00pci_register_write(rt2x00dev, E2PROM_CSR, reg);
}

static void rt2800pci_read_eeprom_pci(struct rt2x00_dev *rt2x00dev)
{
	struct eeprom_93cx6 eeprom;
	u32 reg;

	rt2x00pci_register_read(rt2x00dev, E2PROM_CSR, &reg);

	eeprom.data = rt2x00dev;
	eeprom.register_read = rt2800pci_eepromregister_read;
	eeprom.register_write = rt2800pci_eepromregister_write;
	switch (rt2x00_get_field32(reg, E2PROM_CSR_TYPE))
	{
	case 0:
		eeprom.width = PCI_EEPROM_WIDTH_93C46;
		break;
	case 1:
		eeprom.width = PCI_EEPROM_WIDTH_93C66;
		break;
	default:
		eeprom.width = PCI_EEPROM_WIDTH_93C86;
		break;
	}
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
#endif /* CONFIG_PCI */

/*
 * Queue handlers.
 */
static void rt2800pci_start_queue(struct data_queue *queue)
{
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;
	u32 reg;

	switch (queue->qid) {
	case QID_RX:
		rt2x00pci_register_read(rt2x00dev, MAC_SYS_CTRL, &reg);
		rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_RX, 1);
		rt2x00pci_register_write(rt2x00dev, MAC_SYS_CTRL, reg);
		break;
	case QID_BEACON:
		/*
		 * Allow beacon tasklets to be scheduled for periodic
		 * beacon updates.
		 */
		tasklet_enable(&rt2x00dev->tbtt_tasklet);
		tasklet_enable(&rt2x00dev->pretbtt_tasklet);

		rt2x00pci_register_read(rt2x00dev, BCN_TIME_CFG, &reg);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_TSF_TICKING, 1);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_TBTT_ENABLE, 1);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_GEN, 1);
		rt2x00pci_register_write(rt2x00dev, BCN_TIME_CFG, reg);

		rt2x00pci_register_read(rt2x00dev, INT_TIMER_EN, &reg);
		rt2x00_set_field32(&reg, INT_TIMER_EN_PRE_TBTT_TIMER, 1);
		rt2x00pci_register_write(rt2x00dev, INT_TIMER_EN, reg);
		break;
	default:
		break;
	};
}

static void rt2800pci_kick_queue(struct data_queue *queue)
{
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;
	struct queue_entry *entry;

	switch (queue->qid) {
	case QID_AC_VO:
	case QID_AC_VI:
	case QID_AC_BE:
	case QID_AC_BK:
		entry = rt2x00queue_get_entry(queue, Q_INDEX);
		rt2x00pci_register_write(rt2x00dev, TX_CTX_IDX(queue->qid),
					 entry->entry_idx);
		break;
	case QID_MGMT:
		entry = rt2x00queue_get_entry(queue, Q_INDEX);
		rt2x00pci_register_write(rt2x00dev, TX_CTX_IDX(5),
					 entry->entry_idx);
		break;
	default:
		break;
	}
}

static void rt2800pci_stop_queue(struct data_queue *queue)
{
	struct rt2x00_dev *rt2x00dev = queue->rt2x00dev;
	u32 reg;

	switch (queue->qid) {
	case QID_RX:
		rt2x00pci_register_read(rt2x00dev, MAC_SYS_CTRL, &reg);
		rt2x00_set_field32(&reg, MAC_SYS_CTRL_ENABLE_RX, 0);
		rt2x00pci_register_write(rt2x00dev, MAC_SYS_CTRL, reg);
		break;
	case QID_BEACON:
		rt2x00pci_register_read(rt2x00dev, BCN_TIME_CFG, &reg);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_TSF_TICKING, 0);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_TBTT_ENABLE, 0);
		rt2x00_set_field32(&reg, BCN_TIME_CFG_BEACON_GEN, 0);
		rt2x00pci_register_write(rt2x00dev, BCN_TIME_CFG, reg);

		rt2x00pci_register_read(rt2x00dev, INT_TIMER_EN, &reg);
		rt2x00_set_field32(&reg, INT_TIMER_EN_PRE_TBTT_TIMER, 0);
		rt2x00pci_register_write(rt2x00dev, INT_TIMER_EN, reg);

		/*
		 * Wait for tbtt tasklets to finish.
		 */
		tasklet_disable(&rt2x00dev->tbtt_tasklet);
		tasklet_disable(&rt2x00dev->pretbtt_tasklet);
		break;
	default:
		break;
	}
}

/*
 * Firmware functions
 */
static char *rt2800pci_get_firmware_name(struct rt2x00_dev *rt2x00dev)
{
	return FIRMWARE_RT2860;
}

static int rt2800pci_write_firmware(struct rt2x00_dev *rt2x00dev,
				    const u8 *data, const size_t len)
{
	u32 reg;

	/*
	 * enable Host program ram write selection
	 */
	reg = 0;
	rt2x00_set_field32(&reg, PBF_SYS_CTRL_HOST_RAM_WRITE, 1);
	rt2x00pci_register_write(rt2x00dev, PBF_SYS_CTRL, reg);

	/*
	 * Write firmware to device.
	 */
	rt2x00pci_register_multiwrite(rt2x00dev, FIRMWARE_IMAGE_BASE,
				      data, len);

	rt2x00pci_register_write(rt2x00dev, PBF_SYS_CTRL, 0x00000);
	rt2x00pci_register_write(rt2x00dev, PBF_SYS_CTRL, 0x00001);

	rt2x00pci_register_write(rt2x00dev, H2M_BBP_AGENT, 0);
	rt2x00pci_register_write(rt2x00dev, H2M_MAILBOX_CSR, 0);

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
	struct rt2x00_dev *rt2x00dev = entry->queue->rt2x00dev;
	u32 word;

	if (entry->queue->qid == QID_RX) {
		rt2x00_desc_read(entry_priv->desc, 0, &word);
		rt2x00_set_field32(&word, RXD_W0_SDP0, skbdesc->skb_dma);
		rt2x00_desc_write(entry_priv->desc, 0, word);

		rt2x00_desc_read(entry_priv->desc, 1, &word);
		rt2x00_set_field32(&word, RXD_W1_DMA_DONE, 0);
		rt2x00_desc_write(entry_priv->desc, 1, word);

		/*
		 * Set RX IDX in register to inform hardware that we have
		 * handled this entry and it is available for reuse again.
		 */
		rt2x00pci_register_write(rt2x00dev, RX_CRX_IDX,
				      entry->entry_idx);
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
	rt2x00pci_register_write(rt2x00dev, TX_BASE_PTR0, entry_priv->desc_dma);
	rt2x00pci_register_write(rt2x00dev, TX_MAX_CNT0,
				 rt2x00dev->tx[0].limit);
	rt2x00pci_register_write(rt2x00dev, TX_CTX_IDX0, 0);
	rt2x00pci_register_write(rt2x00dev, TX_DTX_IDX0, 0);

	entry_priv = rt2x00dev->tx[1].entries[0].priv_data;
	rt2x00pci_register_write(rt2x00dev, TX_BASE_PTR1, entry_priv->desc_dma);
	rt2x00pci_register_write(rt2x00dev, TX_MAX_CNT1,
				 rt2x00dev->tx[1].limit);
	rt2x00pci_register_write(rt2x00dev, TX_CTX_IDX1, 0);
	rt2x00pci_register_write(rt2x00dev, TX_DTX_IDX1, 0);

	entry_priv = rt2x00dev->tx[2].entries[0].priv_data;
	rt2x00pci_register_write(rt2x00dev, TX_BASE_PTR2, entry_priv->desc_dma);
	rt2x00pci_register_write(rt2x00dev, TX_MAX_CNT2,
				 rt2x00dev->tx[2].limit);
	rt2x00pci_register_write(rt2x00dev, TX_CTX_IDX2, 0);
	rt2x00pci_register_write(rt2x00dev, TX_DTX_IDX2, 0);

	entry_priv = rt2x00dev->tx[3].entries[0].priv_data;
	rt2x00pci_register_write(rt2x00dev, TX_BASE_PTR3, entry_priv->desc_dma);
	rt2x00pci_register_write(rt2x00dev, TX_MAX_CNT3,
				 rt2x00dev->tx[3].limit);
	rt2x00pci_register_write(rt2x00dev, TX_CTX_IDX3, 0);
	rt2x00pci_register_write(rt2x00dev, TX_DTX_IDX3, 0);

	entry_priv = rt2x00dev->rx->entries[0].priv_data;
	rt2x00pci_register_write(rt2x00dev, RX_BASE_PTR, entry_priv->desc_dma);
	rt2x00pci_register_write(rt2x00dev, RX_MAX_CNT,
				 rt2x00dev->rx[0].limit);
	rt2x00pci_register_write(rt2x00dev, RX_CRX_IDX,
				 rt2x00dev->rx[0].limit - 1);
	rt2x00pci_register_write(rt2x00dev, RX_DRX_IDX, 0);

	/*
	 * Enable global DMA configuration
	 */
	rt2x00pci_register_read(rt2x00dev, WPDMA_GLO_CFG, &reg);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_TX_DMA, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_ENABLE_RX_DMA, 0);
	rt2x00_set_field32(&reg, WPDMA_GLO_CFG_TX_WRITEBACK_DONE, 1);
	rt2x00pci_register_write(rt2x00dev, WPDMA_GLO_CFG, reg);

	rt2x00pci_register_write(rt2x00dev, DELAY_INT_CFG, 0);

	return 0;
}

/*
 * Device state switch handlers.
 */
static void rt2800pci_toggle_irq(struct rt2x00_dev *rt2x00dev,
				 enum dev_state state)
{
	u32 reg;
	unsigned long flags;

	/*
	 * When interrupts are being enabled, the interrupt registers
	 * should clear the register to assure a clean state.
	 */
	if (state == STATE_RADIO_IRQ_ON) {
		rt2x00pci_register_read(rt2x00dev, INT_SOURCE_CSR, &reg);
		rt2x00pci_register_write(rt2x00dev, INT_SOURCE_CSR, reg);

		/*
		 * Enable tasklets. The beacon related tasklets are
		 * enabled when the beacon queue is started.
		 */
		tasklet_enable(&rt2x00dev->txstatus_tasklet);
		tasklet_enable(&rt2x00dev->rxdone_tasklet);
		tasklet_enable(&rt2x00dev->autowake_tasklet);
	}

	spin_lock_irqsave(&rt2x00dev->irqmask_lock, flags);
	reg = 0;
	if (state == STATE_RADIO_IRQ_ON) {
		rt2x00_set_field32(&reg, INT_MASK_CSR_RX_DONE, 1);
		rt2x00_set_field32(&reg, INT_MASK_CSR_TBTT, 1);
		rt2x00_set_field32(&reg, INT_MASK_CSR_PRE_TBTT, 1);
		rt2x00_set_field32(&reg, INT_MASK_CSR_TX_FIFO_STATUS, 1);
		rt2x00_set_field32(&reg, INT_MASK_CSR_AUTO_WAKEUP, 1);
	}
	rt2x00pci_register_write(rt2x00dev, INT_MASK_CSR, reg);
	spin_unlock_irqrestore(&rt2x00dev->irqmask_lock, flags);

	if (state == STATE_RADIO_IRQ_OFF) {
		/*
		 * Ensure that all tasklets are finished before
		 * disabling the interrupts.
		 */
		tasklet_disable(&rt2x00dev->txstatus_tasklet);
		tasklet_disable(&rt2x00dev->rxdone_tasklet);
		tasklet_disable(&rt2x00dev->autowake_tasklet);
	}
}

static int rt2800pci_init_registers(struct rt2x00_dev *rt2x00dev)
{
	u32 reg;

	/*
	 * Reset DMA indexes
	 */
	rt2x00pci_register_read(rt2x00dev, WPDMA_RST_IDX, &reg);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX0, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX1, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX2, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX3, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX4, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DTX_IDX5, 1);
	rt2x00_set_field32(&reg, WPDMA_RST_IDX_DRX_IDX0, 1);
	rt2x00pci_register_write(rt2x00dev, WPDMA_RST_IDX, reg);

	rt2x00pci_register_write(rt2x00dev, PBF_SYS_CTRL, 0x00000e1f);
	rt2x00pci_register_write(rt2x00dev, PBF_SYS_CTRL, 0x00000e00);

	if (rt2x00_rt(rt2x00dev, RT5390)) {
		rt2x00pci_register_read(rt2x00dev, AUX_CTRL, &reg);
		rt2x00_set_field32(&reg, AUX_CTRL_FORCE_PCIE_CLK, 1);
		rt2x00_set_field32(&reg, AUX_CTRL_WAKE_PCIE_EN, 1);
		rt2x00pci_register_write(rt2x00dev, AUX_CTRL, reg);
	}

	rt2x00pci_register_write(rt2x00dev, PWR_PIN_CFG, 0x00000003);

	rt2x00pci_register_read(rt2x00dev, MAC_SYS_CTRL, &reg);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_RESET_CSR, 1);
	rt2x00_set_field32(&reg, MAC_SYS_CTRL_RESET_BBP, 1);
	rt2x00pci_register_write(rt2x00dev, MAC_SYS_CTRL, reg);

	rt2x00pci_register_write(rt2x00dev, MAC_SYS_CTRL, 0x00000000);

	return 0;
}

static int rt2800pci_enable_radio(struct rt2x00_dev *rt2x00dev)
{
	if (unlikely(rt2800_wait_wpdma_ready(rt2x00dev) ||
		     rt2800pci_init_queues(rt2x00dev)))
		return -EIO;

	return rt2800_enable_radio(rt2x00dev);
}

static void rt2800pci_disable_radio(struct rt2x00_dev *rt2x00dev)
{
	if (rt2x00_is_soc(rt2x00dev)) {
		rt2800_disable_radio(rt2x00dev);
		rt2x00pci_register_write(rt2x00dev, PWR_PIN_CFG, 0);
		rt2x00pci_register_write(rt2x00dev, TX_PIN_CFG, 0);
	}
}

static int rt2800pci_set_state(struct rt2x00_dev *rt2x00dev,
			       enum dev_state state)
{
	if (state == STATE_AWAKE) {
		rt2800_mcu_request(rt2x00dev, MCU_WAKEUP, TOKEN_WAKUP, 0, 0x02);
		rt2800pci_mcu_status(rt2x00dev, TOKEN_WAKUP);
	} else if (state == STATE_SLEEP) {
		rt2x00pci_register_write(rt2x00dev, H2M_MAILBOX_STATUS,
					 0xffffffff);
		rt2x00pci_register_write(rt2x00dev, H2M_MAILBOX_CID,
					 0xffffffff);
		rt2800_mcu_request(rt2x00dev, MCU_SLEEP, 0x01, 0xff, 0x01);
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
static __le32 *rt2800pci_get_txwi(struct queue_entry *entry)
{
	return (__le32 *) entry->skb->data;
}

static void rt2800pci_write_tx_desc(struct queue_entry *entry,
				    struct txentry_desc *txdesc)
{
	struct skb_frame_desc *skbdesc = get_skb_frame_desc(entry->skb);
	struct queue_entry_priv_pci *entry_priv = entry->priv_data;
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
	rt2x00_set_field32(&word, TXD_W1_SD_LEN1, entry->skb->len);
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
 * RX control handlers
 */
static void rt2800pci_fill_rxdone(struct queue_entry *entry,
				  struct rxdone_entry_desc *rxdesc)
{
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

		/*
		 * The hardware has already checked the Michael Mic and has
		 * stripped it from the frame. Signal this to mac80211.
		 */
		rxdesc->flags |= RX_FLAG_MMIC_STRIPPED;

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
	rt2800_process_rxwi(entry, rxdesc);
}

/*
 * Interrupt functions.
 */
static void rt2800pci_wakeup(struct rt2x00_dev *rt2x00dev)
{
	struct ieee80211_conf conf = { .flags = 0 };
	struct rt2x00lib_conf libconf = { .conf = &conf };

	rt2800_config(rt2x00dev, &libconf, IEEE80211_CONF_CHANGE_PS);
}

static bool rt2800pci_txdone(struct rt2x00_dev *rt2x00dev)
{
	struct data_queue *queue;
	struct queue_entry *entry;
	u32 status;
	u8 qid;
	int max_tx_done = 16;

	while (kfifo_get(&rt2x00dev->txstatus_fifo, &status)) {
		qid = rt2x00_get_field32(status, TX_STA_FIFO_PID_QUEUE);
		if (unlikely(qid >= QID_RX)) {
			/*
			 * Unknown queue, this shouldn't happen. Just drop
			 * this tx status.
			 */
			WARNING(rt2x00dev, "Got TX status report with "
					   "unexpected pid %u, dropping\n", qid);
			break;
		}

		queue = rt2x00queue_get_tx_queue(rt2x00dev, qid);
		if (unlikely(queue == NULL)) {
			/*
			 * The queue is NULL, this shouldn't happen. Stop
			 * processing here and drop the tx status
			 */
			WARNING(rt2x00dev, "Got TX status for an unavailable "
					   "queue %u, dropping\n", qid);
			break;
		}

		if (unlikely(rt2x00queue_empty(queue))) {
			/*
			 * The queue is empty. Stop processing here
			 * and drop the tx status.
			 */
			WARNING(rt2x00dev, "Got TX status for an empty "
					   "queue %u, dropping\n", qid);
			break;
		}

		entry = rt2x00queue_get_entry(queue, Q_INDEX_DONE);
		rt2800_txdone_entry(entry, status);

		if (--max_tx_done == 0)
			break;
	}

	return !max_tx_done;
}

static inline void rt2800pci_enable_interrupt(struct rt2x00_dev *rt2x00dev,
					      struct rt2x00_field32 irq_field)
{
	u32 reg;

	/*
	 * Enable a single interrupt. The interrupt mask register
	 * access needs locking.
	 */
	spin_lock_irq(&rt2x00dev->irqmask_lock);
	rt2x00pci_register_read(rt2x00dev, INT_MASK_CSR, &reg);
	rt2x00_set_field32(&reg, irq_field, 1);
	rt2x00pci_register_write(rt2x00dev, INT_MASK_CSR, reg);
	spin_unlock_irq(&rt2x00dev->irqmask_lock);
}

static void rt2800pci_txstatus_tasklet(unsigned long data)
{
	struct rt2x00_dev *rt2x00dev = (struct rt2x00_dev *)data;
	if (rt2800pci_txdone(rt2x00dev))
		tasklet_schedule(&rt2x00dev->txstatus_tasklet);

	/*
	 * No need to enable the tx status interrupt here as we always
	 * leave it enabled to minimize the possibility of a tx status
	 * register overflow. See comment in interrupt handler.
	 */
}

static void rt2800pci_pretbtt_tasklet(unsigned long data)
{
	struct rt2x00_dev *rt2x00dev = (struct rt2x00_dev *)data;
	rt2x00lib_pretbtt(rt2x00dev);
	rt2800pci_enable_interrupt(rt2x00dev, INT_MASK_CSR_PRE_TBTT);
}

static void rt2800pci_tbtt_tasklet(unsigned long data)
{
	struct rt2x00_dev *rt2x00dev = (struct rt2x00_dev *)data;
	rt2x00lib_beacondone(rt2x00dev);
	rt2800pci_enable_interrupt(rt2x00dev, INT_MASK_CSR_TBTT);
}

static void rt2800pci_rxdone_tasklet(unsigned long data)
{
	struct rt2x00_dev *rt2x00dev = (struct rt2x00_dev *)data;
	if (rt2x00pci_rxdone(rt2x00dev))
		tasklet_schedule(&rt2x00dev->rxdone_tasklet);
	else
		rt2800pci_enable_interrupt(rt2x00dev, INT_MASK_CSR_RX_DONE);
}

static void rt2800pci_autowake_tasklet(unsigned long data)
{
	struct rt2x00_dev *rt2x00dev = (struct rt2x00_dev *)data;
	rt2800pci_wakeup(rt2x00dev);
	rt2800pci_enable_interrupt(rt2x00dev, INT_MASK_CSR_AUTO_WAKEUP);
}

static void rt2800pci_txstatus_interrupt(struct rt2x00_dev *rt2x00dev)
{
	u32 status;
	int i;

	/*
	 * The TX_FIFO_STATUS interrupt needs special care. We should
	 * read TX_STA_FIFO but we should do it immediately as otherwise
	 * the register can overflow and we would lose status reports.
	 *
	 * Hence, read the TX_STA_FIFO register and copy all tx status
	 * reports into a kernel FIFO which is handled in the txstatus
	 * tasklet. We use a tasklet to process the tx status reports
	 * because we can schedule the tasklet multiple times (when the
	 * interrupt fires again during tx status processing).
	 *
	 * Furthermore we don't disable the TX_FIFO_STATUS
	 * interrupt here but leave it enabled so that the TX_STA_FIFO
	 * can also be read while the tx status tasklet gets executed.
	 *
	 * Since we have only one producer and one consumer we don't
	 * need to lock the kfifo.
	 */
	for (i = 0; i < rt2x00dev->ops->tx->entry_num; i++) {
		rt2x00pci_register_read(rt2x00dev, TX_STA_FIFO, &status);

		if (!rt2x00_get_field32(status, TX_STA_FIFO_VALID))
			break;

		if (!kfifo_put(&rt2x00dev->txstatus_fifo, &status)) {
			WARNING(rt2x00dev, "TX status FIFO overrun,"
				"drop tx status report.\n");
			break;
		}
	}

	/* Schedule the tasklet for processing the tx status. */
	tasklet_schedule(&rt2x00dev->txstatus_tasklet);
}

static irqreturn_t rt2800pci_interrupt(int irq, void *dev_instance)
{
	struct rt2x00_dev *rt2x00dev = dev_instance;
	u32 reg, mask;

	/* Read status and ACK all interrupts */
	rt2x00pci_register_read(rt2x00dev, INT_SOURCE_CSR, &reg);
	rt2x00pci_register_write(rt2x00dev, INT_SOURCE_CSR, reg);

	if (!reg)
		return IRQ_NONE;

	if (!test_bit(DEVICE_STATE_ENABLED_RADIO, &rt2x00dev->flags))
		return IRQ_HANDLED;

	/*
	 * Since INT_MASK_CSR and INT_SOURCE_CSR use the same bits
	 * for interrupts and interrupt masks we can just use the value of
	 * INT_SOURCE_CSR to create the interrupt mask.
	 */
	mask = ~reg;

	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_TX_FIFO_STATUS)) {
		rt2800pci_txstatus_interrupt(rt2x00dev);
		/*
		 * Never disable the TX_FIFO_STATUS interrupt.
		 */
		rt2x00_set_field32(&mask, INT_MASK_CSR_TX_FIFO_STATUS, 1);
	}

	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_PRE_TBTT))
		tasklet_hi_schedule(&rt2x00dev->pretbtt_tasklet);

	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_TBTT))
		tasklet_hi_schedule(&rt2x00dev->tbtt_tasklet);

	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_RX_DONE))
		tasklet_schedule(&rt2x00dev->rxdone_tasklet);

	if (rt2x00_get_field32(reg, INT_SOURCE_CSR_AUTO_WAKEUP))
		tasklet_schedule(&rt2x00dev->autowake_tasklet);

	/*
	 * Disable all interrupts for which a tasklet was scheduled right now,
	 * the tasklet will reenable the appropriate interrupts.
	 */
	spin_lock(&rt2x00dev->irqmask_lock);
	rt2x00pci_register_read(rt2x00dev, INT_MASK_CSR, &reg);
	reg &= mask;
	rt2x00pci_register_write(rt2x00dev, INT_MASK_CSR, reg);
	spin_unlock(&rt2x00dev->irqmask_lock);

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

static int rt2800pci_probe_hw(struct rt2x00_dev *rt2x00dev)
{
	int retval;
	u32 reg;

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
	 * Enable rfkill polling by setting GPIO direction of the
	 * rfkill switch GPIO pin correctly.
	 */
	rt2x00pci_register_read(rt2x00dev, GPIO_CTRL_CFG, &reg);
	rt2x00_set_field32(&reg, GPIO_CTRL_CFG_GPIOD_BIT2, 1);
	rt2x00pci_register_write(rt2x00dev, GPIO_CTRL_CFG, reg);

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
	__set_bit(CAPABILITY_CONTROL_FILTERS, &rt2x00dev->cap_flags);
	__set_bit(CAPABILITY_CONTROL_FILTER_PSPOLL, &rt2x00dev->cap_flags);

	/*
	 * This device has a pre tbtt interrupt and thus fetches
	 * a new beacon directly prior to transmission.
	 */
	__set_bit(CAPABILITY_PRE_TBTT_INTERRUPT, &rt2x00dev->cap_flags);

	/*
	 * This device requires firmware.
	 */
	if (!rt2x00_is_soc(rt2x00dev))
		__set_bit(REQUIRE_FIRMWARE, &rt2x00dev->cap_flags);
	__set_bit(REQUIRE_DMA, &rt2x00dev->cap_flags);
	__set_bit(REQUIRE_L2PAD, &rt2x00dev->cap_flags);
	__set_bit(REQUIRE_TXSTATUS_FIFO, &rt2x00dev->cap_flags);
	__set_bit(REQUIRE_TASKLET_CONTEXT, &rt2x00dev->cap_flags);
	if (!modparam_nohwcrypt)
		__set_bit(CAPABILITY_HW_CRYPTO, &rt2x00dev->cap_flags);
	__set_bit(CAPABILITY_LINK_TUNING, &rt2x00dev->cap_flags);
	__set_bit(REQUIRE_HT_TX_DESC, &rt2x00dev->cap_flags);

	/*
	 * Set the rssi offset.
	 */
	rt2x00dev->rssi_offset = DEFAULT_RSSI_OFFSET;

	return 0;
}

static const struct ieee80211_ops rt2800pci_mac80211_ops = {
	.tx			= rt2x00mac_tx,
	.start			= rt2x00mac_start,
	.stop			= rt2x00mac_stop,
	.add_interface		= rt2x00mac_add_interface,
	.remove_interface	= rt2x00mac_remove_interface,
	.config			= rt2x00mac_config,
	.configure_filter	= rt2x00mac_configure_filter,
	.set_key		= rt2x00mac_set_key,
	.sw_scan_start		= rt2x00mac_sw_scan_start,
	.sw_scan_complete	= rt2x00mac_sw_scan_complete,
	.get_stats		= rt2x00mac_get_stats,
	.get_tkip_seq		= rt2800_get_tkip_seq,
	.set_rts_threshold	= rt2800_set_rts_threshold,
	.bss_info_changed	= rt2x00mac_bss_info_changed,
	.conf_tx		= rt2800_conf_tx,
	.get_tsf		= rt2800_get_tsf,
	.rfkill_poll		= rt2x00mac_rfkill_poll,
	.ampdu_action		= rt2800_ampdu_action,
	.flush			= rt2x00mac_flush,
	.get_survey		= rt2800_get_survey,
	.get_ringparam		= rt2x00mac_get_ringparam,
};

static const struct rt2800_ops rt2800pci_rt2800_ops = {
	.register_read		= rt2x00pci_register_read,
	.register_read_lock	= rt2x00pci_register_read, /* same for PCI */
	.register_write		= rt2x00pci_register_write,
	.register_write_lock	= rt2x00pci_register_write, /* same for PCI */
	.register_multiread	= rt2x00pci_register_multiread,
	.register_multiwrite	= rt2x00pci_register_multiwrite,
	.regbusy_read		= rt2x00pci_regbusy_read,
	.drv_write_firmware	= rt2800pci_write_firmware,
	.drv_init_registers	= rt2800pci_init_registers,
	.drv_get_txwi		= rt2800pci_get_txwi,
};

static const struct rt2x00lib_ops rt2800pci_rt2x00_ops = {
	.irq_handler		= rt2800pci_interrupt,
	.txstatus_tasklet	= rt2800pci_txstatus_tasklet,
	.pretbtt_tasklet	= rt2800pci_pretbtt_tasklet,
	.tbtt_tasklet		= rt2800pci_tbtt_tasklet,
	.rxdone_tasklet		= rt2800pci_rxdone_tasklet,
	.autowake_tasklet	= rt2800pci_autowake_tasklet,
	.probe_hw		= rt2800pci_probe_hw,
	.get_firmware_name	= rt2800pci_get_firmware_name,
	.check_firmware		= rt2800_check_firmware,
	.load_firmware		= rt2800_load_firmware,
	.initialize		= rt2x00pci_initialize,
	.uninitialize		= rt2x00pci_uninitialize,
	.get_entry_state	= rt2800pci_get_entry_state,
	.clear_entry		= rt2800pci_clear_entry,
	.set_device_state	= rt2800pci_set_device_state,
	.rfkill_poll		= rt2800_rfkill_poll,
	.link_stats		= rt2800_link_stats,
	.reset_tuner		= rt2800_reset_tuner,
	.link_tuner		= rt2800_link_tuner,
	.gain_calibration	= rt2800_gain_calibration,
	.start_queue		= rt2800pci_start_queue,
	.kick_queue		= rt2800pci_kick_queue,
	.stop_queue		= rt2800pci_stop_queue,
	.flush_queue		= rt2x00pci_flush_queue,
	.write_tx_desc		= rt2800pci_write_tx_desc,
	.write_tx_data		= rt2800_write_tx_data,
	.write_beacon		= rt2800_write_beacon,
	.clear_beacon		= rt2800_clear_beacon,
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
	.entry_num		= 128,
	.data_size		= AGGREGATION_SIZE,
	.desc_size		= RXD_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_pci),
};

static const struct data_queue_desc rt2800pci_queue_tx = {
	.entry_num		= 64,
	.data_size		= AGGREGATION_SIZE,
	.desc_size		= TXD_DESC_SIZE,
	.priv_size		= sizeof(struct queue_entry_priv_pci),
};

static const struct data_queue_desc rt2800pci_queue_bcn = {
	.entry_num		= 8,
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
	.drv			= &rt2800pci_rt2800_ops,
	.hw			= &rt2800pci_mac80211_ops,
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	.debugfs		= &rt2800_rt2x00debug,
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */
};

/*
 * RT2800pci module information.
 */
#ifdef CONFIG_PCI
static DEFINE_PCI_DEVICE_TABLE(rt2800pci_device_table) = {
	{ PCI_DEVICE(0x1814, 0x0601) },
	{ PCI_DEVICE(0x1814, 0x0681) },
	{ PCI_DEVICE(0x1814, 0x0701) },
	{ PCI_DEVICE(0x1814, 0x0781) },
	{ PCI_DEVICE(0x1814, 0x3090) },
	{ PCI_DEVICE(0x1814, 0x3091) },
	{ PCI_DEVICE(0x1814, 0x3092) },
	{ PCI_DEVICE(0x1432, 0x7708) },
	{ PCI_DEVICE(0x1432, 0x7727) },
	{ PCI_DEVICE(0x1432, 0x7728) },
	{ PCI_DEVICE(0x1432, 0x7738) },
	{ PCI_DEVICE(0x1432, 0x7748) },
	{ PCI_DEVICE(0x1432, 0x7758) },
	{ PCI_DEVICE(0x1432, 0x7768) },
	{ PCI_DEVICE(0x1462, 0x891a) },
	{ PCI_DEVICE(0x1a3b, 0x1059) },
#ifdef CONFIG_RT2800PCI_RT33XX
	{ PCI_DEVICE(0x1814, 0x3390) },
#endif
#ifdef CONFIG_RT2800PCI_RT35XX
	{ PCI_DEVICE(0x1432, 0x7711) },
	{ PCI_DEVICE(0x1432, 0x7722) },
	{ PCI_DEVICE(0x1814, 0x3060) },
	{ PCI_DEVICE(0x1814, 0x3062) },
	{ PCI_DEVICE(0x1814, 0x3562) },
	{ PCI_DEVICE(0x1814, 0x3592) },
	{ PCI_DEVICE(0x1814, 0x3593) },
#endif
#ifdef CONFIG_RT2800PCI_RT53XX
	{ PCI_DEVICE(0x1814, 0x5390) },
	{ PCI_DEVICE(0x1814, 0x539f) },
#endif
	{ 0, }
};
#endif /* CONFIG_PCI */

MODULE_AUTHOR(DRV_PROJECT);
MODULE_VERSION(DRV_VERSION);
MODULE_DESCRIPTION("Ralink RT2800 PCI & PCMCIA Wireless LAN driver.");
MODULE_SUPPORTED_DEVICE("Ralink RT2860 PCI & PCMCIA chipset based cards");
#ifdef CONFIG_PCI
MODULE_FIRMWARE(FIRMWARE_RT2860);
MODULE_DEVICE_TABLE(pci, rt2800pci_device_table);
#endif /* CONFIG_PCI */
MODULE_LICENSE("GPL");

#if defined(CONFIG_RALINK_RT288X) || defined(CONFIG_RALINK_RT305X)
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
#endif /* CONFIG_RALINK_RT288X || CONFIG_RALINK_RT305X */

#ifdef CONFIG_PCI
static int rt2800pci_probe(struct pci_dev *pci_dev,
			   const struct pci_device_id *id)
{
	return rt2x00pci_probe(pci_dev, &rt2800pci_ops);
}

static struct pci_driver rt2800pci_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= rt2800pci_device_table,
	.probe		= rt2800pci_probe,
	.remove		= __devexit_p(rt2x00pci_remove),
	.suspend	= rt2x00pci_suspend,
	.resume		= rt2x00pci_resume,
};
#endif /* CONFIG_PCI */

static int __init rt2800pci_init(void)
{
	int ret = 0;

#if defined(CONFIG_RALINK_RT288X) || defined(CONFIG_RALINK_RT305X)
	ret = platform_driver_register(&rt2800soc_driver);
	if (ret)
		return ret;
#endif
#ifdef CONFIG_PCI
	ret = pci_register_driver(&rt2800pci_driver);
	if (ret) {
#if defined(CONFIG_RALINK_RT288X) || defined(CONFIG_RALINK_RT305X)
		platform_driver_unregister(&rt2800soc_driver);
#endif
		return ret;
	}
#endif

	return ret;
}

static void __exit rt2800pci_exit(void)
{
#ifdef CONFIG_PCI
	pci_unregister_driver(&rt2800pci_driver);
#endif
#if defined(CONFIG_RALINK_RT288X) || defined(CONFIG_RALINK_RT305X)
	platform_driver_unregister(&rt2800soc_driver);
#endif
}

module_init(rt2800pci_init);
module_exit(rt2800pci_exit);
