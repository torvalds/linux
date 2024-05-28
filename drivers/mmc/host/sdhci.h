/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  linux/drivers/mmc/host/sdhci.h - Secure Digital Host Controller Interface driver
 *
 * Header file for Host Controller registers and I/O accessors.
 *
 *  Copyright (C) 2005-2008 Pierre Ossman, All Rights Reserved.
 */
#ifndef __SDHCI_HW_H
#define __SDHCI_HW_H

#include <linux/bits.h>
#include <linux/scatterlist.h>
#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/interrupt.h>

#include <linux/mmc/host.h>

/*
 * Controller registers
 */

#define SDHCI_DMA_ADDRESS	0x00
#define SDHCI_ARGUMENT2		SDHCI_DMA_ADDRESS
#define SDHCI_32BIT_BLK_CNT	SDHCI_DMA_ADDRESS

#define SDHCI_BLOCK_SIZE	0x04
#define  SDHCI_MAKE_BLKSZ(dma, blksz) (((dma & 0x7) << 12) | (blksz & 0xFFF))

#define SDHCI_BLOCK_COUNT	0x06

#define SDHCI_ARGUMENT		0x08

#define SDHCI_TRANSFER_MODE	0x0C
#define  SDHCI_TRNS_DMA		0x01
#define  SDHCI_TRNS_BLK_CNT_EN	0x02
#define  SDHCI_TRNS_AUTO_CMD12	0x04
#define  SDHCI_TRNS_AUTO_CMD23	0x08
#define  SDHCI_TRNS_AUTO_SEL	0x0C
#define  SDHCI_TRNS_READ	0x10
#define  SDHCI_TRNS_MULTI	0x20

#define SDHCI_COMMAND		0x0E
#define  SDHCI_CMD_RESP_MASK	0x03
#define  SDHCI_CMD_CRC		0x08
#define  SDHCI_CMD_INDEX	0x10
#define  SDHCI_CMD_DATA		0x20
#define  SDHCI_CMD_ABORTCMD	0xC0

#define  SDHCI_CMD_RESP_NONE	0x00
#define  SDHCI_CMD_RESP_LONG	0x01
#define  SDHCI_CMD_RESP_SHORT	0x02
#define  SDHCI_CMD_RESP_SHORT_BUSY 0x03

#define SDHCI_MAKE_CMD(c, f) (((c & 0xff) << 8) | (f & 0xff))
#define SDHCI_GET_CMD(c) ((c>>8) & 0x3f)

#define SDHCI_RESPONSE		0x10

#define SDHCI_BUFFER		0x20

#define SDHCI_PRESENT_STATE	0x24
#define  SDHCI_CMD_INHIBIT	0x00000001
#define  SDHCI_DATA_INHIBIT	0x00000002
#define  SDHCI_DOING_WRITE	0x00000100
#define  SDHCI_DOING_READ	0x00000200
#define  SDHCI_SPACE_AVAILABLE	0x00000400
#define  SDHCI_DATA_AVAILABLE	0x00000800
#define  SDHCI_CARD_PRESENT	0x00010000
#define   SDHCI_CARD_PRES_SHIFT	16
#define  SDHCI_CD_STABLE	0x00020000
#define  SDHCI_CD_LVL		0x00040000
#define   SDHCI_CD_LVL_SHIFT	18
#define  SDHCI_WRITE_PROTECT	0x00080000
#define  SDHCI_DATA_LVL_MASK	0x00F00000
#define   SDHCI_DATA_LVL_SHIFT	20
#define   SDHCI_DATA_0_LVL_MASK	0x00100000
#define  SDHCI_CMD_LVL		0x01000000

#define SDHCI_HOST_CONTROL	0x28
#define  SDHCI_CTRL_LED		0x01
#define  SDHCI_CTRL_4BITBUS	0x02
#define  SDHCI_CTRL_HISPD	0x04
#define  SDHCI_CTRL_DMA_MASK	0x18
#define   SDHCI_CTRL_SDMA	0x00
#define   SDHCI_CTRL_ADMA1	0x08
#define   SDHCI_CTRL_ADMA32	0x10
#define   SDHCI_CTRL_ADMA64	0x18
#define   SDHCI_CTRL_ADMA3	0x18
#define  SDHCI_CTRL_8BITBUS	0x20
#define  SDHCI_CTRL_CDTEST_INS	0x40
#define  SDHCI_CTRL_CDTEST_EN	0x80

#define SDHCI_POWER_CONTROL	0x29
#define  SDHCI_POWER_ON		0x01
#define  SDHCI_POWER_180	0x0A
#define  SDHCI_POWER_300	0x0C
#define  SDHCI_POWER_330	0x0E
/*
 * VDD2 - UHS2 or PCIe/NVMe
 * VDD2 power on/off and voltage select
 */
#define  SDHCI_VDD2_POWER_ON	0x10
#define  SDHCI_VDD2_POWER_120	0x80
#define  SDHCI_VDD2_POWER_180	0xA0

#define SDHCI_BLOCK_GAP_CONTROL	0x2A

#define SDHCI_WAKE_UP_CONTROL	0x2B
#define  SDHCI_WAKE_ON_INT	0x01
#define  SDHCI_WAKE_ON_INSERT	0x02
#define  SDHCI_WAKE_ON_REMOVE	0x04

