/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2005-2006 Fen Systems Ltd.
 * Copyright 2006-2013 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/i2c.h>
#include <linux/mii.h>
#include <linux/slab.h>
#include "net_driver.h"
#include "bitfield.h"
#include "efx.h"
#include "nic.h"
#include "farch_regs.h"
#include "io.h"
#include "phy.h"
#include "workarounds.h"
#include "selftest.h"
#include "mdio_10g.h"

/* Hardware control for SFC4000 (aka Falcon). */

/**************************************************************************
 *
 * NIC stats
 *
 **************************************************************************
 */

#define FALCON_MAC_STATS_SIZE 0x100

#define XgRxOctets_offset 0x0
#define XgRxOctets_WIDTH 48
#define XgRxOctetsOK_offset 0x8
#define XgRxOctetsOK_WIDTH 48
#define XgRxPkts_offset 0x10
#define XgRxPkts_WIDTH 32
#define XgRxPktsOK_offset 0x14
#define XgRxPktsOK_WIDTH 32
#define XgRxBroadcastPkts_offset 0x18
#define XgRxBroadcastPkts_WIDTH 32
#define XgRxMulticastPkts_offset 0x1C
#define XgRxMulticastPkts_WIDTH 32
#define XgRxUnicastPkts_offset 0x20
#define XgRxUnicastPkts_WIDTH 32
#define XgRxUndersizePkts_offset 0x24
#define XgRxUndersizePkts_WIDTH 32
#define XgRxOversizePkts_offset 0x28
#define XgRxOversizePkts_WIDTH 32
#define XgRxJabberPkts_offset 0x2C
#define XgRxJabberPkts_WIDTH 32
#define XgRxUndersizeFCSerrorPkts_offset 0x30
#define XgRxUndersizeFCSerrorPkts_WIDTH 32
#define XgRxDropEvents_offset 0x34
#define XgRxDropEvents_WIDTH 32
#define XgRxFCSerrorPkts_offset 0x38
#define XgRxFCSerrorPkts_WIDTH 32
#define XgRxAlignError_offset 0x3C
#define XgRxAlignError_WIDTH 32
#define XgRxSymbolError_offset 0x40
#define XgRxSymbolError_WIDTH 32
#define XgRxInternalMACError_offset 0x44
#define XgRxInternalMACError_WIDTH 32
#define XgRxControlPkts_offset 0x48
#define XgRxControlPkts_WIDTH 32
#define XgRxPausePkts_offset 0x4C
#define XgRxPausePkts_WIDTH 32
#define XgRxPkts64Octets_offset 0x50
#define XgRxPkts64Octets_WIDTH 32
#define XgRxPkts65to127Octets_offset 0x54
#define XgRxPkts65to127Octets_WIDTH 32
#define XgRxPkts128to255Octets_offset 0x58
#define XgRxPkts128to255Octets_WIDTH 32
#define XgRxPkts256to511Octets_offset 0x5C
#define XgRxPkts256to511Octets_WIDTH 32
#define XgRxPkts512to1023Octets_offset 0x60
#define XgRxPkts512to1023Octets_WIDTH 32
#define XgRxPkts1024to15xxOctets_offset 0x64
#define XgRxPkts1024to15xxOctets_WIDTH 32
#define XgRxPkts15xxtoMaxOctets_offset 0x68
#define XgRxPkts15xxtoMaxOctets_WIDTH 32
#define XgRxLengthError_offset 0x6C
#define XgRxLengthError_WIDTH 32
#define XgTxPkts_offset 0x80
#define XgTxPkts_WIDTH 32
#define XgTxOctets_offset 0x88
#define XgTxOctets_WIDTH 48
#define XgTxMulticastPkts_offset 0x90
#define XgTxMulticastPkts_WIDTH 32
#define XgTxBroadcastPkts_offset 0x94
#define XgTxBroadcastPkts_WIDTH 32
#define XgTxUnicastPkts_offset 0x98
#define XgTxUnicastPkts_WIDTH 32
#define XgTxControlPkts_offset 0x9C
#define XgTxControlPkts_WIDTH 32
#define XgTxPausePkts_offset 0xA0
#define XgTxPausePkts_WIDTH 32
#define XgTxPkts64Octets_offset 0xA4
#define XgTxPkts64Octets_WIDTH 32
#define XgTxPkts65to127Octets_offset 0xA8
#define XgTxPkts65to127Octets_WIDTH 32
#define XgTxPkts128to255Octets_offset 0xAC
#define XgTxPkts128to255Octets_WIDTH 32
#define XgTxPkts256to511Octets_offset 0xB0
#define XgTxPkts256to511Octets_WIDTH 32
#define XgTxPkts512to1023Octets_offset 0xB4
#define XgTxPkts512to1023Octets_WIDTH 32
#define XgTxPkts1024to15xxOctets_offset 0xB8
#define XgTxPkts1024to15xxOctets_WIDTH 32
#define XgTxPkts1519toMaxOctets_offset 0xBC
#define XgTxPkts1519toMaxOctets_WIDTH 32
#define XgTxUndersizePkts_offset 0xC0
#define XgTxUndersizePkts_WIDTH 32
#define XgTxOversizePkts_offset 0xC4
#define XgTxOversizePkts_WIDTH 32
#define XgTxNonTcpUdpPkt_offset 0xC8
#define XgTxNonTcpUdpPkt_WIDTH 16
#define XgTxMacSrcErrPkt_offset 0xCC
#define XgTxMacSrcErrPkt_WIDTH 16
#define XgTxIpSrcErrPkt_offset 0xD0
#define XgTxIpSrcErrPkt_WIDTH 16
#define XgDmaDone_offset 0xD4
#define XgDmaDone_WIDTH 32

#define FALCON_XMAC_STATS_DMA_FLAG(efx)				\
	(*(u32 *)((efx)->stats_buffer.addr + XgDmaDone_offset))