#define SDHCI_CLOCK_CONTROL	0x2C
#define  SDHCI_DIVIDER_SHIFT	8
#define  SDHCI_DIVIDER_HI_SHIFT	6
#define  SDHCI_DIV_MASK	0xFF
#define  SDHCI_DIV_MASK_LEN	8
#define  SDHCI_DIV_HI_MASK	0x300
#define  SDHCI_PROG_CLOCK_MODE	0x0020
#define  SDHCI_CLOCK_CARD_EN	0x0004
#define  SDHCI_CLOCK_PLL_EN	0x0008
#define  SDHCI_CLOCK_INT_STABLE	0x0002
#define  SDHCI_CLOCK_INT_EN	0x0001

#define SDHCI_TIMEOUT_CONTROL	0x2E

#define SDHCI_SOFTWARE_RESET	0x2F
#define  SDHCI_RESET_ALL	0x01
#define  SDHCI_RESET_CMD	0x02
#define  SDHCI_RESET_DATA	0x04

#define SDHCI_INT_STATUS	0x30
#define SDHCI_INT_ENABLE	0x34
#define SDHCI_SIGNAL_ENABLE	0x38
#define  SDHCI_INT_RESPONSE	0x00000001
#define  SDHCI_INT_DATA_END	0x00000002
#define  SDHCI_INT_BLK_GAP	0x00000004
#define  SDHCI_INT_DMA_END	0x00000008
#define  SDHCI_INT_SPACE_AVAIL	0x00000010
#define  SDHCI_INT_DATA_AVAIL	0x00000020
#define  SDHCI_INT_CARD_INSERT	0x00000040
#define  SDHCI_INT_CARD_REMOVE	0x00000080
#define  SDHCI_INT_CARD_INT	0x00000100
#define  SDHCI_INT_RETUNE	0x00001000
#define  SDHCI_INT_CQE		0x00004000
#define  SDHCI_INT_ERROR	0x00008000
#define  SDHCI_INT_TIMEOUT	0x00010000
#define  SDHCI_INT_CRC		0x00020000
#define  SDHCI_INT_END_BIT	0x00040000
#define  SDHCI_INT_INDEX	0x00080000
#define  SDHCI_INT_DATA_TIMEOUT	0x00100000
#define  SDHCI_INT_DATA_CRC	0x00200000
#define  SDHCI_INT_DATA_END_BIT	0x00400000
#define  SDHCI_INT_BUS_POWER	0x00800000
#define  SDHCI_INT_AUTO_CMD_ERR	0x01000000
#define  SDHCI_INT_ADMA_ERROR	0x02000000
#define  SDHCI_INT_TUNING_ERROR	0x04000000

#define  SDHCI_INT_NORMAL_MASK	0x00007FFF
#define  SDHCI_INT_ERROR_MASK	0xFFFF8000

#define  SDHCI_INT_CMD_MASK	(SDHCI_INT_RESPONSE | SDHCI_INT_TIMEOUT | \
		SDHCI_INT_CRC | SDHCI_INT_END_BIT | SDHCI_INT_INDEX | \
		SDHCI_INT_AUTO_CMD_ERR)
#define  SDHCI_INT_DATA_MASK	(SDHCI_INT_DATA_END | SDHCI_INT_DMA_END | \
		SDHCI_INT_DATA_AVAIL | SDHCI_INT_SPACE_AVAIL | \
		SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_DATA_CRC | \
		SDHCI_INT_DATA_END_BIT | SDHCI_INT_ADMA_ERROR | \
		SDHCI_INT_BLK_GAP | SDHCI_INT_TUNING_ERROR)
#define SDHCI_INT_ALL_MASK	((unsigned int)-1)

#define SDHCI_CQE_INT_ERR_MASK ( \
	SDHCI_INT_ADMA_ERROR | SDHCI_INT_BUS_POWER | SDHCI_INT_DATA_END_BIT | \
	SDHCI_INT_DATA_CRC | SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_INDEX | \
	SDHCI_INT_END_BIT | SDHCI_INT_CRC | SDHCI_INT_TIMEOUT)

#define SDHCI_CQE_INT_MASK (SDHCI_CQE_INT_ERR_MASK | SDHCI_INT_CQE)

#define SDHCI_AUTO_CMD_STATUS	0x3C
#define  SDHCI_AUTO_CMD_TIMEOUT	0x00000002
#define  SDHCI_AUTO_CMD_CRC	0x00000004
#define  SDHCI_AUTO_CMD_END_BIT	0x00000008
#define  SDHCI_AUTO_CMD_INDEX	0x00000010