#define FALCON_DMA_STAT(ext_name, hw_name)				\
	[FALCON_STAT_ ## ext_name] =					\
	{ #ext_name,							\
	  /* 48-bit stats are zero-padded to 64 on DMA */		\
	  hw_name ## _ ## WIDTH == 48 ? 64 : hw_name ## _ ## WIDTH,	\
	  hw_name ## _ ## offset }
#define FALCON_OTHER_STAT(ext_name)					\
	[FALCON_STAT_ ## ext_name] = { #ext_name, 0, 0 }
#define GENERIC_SW_STAT(ext_name)				\
	[GENERIC_STAT_ ## ext_name] = { #ext_name, 0, 0 }

static const struct efx_hw_stat_desc falcon_stat_desc[FALCON_STAT_COUNT] = {
	FALCON_DMA_STAT(tx_bytes, XgTxOctets),
	FALCON_DMA_STAT(tx_packets, XgTxPkts),
	FALCON_DMA_STAT(tx_pause, XgTxPausePkts),
	FALCON_DMA_STAT(tx_control, XgTxControlPkts),
	FALCON_DMA_STAT(tx_unicast, XgTxUnicastPkts),
	FALCON_DMA_STAT(tx_multicast, XgTxMulticastPkts),
	FALCON_DMA_STAT(tx_broadcast, XgTxBroadcastPkts),
	FALCON_DMA_STAT(tx_lt64, XgTxUndersizePkts),
	FALCON_DMA_STAT(tx_64, XgTxPkts64Octets),
	FALCON_DMA_STAT(tx_65_to_127, XgTxPkts65to127Octets),
	FALCON_DMA_STAT(tx_128_to_255, XgTxPkts128to255Octets),
	FALCON_DMA_STAT(tx_256_to_511, XgTxPkts256to511Octets),
	FALCON_DMA_STAT(tx_512_to_1023, XgTxPkts512to1023Octets),
	FALCON_DMA_STAT(tx_1024_to_15xx, XgTxPkts1024to15xxOctets),
	FALCON_DMA_STAT(tx_15xx_to_jumbo, XgTxPkts1519toMaxOctets),
	FALCON_DMA_STAT(tx_gtjumbo, XgTxOversizePkts),
	FALCON_DMA_STAT(tx_non_tcpudp, XgTxNonTcpUdpPkt),
	FALCON_DMA_STAT(tx_mac_src_error, XgTxMacSrcErrPkt),
	FALCON_DMA_STAT(tx_ip_src_error, XgTxIpSrcErrPkt),
	FALCON_DMA_STAT(rx_bytes, XgRxOctets),
	FALCON_DMA_STAT(rx_good_bytes, XgRxOctetsOK),
	FALCON_OTHER_STAT(rx_bad_bytes),
	FALCON_DMA_STAT(rx_packets, XgRxPkts),
	FALCON_DMA_STAT(rx_good, XgRxPktsOK),
	FALCON_DMA_STAT(rx_bad, XgRxFCSerrorPkts),
	FALCON_DMA_STAT(rx_pause, XgRxPausePkts),
	FALCON_DMA_STAT(rx_control, XgRxControlPkts),
	FALCON_DMA_STAT(rx_unicast, XgRxUnicastPkts),
	FALCON_DMA_STAT(rx_multicast, XgRxMulticastPkts),
	FALCON_DMA_STAT(rx_broadcast, XgRxBroadcastPkts),
	FALCON_DMA_STAT(rx_lt64, XgRxUndersizePkts),
	FALCON_DMA_STAT(rx_64, XgRxPkts64Octets),
	FALCON_DMA_STAT(rx_65_to_127, XgRxPkts65to127Octets),
	FALCON_DMA_STAT(rx_128_to_255, XgRxPkts128to255Octets),
	FALCON_DMA_STAT(rx_256_to_511, XgRxPkts256to511Octets),
	FALCON_DMA_STAT(rx_512_to_1023, XgRxPkts512to1023Octets),
	FALCON_DMA_STAT(rx_1024_to_15xx, XgRxPkts1024to15xxOctets),
	FALCON_DMA_STAT(rx_15xx_to_jumbo, XgRxPkts15xxtoMaxOctets),
	FALCON_DMA_STAT(rx_gtjumbo, XgRxOversizePkts),
	FALCON_DMA_STAT(rx_bad_lt64, XgRxUndersizeFCSerrorPkts),
	FALCON_DMA_STAT(rx_bad_gtjumbo, XgRxJabberPkts),
	FALCON_DMA_STAT(rx_overflow, XgRxDropEvents),
	FALCON_DMA_STAT(rx_symbol_error, XgRxSymbolError),
	FALCON_DMA_STAT(rx_align_error, XgRxAlignError),
	FALCON_DMA_STAT(rx_length_error, XgRxLengthError),
	FALCON_DMA_STAT(rx_internal_error, XgRxInternalMACError),
	FALCON_OTHER_STAT(rx_nodesc_drop_cnt),
	GENERIC_SW_STAT(rx_nodesc_trunc),
	GENERIC_SW_STAT(rx_noskb_drops),
};
static const unsigned long falcon_stat_mask[] = {
	[0 ... BITS_TO_LONGS(FALCON_STAT_COUNT) - 1] = ~0UL,
};

/**************************************************************************
 *
 * Basic SPI command set and bit definitions
 *
 *************************************************************************/

#define SPI_WRSR 0x01		/* Write status register */
#define SPI_WRITE 0x02		/* Write data to memory array */
#define SPI_READ 0x03		/* Read data from memory array */
#define SPI_WRDI 0x04		/* Reset write enable latch */
#define SPI_RDSR 0x05		/* Read status register */
#define SPI_WREN 0x06		/* Set write enable latch */
#define SPI_SST_EWSR 0x50	/* SST: Enable write to status register */

#define SPI_STATUS_WPEN 0x80	/* Write-protect pin enabled */
#define SPI_STATUS_BP2 0x10	/* Block protection bit 2 */
#define SPI_STATUS_BP1 0x08	/* Block protection bit 1 */
#define SPI_STATUS_BP0 0x04	/* Block protection bit 0 */
#define SPI_STATUS_WEN 0x02	/* State of the write enable latch */
#define SPI_STATUS_NRDY 0x01	/* Device busy flag */

/**************************************************************************
 *
 * Non-volatile memory layout
 *
 **************************************************************************
 */

/* SFC4000 flash is partitioned into:
 *     0-0x400       chip and board config (see struct falcon_nvconfig)
 *     0x400-0x8000  unused (or may contain VPD if EEPROM not present)
 *     0x8000-end    boot code (mapped to PCI expansion ROM)
 * SFC4000 small EEPROM (size < 0x400) is used for VPD only.
 * SFC4000 large EEPROM (size >= 0x400) is partitioned into:
 *     0-0x400       chip and board config
 *     configurable  VPD
 *     0x800-0x1800  boot config
 * Aside from the chip and board config, all of these are optional and may
 * be absent or truncated depending on the devices used.
 */
#define FALCON_NVCONFIG_END 0x400U
#define FALCON_FLASH_BOOTCODE_START 0x8000U
#define FALCON_EEPROM_BOOTCONFIG_START 0x800U
#define FALCON_EEPROM_BOOTCONFIG_END 0x1800U

/* Board configuration v2 (v1 is obsolete; later versions are compatible) */
struct falcon_nvconfig_board_v2 {
	__le16 nports;
	u8 port0_phy_addr;
	u8 port0_phy_type;
	u8 port1_phy_addr;
	u8 port1_phy_type;
	__le16 asic_sub_revision;
	__le16 board_revision;
} __packed;

/* Board configuration v3 extra information */
struct falcon_nvconfig_board_v3 {
	__le32 spi_device_type[2];
} __packed;

/* Bit numbers for spi_device_type */
#define SPI_DEV_TYPE_SIZE_LBN 0
#define SPI_DEV_TYPE_SIZE_WIDTH 5
#define SPI_DEV_TYPE_ADDR_LEN_LBN 6
#define SPI_DEV_TYPE_ADDR_LEN_WIDTH 2
#define SPI_DEV_TYPE_ERASE_CMD_LBN 8
#define SPI_DEV_TYPE_ERASE_CMD_WIDTH 8
#define SPI_DEV_TYPE_ERASE_SIZE_LBN 16
#define SPI_DEV_TYPE_ERASE_SIZE_WIDTH 5
#define SPI_DEV_TYPE_BLOCK_SIZE_LBN 24
#define SPI_DEV_TYPE_BLOCK_SIZE_WIDTH 5
#define SPI_DEV_TYPE_FIELD(type, field)					\
	(((type) >> EFX_LOW_BIT(field)) & EFX_MASK32(EFX_WIDTH(field)))

#define FALCON_NVCONFIG_OFFSET 0x300

#define FALCON_NVCONFIG_BOARD_MAGIC_NUM 0xFA1C
struct falcon_nvconfig {
	efx_oword_t ee_vpd_cfg_reg;			/* 0x300 */
	u8 mac_address[2][8];			/* 0x310 */
	efx_oword_t pcie_sd_ctl0123_reg;		/* 0x320 */
	efx_oword_t pcie_sd_ctl45_reg;			/* 0x330 */
	efx_oword_t pcie_pcs_ctl_stat_reg;		/* 0x340 */
	efx_oword_t hw_init_reg;			/* 0x350 */
	efx_oword_t nic_stat_reg;			/* 0x360 */
	efx_oword_t glb_ctl_reg;			/* 0x370 */
	efx_oword_t srm_cfg_reg;			/* 0x380 */
	efx_oword_t spare_reg;				/* 0x390 */
	__le16 board_magic_num;			/* 0x3A0 */
	__le16 board_struct_ver;
	__le16 board_checksum;
	struct falcon_nvconfig_board_v2 board_v2;
	efx_oword_t ee_base_page_reg;			/* 0x3B0 */
	struct falcon_nvconfig_board_v3 board_v3;	/* 0x3C0 */
} __packed;

/*************************************************************************/

static int falcon_reset_hw(struct efx_nic *efx, enum reset_type method);
static void falcon_reconfigure_mac_wrapper(struct efx_nic *efx);

static const unsigned int
/* "Large" EEPROM device: Atmel AT25640 or similar
 * 8 KB, 16-bit address, 32 B write block */
large_eeprom_type = ((13 << SPI_DEV_TYPE_SIZE_LBN)
		     | (2 << SPI_DEV_TYPE_ADDR_LEN_LBN)
		     | (5 << SPI_DEV_TYPE_BLOCK_SIZE_LBN)),
/* Default flash device: Atmel AT25F1024
 * 128 KB, 24-bit address, 32 KB erase block, 256 B write block */
default_flash_type = ((17 << SPI_DEV_TYPE_SIZE_LBN)
		      | (3 << SPI_DEV_TYPE_ADDR_LEN_LBN)
		      | (0x52 << SPI_DEV_TYPE_ERASE_CMD_LBN)
		      | (15 << SPI_DEV_TYPE_ERASE_SIZE_LBN)
		      | (8 << SPI_DEV_TYPE_BLOCK_SIZE_LBN));

/**************************************************************************
 *
 * I2C bus - this is a bit-bashing interface using GPIO pins
 * Note that it uses the output enables to tristate the outputs
 * SDA is the data pin and SCL is the clock
 *
 **************************************************************************
 */
static void falcon_setsda(void *data, int state)
{
	struct efx_nic *efx = (struct efx_nic *)data;
	efx_oword_t reg;

	efx_reado(efx, &reg, FR_AB_GPIO_CTL);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_GPIO3_OEN, !state);
	efx_writeo(efx, &reg, FR_AB_GPIO_CTL);
}

static void falcon_setscl(void *data, int state)
{
	struct efx_nic *efx = (struct efx_nic *)data;
	efx_oword_t reg;

	efx_reado(efx, &reg, FR_AB_GPIO_CTL);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_GPIO0_OEN, !state);
	efx_writeo(efx, &reg, FR_AB_GPIO_CTL);
}

static int falcon_getsda(void *data)
{
	struct efx_nic *efx = (struct efx_nic *)data;
	efx_oword_t reg;

	efx_reado(efx, &reg, FR_AB_GPIO_CTL);
	return EFX_OWORD_FIELD(reg, FRF_AB_GPIO3_IN);
}

static int falcon_getscl(void *data)
{
	struct efx_nic *efx = (struct efx_nic *)data;
	efx_oword_t reg;

	efx_reado(efx, &reg, FR_AB_GPIO_CTL);
	return EFX_OWORD_FIELD(reg, FRF_AB_GPIO0_IN);
}

static const struct i2c_algo_bit_data falcon_i2c_bit_operations = {
	.setsda		= falcon_setsda,
	.setscl		= falcon_setscl,
	.getsda		= falcon_getsda,
	.getscl		= falcon_getscl,
	.udelay		= 5,
	/* Wait up to 50 ms for slave to let us pull SCL high */
	.timeout	= DIV_ROUND_UP(HZ, 20),
};

static void falcon_push_irq_moderation(struct efx_channel *channel)
{
	efx_dword_t timer_cmd;
	struct efx_nic *efx = channel->efx;

	/* Set timer register */
	if (channel->irq_moderation) {
		EFX_POPULATE_DWORD_2(timer_cmd,
				     FRF_AB_TC_TIMER_MODE,
				     FFE_BB_TIMER_MODE_INT_HLDOFF,
				     FRF_AB_TC_TIMER_VAL,
				     channel->irq_moderation - 1);
	} else {
		EFX_POPULATE_DWORD_2(timer_cmd,
				     FRF_AB_TC_TIMER_MODE,
				     FFE_BB_TIMER_MODE_DIS,
				     FRF_AB_TC_TIMER_VAL, 0);
	}
	BUILD_BUG_ON(FR_AA_TIMER_COMMAND_KER != FR_BZ_TIMER_COMMAND_P0);
	efx_writed_page_locked(efx, &timer_cmd, FR_BZ_TIMER_COMMAND_P0,
			       channel->channel);
}

static void falcon_deconfigure_mac_wrapper(struct efx_nic *efx);

static void falcon_prepare_flush(struct efx_nic *efx)
{
	falcon_deconfigure_mac_wrapper(efx);

	/* Wait for the tx and rx fifo's to get to the next packet boundary
	 * (~1ms without back-pressure), then to drain the remainder of the
	 * fifo's at data path speeds (negligible), with a healthy margin. */
	msleep(10);
}

/* Acknowledge a legacy interrupt from Falcon
 *
 * This acknowledges a legacy (not MSI) interrupt via INT_ACK_KER_REG.
 *
 * Due to SFC bug 3706 (silicon revision <=A1) reads can be duplicated in the
 * BIU. Interrupt acknowledge is read sensitive so must write instead
 * (then read to ensure the BIU collector is flushed)
 *
 * NB most hardware supports MSI interrupts
 */
static inline void falcon_irq_ack_a1(struct efx_nic *efx)
{
	efx_dword_t reg;

	EFX_POPULATE_DWORD_1(reg, FRF_AA_INT_ACK_KER_FIELD, 0xb7eb7e);
	efx_writed(efx, &reg, FR_AA_INT_ACK_KER);
	efx_readd(efx, &reg, FR_AA_WORK_AROUND_BROKEN_PCI_READS);
}

static irqreturn_t falcon_legacy_interrupt_a1(int irq, void *dev_id)
{
	struct efx_nic *efx = dev_id;
	efx_oword_t *int_ker = efx->irq_status.addr;
	int syserr;
	int queues;

	/* Check to see if this is our interrupt.  If it isn't, we
	 * exit without having touched the hardware.
	 */
	if (unlikely(EFX_OWORD_IS_ZERO(*int_ker))) {
		netif_vdbg(efx, intr, efx->net_dev,
			   "IRQ %d on CPU %d not for me\n", irq,
			   raw_smp_processor_id());
		return IRQ_NONE;
	}
	efx->last_irq_cpu = raw_smp_processor_id();
	netif_vdbg(efx, intr, efx->net_dev,
		   "IRQ %d on CPU %d status " EFX_OWORD_FMT "\n",
		   irq, raw_smp_processor_id(), EFX_OWORD_VAL(*int_ker));

	if (!likely(ACCESS_ONCE(efx->irq_soft_enabled)))
		return IRQ_HANDLED;

	/* Check to see if we have a serious error condition */
	syserr = EFX_OWORD_FIELD(*int_ker, FSF_AZ_NET_IVEC_FATAL_INT);
	if (unlikely(syserr))
		return efx_farch_fatal_interrupt(efx);

	/* Determine interrupting queues, clear interrupt status
	 * register and acknowledge the device interrupt.
	 */
	BUILD_BUG_ON(FSF_AZ_NET_IVEC_INT_Q_WIDTH > EFX_MAX_CHANNELS);
	queues = EFX_OWORD_FIELD(*int_ker, FSF_AZ_NET_IVEC_INT_Q);
	EFX_ZERO_OWORD(*int_ker);
	wmb(); /* Ensure the vector is cleared before interrupt ack */
	falcon_irq_ack_a1(efx);

	if (queues & 1)
		efx_schedule_channel_irq(efx_get_channel(efx, 0));
	if (queues & 2)
		efx_schedule_channel_irq(efx_get_channel(efx, 1));
	return IRQ_HANDLED;
}

/**************************************************************************
 *
 * RSS
 *
 **************************************************************************
 */
static int dummy_rx_push_rss_config(struct efx_nic *efx, bool user,
				    const u32 *rx_indir_table)
{
	(void) efx;
	(void) user;
	(void) rx_indir_table;
	return -ENOSYS;
}

static int falcon_b0_rx_push_rss_config(struct efx_nic *efx, bool user,
					const u32 *rx_indir_table)
{
	efx_oword_t temp;

	(void) user;
	/* Set hash key for IPv4 */
	memcpy(&temp, efx->rx_hash_key, sizeof(temp));
	efx_writeo(efx, &temp, FR_BZ_RX_RSS_TKEY);

	memcpy(efx->rx_indir_table, rx_indir_table,
	       sizeof(efx->rx_indir_table));
	efx_farch_rx_push_indir_table(efx);
	return 0;
}

/**************************************************************************
 *
 * EEPROM/flash
 *
 **************************************************************************
 */

#define FALCON_SPI_MAX_LEN sizeof(efx_oword_t)

static int falcon_spi_poll(struct efx_nic *efx)
{
	efx_oword_t reg;
	efx_reado(efx, &reg, FR_AB_EE_SPI_HCMD);
	return EFX_OWORD_FIELD(reg, FRF_AB_EE_SPI_HCMD_CMD_EN) ? -EBUSY : 0;
}

/* Wait for SPI command completion */
static int falcon_spi_wait(struct efx_nic *efx)
{
	/* Most commands will finish quickly, so we start polling at
	 * very short intervals.  Sometimes the command may have to
	 * wait for VPD or expansion ROM access outside of our
	 * control, so we allow up to 100 ms. */
	unsigned long timeout = jiffies + 1 + DIV_ROUND_UP(HZ, 10);
	int i;

	for (i = 0; i < 10; i++) {
		if (!falcon_spi_poll(efx))
			return 0;
		udelay(10);
	}

	for (;;) {
		if (!falcon_spi_poll(efx))
			return 0;
		if (time_after_eq(jiffies, timeout)) {
			netif_err(efx, hw, efx->net_dev,
				  "timed out waiting for SPI\n");
			return -ETIMEDOUT;
		}
		schedule_timeout_uninterruptible(1);
	}
}

static int
falcon_spi_cmd(struct efx_nic *efx, const struct falcon_spi_device *spi,
	       unsigned int command, int address,
	       const void *in, void *out, size_t len)
{
	bool addressed = (address >= 0);
	bool reading = (out != NULL);
	efx_oword_t reg;
	int rc;

	/* Input validation */
	if (len > FALCON_SPI_MAX_LEN)
		return -EINVAL;

	/* Check that previous command is not still running */
	rc = falcon_spi_poll(efx);
	if (rc)
		return rc;

	/* Program address register, if we have an address */
	if (addressed) {
		EFX_POPULATE_OWORD_1(reg, FRF_AB_EE_SPI_HADR_ADR, address);
		efx_writeo(efx, &reg, FR_AB_EE_SPI_HADR);
	}

	/* Program data register, if we have data */
	if (in != NULL) {
		memcpy(&reg, in, len);
		efx_writeo(efx, &reg, FR_AB_EE_SPI_HDATA);
	}

	/* Issue read/write command */
	EFX_POPULATE_OWORD_7(reg,
			     FRF_AB_EE_SPI_HCMD_CMD_EN, 1,
			     FRF_AB_EE_SPI_HCMD_SF_SEL, spi->device_id,
			     FRF_AB_EE_SPI_HCMD_DABCNT, len,
			     FRF_AB_EE_SPI_HCMD_READ, reading,
			     FRF_AB_EE_SPI_HCMD_DUBCNT, 0,
			     FRF_AB_EE_SPI_HCMD_ADBCNT,
			     (addressed ? spi->addr_len : 0),
			     FRF_AB_EE_SPI_HCMD_ENC, command);
	efx_writeo(efx, &reg, FR_AB_EE_SPI_HCMD);

	/* Wait for read/write to complete */
	rc = falcon_spi_wait(efx);
	if (rc)
		return rc;

	/* Read data */
	if (out != NULL) {
		efx_reado(efx, &reg, FR_AB_EE_SPI_HDATA);
		memcpy(out, &reg, len);
	}

	return 0;
}

static inline u8
falcon_spi_munge_command(const struct falcon_spi_device *spi,
			 const u8 command, const unsigned int address)
{
	return command | (((address >> 8) & spi->munge_address) << 3);
}

static int
falcon_spi_read(struct efx_nic *efx, const struct falcon_spi_device *spi,
		loff_t start, size_t len, size_t *retlen, u8 *buffer)
{
	size_t block_len, pos = 0;
	unsigned int command;
	int rc = 0;

	while (pos < len) {
		block_len = min(len - pos, FALCON_SPI_MAX_LEN);

		command = falcon_spi_munge_command(spi, SPI_READ, start + pos);
		rc = falcon_spi_cmd(efx, spi, command, start + pos, NULL,
				    buffer + pos, block_len);
		if (rc)
			break;
		pos += block_len;

		/* Avoid locking up the system */
		cond_resched();
		if (signal_pending(current)) {
			rc = -EINTR;
			break;
		}
	}

	if (retlen)
		*retlen = pos;
	return rc;
}

#ifdef CONFIG_SFC_MTD

struct falcon_mtd_partition {
	struct efx_mtd_partition common;
	const struct falcon_spi_device *spi;
	size_t offset;
};

#define to_falcon_mtd_partition(mtd)				\
	container_of(mtd, struct falcon_mtd_partition, common.mtd)

static size_t
falcon_spi_write_limit(const struct falcon_spi_device *spi, size_t start)
{
	return min(FALCON_SPI_MAX_LEN,
		   (spi->block_size - (start & (spi->block_size - 1))));
}

/* Wait up to 10 ms for buffered write completion */
static int
falcon_spi_wait_write(struct efx_nic *efx, const struct falcon_spi_device *spi)
{
	unsigned long timeout = jiffies + 1 + DIV_ROUND_UP(HZ, 100);
	u8 status;
	int rc;

	for (;;) {
		rc = falcon_spi_cmd(efx, spi, SPI_RDSR, -1, NULL,
				    &status, sizeof(status));
		if (rc)
			return rc;
		if (!(status & SPI_STATUS_NRDY))
			return 0;
		if (time_after_eq(jiffies, timeout)) {
			netif_err(efx, hw, efx->net_dev,
				  "SPI write timeout on device %d"
				  " last status=0x%02x\n",
				  spi->device_id, status);
			return -ETIMEDOUT;
		}
		schedule_timeout_uninterruptible(1);
	}
}

static int
falcon_spi_write(struct efx_nic *efx, const struct falcon_spi_device *spi,
		 loff_t start, size_t len, size_t *retlen, const u8 *buffer)
{
	u8 verify_buffer[FALCON_SPI_MAX_LEN];
	size_t block_len, pos = 0;
	unsigned int command;
	int rc = 0;

	while (pos < len) {
		rc = falcon_spi_cmd(efx, spi, SPI_WREN, -1, NULL, NULL, 0);
		if (rc)
			break;

		block_len = min(len - pos,
				falcon_spi_write_limit(spi, start + pos));
		command = falcon_spi_munge_command(spi, SPI_WRITE, start + pos);
		rc = falcon_spi_cmd(efx, spi, command, start + pos,
				    buffer + pos, NULL, block_len);
		if (rc)
			break;

		rc = falcon_spi_wait_write(efx, spi);
		if (rc)
			break;

		command = falcon_spi_munge_command(spi, SPI_READ, start + pos);
		rc = falcon_spi_cmd(efx, spi, command, start + pos,
				    NULL, verify_buffer, block_len);
		if (memcmp(verify_buffer, buffer + pos, block_len)) {
			rc = -EIO;
			break;
		}

		pos += block_len;

		/* Avoid locking up the system */
		cond_resched();
		if (signal_pending(current)) {
			rc = -EINTR;
			break;
		}
	}

	if (retlen)
		*retlen = pos;
	return rc;
}

static int
falcon_spi_slow_wait(struct falcon_mtd_partition *part, bool uninterruptible)
{
	const struct falcon_spi_device *spi = part->spi;
	struct efx_nic *efx = part->common.mtd.priv;
	u8 status;
	int rc, i;

	/* Wait up to 4s for flash/EEPROM to finish a slow operation. */
	for (i = 0; i < 40; i++) {
		__set_current_state(uninterruptible ?
				    TASK_UNINTERRUPTIBLE : TASK_INTERRUPTIBLE);
		schedule_timeout(HZ / 10);
		rc = falcon_spi_cmd(efx, spi, SPI_RDSR, -1, NULL,
				    &status, sizeof(status));
		if (rc)
			return rc;
		if (!(status & SPI_STATUS_NRDY))
			return 0;
		if (signal_pending(current))
			return -EINTR;
	}
	pr_err("%s: timed out waiting for %s\n",
	       part->common.name, part->common.dev_type_name);
	return -ETIMEDOUT;
}

static int
falcon_spi_unlock(struct efx_nic *efx, const struct falcon_spi_device *spi)
{
	const u8 unlock_mask = (SPI_STATUS_BP2 | SPI_STATUS_BP1 |
				SPI_STATUS_BP0);
	u8 status;
	int rc;

	rc = falcon_spi_cmd(efx, spi, SPI_RDSR, -1, NULL,
			    &status, sizeof(status));
	if (rc)
		return rc;

	if (!(status & unlock_mask))
		return 0; /* already unlocked */

	rc = falcon_spi_cmd(efx, spi, SPI_WREN, -1, NULL, NULL, 0);
	if (rc)
		return rc;
	rc = falcon_spi_cmd(efx, spi, SPI_SST_EWSR, -1, NULL, NULL, 0);
	if (rc)
		return rc;

	status &= ~unlock_mask;
	rc = falcon_spi_cmd(efx, spi, SPI_WRSR, -1, &status,
			    NULL, sizeof(status));
	if (rc)
		return rc;
	rc = falcon_spi_wait_write(efx, spi);
	if (rc)
		return rc;

	return 0;
}

#define FALCON_SPI_VERIFY_BUF_LEN 16

static int
falcon_spi_erase(struct falcon_mtd_partition *part, loff_t start, size_t len)
{
	const struct falcon_spi_device *spi = part->spi;
	struct efx_nic *efx = part->common.mtd.priv;
	unsigned pos, block_len;
	u8 empty[FALCON_SPI_VERIFY_BUF_LEN];
	u8 buffer[FALCON_SPI_VERIFY_BUF_LEN];
	int rc;

	if (len != spi->erase_size)
		return -EINVAL;

	if (spi->erase_command == 0)
		return -EOPNOTSUPP;

	rc = falcon_spi_unlock(efx, spi);
	if (rc)
		return rc;
	rc = falcon_spi_cmd(efx, spi, SPI_WREN, -1, NULL, NULL, 0);
	if (rc)
		return rc;
	rc = falcon_spi_cmd(efx, spi, spi->erase_command, start, NULL,
			    NULL, 0);
	if (rc)
		return rc;
	rc = falcon_spi_slow_wait(part, false);

	/* Verify the entire region has been wiped */
	memset(empty, 0xff, sizeof(empty));
	for (pos = 0; pos < len; pos += block_len) {
		block_len = min(len - pos, sizeof(buffer));
		rc = falcon_spi_read(efx, spi, start + pos, block_len,
				     NULL, buffer);
		if (rc)
			return rc;
		if (memcmp(empty, buffer, block_len))
			return -EIO;

		/* Avoid locking up the system */
		cond_resched();
		if (signal_pending(current))
			return -EINTR;
	}

	return rc;
}

static void falcon_mtd_rename(struct efx_mtd_partition *part)
{
	struct efx_nic *efx = part->mtd.priv;

	snprintf(part->name, sizeof(part->name), "%s %s",
		 efx->name, part->type_name);
}

static int falcon_mtd_read(struct mtd_info *mtd, loff_t start,
			   size_t len, size_t *retlen, u8 *buffer)
{
	struct falcon_mtd_partition *part = to_falcon_mtd_partition(mtd);
	struct efx_nic *efx = mtd->priv;
	struct falcon_nic_data *nic_data = efx->nic_data;
	int rc;

	rc = mutex_lock_interruptible(&nic_data->spi_lock);
	if (rc)
		return rc;
	rc = falcon_spi_read(efx, part->spi, part->offset + start,
			     len, retlen, buffer);
	mutex_unlock(&nic_data->spi_lock);
	return rc;
}

static int falcon_mtd_erase(struct mtd_info *mtd, loff_t start, size_t len)
{
	struct falcon_mtd_partition *part = to_falcon_mtd_partition(mtd);
	struct efx_nic *efx = mtd->priv;
	struct falcon_nic_data *nic_data = efx->nic_data;
	int rc;

	rc = mutex_lock_interruptible(&nic_data->spi_lock);
	if (rc)
		return rc;
	rc = falcon_spi_erase(part, part->offset + start, len);
	mutex_unlock(&nic_data->spi_lock);
	return rc;
}

static int falcon_mtd_write(struct mtd_info *mtd, loff_t start,
			    size_t len, size_t *retlen, const u8 *buffer)
{
	struct falcon_mtd_partition *part = to_falcon_mtd_partition(mtd);
	struct efx_nic *efx = mtd->priv;
	struct falcon_nic_data *nic_data = efx->nic_data;
	int rc;

	rc = mutex_lock_interruptible(&nic_data->spi_lock);
	if (rc)
		return rc;
	rc = falcon_spi_write(efx, part->spi, part->offset + start,
			      len, retlen, buffer);
	mutex_unlock(&nic_data->spi_lock);
	return rc;
}

static int falcon_mtd_sync(struct mtd_info *mtd)
{
	struct falcon_mtd_partition *part = to_falcon_mtd_partition(mtd);
	struct efx_nic *efx = mtd->priv;
	struct falcon_nic_data *nic_data = efx->nic_data;
	int rc;

	mutex_lock(&nic_data->spi_lock);
	rc = falcon_spi_slow_wait(part, true);
	mutex_unlock(&nic_data->spi_lock);
	return rc;
}

static int falcon_mtd_probe(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	struct falcon_mtd_partition *parts;
	struct falcon_spi_device *spi;
	size_t n_parts;
	int rc = -ENODEV;

	ASSERT_RTNL();

	/* Allocate space for maximum number of partitions */
	parts = kcalloc(2, sizeof(*parts), GFP_KERNEL);
	if (!parts)
		return -ENOMEM;
	n_parts = 0;

	spi = &nic_data->spi_flash;
	if (falcon_spi_present(spi) && spi->size > FALCON_FLASH_BOOTCODE_START) {
		parts[n_parts].spi = spi;
		parts[n_parts].offset = FALCON_FLASH_BOOTCODE_START;
		parts[n_parts].common.dev_type_name = "flash";
		parts[n_parts].common.type_name = "sfc_flash_bootrom";
		parts[n_parts].common.mtd.type = MTD_NORFLASH;
		parts[n_parts].common.mtd.flags = MTD_CAP_NORFLASH;
		parts[n_parts].common.mtd.size = spi->size - FALCON_FLASH_BOOTCODE_START;
		parts[n_parts].common.mtd.erasesize = spi->erase_size;
		n_parts++;
	}

	spi = &nic_data->spi_eeprom;
	if (falcon_spi_present(spi) && spi->size > FALCON_EEPROM_BOOTCONFIG_START) {
		parts[n_parts].spi = spi;
		parts[n_parts].offset = FALCON_EEPROM_BOOTCONFIG_START;
		parts[n_parts].common.dev_type_name = "EEPROM";
		parts[n_parts].common.type_name = "sfc_bootconfig";
		parts[n_parts].common.mtd.type = MTD_RAM;
		parts[n_parts].common.mtd.flags = MTD_CAP_RAM;
		parts[n_parts].common.mtd.size =
			min(spi->size, FALCON_EEPROM_BOOTCONFIG_END) -
			FALCON_EEPROM_BOOTCONFIG_START;
		parts[n_parts].common.mtd.erasesize = spi->erase_size;
		n_parts++;
	}

	rc = efx_mtd_add(efx, &parts[0].common, n_parts, sizeof(*parts));
	if (rc)
		kfree(parts);
	return rc;
}

#endif /* CONFIG_SFC_MTD */

/**************************************************************************
 *
 * XMAC operations
 *
 **************************************************************************
 */

/* Configure the XAUI driver that is an output from Falcon */
static void falcon_setup_xaui(struct efx_nic *efx)
{
	efx_oword_t sdctl, txdrv;

	/* Move the XAUI into low power, unless there is no PHY, in
	 * which case the XAUI will have to drive a cable. */
	if (efx->phy_type == PHY_TYPE_NONE)
		return;

	efx_reado(efx, &sdctl, FR_AB_XX_SD_CTL);
	EFX_SET_OWORD_FIELD(sdctl, FRF_AB_XX_HIDRVD, FFE_AB_XX_SD_CTL_DRV_DEF);
	EFX_SET_OWORD_FIELD(sdctl, FRF_AB_XX_LODRVD, FFE_AB_XX_SD_CTL_DRV_DEF);
	EFX_SET_OWORD_FIELD(sdctl, FRF_AB_XX_HIDRVC, FFE_AB_XX_SD_CTL_DRV_DEF);
	EFX_SET_OWORD_FIELD(sdctl, FRF_AB_XX_LODRVC, FFE_AB_XX_SD_CTL_DRV_DEF);
	EFX_SET_OWORD_FIELD(sdctl, FRF_AB_XX_HIDRVB, FFE_AB_XX_SD_CTL_DRV_DEF);
	EFX_SET_OWORD_FIELD(sdctl, FRF_AB_XX_LODRVB, FFE_AB_XX_SD_CTL_DRV_DEF);
	EFX_SET_OWORD_FIELD(sdctl, FRF_AB_XX_HIDRVA, FFE_AB_XX_SD_CTL_DRV_DEF);
	EFX_SET_OWORD_FIELD(sdctl, FRF_AB_XX_LODRVA, FFE_AB_XX_SD_CTL_DRV_DEF);
	efx_writeo(efx, &sdctl, FR_AB_XX_SD_CTL);

	EFX_POPULATE_OWORD_8(txdrv,
			     FRF_AB_XX_DEQD, FFE_AB_XX_TXDRV_DEQ_DEF,
			     FRF_AB_XX_DEQC, FFE_AB_XX_TXDRV_DEQ_DEF,
			     FRF_AB_XX_DEQB, FFE_AB_XX_TXDRV_DEQ_DEF,
			     FRF_AB_XX_DEQA, FFE_AB_XX_TXDRV_DEQ_DEF,
			     FRF_AB_XX_DTXD, FFE_AB_XX_TXDRV_DTX_DEF,
			     FRF_AB_XX_DTXC, FFE_AB_XX_TXDRV_DTX_DEF,
			     FRF_AB_XX_DTXB, FFE_AB_XX_TXDRV_DTX_DEF,
			     FRF_AB_XX_DTXA, FFE_AB_XX_TXDRV_DTX_DEF);
	efx_writeo(efx, &txdrv, FR_AB_XX_TXDRV_CTL);
}

int falcon_reset_xaui(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	efx_oword_t reg;
	int count;

	/* Don't fetch MAC statistics over an XMAC reset */
	WARN_ON(nic_data->stats_disable_count == 0);

	/* Start reset sequence */
	EFX_POPULATE_OWORD_1(reg, FRF_AB_XX_RST_XX_EN, 1);
	efx_writeo(efx, &reg, FR_AB_XX_PWR_RST);

	/* Wait up to 10 ms for completion, then reinitialise */
	for (count = 0; count < 1000; count++) {
		efx_reado(efx, &reg, FR_AB_XX_PWR_RST);
		if (EFX_OWORD_FIELD(reg, FRF_AB_XX_RST_XX_EN) == 0 &&
		    EFX_OWORD_FIELD(reg, FRF_AB_XX_SD_RST_ACT) == 0) {
			falcon_setup_xaui(efx);
			return 0;
		}
		udelay(10);
	}
	netif_err(efx, hw, efx->net_dev,
		  "timed out waiting for XAUI/XGXS reset\n");
	return -ETIMEDOUT;
}

static void falcon_ack_status_intr(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	efx_oword_t reg;

	if ((efx_nic_rev(efx) != EFX_REV_FALCON_B0) || LOOPBACK_INTERNAL(efx))
		return;

	/* We expect xgmii faults if the wireside link is down */
	if (!efx->link_state.up)
		return;

	/* We can only use this interrupt to signal the negative edge of
	 * xaui_align [we have to poll the positive edge]. */
	if (nic_data->xmac_poll_required)
		return;

	efx_reado(efx, &reg, FR_AB_XM_MGT_INT_MSK);
}

static bool falcon_xgxs_link_ok(struct efx_nic *efx)
{
	efx_oword_t reg;
	bool align_done, link_ok = false;
	int sync_status;

	/* Read link status */
	efx_reado(efx, &reg, FR_AB_XX_CORE_STAT);

	align_done = EFX_OWORD_FIELD(reg, FRF_AB_XX_ALIGN_DONE);
	sync_status = EFX_OWORD_FIELD(reg, FRF_AB_XX_SYNC_STAT);
	if (align_done && (sync_status == FFE_AB_XX_STAT_ALL_LANES))
		link_ok = true;

	/* Clear link status ready for next read */
	EFX_SET_OWORD_FIELD(reg, FRF_AB_XX_COMMA_DET, FFE_AB_XX_STAT_ALL_LANES);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_XX_CHAR_ERR, FFE_AB_XX_STAT_ALL_LANES);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_XX_DISPERR, FFE_AB_XX_STAT_ALL_LANES);
	efx_writeo(efx, &reg, FR_AB_XX_CORE_STAT);

	return link_ok;
}

static bool falcon_xmac_link_ok(struct efx_nic *efx)
{
	/*
	 * Check MAC's XGXS link status except when using XGMII loopback
	 * which bypasses the XGXS block.
	 * If possible, check PHY's XGXS link status except when using
	 * MAC loopback.
	 */
	return (efx->loopback_mode == LOOPBACK_XGMII ||
		falcon_xgxs_link_ok(efx)) &&
		(!(efx->mdio.mmds & (1 << MDIO_MMD_PHYXS)) ||
		 LOOPBACK_INTERNAL(efx) ||
		 efx_mdio_phyxgxs_lane_sync(efx));
}

static void falcon_reconfigure_xmac_core(struct efx_nic *efx)
{
	unsigned int max_frame_len;
	efx_oword_t reg;
	bool rx_fc = !!(efx->link_state.fc & EFX_FC_RX);
	bool tx_fc = !!(efx->link_state.fc & EFX_FC_TX);

	/* Configure MAC  - cut-thru mode is hard wired on */
	EFX_POPULATE_OWORD_3(reg,
			     FRF_AB_XM_RX_JUMBO_MODE, 1,
			     FRF_AB_XM_TX_STAT_EN, 1,
			     FRF_AB_XM_RX_STAT_EN, 1);
	efx_writeo(efx, &reg, FR_AB_XM_GLB_CFG);

	/* Configure TX */
	EFX_POPULATE_OWORD_6(reg,
			     FRF_AB_XM_TXEN, 1,
			     FRF_AB_XM_TX_PRMBL, 1,
			     FRF_AB_XM_AUTO_PAD, 1,
			     FRF_AB_XM_TXCRC, 1,
			     FRF_AB_XM_FCNTL, tx_fc,
			     FRF_AB_XM_IPG, 0x3);
	efx_writeo(efx, &reg, FR_AB_XM_TX_CFG);

	/* Configure RX */
	EFX_POPULATE_OWORD_5(reg,
			     FRF_AB_XM_RXEN, 1,
			     FRF_AB_XM_AUTO_DEPAD, 0,
			     FRF_AB_XM_ACPT_ALL_MCAST, 1,
			     FRF_AB_XM_ACPT_ALL_UCAST, !efx->unicast_filter,
			     FRF_AB_XM_PASS_CRC_ERR, 1);
	efx_writeo(efx, &reg, FR_AB_XM_RX_CFG);

	/* Set frame length */
	max_frame_len = EFX_MAX_FRAME_LEN(efx->net_dev->mtu);
	EFX_POPULATE_OWORD_1(reg, FRF_AB_XM_MAX_RX_FRM_SIZE, max_frame_len);
	efx_writeo(efx, &reg, FR_AB_XM_RX_PARAM);
	EFX_POPULATE_OWORD_2(reg,
			     FRF_AB_XM_MAX_TX_FRM_SIZE, max_frame_len,
			     FRF_AB_XM_TX_JUMBO_MODE, 1);
	efx_writeo(efx, &reg, FR_AB_XM_TX_PARAM);

	EFX_POPULATE_OWORD_2(reg,
			     FRF_AB_XM_PAUSE_TIME, 0xfffe, /* MAX PAUSE TIME */
			     FRF_AB_XM_DIS_FCNTL, !rx_fc);
	efx_writeo(efx, &reg, FR_AB_XM_FC);

	/* Set MAC address */
	memcpy(&reg, &efx->net_dev->dev_addr[0], 4);
	efx_writeo(efx, &reg, FR_AB_XM_ADR_LO);
	memcpy(&reg, &efx->net_dev->dev_addr[4], 2);
	efx_writeo(efx, &reg, FR_AB_XM_ADR_HI);
}