#define SDHCI_HOST_CONTROL2		0x3E
#define  SDHCI_CTRL_UHS_MASK		0x0007
#define   SDHCI_CTRL_UHS_SDR12		0x0000
#define   SDHCI_CTRL_UHS_SDR25		0x0001
#define   SDHCI_CTRL_UHS_SDR50		0x0002
#define   SDHCI_CTRL_UHS_SDR104		0x0003
#define   SDHCI_CTRL_UHS_DDR50		0x0004
#define   SDHCI_CTRL_HS400		0x0005 /* Non-standard */
#define  SDHCI_CTRL_VDD_180		0x0008
#define  SDHCI_CTRL_DRV_TYPE_MASK	0x0030
#define   SDHCI_CTRL_DRV_TYPE_B		0x0000
#define   SDHCI_CTRL_DRV_TYPE_A		0x0010
#define   SDHCI_CTRL_DRV_TYPE_C		0x0020
#define   SDHCI_CTRL_DRV_TYPE_D		0x0030
#define  SDHCI_CTRL_EXEC_TUNING		0x0040
#define  SDHCI_CTRL_TUNED_CLK		0x0080
#define  SDHCI_CMD23_ENABLE		0x0800
#define  SDHCI_CTRL_V4_MODE		0x1000
#define  SDHCI_CTRL_64BIT_ADDR		0x2000
#define  SDHCI_CTRL_PRESET_VAL_ENABLE	0x8000

#define SDHCI_CAPABILITIES	0x40
#define  SDHCI_TIMEOUT_CLK_MASK		GENMASK(5, 0)
#define  SDHCI_TIMEOUT_CLK_SHIFT 0
#define  SDHCI_TIMEOUT_CLK_UNIT	0x00000080
#define  SDHCI_CLOCK_BASE_MASK		GENMASK(13, 8)
#define  SDHCI_CLOCK_BASE_SHIFT	8
#define  SDHCI_CLOCK_V3_BASE_MASK	GENMASK(15, 8)
#define  SDHCI_MAX_BLOCK_MASK	0x00030000
#define  SDHCI_MAX_BLOCK_SHIFT  16
#define  SDHCI_CAN_DO_8BIT	0x00040000
#define  SDHCI_CAN_DO_ADMA2	0x00080000
#define  SDHCI_CAN_DO_ADMA1	0x00100000
#define  SDHCI_CAN_DO_HISPD	0x00200000
#define  SDHCI_CAN_DO_SDMA	0x00400000
#define  SDHCI_CAN_DO_SUSPEND	0x00800000
#define  SDHCI_CAN_VDD_330	0x01000000
#define  SDHCI_CAN_VDD_300	0x02000000
#define  SDHCI_CAN_VDD_180	0x04000000
#define  SDHCI_CAN_64BIT_V4	0x08000000
#define  SDHCI_CAN_64BIT	0x10000000

#define SDHCI_CAPABILITIES_1	0x44
#define  SDHCI_SUPPORT_SDR50	0x00000001
#define  SDHCI_SUPPORT_SDR104	0x00000002
#define  SDHCI_SUPPORT_DDR50	0x00000004
#define  SDHCI_DRIVER_TYPE_A	0x00000010
#define  SDHCI_DRIVER_TYPE_C	0x00000020
#define  SDHCI_DRIVER_TYPE_D	0x00000040
#define  SDHCI_RETUNING_TIMER_COUNT_MASK	GENMASK(11, 8)
#define  SDHCI_USE_SDR50_TUNING			0x00002000
#define  SDHCI_RETUNING_MODE_MASK		GENMASK(15, 14)
#define  SDHCI_CLOCK_MUL_MASK			GENMASK(23, 16)
#define  SDHCI_CAN_DO_ADMA3	0x08000000
#define  SDHCI_SUPPORT_HS400	0x80000000 /* Non-standard */

#define SDHCI_MAX_CURRENT		0x48
#define  SDHCI_MAX_CURRENT_LIMIT	GENMASK(7, 0)
#define  SDHCI_MAX_CURRENT_330_MASK	GENMASK(7, 0)
#define  SDHCI_MAX_CURRENT_300_MASK	GENMASK(15, 8)
#define  SDHCI_MAX_CURRENT_180_MASK	GENMASK(23, 16)
#define   SDHCI_MAX_CURRENT_MULTIPLIER	4

/* 4C-4F reserved for more max current */

#define SDHCI_SET_ACMD12_ERROR	0x50
#define SDHCI_SET_INT_ERROR	0x52

#define SDHCI_ADMA_ERROR	0x54

/* 55-57 reserved */

#define SDHCI_ADMA_ADDRESS	0x58
#define SDHCI_ADMA_ADDRESS_HI	0x5C

/* 60-FB reserved */

#define SDHCI_PRESET_FOR_HIGH_SPEED	0x64
#define SDHCI_PRESET_FOR_SDR12 0x66
#define SDHCI_PRESET_FOR_SDR25 0x68
#define SDHCI_PRESET_FOR_SDR50 0x6A
#define SDHCI_PRESET_FOR_SDR104        0x6C
#define SDHCI_PRESET_FOR_DDR50 0x6E
#define SDHCI_PRESET_FOR_HS400 0x74 /* Non-standard */
#define SDHCI_PRESET_DRV_MASK		GENMASK(15, 14)
#define SDHCI_PRESET_CLKGEN_SEL		BIT(10)
#define SDHCI_PRESET_SDCLK_FREQ_MASK	GENMASK(9, 0)

#define SDHCI_SLOT_INT_STATUS	0xFC

#define SDHCI_HOST_VERSION	0xFE
#define  SDHCI_VENDOR_VER_MASK	0xFF00
#define  SDHCI_VENDOR_VER_SHIFT	8
#define  SDHCI_SPEC_VER_MASK	0x00FF
#define  SDHCI_SPEC_VER_SHIFT	0
#define   SDHCI_SPEC_100	0
#define   SDHCI_SPEC_200	1
#define   SDHCI_SPEC_300	2
#define   SDHCI_SPEC_400	3
#define   SDHCI_SPEC_410	4
#define   SDHCI_SPEC_420	5