static void falcon_reconfigure_xgxs_core(struct efx_nic *efx)
{
	efx_oword_t reg;
	bool xgxs_loopback = (efx->loopback_mode == LOOPBACK_XGXS);
	bool xaui_loopback = (efx->loopback_mode == LOOPBACK_XAUI);
	bool xgmii_loopback = (efx->loopback_mode == LOOPBACK_XGMII);
	bool old_xgmii_loopback, old_xgxs_loopback, old_xaui_loopback;

	/* XGXS block is flaky and will need to be reset if moving
	 * into our out of XGMII, XGXS or XAUI loopbacks. */
	efx_reado(efx, &reg, FR_AB_XX_CORE_STAT);
	old_xgxs_loopback = EFX_OWORD_FIELD(reg, FRF_AB_XX_XGXS_LB_EN);
	old_xgmii_loopback = EFX_OWORD_FIELD(reg, FRF_AB_XX_XGMII_LB_EN);

	efx_reado(efx, &reg, FR_AB_XX_SD_CTL);
	old_xaui_loopback = EFX_OWORD_FIELD(reg, FRF_AB_XX_LPBKA);

	/* The PHY driver may have turned XAUI off */
	if ((xgxs_loopback != old_xgxs_loopback) ||
	    (xaui_loopback != old_xaui_loopback) ||
	    (xgmii_loopback != old_xgmii_loopback))
		falcon_reset_xaui(efx);

	efx_reado(efx, &reg, FR_AB_XX_CORE_STAT);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_XX_FORCE_SIG,
			    (xgxs_loopback || xaui_loopback) ?
			    FFE_AB_XX_FORCE_SIG_ALL_LANES : 0);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_XX_XGXS_LB_EN, xgxs_loopback);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_XX_XGMII_LB_EN, xgmii_loopback);
	efx_writeo(efx, &reg, FR_AB_XX_CORE_STAT);

	efx_reado(efx, &reg, FR_AB_XX_SD_CTL);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_XX_LPBKD, xaui_loopback);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_XX_LPBKC, xaui_loopback);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_XX_LPBKB, xaui_loopback);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_XX_LPBKA, xaui_loopback);
	efx_writeo(efx, &reg, FR_AB_XX_SD_CTL);
}


/* Try to bring up the Falcon side of the Falcon-Phy XAUI link */
static bool falcon_xmac_link_ok_retry(struct efx_nic *efx, int tries)
{
	bool mac_up = falcon_xmac_link_ok(efx);

	if (LOOPBACK_MASK(efx) & LOOPBACKS_EXTERNAL(efx) & LOOPBACKS_WS ||
	    efx_phy_mode_disabled(efx->phy_mode))
		/* XAUI link is expected to be down */
		return mac_up;

	falcon_stop_nic_stats(efx);

	while (!mac_up && tries) {
		netif_dbg(efx, hw, efx->net_dev, "bashing xaui\n");
		falcon_reset_xaui(efx);
		udelay(200);

		mac_up = falcon_xmac_link_ok(efx);
		--tries;
	}

	falcon_start_nic_stats(efx);

	return mac_up;
}

static bool falcon_xmac_check_fault(struct efx_nic *efx)
{
	return !falcon_xmac_link_ok_retry(efx, 5);
}

static int falcon_reconfigure_xmac(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;

	efx_farch_filter_sync_rx_mode(efx);

	falcon_reconfigure_xgxs_core(efx);
	falcon_reconfigure_xmac_core(efx);

	falcon_reconfigure_mac_wrapper(efx);

	nic_data->xmac_poll_required = !falcon_xmac_link_ok_retry(efx, 5);
	falcon_ack_status_intr(efx);

	return 0;
}

static void falcon_poll_xmac(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;

	/* We expect xgmii faults if the wireside link is down */
	if (!efx->link_state.up || !nic_data->xmac_poll_required)
		return;

	nic_data->xmac_poll_required = !falcon_xmac_link_ok_retry(efx, 1);
	falcon_ack_status_intr(efx);
}

/**************************************************************************
 *
 * MAC wrapper
 *
 **************************************************************************
 */

static void falcon_push_multicast_hash(struct efx_nic *efx)
{
	union efx_multicast_hash *mc_hash = &efx->multicast_hash;

	WARN_ON(!mutex_is_locked(&efx->mac_lock));

	efx_writeo(efx, &mc_hash->oword[0], FR_AB_MAC_MC_HASH_REG0);
	efx_writeo(efx, &mc_hash->oword[1], FR_AB_MAC_MC_HASH_REG1);
}

static void falcon_reset_macs(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	efx_oword_t reg, mac_ctrl;
	int count;

	if (efx_nic_rev(efx) < EFX_REV_FALCON_B0) {
		/* It's not safe to use GLB_CTL_REG to reset the
		 * macs, so instead use the internal MAC resets
		 */
		EFX_POPULATE_OWORD_1(reg, FRF_AB_XM_CORE_RST, 1);
		efx_writeo(efx, &reg, FR_AB_XM_GLB_CFG);

		for (count = 0; count < 10000; count++) {
			efx_reado(efx, &reg, FR_AB_XM_GLB_CFG);
			if (EFX_OWORD_FIELD(reg, FRF_AB_XM_CORE_RST) ==
			    0)
				return;
			udelay(10);
		}

		netif_err(efx, hw, efx->net_dev,
			  "timed out waiting for XMAC core reset\n");
	}

	/* Mac stats will fail whist the TX fifo is draining */
	WARN_ON(nic_data->stats_disable_count == 0);

	efx_reado(efx, &mac_ctrl, FR_AB_MAC_CTRL);
	EFX_SET_OWORD_FIELD(mac_ctrl, FRF_BB_TXFIFO_DRAIN_EN, 1);
	efx_writeo(efx, &mac_ctrl, FR_AB_MAC_CTRL);

	efx_reado(efx, &reg, FR_AB_GLB_CTL);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_RST_XGTX, 1);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_RST_XGRX, 1);
	EFX_SET_OWORD_FIELD(reg, FRF_AB_RST_EM, 1);
	efx_writeo(efx, &reg, FR_AB_GLB_CTL);

	count = 0;
	while (1) {
		efx_reado(efx, &reg, FR_AB_GLB_CTL);
		if (!EFX_OWORD_FIELD(reg, FRF_AB_RST_XGTX) &&
		    !EFX_OWORD_FIELD(reg, FRF_AB_RST_XGRX) &&
		    !EFX_OWORD_FIELD(reg, FRF_AB_RST_EM)) {
			netif_dbg(efx, hw, efx->net_dev,
				  "Completed MAC reset after %d loops\n",
				  count);
			break;
		}
		if (count > 20) {
			netif_err(efx, hw, efx->net_dev, "MAC reset failed\n");
			break;
		}
		count++;
		udelay(10);
	}

	/* Ensure the correct MAC is selected before statistics
	 * are re-enabled by the caller */
	efx_writeo(efx, &mac_ctrl, FR_AB_MAC_CTRL);

	falcon_setup_xaui(efx);
}

static void falcon_drain_tx_fifo(struct efx_nic *efx)
{
	efx_oword_t reg;

	if ((efx_nic_rev(efx) < EFX_REV_FALCON_B0) ||
	    (efx->loopback_mode != LOOPBACK_NONE))
		return;

	efx_reado(efx, &reg, FR_AB_MAC_CTRL);
	/* There is no point in draining more than once */
	if (EFX_OWORD_FIELD(reg, FRF_BB_TXFIFO_DRAIN_EN))
		return;

	falcon_reset_macs(efx);
}

static void falcon_deconfigure_mac_wrapper(struct efx_nic *efx)
{
	efx_oword_t reg;

	if (efx_nic_rev(efx) < EFX_REV_FALCON_B0)
		return;

	/* Isolate the MAC -> RX */
	efx_reado(efx, &reg, FR_AZ_RX_CFG);
	EFX_SET_OWORD_FIELD(reg, FRF_BZ_RX_INGR_EN, 0);
	efx_writeo(efx, &reg, FR_AZ_RX_CFG);

	/* Isolate TX -> MAC */
	falcon_drain_tx_fifo(efx);
}

static void falcon_reconfigure_mac_wrapper(struct efx_nic *efx)
{
	struct efx_link_state *link_state = &efx->link_state;
	efx_oword_t reg;
	int link_speed, isolate;

	isolate = !!ACCESS_ONCE(efx->reset_pending);

	switch (link_state->speed) {
	case 10000: link_speed = 3; break;
	case 1000:  link_speed = 2; break;
	case 100:   link_speed = 1; break;
	default:    link_speed = 0; break;
	}

	/* MAC_LINK_STATUS controls MAC backpressure but doesn't work
	 * as advertised.  Disable to ensure packets are not
	 * indefinitely held and TX queue can be flushed at any point
	 * while the link is down. */
	EFX_POPULATE_OWORD_5(reg,
			     FRF_AB_MAC_XOFF_VAL, 0xffff /* max pause time */,
			     FRF_AB_MAC_BCAD_ACPT, 1,
			     FRF_AB_MAC_UC_PROM, !efx->unicast_filter,
			     FRF_AB_MAC_LINK_STATUS, 1, /* always set */
			     FRF_AB_MAC_SPEED, link_speed);
	/* On B0, MAC backpressure can be disabled and packets get
	 * discarded. */
	if (efx_nic_rev(efx) >= EFX_REV_FALCON_B0) {
		EFX_SET_OWORD_FIELD(reg, FRF_BB_TXFIFO_DRAIN_EN,
				    !link_state->up || isolate);
	}

	efx_writeo(efx, &reg, FR_AB_MAC_CTRL);

	/* Restore the multicast hash registers. */
	falcon_push_multicast_hash(efx);

	efx_reado(efx, &reg, FR_AZ_RX_CFG);
	/* Enable XOFF signal from RX FIFO (we enabled it during NIC
	 * initialisation but it may read back as 0) */
	EFX_SET_OWORD_FIELD(reg, FRF_AZ_RX_XOFF_MAC_EN, 1);
	/* Unisolate the MAC -> RX */
	if (efx_nic_rev(efx) >= EFX_REV_FALCON_B0)
		EFX_SET_OWORD_FIELD(reg, FRF_BZ_RX_INGR_EN, !isolate);
	efx_writeo(efx, &reg, FR_AZ_RX_CFG);
}

static void falcon_stats_request(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	efx_oword_t reg;

	WARN_ON(nic_data->stats_pending);
	WARN_ON(nic_data->stats_disable_count);

	FALCON_XMAC_STATS_DMA_FLAG(efx) = 0;
	nic_data->stats_pending = true;
	wmb(); /* ensure done flag is clear */

	/* Initiate DMA transfer of stats */
	EFX_POPULATE_OWORD_2(reg,
			     FRF_AB_MAC_STAT_DMA_CMD, 1,
			     FRF_AB_MAC_STAT_DMA_ADR,
			     efx->stats_buffer.dma_addr);
	efx_writeo(efx, &reg, FR_AB_MAC_STAT_DMA);

	mod_timer(&nic_data->stats_timer, round_jiffies_up(jiffies + HZ / 2));
}

static void falcon_stats_complete(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;

	if (!nic_data->stats_pending)
		return;

	nic_data->stats_pending = false;
	if (FALCON_XMAC_STATS_DMA_FLAG(efx)) {
		rmb(); /* read the done flag before the stats */
		efx_nic_update_stats(falcon_stat_desc, FALCON_STAT_COUNT,
				     falcon_stat_mask, nic_data->stats,
				     efx->stats_buffer.addr, true);
	} else {
		netif_err(efx, hw, efx->net_dev,
			  "timed out waiting for statistics\n");
	}
}