/*
 * End of controller registers.
 */

#define SDHCI_MAX_DIV_SPEC_200	256
#define SDHCI_MAX_DIV_SPEC_300	2046

/*
 * Host SDMA buffer boundary. Valid values from 4K to 512K in powers of 2.
 */
#define SDHCI_DEFAULT_BOUNDARY_SIZE  (512 * 1024)
#define SDHCI_DEFAULT_BOUNDARY_ARG   (ilog2(SDHCI_DEFAULT_BOUNDARY_SIZE) - 12)

/* ADMA2 32-bit DMA descriptor size */
#define SDHCI_ADMA2_32_DESC_SZ	8

/* ADMA2 32-bit descriptor */
struct sdhci_adma2_32_desc {
	__le16	cmd;
	__le16	len;
	__le32	addr;
}  __packed __aligned(4);

/* ADMA2 data alignment */
#define SDHCI_ADMA2_ALIGN	4
#define SDHCI_ADMA2_MASK	(SDHCI_ADMA2_ALIGN - 1)

/*
 * ADMA2 descriptor alignment.  Some controllers (e.g. Intel) require 8 byte
 * alignment for the descriptor table even in 32-bit DMA mode.  Memory
 * allocation is at least 8 byte aligned anyway, so just stipulate 8 always.
 */
#define SDHCI_ADMA2_DESC_ALIGN	8

/*
 * ADMA2 64-bit DMA descriptor size
 * According to SD Host Controller spec v4.10, there are two kinds of
 * descriptors for 64-bit addressing mode: 96-bit Descriptor and 128-bit
 * Descriptor, if Host Version 4 Enable is set in the Host Control 2
 * register, 128-bit Descriptor will be selected.
 */
#define SDHCI_ADMA2_64_DESC_SZ(host)	((host)->v4_mode ? 16 : 12)

/*
 * ADMA2 64-bit descriptor. Note 12-byte descriptor can't always be 8-byte
 * aligned.
 */
struct sdhci_adma2_64_desc {
	__le16	cmd;
	__le16	len;
	__le32	addr_lo;
	__le32	addr_hi;
}  __packed __aligned(4);

#define ADMA2_TRAN_VALID	0x21
#define ADMA2_NOP_END_VALID	0x3
#define ADMA2_END		0x2

/*
 * Maximum segments assuming a 512KiB maximum requisition size and a minimum
 * 4KiB page size. Note this also allows enough for multiple descriptors in
 * case of PAGE_SIZE >= 64KiB.
 */
#define SDHCI_MAX_SEGS		128

/* Allow for a command request and a data request at the same time */
#define SDHCI_MAX_MRQS		2

/*
 * 48bit command and 136 bit response in 100KHz clock could take upto 2.48ms.
 * However since the start time of the command, the time between
 * command and response, and the time between response and start of data is
 * not known, set the command transfer time to 10ms.
 */
#define MMC_CMD_TRANSFER_TIME	(10 * NSEC_PER_MSEC) /* max 10 ms */

#define sdhci_err_stats_inc(host, err_name) \
	mmc_debugfs_err_stats_inc((host)->mmc, MMC_ERR_##err_name)

enum sdhci_cookie {
	COOKIE_UNMAPPED,
	COOKIE_PRE_MAPPED,	/* mapped by sdhci_pre_req() */
	COOKIE_MAPPED,		/* mapped by sdhci_prepare_data() */
};

struct sdhci_host {
	/* Data set by hardware interface driver */
	const char *hw_name;	/* Hardware bus name */

	unsigned int quirks;	/* Deviations from spec. */

/* Controller doesn't honor resets unless we touch the clock register */
#define SDHCI_QUIRK_CLOCK_BEFORE_RESET			(1<<0)
/* Controller has bad caps bits, but really supports DMA */
#define SDHCI_QUIRK_FORCE_DMA				(1<<1)
/* Controller doesn't like to be reset when there is no card inserted. */
#define SDHCI_QUIRK_NO_CARD_NO_RESET			(1<<2)
/* Controller doesn't like clearing the power reg before a change */
#define SDHCI_QUIRK_SINGLE_POWER_WRITE			(1<<3)
/* Controller has an unusable DMA engine */
#define SDHCI_QUIRK_BROKEN_DMA				(1<<5)
/* Controller has an unusable ADMA engine */
#define SDHCI_QUIRK_BROKEN_ADMA				(1<<6)
/* Controller can only DMA from 32-bit aligned addresses */
#define SDHCI_QUIRK_32BIT_DMA_ADDR			(1<<7)
/* Controller can only DMA chunk sizes that are a multiple of 32 bits */
#define SDHCI_QUIRK_32BIT_DMA_SIZE			(1<<8)
/* Controller can only ADMA chunks that are a multiple of 32 bits */
#define SDHCI_QUIRK_32BIT_ADMA_SIZE			(1<<9)
/* Controller needs to be reset after each request to stay stable */
#define SDHCI_QUIRK_RESET_AFTER_REQUEST			(1<<10)
/* Controller needs voltage and power writes to happen separately */
#define SDHCI_QUIRK_NO_SIMULT_VDD_AND_POWER		(1<<11)
/* Controller provides an incorrect timeout value for transfers */
#define SDHCI_QUIRK_BROKEN_TIMEOUT_VAL			(1<<12)
/* Controller has an issue with buffer bits for small transfers */
#define SDHCI_QUIRK_BROKEN_SMALL_PIO			(1<<13)
/* Controller does not provide transfer-complete interrupt when not busy */
#define SDHCI_QUIRK_NO_BUSY_IRQ				(1<<14)
/* Controller has unreliable card detection */
#define SDHCI_QUIRK_BROKEN_CARD_DETECTION		(1<<15)
/* Controller reports inverted write-protect state */
#define SDHCI_QUIRK_INVERTED_WRITE_PROTECT		(1<<16)
/* Controller has unusable command queue engine */
#define SDHCI_QUIRK_BROKEN_CQE				(1<<17)
/* Controller does not like fast PIO transfers */
#define SDHCI_QUIRK_PIO_NEEDS_DELAY			(1<<18)
/* Controller does not have a LED */
#define SDHCI_QUIRK_NO_LED				(1<<19)
/* Controller has to be forced to use block size of 2048 bytes */
#define SDHCI_QUIRK_FORCE_BLK_SZ_2048			(1<<20)
/* Controller cannot do multi-block transfers */
#define SDHCI_QUIRK_NO_MULTIBLOCK			(1<<21)
/* Controller can only handle 1-bit data transfers */
#define SDHCI_QUIRK_FORCE_1_BIT_DATA			(1<<22)
/* Controller needs 10ms delay between applying power and clock */
#define SDHCI_QUIRK_DELAY_AFTER_POWER			(1<<23)
/* Controller uses SDCLK instead of TMCLK for data timeouts */
#define SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK		(1<<24)
/* Controller reports wrong base clock capability */
#define SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN		(1<<25)
/* Controller cannot support End Attribute in NOP ADMA descriptor */
#define SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC		(1<<26)
/* Controller uses Auto CMD12 command to stop the transfer */
#define SDHCI_QUIRK_MULTIBLOCK_READ_ACMD12		(1<<28)
/* Controller doesn't have HISPD bit field in HI-SPEED SD card */
#define SDHCI_QUIRK_NO_HISPD_BIT			(1<<29)
/* Controller treats ADMA descriptors with length 0000h incorrectly */
#define SDHCI_QUIRK_BROKEN_ADMA_ZEROLEN_DESC		(1<<30)
/* The read-only detection via SDHCI_PRESENT_STATE register is unstable */
#define SDHCI_QUIRK_UNSTABLE_RO_DETECT			(1<<31)

	unsigned int quirks2;	/* More deviations from spec. */

#define SDHCI_QUIRK2_HOST_OFF_CARD_ON			(1<<0)
#define SDHCI_QUIRK2_HOST_NO_CMD23			(1<<1)
/* The system physically doesn't support 1.8v, even if the host does */
#define SDHCI_QUIRK2_NO_1_8_V				(1<<2)
#define SDHCI_QUIRK2_PRESET_VALUE_BROKEN		(1<<3)
#define SDHCI_QUIRK2_CARD_ON_NEEDS_BUS_ON		(1<<4)
/* Controller has a non-standard host control register */
#define SDHCI_QUIRK2_BROKEN_HOST_CONTROL		(1<<5)
/* Controller does not support HS200 */
#define SDHCI_QUIRK2_BROKEN_HS200			(1<<6)
/* Controller does not support DDR50 */
#define SDHCI_QUIRK2_BROKEN_DDR50			(1<<7)
/* Stop command (CMD12) can set Transfer Complete when not using MMC_RSP_BUSY */
#define SDHCI_QUIRK2_STOP_WITH_TC			(1<<8)
/* Controller does not support 64-bit DMA */
#define SDHCI_QUIRK2_BROKEN_64_BIT_DMA			(1<<9)
/* need clear transfer mode register before send cmd */
#define SDHCI_QUIRK2_CLEAR_TRANSFERMODE_REG_BEFORE_CMD	(1<<10)
/* Capability register bit-63 indicates HS400 support */
#define SDHCI_QUIRK2_CAPS_BIT63_FOR_HS400		(1<<11)
/* forced tuned clock */
#define SDHCI_QUIRK2_TUNING_WORK_AROUND			(1<<12)
/* disable the block count for single block transactions */
#define SDHCI_QUIRK2_SUPPORT_SINGLE			(1<<13)
/* Controller broken with using ACMD23 */
#define SDHCI_QUIRK2_ACMD23_BROKEN			(1<<14)
/* Broken Clock divider zero in controller */
#define SDHCI_QUIRK2_CLOCK_DIV_ZERO_BROKEN		(1<<15)
/* Controller has CRC in 136 bit Command Response */
#define SDHCI_QUIRK2_RSP_136_HAS_CRC			(1<<16)
/*
 * Disable HW timeout if the requested timeout is more than the maximum
 * obtainable timeout.
 */
#define SDHCI_QUIRK2_DISABLE_HW_TIMEOUT			(1<<17)
/*
 * 32-bit block count may not support eMMC where upper bits of CMD23 are used
 * for other purposes.  Consequently we support 16-bit block count by default.
 * Otherwise, SDHCI_QUIRK2_USE_32BIT_BLK_CNT can be selected to use 32-bit
 * block count.
 */
#define SDHCI_QUIRK2_USE_32BIT_BLK_CNT			(1<<18)
/* Issue CMD and DATA reset together */
#define SDHCI_QUIRK2_ISSUE_CMD_DAT_RESET_TOGETHER	(1<<19)

	int irq;		/* Device IRQ */
	void __iomem *ioaddr;	/* Mapped address */
	phys_addr_t mapbase;	/* physical address base */
	char *bounce_buffer;	/* For packing SDMA reads/writes */
	dma_addr_t bounce_addr;
	unsigned int bounce_buffer_size;

	const struct sdhci_ops *ops;	/* Low level hw interface */

	/* Internal data */
	struct mmc_host *mmc;	/* MMC structure */
	struct mmc_host_ops mmc_host_ops;	/* MMC host ops */
	u64 dma_mask;		/* custom DMA mask */

#if IS_ENABLED(CONFIG_LEDS_CLASS)
	struct led_classdev led;	/* LED control */
	char led_name[32];
#endif

	spinlock_t lock;	/* Mutex */

	int flags;		/* Host attributes */
#define SDHCI_USE_SDMA		(1<<0)	/* Host is SDMA capable */
#define SDHCI_USE_ADMA		(1<<1)	/* Host is ADMA capable */
#define SDHCI_REQ_USE_DMA	(1<<2)	/* Use DMA for this req. */
#define SDHCI_DEVICE_DEAD	(1<<3)	/* Device unresponsive */
#define SDHCI_SDR50_NEEDS_TUNING (1<<4)	/* SDR50 needs tuning */
#define SDHCI_AUTO_CMD12	(1<<6)	/* Auto CMD12 support */
#define SDHCI_AUTO_CMD23	(1<<7)	/* Auto CMD23 support */
#define SDHCI_PV_ENABLED	(1<<8)	/* Preset value enabled */
#define SDHCI_USE_64_BIT_DMA	(1<<12)	/* Use 64-bit DMA */
#define SDHCI_HS400_TUNING	(1<<13)	/* Tuning for HS400 */
#define SDHCI_SIGNALING_330	(1<<14)	/* Host is capable of 3.3V signaling */
#define SDHCI_SIGNALING_180	(1<<15)	/* Host is capable of 1.8V signaling */
#define SDHCI_SIGNALING_120	(1<<16)	/* Host is capable of 1.2V signaling */

	unsigned int version;	/* SDHCI spec. version */

	unsigned int max_clk;	/* Max possible freq (MHz) */
	unsigned int timeout_clk;	/* Timeout freq (KHz) */
	u8 max_timeout_count;	/* Vendor specific max timeout count */
	unsigned int clk_mul;	/* Clock Muliplier value */

	unsigned int clock;	/* Current clock (MHz) */
	u8 pwr;			/* Current voltage */
	u8 drv_type;		/* Current UHS-I driver type */
	bool reinit_uhs;	/* Force UHS-related re-initialization */

	bool runtime_suspended;	/* Host is runtime suspended */
	bool bus_on;		/* Bus power prevents runtime suspend */
	bool preset_enabled;	/* Preset is enabled */
	bool pending_reset;	/* Cmd/data reset is pending */
	bool irq_wake_enabled;	/* IRQ wakeup is enabled */
	bool v4_mode;		/* Host Version 4 Enable */
	bool use_external_dma;	/* Host selects to use external DMA */
	bool always_defer_done;	/* Always defer to complete requests */

	struct mmc_request *mrqs_done[SDHCI_MAX_MRQS];	/* Requests done */
	struct mmc_command *cmd;	/* Current command */
	struct mmc_command *data_cmd;	/* Current data command */
	struct mmc_command *deferred_cmd;	/* Deferred command */
	struct mmc_data *data;	/* Current data request */
	unsigned int data_early:1;	/* Data finished before cmd */

	struct sg_mapping_iter sg_miter;	/* SG state for PIO */
	unsigned int blocks;	/* remaining PIO blocks */

	int sg_count;		/* Mapped sg entries */
	int max_adma;		/* Max. length in ADMA descriptor */

	void *adma_table;	/* ADMA descriptor table */
	void *align_buffer;	/* Bounce buffer */

	size_t adma_table_sz;	/* ADMA descriptor table size */
	size_t align_buffer_sz;	/* Bounce buffer size */

	dma_addr_t adma_addr;	/* Mapped ADMA descr. table */
	dma_addr_t align_addr;	/* Mapped bounce buffer */

	unsigned int desc_sz;	/* ADMA current descriptor size */
	unsigned int alloc_desc_sz;	/* ADMA descr. max size host supports */

	struct workqueue_struct *complete_wq;	/* Request completion wq */
	struct work_struct	complete_work;	/* Request completion work */

	struct timer_list timer;	/* Timer for timeouts */
	struct timer_list data_timer;	/* Timer for data timeouts */

#if IS_ENABLED(CONFIG_MMC_SDHCI_EXTERNAL_DMA)
	struct dma_chan *rx_chan;
	struct dma_chan *tx_chan;
#endif

	u32 caps;		/* CAPABILITY_0 */
	u32 caps1;		/* CAPABILITY_1 */
	bool read_caps;		/* Capability flags have been read */

	bool sdhci_core_to_disable_vqmmc;  /* sdhci core can disable vqmmc */
	unsigned int            ocr_avail_sdio;	/* OCR bit masks */
	unsigned int            ocr_avail_sd;
	unsigned int            ocr_avail_mmc;
	u32 ocr_mask;		/* available voltages */

	unsigned		timing;		/* Current timing */

	u32			thread_isr;

	/* cached registers */
	u32			ier;

	bool			cqe_on;		/* CQE is operating */
	u32			cqe_ier;	/* CQE interrupt mask */
	u32			cqe_err_ier;	/* CQE error interrupt mask */

	wait_queue_head_t	buf_ready_int;	/* Waitqueue for Buffer Read Ready interrupt */
	unsigned int		tuning_done;	/* Condition flag set when CMD19 succeeds */

	unsigned int		tuning_count;	/* Timer count for re-tuning */
	unsigned int		tuning_mode;	/* Re-tuning mode supported by host */
	unsigned int		tuning_err;	/* Error code for re-tuning */
#define SDHCI_TUNING_MODE_1	0
#define SDHCI_TUNING_MODE_2	1
#define SDHCI_TUNING_MODE_3	2
	/* Delay (ms) between tuning commands */
	int			tuning_delay;
	int			tuning_loop_count;

	/* Host SDMA buffer boundary. */
	u32			sdma_boundary;

	/* Host ADMA table count */
	u32			adma_table_cnt;

	u64			data_timeout;

	unsigned long private[] ____cacheline_aligned;
};

struct sdhci_ops {
#ifdef CONFIG_MMC_SDHCI_IO_ACCESSORS
	u32		(*read_l)(struct sdhci_host *host, int reg);
	u16		(*read_w)(struct sdhci_host *host, int reg);
	u8		(*read_b)(struct sdhci_host *host, int reg);
	void		(*write_l)(struct sdhci_host *host, u32 val, int reg);
	void		(*write_w)(struct sdhci_host *host, u16 val, int reg);
	void		(*write_b)(struct sdhci_host *host, u8 val, int reg);
#endif

	void	(*set_clock)(struct sdhci_host *host, unsigned int clock);
	void	(*set_power)(struct sdhci_host *host, unsigned char mode,
			     unsigned short vdd);

	u32		(*irq)(struct sdhci_host *host, u32 intmask);

	int		(*set_dma_mask)(struct sdhci_host *host);
	int		(*enable_dma)(struct sdhci_host *host);
	unsigned int	(*get_max_clock)(struct sdhci_host *host);
	unsigned int	(*get_min_clock)(struct sdhci_host *host);
	/* get_timeout_clock should return clk rate in unit of Hz */
	unsigned int	(*get_timeout_clock)(struct sdhci_host *host);
	unsigned int	(*get_max_timeout_count)(struct sdhci_host *host);
	void		(*set_timeout)(struct sdhci_host *host,
				       struct mmc_command *cmd);
	void		(*set_bus_width)(struct sdhci_host *host, int width);
	void (*platform_send_init_74_clocks)(struct sdhci_host *host,
					     u8 power_mode);
	unsigned int    (*get_ro)(struct sdhci_host *host);
	void		(*reset)(struct sdhci_host *host, u8 mask);
	int	(*platform_execute_tuning)(struct sdhci_host *host, u32 opcode);
	void	(*set_uhs_signaling)(struct sdhci_host *host, unsigned int uhs);
	void	(*hw_reset)(struct sdhci_host *host);
	void    (*adma_workaround)(struct sdhci_host *host, u32 intmask);
	void    (*card_event)(struct sdhci_host *host);
	void	(*voltage_switch)(struct sdhci_host *host);
	void	(*adma_write_desc)(struct sdhci_host *host, void **desc,
				   dma_addr_t addr, int len, unsigned int cmd);
	void	(*copy_to_bounce_buffer)(struct sdhci_host *host,
					 struct mmc_data *data,
					 unsigned int length);
	void	(*request_done)(struct sdhci_host *host,
				struct mmc_request *mrq);
	void    (*dump_vendor_regs)(struct sdhci_host *host);
};

#ifdef CONFIG_MMC_SDHCI_IO_ACCESSORS

static inline void sdhci_writel(struct sdhci_host *host, u32 val, int reg)
{
	if (unlikely(host->ops->write_l))
		host->ops->write_l(host, val, reg);
	else
		writel(val, host->ioaddr + reg);
}

static inline void sdhci_writew(struct sdhci_host *host, u16 val, int reg)
{
	if (unlikely(host->ops->write_w))
		host->ops->write_w(host, val, reg);
	else
		writew(val, host->ioaddr + reg);
}

static inline void sdhci_writeb(struct sdhci_host *host, u8 val, int reg)
{
	if (unlikely(host->ops->write_b))
		host->ops->write_b(host, val, reg);
	else
		writeb(val, host->ioaddr + reg);
}

static inline u32 sdhci_readl(struct sdhci_host *host, int reg)
{
	if (unlikely(host->ops->read_l))
		return host->ops->read_l(host, reg);
	else
		return readl(host->ioaddr + reg);
}

static inline u16 sdhci_readw(struct sdhci_host *host, int reg)
{
	if (unlikely(host->ops->read_w))
		return host->ops->read_w(host, reg);
	else
		return readw(host->ioaddr + reg);
}

static inline u8 sdhci_readb(struct sdhci_host *host, int reg)
{
	if (unlikely(host->ops->read_b))
		return host->ops->read_b(host, reg);
	else
		return readb(host->ioaddr + reg);
}

#else

static inline void sdhci_writel(struct sdhci_host *host, u32 val, int reg)
{
	writel(val, host->ioaddr + reg);
}

static inline void sdhci_writew(struct sdhci_host *host, u16 val, int reg)
{
	writew(val, host->ioaddr + reg);
}

static inline void sdhci_writeb(struct sdhci_host *host, u8 val, int reg)
{
	writeb(val, host->ioaddr + reg);
}

static inline u32 sdhci_readl(struct sdhci_host *host, int reg)
{
	return readl(host->ioaddr + reg);
}

static inline u16 sdhci_readw(struct sdhci_host *host, int reg)
{
	return readw(host->ioaddr + reg);
}

static inline u8 sdhci_readb(struct sdhci_host *host, int reg)
{
	return readb(host->ioaddr + reg);
}

#endif /* CONFIG_MMC_SDHCI_IO_ACCESSORS */

struct sdhci_host *sdhci_alloc_host(struct device *dev, size_t priv_size);
void sdhci_free_host(struct sdhci_host *host);

static inline void *sdhci_priv(struct sdhci_host *host)
{
	return host->private;
}

void __sdhci_read_caps(struct sdhci_host *host, const u16 *ver,
		       const u32 *caps, const u32 *caps1);
int sdhci_setup_host(struct sdhci_host *host);
void sdhci_cleanup_host(struct sdhci_host *host);
int __sdhci_add_host(struct sdhci_host *host);
int sdhci_add_host(struct sdhci_host *host);
void sdhci_remove_host(struct sdhci_host *host, int dead);

static inline void sdhci_read_caps(struct sdhci_host *host)
{
	__sdhci_read_caps(host, NULL, NULL, NULL);
}

u16 sdhci_calc_clk(struct sdhci_host *host, unsigned int clock,
		   unsigned int *actual_clock);
void sdhci_set_clock(struct sdhci_host *host, unsigned int clock);
void sdhci_enable_clk(struct sdhci_host *host, u16 clk);
void sdhci_set_power(struct sdhci_host *host, unsigned char mode,
		     unsigned short vdd);
void sdhci_set_power_and_bus_voltage(struct sdhci_host *host,
				     unsigned char mode,
				     unsigned short vdd);
void sdhci_set_power_noreg(struct sdhci_host *host, unsigned char mode,
			   unsigned short vdd);
int sdhci_get_cd_nogpio(struct mmc_host *mmc);
void sdhci_request(struct mmc_host *mmc, struct mmc_request *mrq);
int sdhci_request_atomic(struct mmc_host *mmc, struct mmc_request *mrq);
void sdhci_set_bus_width(struct sdhci_host *host, int width);
void sdhci_reset(struct sdhci_host *host, u8 mask);
void sdhci_set_uhs_signaling(struct sdhci_host *host, unsigned timing);
int sdhci_execute_tuning(struct mmc_host *mmc, u32 opcode);
int __sdhci_execute_tuning(struct sdhci_host *host, u32 opcode);
void sdhci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios);
int sdhci_start_signal_voltage_switch(struct mmc_host *mmc,
				      struct mmc_ios *ios);