static void falcon_stats_timer_func(unsigned long context)
{
	struct efx_nic *efx = (struct efx_nic *)context;
	struct falcon_nic_data *nic_data = efx->nic_data;

	spin_lock(&efx->stats_lock);

	falcon_stats_complete(efx);
	if (nic_data->stats_disable_count == 0)
		falcon_stats_request(efx);

	spin_unlock(&efx->stats_lock);
}

static bool falcon_loopback_link_poll(struct efx_nic *efx)
{
	struct efx_link_state old_state = efx->link_state;

	WARN_ON(!mutex_is_locked(&efx->mac_lock));
	WARN_ON(!LOOPBACK_INTERNAL(efx));

	efx->link_state.fd = true;
	efx->link_state.fc = efx->wanted_fc;
	efx->link_state.up = true;
	efx->link_state.speed = 10000;

	return !efx_link_state_equal(&efx->link_state, &old_state);
}

static int falcon_reconfigure_port(struct efx_nic *efx)
{
	int rc;

	WARN_ON(efx_nic_rev(efx) > EFX_REV_FALCON_B0);

	/* Poll the PHY link state *before* reconfiguring it. This means we
	 * will pick up the correct speed (in loopback) to select the correct
	 * MAC.
	 */
	if (LOOPBACK_INTERNAL(efx))
		falcon_loopback_link_poll(efx);
	else
		efx->phy_op->poll(efx);

	falcon_stop_nic_stats(efx);
	falcon_deconfigure_mac_wrapper(efx);

	falcon_reset_macs(efx);

	efx->phy_op->reconfigure(efx);
	rc = falcon_reconfigure_xmac(efx);
	BUG_ON(rc);

	falcon_start_nic_stats(efx);

	/* Synchronise efx->link_state with the kernel */
	efx_link_status_changed(efx);

	return 0;
}

/* TX flow control may automatically turn itself off if the link
 * partner (intermittently) stops responding to pause frames. There
 * isn't any indication that this has happened, so the best we do is
 * leave it up to the user to spot this and fix it by cycling transmit
 * flow control on this end.
 */

static void falcon_a1_prepare_enable_fc_tx(struct efx_nic *efx)
{
	/* Schedule a reset to recover */
	efx_schedule_reset(efx, RESET_TYPE_INVISIBLE);
}

static void falcon_b0_prepare_enable_fc_tx(struct efx_nic *efx)
{
	/* Recover by resetting the EM block */
	falcon_stop_nic_stats(efx);
	falcon_drain_tx_fifo(efx);
	falcon_reconfigure_xmac(efx);
	falcon_start_nic_stats(efx);
}

/**************************************************************************
 *
 * PHY access via GMII
 *
 **************************************************************************
 */

/* Wait for GMII access to complete */
static int falcon_gmii_wait(struct efx_nic *efx)
{
	efx_oword_t md_stat;
	int count;

	/* wait up to 50ms - taken max from datasheet */
	for (count = 0; count < 5000; count++) {
		efx_reado(efx, &md_stat, FR_AB_MD_STAT);
		if (EFX_OWORD_FIELD(md_stat, FRF_AB_MD_BSY) == 0) {
			if (EFX_OWORD_FIELD(md_stat, FRF_AB_MD_LNFL) != 0 ||
			    EFX_OWORD_FIELD(md_stat, FRF_AB_MD_BSERR) != 0) {
				netif_err(efx, hw, efx->net_dev,
					  "error from GMII access "
					  EFX_OWORD_FMT"\n",
					  EFX_OWORD_VAL(md_stat));
				return -EIO;
			}
			return 0;
		}
		udelay(10);
	}
	netif_err(efx, hw, efx->net_dev, "timed out waiting for GMII\n");
	return -ETIMEDOUT;
}

/* Write an MDIO register of a PHY connected to Falcon. */
static int falcon_mdio_write(struct net_device *net_dev,
			     int prtad, int devad, u16 addr, u16 value)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct falcon_nic_data *nic_data = efx->nic_data;
	efx_oword_t reg;
	int rc;

	netif_vdbg(efx, hw, efx->net_dev,
		   "writing MDIO %d register %d.%d with 0x%04x\n",
		    prtad, devad, addr, value);

	mutex_lock(&nic_data->mdio_lock);

	/* Check MDIO not currently being accessed */
	rc = falcon_gmii_wait(efx);
	if (rc)
		goto out;

	/* Write the address/ID register */
	EFX_POPULATE_OWORD_1(reg, FRF_AB_MD_PHY_ADR, addr);
	efx_writeo(efx, &reg, FR_AB_MD_PHY_ADR);

	EFX_POPULATE_OWORD_2(reg, FRF_AB_MD_PRT_ADR, prtad,
			     FRF_AB_MD_DEV_ADR, devad);
	efx_writeo(efx, &reg, FR_AB_MD_ID);

	/* Write data */
	EFX_POPULATE_OWORD_1(reg, FRF_AB_MD_TXD, value);
	efx_writeo(efx, &reg, FR_AB_MD_TXD);

	EFX_POPULATE_OWORD_2(reg,
			     FRF_AB_MD_WRC, 1,
			     FRF_AB_MD_GC, 0);
	efx_writeo(efx, &reg, FR_AB_MD_CS);

	/* Wait for data to be written */
	rc = falcon_gmii_wait(efx);
	if (rc) {
		/* Abort the write operation */
		EFX_POPULATE_OWORD_2(reg,
				     FRF_AB_MD_WRC, 0,
				     FRF_AB_MD_GC, 1);
		efx_writeo(efx, &reg, FR_AB_MD_CS);
		udelay(10);
	}

out:
	mutex_unlock(&nic_data->mdio_lock);
	return rc;
}

/* Read an MDIO register of a PHY connected to Falcon. */
static int falcon_mdio_read(struct net_device *net_dev,
			    int prtad, int devad, u16 addr)
{
	struct efx_nic *efx = netdev_priv(net_dev);
	struct falcon_nic_data *nic_data = efx->nic_data;
	efx_oword_t reg;
	int rc;

	mutex_lock(&nic_data->mdio_lock);

	/* Check MDIO not currently being accessed */
	rc = falcon_gmii_wait(efx);
	if (rc)
		goto out;

	EFX_POPULATE_OWORD_1(reg, FRF_AB_MD_PHY_ADR, addr);
	efx_writeo(efx, &reg, FR_AB_MD_PHY_ADR);

	EFX_POPULATE_OWORD_2(reg, FRF_AB_MD_PRT_ADR, prtad,
			     FRF_AB_MD_DEV_ADR, devad);
	efx_writeo(efx, &reg, FR_AB_MD_ID);

	/* Request data to be read */
	EFX_POPULATE_OWORD_2(reg, FRF_AB_MD_RDC, 1, FRF_AB_MD_GC, 0);
	efx_writeo(efx, &reg, FR_AB_MD_CS);

	/* Wait for data to become available */
	rc = falcon_gmii_wait(efx);
	if (rc == 0) {
		efx_reado(efx, &reg, FR_AB_MD_RXD);
		rc = EFX_OWORD_FIELD(reg, FRF_AB_MD_RXD);
		netif_vdbg(efx, hw, efx->net_dev,
			   "read from MDIO %d register %d.%d, got %04x\n",
			   prtad, devad, addr, rc);
	} else {
		/* Abort the read operation */
		EFX_POPULATE_OWORD_2(reg,
				     FRF_AB_MD_RIC, 0,
				     FRF_AB_MD_GC, 1);
		efx_writeo(efx, &reg, FR_AB_MD_CS);

		netif_dbg(efx, hw, efx->net_dev,
			  "read from MDIO %d register %d.%d, got error %d\n",
			  prtad, devad, addr, rc);
	}

out:
	mutex_unlock(&nic_data->mdio_lock);
	return rc;
}

/* This call is responsible for hooking in the MAC and PHY operations */
static int falcon_probe_port(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	int rc;

	switch (efx->phy_type) {
	case PHY_TYPE_SFX7101:
		efx->phy_op = &falcon_sfx7101_phy_ops;
		break;
	case PHY_TYPE_QT2022C2:
	case PHY_TYPE_QT2025C:
		efx->phy_op = &falcon_qt202x_phy_ops;
		break;
	case PHY_TYPE_TXC43128:
		efx->phy_op = &falcon_txc_phy_ops;
		break;
	default:
		netif_err(efx, probe, efx->net_dev, "Unknown PHY type %d\n",
			  efx->phy_type);
		return -ENODEV;
	}

	/* Fill out MDIO structure and loopback modes */
	mutex_init(&nic_data->mdio_lock);
	efx->mdio.mdio_read = falcon_mdio_read;
	efx->mdio.mdio_write = falcon_mdio_write;
	rc = efx->phy_op->probe(efx);
	if (rc != 0)
		return rc;

	/* Initial assumption */
	efx->link_state.speed = 10000;
	efx->link_state.fd = true;

	/* Hardware flow ctrl. FalconA RX FIFO too small for pause generation */
	if (efx_nic_rev(efx) >= EFX_REV_FALCON_B0)
		efx->wanted_fc = EFX_FC_RX | EFX_FC_TX;
	else
		efx->wanted_fc = EFX_FC_RX;
	if (efx->mdio.mmds & MDIO_DEVS_AN)
		efx->wanted_fc |= EFX_FC_AUTO;

	/* Allocate buffer for stats */
	rc = efx_nic_alloc_buffer(efx, &efx->stats_buffer,
				  FALCON_MAC_STATS_SIZE, GFP_KERNEL);
	if (rc)
		return rc;
	netif_dbg(efx, probe, efx->net_dev,
		  "stats buffer at %llx (virt %p phys %llx)\n",
		  (u64)efx->stats_buffer.dma_addr,
		  efx->stats_buffer.addr,
		  (u64)virt_to_phys(efx->stats_buffer.addr));

	return 0;
}

static void falcon_remove_port(struct efx_nic *efx)
{
	efx->phy_op->remove(efx);
	efx_nic_free_buffer(efx, &efx->stats_buffer);
}

/* Global events are basically PHY events */
static bool
falcon_handle_global_event(struct efx_channel *channel, efx_qword_t *event)
{
	struct efx_nic *efx = channel->efx;
	struct falcon_nic_data *nic_data = efx->nic_data;

	if (EFX_QWORD_FIELD(*event, FSF_AB_GLB_EV_G_PHY0_INTR) ||
	    EFX_QWORD_FIELD(*event, FSF_AB_GLB_EV_XG_PHY0_INTR) ||
	    EFX_QWORD_FIELD(*event, FSF_AB_GLB_EV_XFP_PHY0_INTR))
		/* Ignored */
		return true;

	if ((efx_nic_rev(efx) == EFX_REV_FALCON_B0) &&
	    EFX_QWORD_FIELD(*event, FSF_BB_GLB_EV_XG_MGT_INTR)) {
		nic_data->xmac_poll_required = true;
		return true;
	}

	if (efx_nic_rev(efx) <= EFX_REV_FALCON_A1 ?
	    EFX_QWORD_FIELD(*event, FSF_AA_GLB_EV_RX_RECOVERY) :
	    EFX_QWORD_FIELD(*event, FSF_BB_GLB_EV_RX_RECOVERY)) {
		netif_err(efx, rx_err, efx->net_dev,
			  "channel %d seen global RX_RESET event. Resetting.\n",
			  channel->channel);

		atomic_inc(&efx->rx_reset);
		efx_schedule_reset(efx, EFX_WORKAROUND_6555(efx) ?
				   RESET_TYPE_RX_RECOVERY : RESET_TYPE_DISABLE);
		return true;
	}

	return false;
}

/**************************************************************************
 *
 * Falcon test code
 *
 **************************************************************************/

static int
falcon_read_nvram(struct efx_nic *efx, struct falcon_nvconfig *nvconfig_out)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	struct falcon_nvconfig *nvconfig;
	struct falcon_spi_device *spi;
	void *region;
	int rc, magic_num, struct_ver;
	__le16 *word, *limit;
	u32 csum;

	if (falcon_spi_present(&nic_data->spi_flash))
		spi = &nic_data->spi_flash;
	else if (falcon_spi_present(&nic_data->spi_eeprom))
		spi = &nic_data->spi_eeprom;
	else
		return -EINVAL;

	region = kmalloc(FALCON_NVCONFIG_END, GFP_KERNEL);
	if (!region)
		return -ENOMEM;
	nvconfig = region + FALCON_NVCONFIG_OFFSET;

	mutex_lock(&nic_data->spi_lock);
	rc = falcon_spi_read(efx, spi, 0, FALCON_NVCONFIG_END, NULL, region);
	mutex_unlock(&nic_data->spi_lock);
	if (rc) {
		netif_err(efx, hw, efx->net_dev, "Failed to read %s\n",
			  falcon_spi_present(&nic_data->spi_flash) ?
			  "flash" : "EEPROM");
		rc = -EIO;
		goto out;
	}

	magic_num = le16_to_cpu(nvconfig->board_magic_num);
	struct_ver = le16_to_cpu(nvconfig->board_struct_ver);

	rc = -EINVAL;
	if (magic_num != FALCON_NVCONFIG_BOARD_MAGIC_NUM) {
		netif_err(efx, hw, efx->net_dev,
			  "NVRAM bad magic 0x%x\n", magic_num);
		goto out;
	}
	if (struct_ver < 2) {
		netif_err(efx, hw, efx->net_dev,
			  "NVRAM has ancient version 0x%x\n", struct_ver);
		goto out;
	} else if (struct_ver < 4) {
		word = &nvconfig->board_magic_num;
		limit = (__le16 *) (nvconfig + 1);
	} else {
		word = region;
		limit = region + FALCON_NVCONFIG_END;
	}
	for (csum = 0; word < limit; ++word)
		csum += le16_to_cpu(*word);

	if (~csum & 0xffff) {
		netif_err(efx, hw, efx->net_dev,
			  "NVRAM has incorrect checksum\n");
		goto out;
	}

	rc = 0;
	if (nvconfig_out)
		memcpy(nvconfig_out, nvconfig, sizeof(*nvconfig));

 out:
	kfree(region);
	return rc;
}

static int falcon_test_nvram(struct efx_nic *efx)
{
	return falcon_read_nvram(efx, NULL);
}