void sdhci_enable_sdio_irq(struct mmc_host *mmc, int enable);
void sdhci_adma_write_desc(struct sdhci_host *host, void **desc,
			   dma_addr_t addr, int len, unsigned int cmd);

#ifdef CONFIG_PM
int sdhci_suspend_host(struct sdhci_host *host);
int sdhci_resume_host(struct sdhci_host *host);
int sdhci_runtime_suspend_host(struct sdhci_host *host);
int sdhci_runtime_resume_host(struct sdhci_host *host, int soft_reset);
#endif

void sdhci_cqe_enable(struct mmc_host *mmc);
void sdhci_cqe_disable(struct mmc_host *mmc, bool recovery);
bool sdhci_cqe_irq(struct sdhci_host *host, u32 intmask, int *cmd_error,
		   int *data_error);

void sdhci_dumpregs(struct sdhci_host *host);
void sdhci_enable_v4_mode(struct sdhci_host *host);

void sdhci_start_tuning(struct sdhci_host *host);
void sdhci_end_tuning(struct sdhci_host *host);
void sdhci_reset_tuning(struct sdhci_host *host);
void sdhci_send_tuning(struct sdhci_host *host, u32 opcode);
void sdhci_abort_tuning(struct sdhci_host *host, u32 opcode);
void sdhci_switch_external_dma(struct sdhci_host *host, bool en);
void sdhci_set_data_timeout_irq(struct sdhci_host *host, bool enable);
void __sdhci_set_timeout(struct sdhci_host *host, struct mmc_command *cmd);

#endif /* __SDHCI_HW_H */