static const struct efx_farch_register_test falcon_b0_register_tests[] = {
	{ FR_AZ_ADR_REGION,
	  EFX_OWORD32(0x0003FFFF, 0x0003FFFF, 0x0003FFFF, 0x0003FFFF) },
	{ FR_AZ_RX_CFG,
	  EFX_OWORD32(0xFFFFFFFE, 0x00017FFF, 0x00000000, 0x00000000) },
	{ FR_AZ_TX_CFG,
	  EFX_OWORD32(0x7FFF0037, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AZ_TX_RESERVED,
	  EFX_OWORD32(0xFFFEFE80, 0x1FFFFFFF, 0x020000FE, 0x007FFFFF) },
	{ FR_AB_MAC_CTRL,
	  EFX_OWORD32(0xFFFF0000, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AZ_SRM_TX_DC_CFG,
	  EFX_OWORD32(0x001FFFFF, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AZ_RX_DC_CFG,
	  EFX_OWORD32(0x0000000F, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AZ_RX_DC_PF_WM,
	  EFX_OWORD32(0x000003FF, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_BZ_DP_CTRL,
	  EFX_OWORD32(0x00000FFF, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AB_GM_CFG2,
	  EFX_OWORD32(0x00007337, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AB_GMF_CFG0,
	  EFX_OWORD32(0x00001F1F, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AB_XM_GLB_CFG,
	  EFX_OWORD32(0x00000C68, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AB_XM_TX_CFG,
	  EFX_OWORD32(0x00080164, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AB_XM_RX_CFG,
	  EFX_OWORD32(0x07100A0C, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AB_XM_RX_PARAM,
	  EFX_OWORD32(0x00001FF8, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AB_XM_FC,
	  EFX_OWORD32(0xFFFF0001, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AB_XM_ADR_LO,
	  EFX_OWORD32(0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000) },
	{ FR_AB_XX_SD_CTL,
	  EFX_OWORD32(0x0003FF0F, 0x00000000, 0x00000000, 0x00000000) },
};

static int
falcon_b0_test_chip(struct efx_nic *efx, struct efx_self_tests *tests)
{
	enum reset_type reset_method = RESET_TYPE_INVISIBLE;
	int rc, rc2;

	mutex_lock(&efx->mac_lock);
	if (efx->loopback_modes) {
		/* We need the 312 clock from the PHY to test the XMAC
		 * registers, so move into XGMII loopback if available */
		if (efx->loopback_modes & (1 << LOOPBACK_XGMII))
			efx->loopback_mode = LOOPBACK_XGMII;
		else
			efx->loopback_mode = __ffs(efx->loopback_modes);
	}
	__efx_reconfigure_port(efx);
	mutex_unlock(&efx->mac_lock);

	efx_reset_down(efx, reset_method);

	tests->registers =
		efx_farch_test_registers(efx, falcon_b0_register_tests,
					 ARRAY_SIZE(falcon_b0_register_tests))
		? -1 : 1;

	rc = falcon_reset_hw(efx, reset_method);
	rc2 = efx_reset_up(efx, reset_method, rc == 0);
	return rc ? rc : rc2;
}

/**************************************************************************
 *
 * Device reset
 *
 **************************************************************************
 */

static enum reset_type falcon_map_reset_reason(enum reset_type reason)
{
	switch (reason) {
	case RESET_TYPE_RX_RECOVERY:
	case RESET_TYPE_DMA_ERROR:
	case RESET_TYPE_TX_SKIP:
		/* These can occasionally occur due to hardware bugs.
		 * We try to reset without disrupting the link.
		 */
		return RESET_TYPE_INVISIBLE;
	default:
		return RESET_TYPE_ALL;
	}
}

static int falcon_map_reset_flags(u32 *flags)
{
	enum {
		FALCON_RESET_INVISIBLE = (ETH_RESET_DMA | ETH_RESET_FILTER |
					  ETH_RESET_OFFLOAD | ETH_RESET_MAC),
		FALCON_RESET_ALL = FALCON_RESET_INVISIBLE | ETH_RESET_PHY,
		FALCON_RESET_WORLD = FALCON_RESET_ALL | ETH_RESET_IRQ,
	};

	if ((*flags & FALCON_RESET_WORLD) == FALCON_RESET_WORLD) {
		*flags &= ~FALCON_RESET_WORLD;
		return RESET_TYPE_WORLD;
	}

	if ((*flags & FALCON_RESET_ALL) == FALCON_RESET_ALL) {
		*flags &= ~FALCON_RESET_ALL;
		return RESET_TYPE_ALL;
	}

	if ((*flags & FALCON_RESET_INVISIBLE) == FALCON_RESET_INVISIBLE) {
		*flags &= ~FALCON_RESET_INVISIBLE;
		return RESET_TYPE_INVISIBLE;
	}

	return -EINVAL;
}

/* Resets NIC to known state.  This routine must be called in process
 * context and is allowed to sleep. */
static int __falcon_reset_hw(struct efx_nic *efx, enum reset_type method)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	efx_oword_t glb_ctl_reg_ker;
	int rc;

	netif_dbg(efx, hw, efx->net_dev, "performing %s hardware reset\n",
		  RESET_TYPE(method));

	/* Initiate device reset */
	if (method == RESET_TYPE_WORLD) {
		rc = pci_save_state(efx->pci_dev);
		if (rc) {
			netif_err(efx, drv, efx->net_dev,
				  "failed to backup PCI state of primary "
				  "function prior to hardware reset\n");
			goto fail1;
		}
		if (efx_nic_is_dual_func(efx)) {
			rc = pci_save_state(nic_data->pci_dev2);
			if (rc) {
				netif_err(efx, drv, efx->net_dev,
					  "failed to backup PCI state of "
					  "secondary function prior to "
					  "hardware reset\n");
				goto fail2;
			}
		}

		EFX_POPULATE_OWORD_2(glb_ctl_reg_ker,
				     FRF_AB_EXT_PHY_RST_DUR,
				     FFE_AB_EXT_PHY_RST_DUR_10240US,
				     FRF_AB_SWRST, 1);
	} else {
		EFX_POPULATE_OWORD_7(glb_ctl_reg_ker,
				     /* exclude PHY from "invisible" reset */
				     FRF_AB_EXT_PHY_RST_CTL,
				     method == RESET_TYPE_INVISIBLE,
				     /* exclude EEPROM/flash and PCIe */
				     FRF_AB_PCIE_CORE_RST_CTL, 1,
				     FRF_AB_PCIE_NSTKY_RST_CTL, 1,
				     FRF_AB_PCIE_SD_RST_CTL, 1,
				     FRF_AB_EE_RST_CTL, 1,
				     FRF_AB_EXT_PHY_RST_DUR,
				     FFE_AB_EXT_PHY_RST_DUR_10240US,
				     FRF_AB_SWRST, 1);
	}
	efx_writeo(efx, &glb_ctl_reg_ker, FR_AB_GLB_CTL);

	netif_dbg(efx, hw, efx->net_dev, "waiting for hardware reset\n");
	schedule_timeout_uninterruptible(HZ / 20);

	/* Restore PCI configuration if needed */
	if (method == RESET_TYPE_WORLD) {
		if (efx_nic_is_dual_func(efx))
			pci_restore_state(nic_data->pci_dev2);
		pci_restore_state(efx->pci_dev);
		netif_dbg(efx, drv, efx->net_dev,
			  "successfully restored PCI config\n");
	}

	/* Assert that reset complete */
	efx_reado(efx, &glb_ctl_reg_ker, FR_AB_GLB_CTL);
	if (EFX_OWORD_FIELD(glb_ctl_reg_ker, FRF_AB_SWRST) != 0) {
		rc = -ETIMEDOUT;
		netif_err(efx, hw, efx->net_dev,
			  "timed out waiting for hardware reset\n");
		goto fail3;
	}
	netif_dbg(efx, hw, efx->net_dev, "hardware reset complete\n");

	return 0;

	/* pci_save_state() and pci_restore_state() MUST be called in pairs */
fail2:
	pci_restore_state(efx->pci_dev);
fail1:
fail3:
	return rc;
}

static int falcon_reset_hw(struct efx_nic *efx, enum reset_type method)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	int rc;

	mutex_lock(&nic_data->spi_lock);
	rc = __falcon_reset_hw(efx, method);
	mutex_unlock(&nic_data->spi_lock);

	return rc;
}

static void falcon_monitor(struct efx_nic *efx)
{
	bool link_changed;
	int rc;

	BUG_ON(!mutex_is_locked(&efx->mac_lock));

	rc = falcon_board(efx)->type->monitor(efx);
	if (rc) {
		netif_err(efx, hw, efx->net_dev,
			  "Board sensor %s; shutting down PHY\n",
			  (rc == -ERANGE) ? "reported fault" : "failed");
		efx->phy_mode |= PHY_MODE_LOW_POWER;
		rc = __efx_reconfigure_port(efx);
		WARN_ON(rc);
	}

	if (LOOPBACK_INTERNAL(efx))
		link_changed = falcon_loopback_link_poll(efx);
	else
		link_changed = efx->phy_op->poll(efx);

	if (link_changed) {
		falcon_stop_nic_stats(efx);
		falcon_deconfigure_mac_wrapper(efx);

		falcon_reset_macs(efx);
		rc = falcon_reconfigure_xmac(efx);
		BUG_ON(rc);

		falcon_start_nic_stats(efx);

		efx_link_status_changed(efx);
	}

	falcon_poll_xmac(efx);
}

/* Zeroes out the SRAM contents.  This routine must be called in
 * process context and is allowed to sleep.
 */
static int falcon_reset_sram(struct efx_nic *efx)
{
	efx_oword_t srm_cfg_reg_ker, gpio_cfg_reg_ker;
	int count;

	/* Set the SRAM wake/sleep GPIO appropriately. */
	efx_reado(efx, &gpio_cfg_reg_ker, FR_AB_GPIO_CTL);
	EFX_SET_OWORD_FIELD(gpio_cfg_reg_ker, FRF_AB_GPIO1_OEN, 1);
	EFX_SET_OWORD_FIELD(gpio_cfg_reg_ker, FRF_AB_GPIO1_OUT, 1);
	efx_writeo(efx, &gpio_cfg_reg_ker, FR_AB_GPIO_CTL);

	/* Initiate SRAM reset */
	EFX_POPULATE_OWORD_2(srm_cfg_reg_ker,
			     FRF_AZ_SRM_INIT_EN, 1,
			     FRF_AZ_SRM_NB_SZ, 0);
	efx_writeo(efx, &srm_cfg_reg_ker, FR_AZ_SRM_CFG);

	/* Wait for SRAM reset to complete */
	count = 0;
	do {
		netif_dbg(efx, hw, efx->net_dev,
			  "waiting for SRAM reset (attempt %d)...\n", count);

		/* SRAM reset is slow; expect around 16ms */
		schedule_timeout_uninterruptible(HZ / 50);

		/* Check for reset complete */
		efx_reado(efx, &srm_cfg_reg_ker, FR_AZ_SRM_CFG);
		if (!EFX_OWORD_FIELD(srm_cfg_reg_ker, FRF_AZ_SRM_INIT_EN)) {
			netif_dbg(efx, hw, efx->net_dev,
				  "SRAM reset complete\n");

			return 0;
		}
	} while (++count < 20);	/* wait up to 0.4 sec */

	netif_err(efx, hw, efx->net_dev, "timed out waiting for SRAM reset\n");
	return -ETIMEDOUT;
}

static void falcon_spi_device_init(struct efx_nic *efx,
				  struct falcon_spi_device *spi_device,
				  unsigned int device_id, u32 device_type)
{
	if (device_type != 0) {
		spi_device->device_id = device_id;
		spi_device->size =
			1 << SPI_DEV_TYPE_FIELD(device_type, SPI_DEV_TYPE_SIZE);
		spi_device->addr_len =
			SPI_DEV_TYPE_FIELD(device_type, SPI_DEV_TYPE_ADDR_LEN);
		spi_device->munge_address = (spi_device->size == 1 << 9 &&
					     spi_device->addr_len == 1);
		spi_device->erase_command =
			SPI_DEV_TYPE_FIELD(device_type, SPI_DEV_TYPE_ERASE_CMD);
		spi_device->erase_size =
			1 << SPI_DEV_TYPE_FIELD(device_type,
						SPI_DEV_TYPE_ERASE_SIZE);
		spi_device->block_size =
			1 << SPI_DEV_TYPE_FIELD(device_type,
						SPI_DEV_TYPE_BLOCK_SIZE);
	} else {
		spi_device->size = 0;
	}
}

/* Extract non-volatile configuration */
static int falcon_probe_nvconfig(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	struct falcon_nvconfig *nvconfig;
	int rc;

	nvconfig = kmalloc(sizeof(*nvconfig), GFP_KERNEL);
	if (!nvconfig)
		return -ENOMEM;

	rc = falcon_read_nvram(efx, nvconfig);
	if (rc)
		goto out;

	efx->phy_type = nvconfig->board_v2.port0_phy_type;
	efx->mdio.prtad = nvconfig->board_v2.port0_phy_addr;

	if (le16_to_cpu(nvconfig->board_struct_ver) >= 3) {
		falcon_spi_device_init(
			efx, &nic_data->spi_flash, FFE_AB_SPI_DEVICE_FLASH,
			le32_to_cpu(nvconfig->board_v3
				    .spi_device_type[FFE_AB_SPI_DEVICE_FLASH]));
		falcon_spi_device_init(
			efx, &nic_data->spi_eeprom, FFE_AB_SPI_DEVICE_EEPROM,
			le32_to_cpu(nvconfig->board_v3
				    .spi_device_type[FFE_AB_SPI_DEVICE_EEPROM]));
	}

	/* Read the MAC addresses */
	ether_addr_copy(efx->net_dev->perm_addr, nvconfig->mac_address[0]);

	netif_dbg(efx, probe, efx->net_dev, "PHY is %d phy_id %d\n",
		  efx->phy_type, efx->mdio.prtad);

	rc = falcon_probe_board(efx,
				le16_to_cpu(nvconfig->board_v2.board_revision));
out:
	kfree(nvconfig);
	return rc;
}

static int falcon_dimension_resources(struct efx_nic *efx)
{
	efx->rx_dc_base = 0x20000;
	efx->tx_dc_base = 0x26000;
	return 0;
}

/* Probe all SPI devices on the NIC */
static void falcon_probe_spi_devices(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	efx_oword_t nic_stat, gpio_ctl, ee_vpd_cfg;
	int boot_dev;

	efx_reado(efx, &gpio_ctl, FR_AB_GPIO_CTL);
	efx_reado(efx, &nic_stat, FR_AB_NIC_STAT);
	efx_reado(efx, &ee_vpd_cfg, FR_AB_EE_VPD_CFG0);

	if (EFX_OWORD_FIELD(gpio_ctl, FRF_AB_GPIO3_PWRUP_VALUE)) {
		boot_dev = (EFX_OWORD_FIELD(nic_stat, FRF_AB_SF_PRST) ?
			    FFE_AB_SPI_DEVICE_FLASH : FFE_AB_SPI_DEVICE_EEPROM);
		netif_dbg(efx, probe, efx->net_dev, "Booted from %s\n",
			  boot_dev == FFE_AB_SPI_DEVICE_FLASH ?
			  "flash" : "EEPROM");
	} else {
		/* Disable VPD and set clock dividers to safe
		 * values for initial programming. */
		boot_dev = -1;
		netif_dbg(efx, probe, efx->net_dev,
			  "Booted from internal ASIC settings;"
			  " setting SPI config\n");
		EFX_POPULATE_OWORD_3(ee_vpd_cfg, FRF_AB_EE_VPD_EN, 0,
				     /* 125 MHz / 7 ~= 20 MHz */
				     FRF_AB_EE_SF_CLOCK_DIV, 7,
				     /* 125 MHz / 63 ~= 2 MHz */
				     FRF_AB_EE_EE_CLOCK_DIV, 63);
		efx_writeo(efx, &ee_vpd_cfg, FR_AB_EE_VPD_CFG0);
	}

	mutex_init(&nic_data->spi_lock);

	if (boot_dev == FFE_AB_SPI_DEVICE_FLASH)
		falcon_spi_device_init(efx, &nic_data->spi_flash,
				       FFE_AB_SPI_DEVICE_FLASH,
				       default_flash_type);
	if (boot_dev == FFE_AB_SPI_DEVICE_EEPROM)
		falcon_spi_device_init(efx, &nic_data->spi_eeprom,
				       FFE_AB_SPI_DEVICE_EEPROM,
				       large_eeprom_type);
}

static unsigned int falcon_a1_mem_map_size(struct efx_nic *efx)
{
	return 0x20000;
}

static unsigned int falcon_b0_mem_map_size(struct efx_nic *efx)
{
	/* Map everything up to and including the RSS indirection table.
	 * The PCI core takes care of mapping the MSI-X tables.
	 */
	return FR_BZ_RX_INDIRECTION_TBL +
		FR_BZ_RX_INDIRECTION_TBL_STEP * FR_BZ_RX_INDIRECTION_TBL_ROWS;
}

static int falcon_probe_nic(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data;
	struct falcon_board *board;
	int rc;

	efx->primary = efx; /* only one usable function per controller */

	/* Allocate storage for hardware specific data */
	nic_data = kzalloc(sizeof(*nic_data), GFP_KERNEL);
	if (!nic_data)
		return -ENOMEM;
	efx->nic_data = nic_data;

	rc = -ENODEV;

	if (efx_farch_fpga_ver(efx) != 0) {
		netif_err(efx, probe, efx->net_dev,
			  "Falcon FPGA not supported\n");
		goto fail1;
	}

	if (efx_nic_rev(efx) <= EFX_REV_FALCON_A1) {
		efx_oword_t nic_stat;
		struct pci_dev *dev;
		u8 pci_rev = efx->pci_dev->revision;

		if ((pci_rev == 0xff) || (pci_rev == 0)) {
			netif_err(efx, probe, efx->net_dev,
				  "Falcon rev A0 not supported\n");
			goto fail1;
		}
		efx_reado(efx, &nic_stat, FR_AB_NIC_STAT);
		if (EFX_OWORD_FIELD(nic_stat, FRF_AB_STRAP_10G) == 0) {
			netif_err(efx, probe, efx->net_dev,
				  "Falcon rev A1 1G not supported\n");
			goto fail1;
		}
		if (EFX_OWORD_FIELD(nic_stat, FRF_AA_STRAP_PCIE) == 0) {
			netif_err(efx, probe, efx->net_dev,
				  "Falcon rev A1 PCI-X not supported\n");
			goto fail1;
		}

		dev = pci_dev_get(efx->pci_dev);
		while ((dev = pci_get_device(PCI_VENDOR_ID_SOLARFLARE,
					     PCI_DEVICE_ID_SOLARFLARE_SFC4000A_1,
					     dev))) {
			if (dev->bus == efx->pci_dev->bus &&
			    dev->devfn == efx->pci_dev->devfn + 1) {
				nic_data->pci_dev2 = dev;
				break;
			}
		}
		if (!nic_data->pci_dev2) {
			netif_err(efx, probe, efx->net_dev,
				  "failed to find secondary function\n");
			rc = -ENODEV;
			goto fail2;
		}
	}

	/* Now we can reset the NIC */
	rc = __falcon_reset_hw(efx, RESET_TYPE_ALL);
	if (rc) {
		netif_err(efx, probe, efx->net_dev, "failed to reset NIC\n");
		goto fail3;
	}

	/* Allocate memory for INT_KER */
	rc = efx_nic_alloc_buffer(efx, &efx->irq_status, sizeof(efx_oword_t),
				  GFP_KERNEL);
	if (rc)
		goto fail4;
	BUG_ON(efx->irq_status.dma_addr & 0x0f);

	netif_dbg(efx, probe, efx->net_dev,
		  "INT_KER at %llx (virt %p phys %llx)\n",
		  (u64)efx->irq_status.dma_addr,
		  efx->irq_status.addr,
		  (u64)virt_to_phys(efx->irq_status.addr));

	falcon_probe_spi_devices(efx);

	/* Read in the non-volatile configuration */
	rc = falcon_probe_nvconfig(efx);
	if (rc) {
		if (rc == -EINVAL)
			netif_err(efx, probe, efx->net_dev, "NVRAM is invalid\n");
		goto fail5;
	}

	efx->max_channels = (efx_nic_rev(efx) <= EFX_REV_FALCON_A1 ? 4 :
			     EFX_MAX_CHANNELS);
	efx->timer_quantum_ns = 4968; /* 621 cycles */

	/* Initialise I2C adapter */
	board = falcon_board(efx);
	board->i2c_adap.owner = THIS_MODULE;
	board->i2c_data = falcon_i2c_bit_operations;
	board->i2c_data.data = efx;
	board->i2c_adap.algo_data = &board->i2c_data;
	board->i2c_adap.dev.parent = &efx->pci_dev->dev;
	strlcpy(board->i2c_adap.name, "SFC4000 GPIO",
		sizeof(board->i2c_adap.name));
	rc = i2c_bit_add_bus(&board->i2c_adap);
	if (rc)
		goto fail5;

	rc = falcon_board(efx)->type->init(efx);
	if (rc) {
		netif_err(efx, probe, efx->net_dev,
			  "failed to initialise board\n");
		goto fail6;
	}

	nic_data->stats_disable_count = 1;
	setup_timer(&nic_data->stats_timer, &falcon_stats_timer_func,
		    (unsigned long)efx);

	return 0;

 fail6:
	i2c_del_adapter(&board->i2c_adap);
	memset(&board->i2c_adap, 0, sizeof(board->i2c_adap));
 fail5:
	efx_nic_free_buffer(efx, &efx->irq_status);
 fail4:
 fail3:
	if (nic_data->pci_dev2) {
		pci_dev_put(nic_data->pci_dev2);
		nic_data->pci_dev2 = NULL;
	}
 fail2:
 fail1:
	kfree(efx->nic_data);
	return rc;
}

static void falcon_init_rx_cfg(struct efx_nic *efx)
{
	/* RX control FIFO thresholds (32 entries) */
	const unsigned ctrl_xon_thr = 20;
	const unsigned ctrl_xoff_thr = 25;
	efx_oword_t reg;

	efx_reado(efx, &reg, FR_AZ_RX_CFG);
	if (efx_nic_rev(efx) <= EFX_REV_FALCON_A1) {
		/* Data FIFO size is 5.5K.  The RX DMA engine only
		 * supports scattering for user-mode queues, but will
		 * split DMA writes at intervals of RX_USR_BUF_SIZE
		 * (32-byte units) even for kernel-mode queues.  We
		 * set it to be so large that that never happens.
		 */
		EFX_SET_OWORD_FIELD(reg, FRF_AA_RX_DESC_PUSH_EN, 0);
		EFX_SET_OWORD_FIELD(reg, FRF_AA_RX_USR_BUF_SIZE,
				    (3 * 4096) >> 5);
		EFX_SET_OWORD_FIELD(reg, FRF_AA_RX_XON_MAC_TH, 512 >> 8);
		EFX_SET_OWORD_FIELD(reg, FRF_AA_RX_XOFF_MAC_TH, 2048 >> 8);
		EFX_SET_OWORD_FIELD(reg, FRF_AA_RX_XON_TX_TH, ctrl_xon_thr);
		EFX_SET_OWORD_FIELD(reg, FRF_AA_RX_XOFF_TX_TH, ctrl_xoff_thr);
	} else {
		/* Data FIFO size is 80K; register fields moved */
		EFX_SET_OWORD_FIELD(reg, FRF_BZ_RX_DESC_PUSH_EN, 0);
		EFX_SET_OWORD_FIELD(reg, FRF_BZ_RX_USR_BUF_SIZE,
				    EFX_RX_USR_BUF_SIZE >> 5);
		/* Send XON and XOFF at ~3 * max MTU away from empty/full */
		EFX_SET_OWORD_FIELD(reg, FRF_BZ_RX_XON_MAC_TH, 27648 >> 8);
		EFX_SET_OWORD_FIELD(reg, FRF_BZ_RX_XOFF_MAC_TH, 54272 >> 8);
		EFX_SET_OWORD_FIELD(reg, FRF_BZ_RX_XON_TX_TH, ctrl_xon_thr);
		EFX_SET_OWORD_FIELD(reg, FRF_BZ_RX_XOFF_TX_TH, ctrl_xoff_thr);
		EFX_SET_OWORD_FIELD(reg, FRF_BZ_RX_INGR_EN, 1);

		/* Enable hash insertion. This is broken for the
		 * 'Falcon' hash so also select Toeplitz TCP/IPv4 and
		 * IPv4 hashes. */
		EFX_SET_OWORD_FIELD(reg, FRF_BZ_RX_HASH_INSRT_HDR, 1);
		EFX_SET_OWORD_FIELD(reg, FRF_BZ_RX_HASH_ALG, 1);
		EFX_SET_OWORD_FIELD(reg, FRF_BZ_RX_IP_HASH, 1);
	}
	/* Always enable XOFF signal from RX FIFO.  We enable
	 * or disable transmission of pause frames at the MAC. */
	EFX_SET_OWORD_FIELD(reg, FRF_AZ_RX_XOFF_MAC_EN, 1);
	efx_writeo(efx, &reg, FR_AZ_RX_CFG);
}

/* This call performs hardware-specific global initialisation, such as
 * defining the descriptor cache sizes and number of RSS channels.
 * It does not set up any buffers, descriptor rings or event queues.
 */
static int falcon_init_nic(struct efx_nic *efx)
{
	efx_oword_t temp;
	int rc;

	/* Use on-chip SRAM */
	efx_reado(efx, &temp, FR_AB_NIC_STAT);
	EFX_SET_OWORD_FIELD(temp, FRF_AB_ONCHIP_SRAM, 1);
	efx_writeo(efx, &temp, FR_AB_NIC_STAT);

	rc = falcon_reset_sram(efx);
	if (rc)
		return rc;

	/* Clear the parity enables on the TX data fifos as
	 * they produce false parity errors because of timing issues
	 */
	if (EFX_WORKAROUND_5129(efx)) {
		efx_reado(efx, &temp, FR_AZ_CSR_SPARE);
		EFX_SET_OWORD_FIELD(temp, FRF_AB_MEM_PERR_EN_TX_DATA, 0);
		efx_writeo(efx, &temp, FR_AZ_CSR_SPARE);
	}

	if (EFX_WORKAROUND_7244(efx)) {
		efx_reado(efx, &temp, FR_BZ_RX_FILTER_CTL);
		EFX_SET_OWORD_FIELD(temp, FRF_BZ_UDP_FULL_SRCH_LIMIT, 8);
		EFX_SET_OWORD_FIELD(temp, FRF_BZ_UDP_WILD_SRCH_LIMIT, 8);
		EFX_SET_OWORD_FIELD(temp, FRF_BZ_TCP_FULL_SRCH_LIMIT, 8);
		EFX_SET_OWORD_FIELD(temp, FRF_BZ_TCP_WILD_SRCH_LIMIT, 8);
		efx_writeo(efx, &temp, FR_BZ_RX_FILTER_CTL);
	}

	/* XXX This is documented only for Falcon A0/A1 */
	/* Setup RX.  Wait for descriptor is broken and must
	 * be disabled.  RXDP recovery shouldn't be needed, but is.
	 */
	efx_reado(efx, &temp, FR_AA_RX_SELF_RST);
	EFX_SET_OWORD_FIELD(temp, FRF_AA_RX_NODESC_WAIT_DIS, 1);
	EFX_SET_OWORD_FIELD(temp, FRF_AA_RX_SELF_RST_EN, 1);
	if (EFX_WORKAROUND_5583(efx))
		EFX_SET_OWORD_FIELD(temp, FRF_AA_RX_ISCSI_DIS, 1);
	efx_writeo(efx, &temp, FR_AA_RX_SELF_RST);

	/* Do not enable TX_NO_EOP_DISC_EN, since it limits packets to 16
	 * descriptors (which is bad).
	 */
	efx_reado(efx, &temp, FR_AZ_TX_CFG);
	EFX_SET_OWORD_FIELD(temp, FRF_AZ_TX_NO_EOP_DISC_EN, 0);
	efx_writeo(efx, &temp, FR_AZ_TX_CFG);

	falcon_init_rx_cfg(efx);

	if (efx_nic_rev(efx) >= EFX_REV_FALCON_B0) {
		falcon_b0_rx_push_rss_config(efx, false, efx->rx_indir_table);

		/* Set destination of both TX and RX Flush events */
		EFX_POPULATE_OWORD_1(temp, FRF_BZ_FLS_EVQ_ID, 0);
		efx_writeo(efx, &temp, FR_BZ_DP_CTRL);
	}

	efx_farch_init_common(efx);

	return 0;
}

static void falcon_remove_nic(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	struct falcon_board *board = falcon_board(efx);

	board->type->fini(efx);

	/* Remove I2C adapter and clear it in preparation for a retry */
	i2c_del_adapter(&board->i2c_adap);
	memset(&board->i2c_adap, 0, sizeof(board->i2c_adap));

	efx_nic_free_buffer(efx, &efx->irq_status);

	__falcon_reset_hw(efx, RESET_TYPE_ALL);

	/* Release the second function after the reset */
	if (nic_data->pci_dev2) {
		pci_dev_put(nic_data->pci_dev2);
		nic_data->pci_dev2 = NULL;
	}

	/* Tear down the private nic state */
	kfree(efx->nic_data);
	efx->nic_data = NULL;
}

static size_t falcon_describe_nic_stats(struct efx_nic *efx, u8 *names)
{
	return efx_nic_describe_stats(falcon_stat_desc, FALCON_STAT_COUNT,
				      falcon_stat_mask, names);
}

static size_t falcon_update_nic_stats(struct efx_nic *efx, u64 *full_stats,
				      struct rtnl_link_stats64 *core_stats)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	u64 *stats = nic_data->stats;
	efx_oword_t cnt;

	if (!nic_data->stats_disable_count) {
		efx_reado(efx, &cnt, FR_AZ_RX_NODESC_DROP);
		stats[FALCON_STAT_rx_nodesc_drop_cnt] +=
			EFX_OWORD_FIELD(cnt, FRF_AB_RX_NODESC_DROP_CNT);

		if (nic_data->stats_pending &&
		    FALCON_XMAC_STATS_DMA_FLAG(efx)) {
			nic_data->stats_pending = false;
			rmb(); /* read the done flag before the stats */
			efx_nic_update_stats(
				falcon_stat_desc, FALCON_STAT_COUNT,
				falcon_stat_mask,
				stats, efx->stats_buffer.addr, true);
		}

		/* Update derived statistic */
		efx_update_diff_stat(&stats[FALCON_STAT_rx_bad_bytes],
				     stats[FALCON_STAT_rx_bytes] -
				     stats[FALCON_STAT_rx_good_bytes] -
				     stats[FALCON_STAT_rx_control] * 64);
		efx_update_sw_stats(efx, stats);
	}

	if (full_stats)
		memcpy(full_stats, stats, sizeof(u64) * FALCON_STAT_COUNT);

	if (core_stats) {
		core_stats->rx_packets = stats[FALCON_STAT_rx_packets];
		core_stats->tx_packets = stats[FALCON_STAT_tx_packets];
		core_stats->rx_bytes = stats[FALCON_STAT_rx_bytes];
		core_stats->tx_bytes = stats[FALCON_STAT_tx_bytes];
		core_stats->rx_dropped = stats[FALCON_STAT_rx_nodesc_drop_cnt] +
					 stats[GENERIC_STAT_rx_nodesc_trunc] +
					 stats[GENERIC_STAT_rx_noskb_drops];
		core_stats->multicast = stats[FALCON_STAT_rx_multicast];
		core_stats->rx_length_errors =
			stats[FALCON_STAT_rx_gtjumbo] +
			stats[FALCON_STAT_rx_length_error];
		core_stats->rx_crc_errors = stats[FALCON_STAT_rx_bad];
		core_stats->rx_frame_errors = stats[FALCON_STAT_rx_align_error];
		core_stats->rx_fifo_errors = stats[FALCON_STAT_rx_overflow];

		core_stats->rx_errors = (core_stats->rx_length_errors +
					 core_stats->rx_crc_errors +
					 core_stats->rx_frame_errors +
					 stats[FALCON_STAT_rx_symbol_error]);
	}

	return FALCON_STAT_COUNT;
}

void falcon_start_nic_stats(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;

	spin_lock_bh(&efx->stats_lock);
	if (--nic_data->stats_disable_count == 0)
		falcon_stats_request(efx);
	spin_unlock_bh(&efx->stats_lock);
}

/* We don't acutally pull stats on falcon. Wait 10ms so that
 * they arrive when we call this just after start_stats
 */
static void falcon_pull_nic_stats(struct efx_nic *efx)
{
	msleep(10);
}

void falcon_stop_nic_stats(struct efx_nic *efx)
{
	struct falcon_nic_data *nic_data = efx->nic_data;
	int i;

	might_sleep();

	spin_lock_bh(&efx->stats_lock);
	++nic_data->stats_disable_count;
	spin_unlock_bh(&efx->stats_lock);

	del_timer_sync(&nic_data->stats_timer);

	/* Wait enough time for the most recent transfer to
	 * complete. */
	for (i = 0; i < 4 && nic_data->stats_pending; i++) {
		if (FALCON_XMAC_STATS_DMA_FLAG(efx))
			break;
		msleep(1);
	}

	spin_lock_bh(&efx->stats_lock);
	falcon_stats_complete(efx);
	spin_unlock_bh(&efx->stats_lock);
}

static void falcon_set_id_led(struct efx_nic *efx, enum efx_led_mode mode)
{
	falcon_board(efx)->type->set_id_led(efx, mode);
}

/**************************************************************************
 *
 * Wake on LAN
 *
 **************************************************************************
 */

static void falcon_get_wol(struct efx_nic *efx, struct ethtool_wolinfo *wol)
{
	wol->supported = 0;
	wol->wolopts = 0;
	memset(&wol->sopass, 0, sizeof(wol->sopass));
}

static int falcon_set_wol(struct efx_nic *efx, u32 type)
{
	if (type != 0)
		return -EINVAL;
	return 0;
}

/**************************************************************************
 *
 * Revision-dependent attributes used by efx.c and nic.c
 *
 **************************************************************************
 */

const struct efx_nic_type falcon_a1_nic_type = {
	.is_vf = false,
	.mem_bar = EFX_MEM_BAR,
	.mem_map_size = falcon_a1_mem_map_size,
	.probe = falcon_probe_nic,
	.remove = falcon_remove_nic,
	.init = falcon_init_nic,
	.dimension_resources = falcon_dimension_resources,
	.fini = falcon_irq_ack_a1,
	.monitor = falcon_monitor,
	.map_reset_reason = falcon_map_reset_reason,
	.map_reset_flags = falcon_map_reset_flags,
	.reset = falcon_reset_hw,
	.probe_port = falcon_probe_port,
	.remove_port = falcon_remove_port,
	.handle_global_event = falcon_handle_global_event,
	.fini_dmaq = efx_farch_fini_dmaq,
	.prepare_flush = falcon_prepare_flush,
	.finish_flush = efx_port_dummy_op_void,
	.prepare_flr = efx_port_dummy_op_void,
	.finish_flr = efx_farch_finish_flr,
	.describe_stats = falcon_describe_nic_stats,
	.update_stats = falcon_update_nic_stats,
	.start_stats = falcon_start_nic_stats,
	.pull_stats = falcon_pull_nic_stats,
	.stop_stats = falcon_stop_nic_stats,
	.set_id_led = falcon_set_id_led,
	.push_irq_moderation = falcon_push_irq_moderation,
	.reconfigure_port = falcon_reconfigure_port,
	.prepare_enable_fc_tx = falcon_a1_prepare_enable_fc_tx,
	.reconfigure_mac = falcon_reconfigure_xmac,
	.check_mac_fault = falcon_xmac_check_fault,
	.get_wol = falcon_get_wol,
	.set_wol = falcon_set_wol,
	.resume_wol = efx_port_dummy_op_void,
	.test_nvram = falcon_test_nvram,
	.irq_enable_master = efx_farch_irq_enable_master,
	.irq_test_generate = efx_farch_irq_test_generate,
	.irq_disable_non_ev = efx_farch_irq_disable_master,
	.irq_handle_msi = efx_farch_msi_interrupt,
	.irq_handle_legacy = falcon_legacy_interrupt_a1,
	.tx_probe = efx_farch_tx_probe,
	.tx_init = efx_farch_tx_init,
	.tx_remove = efx_farch_tx_remove,
	.tx_write = efx_farch_tx_write,
	.rx_push_rss_config = dummy_rx_push_rss_config,
	.rx_probe = efx_farch_rx_probe,
	.rx_init = efx_farch_rx_init,
	.rx_remove = efx_farch_rx_remove,
	.rx_write = efx_farch_rx_write,
	.rx_defer_refill = efx_farch_rx_defer_refill,
	.ev_probe = efx_farch_ev_probe,
	.ev_init = efx_farch_ev_init,
	.ev_fini = efx_farch_ev_fini,
	.ev_remove = efx_farch_ev_remove,
	.ev_process = efx_farch_ev_process,
	.ev_read_ack = efx_farch_ev_read_ack,
	.ev_test_generate = efx_farch_ev_test_generate,

	/* We don't expose the filter table on Falcon A1 as it is not
	 * mapped into function 0, but these implementations still
	 * work with a degenerate case of all tables set to size 0.
	 */
	.filter_table_probe = efx_farch_filter_table_probe,
	.filter_table_restore = efx_farch_filter_table_restore,
	.filter_table_remove = efx_farch_filter_table_remove,
	.filter_insert = efx_farch_filter_insert,
	.filter_remove_safe = efx_farch_filter_remove_safe,
	.filter_get_safe = efx_farch_filter_get_safe,
	.filter_clear_rx = efx_farch_filter_clear_rx,
	.filter_count_rx_used = efx_farch_filter_count_rx_used,
	.filter_get_rx_id_limit = efx_farch_filter_get_rx_id_limit,
	.filter_get_rx_ids = efx_farch_filter_get_rx_ids,

#ifdef CONFIG_SFC_MTD
	.mtd_probe = falcon_mtd_probe,
	.mtd_rename = falcon_mtd_rename,
	.mtd_read = falcon_mtd_read,
	.mtd_erase = falcon_mtd_erase,
	.mtd_write = falcon_mtd_write,
	.mtd_sync = falcon_mtd_sync,
#endif

	.revision = EFX_REV_FALCON_A1,
	.txd_ptr_tbl_base = FR_AA_TX_DESC_PTR_TBL_KER,
	.rxd_ptr_tbl_base = FR_AA_RX_DESC_PTR_TBL_KER,
	.buf_tbl_base = FR_AA_BUF_FULL_TBL_KER,
	.evq_ptr_tbl_base = FR_AA_EVQ_PTR_TBL_KER,
	.evq_rptr_tbl_base = FR_AA_EVQ_RPTR_KER,
	.max_dma_mask = DMA_BIT_MASK(FSF_AZ_TX_KER_BUF_ADDR_WIDTH),
	.rx_buffer_padding = 0x24,
	.can_rx_scatter = false,
	.max_interrupt_mode = EFX_INT_MODE_MSI,
	.timer_period_max =  1 << FRF_AB_TC_TIMER_VAL_WIDTH,
	.offload_features = NETIF_F_IP_CSUM,
	.mcdi_max_ver = -1,
};

const struct efx_nic_type falcon_b0_nic_type = {
	.is_vf = false,
	.mem_bar = EFX_MEM_BAR,
	.mem_map_size = falcon_b0_mem_map_size,
	.probe = falcon_probe_nic,
	.remove = falcon_remove_nic,
	.init = falcon_init_nic,
	.dimension_resources = falcon_dimension_resources,
	.fini = efx_port_dummy_op_void,
	.monitor = falcon_monitor,
	.map_reset_reason = falcon_map_reset_reason,
	.map_reset_flags = falcon_map_reset_flags,
	.reset = falcon_reset_hw,
	.probe_port = falcon_probe_port,
	.remove_port = falcon_remove_port,
	.handle_global_event = falcon_handle_global_event,
	.fini_dmaq = efx_farch_fini_dmaq,
	.prepare_flush = falcon_prepare_flush,
	.finish_flush = efx_port_dummy_op_void,
	.prepare_flr = efx_port_dummy_op_void,
	.finish_flr = efx_farch_finish_flr,
	.describe_stats = falcon_describe_nic_stats,
	.update_stats = falcon_update_nic_stats,
	.start_stats = falcon_start_nic_stats,
	.pull_stats = falcon_pull_nic_stats,
	.stop_stats = falcon_stop_nic_stats,
	.set_id_led = falcon_set_id_led,
	.push_irq_moderation = falcon_push_irq_moderation,
	.reconfigure_port = falcon_reconfigure_port,
	.prepare_enable_fc_tx = falcon_b0_prepare_enable_fc_tx,
	.reconfigure_mac = falcon_reconfigure_xmac,
	.check_mac_fault = falcon_xmac_check_fault,
	.get_wol = falcon_get_wol,
	.set_wol = falcon_set_wol,
	.resume_wol = efx_port_dummy_op_void,
	.test_chip = falcon_b0_test_chip,
	.test_nvram = falcon_test_nvram,
	.irq_enable_master = efx_farch_irq_enable_master,
	.irq_test_generate = efx_farch_irq_test_generate,
	.irq_disable_non_ev = efx_farch_irq_disable_master,
	.irq_handle_msi = efx_farch_msi_interrupt,
	.irq_handle_legacy = efx_farch_legacy_interrupt,
	.tx_probe = efx_farch_tx_probe,
	.tx_init = efx_farch_tx_init,
	.tx_remove = efx_farch_tx_remove,
	.tx_write = efx_farch_tx_write,
	.rx_push_rss_config = falcon_b0_rx_push_rss_config,
	.rx_probe = efx_farch_rx_probe,
	.rx_init = efx_farch_rx_init,
	.rx_remove = efx_farch_rx_remove,
	.rx_write = efx_farch_rx_write,
	.rx_defer_refill = efx_farch_rx_defer_refill,
	.ev_probe = efx_farch_ev_probe,
	.ev_init = efx_farch_ev_init,
	.ev_fini = efx_farch_ev_fini,
	.ev_remove = efx_farch_ev_remove,
	.ev_process = efx_farch_ev_process,
	.ev_read_ack = efx_farch_ev_read_ack,
	.ev_test_generate = efx_farch_ev_test_generate,
	.filter_table_probe = efx_farch_filter_table_probe,
	.filter_table_restore = efx_farch_filter_table_restore,
	.filter_table_remove = efx_farch_filter_table_remove,
	.filter_update_rx_scatter = efx_farch_filter_update_rx_scatter,
	.filter_insert = efx_farch_filter_insert,
	.filter_remove_safe = efx_farch_filter_remove_safe,
	.filter_get_safe = efx_farch_filter_get_safe,
	.filter_clear_rx = efx_farch_filter_clear_rx,
	.filter_count_rx_used = efx_farch_filter_count_rx_used,
	.filter_get_rx_id_limit = efx_farch_filter_get_rx_id_limit,
	.filter_get_rx_ids = efx_farch_filter_get_rx_ids,
#ifdef CONFIG_RFS_ACCEL
	.filter_rfs_insert = efx_farch_filter_rfs_insert,
	.filter_rfs_expire_one = efx_farch_filter_rfs_expire_one,
#endif
#ifdef CONFIG_SFC_MTD
	.mtd_probe = falcon_mtd_probe,
	.mtd_rename = falcon_mtd_rename,
	.mtd_read = falcon_mtd_read,
	.mtd_erase = falcon_mtd_erase,
	.mtd_write = falcon_mtd_write,
	.mtd_sync = falcon_mtd_sync,
#endif

	.revision = EFX_REV_FALCON_B0,
	.txd_ptr_tbl_base = FR_BZ_TX_DESC_PTR_TBL,
	.rxd_ptr_tbl_base = FR_BZ_RX_DESC_PTR_TBL,
	.buf_tbl_base = FR_BZ_BUF_FULL_TBL,
	.evq_ptr_tbl_base = FR_BZ_EVQ_PTR_TBL,
	.evq_rptr_tbl_base = FR_BZ_EVQ_RPTR,
	.max_dma_mask = DMA_BIT_MASK(FSF_AZ_TX_KER_BUF_ADDR_WIDTH),
	.rx_prefix_size = FS_BZ_RX_PREFIX_SIZE,
	.rx_hash_offset = FS_BZ_RX_PREFIX_HASH_OFST,
	.rx_buffer_padding = 0,
	.can_rx_scatter = true,
	.max_interrupt_mode = EFX_INT_MODE_MSIX,
	.timer_period_max =  1 << FRF_AB_TC_TIMER_VAL_WIDTH,
	.offload_features = NETIF_F_IP_CSUM | NETIF_F_RXHASH | NETIF_F_NTUPLE,
	.mcdi_max_ver = -1,
	.max_rx_ip_filters = FR_BZ_RX_FILTER_TBL0_ROWS,
};
