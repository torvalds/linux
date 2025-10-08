/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ZL3073X_REGS_H
#define _ZL3073X_REGS_H

#include <linux/bitfield.h>
#include <linux/bits.h>

/*
 * Register address structure:
 * ===========================
 *  25        19 18  16 15     7 6           0
 * +------------------------------------------+
 * | max_offset | size |  page  | page_offset |
 * +------------------------------------------+
 *
 * page_offset ... <0x00..0x7F>
 * page .......... HW page number
 * size .......... register byte size (1, 2, 4 or 6)
 * max_offset .... maximal offset for indexed registers
 *                 (for non-indexed regs max_offset == page_offset)
 */

#define ZL_REG_OFFSET_MASK	GENMASK(6, 0)
#define ZL_REG_PAGE_MASK	GENMASK(15, 7)
#define ZL_REG_SIZE_MASK	GENMASK(18, 16)
#define ZL_REG_MAX_OFFSET_MASK	GENMASK(25, 19)
#define ZL_REG_ADDR_MASK	GENMASK(15, 0)

#define ZL_REG_OFFSET(_reg)	FIELD_GET(ZL_REG_OFFSET_MASK, _reg)
#define ZL_REG_PAGE(_reg)	FIELD_GET(ZL_REG_PAGE_MASK, _reg)
#define ZL_REG_MAX_OFFSET(_reg)	FIELD_GET(ZL_REG_MAX_OFFSET_MASK, _reg)
#define ZL_REG_SIZE(_reg)	FIELD_GET(ZL_REG_SIZE_MASK, _reg)
#define ZL_REG_ADDR(_reg)	FIELD_GET(ZL_REG_ADDR_MASK, _reg)

/**
 * ZL_REG_IDX - define indexed register
 * @_idx: index of register to access
 * @_page: register page
 * @_offset: register offset in page
 * @_size: register byte size (1, 2, 4 or 6)
 * @_items: number of register indices
 * @_stride: stride between items in bytes
 *
 * All parameters except @_idx should be constant.
 */
#define ZL_REG_IDX(_idx, _page, _offset, _size, _items, _stride)	\
	(FIELD_PREP(ZL_REG_OFFSET_MASK,					\
		    (_offset) + (_idx) * (_stride))		|	\
	 FIELD_PREP_CONST(ZL_REG_PAGE_MASK, _page)		|	\
	 FIELD_PREP_CONST(ZL_REG_SIZE_MASK, _size)		|	\
	 FIELD_PREP_CONST(ZL_REG_MAX_OFFSET_MASK,			\
			  (_offset) + ((_items) - 1) * (_stride)))

/**
 * ZL_REG - define simple (non-indexed) register
 * @_page: register page
 * @_offset: register offset in page
 * @_size: register byte size (1, 2, 4 or 6)
 *
 * All parameters should be constant.
 */
#define ZL_REG(_page, _offset, _size)					\
	ZL_REG_IDX(0, _page, _offset, _size, 1, 0)

/**************************
 * Register Page 0, General
 **************************/

#define ZL_REG_INFO				ZL_REG(0, 0x00, 1)
#define ZL_INFO_READY				BIT(7)

#define ZL_REG_ID				ZL_REG(0, 0x01, 2)
#define ZL_REG_REVISION				ZL_REG(0, 0x03, 2)
#define ZL_REG_FW_VER				ZL_REG(0, 0x05, 2)
#define ZL_REG_CUSTOM_CONFIG_VER		ZL_REG(0, 0x07, 4)

#define ZL_REG_RESET_STATUS			ZL_REG(0, 0x18, 1)
#define ZL_REG_RESET_STATUS_RESET		BIT(0)

/*************************
 * Register Page 2, Status
 *************************/

#define ZL_REG_REF_MON_STATUS(_idx)					\
	ZL_REG_IDX(_idx, 2, 0x02, 1, ZL3073X_NUM_REFS, 1)
#define ZL_REF_MON_STATUS_OK			0 /* all bits zeroed */

#define ZL_REG_DPLL_MON_STATUS(_idx)					\
	ZL_REG_IDX(_idx, 2, 0x10, 1, ZL3073X_MAX_CHANNELS, 1)
#define ZL_DPLL_MON_STATUS_STATE		GENMASK(1, 0)
#define ZL_DPLL_MON_STATUS_STATE_ACQUIRING	0
#define ZL_DPLL_MON_STATUS_STATE_LOCK		1
#define ZL_DPLL_MON_STATUS_STATE_HOLDOVER	2
#define ZL_DPLL_MON_STATUS_HO_READY		BIT(2)

#define ZL_REG_DPLL_REFSEL_STATUS(_idx)					\
	ZL_REG_IDX(_idx, 2, 0x30, 1, ZL3073X_MAX_CHANNELS, 1)
#define ZL_DPLL_REFSEL_STATUS_REFSEL		GENMASK(3, 0)
#define ZL_DPLL_REFSEL_STATUS_STATE		GENMASK(6, 4)
#define ZL_DPLL_REFSEL_STATUS_STATE_LOCK	4

#define ZL_REG_REF_FREQ(_idx)						\
	ZL_REG_IDX(_idx, 2, 0x44, 4, ZL3073X_NUM_REFS, 4)

/**********************
 * Register Page 4, Ref
 **********************/

#define ZL_REG_REF_PHASE_ERR_READ_RQST		ZL_REG(4, 0x0f, 1)
#define ZL_REF_PHASE_ERR_READ_RQST_RD		BIT(0)

#define ZL_REG_REF_FREQ_MEAS_CTRL		ZL_REG(4, 0x1c, 1)
#define ZL_REF_FREQ_MEAS_CTRL			GENMASK(1, 0)
#define ZL_REF_FREQ_MEAS_CTRL_REF_FREQ		1
#define ZL_REF_FREQ_MEAS_CTRL_REF_FREQ_OFF	2
#define ZL_REF_FREQ_MEAS_CTRL_DPLL_FREQ_OFF	3

#define ZL_REG_REF_FREQ_MEAS_MASK_3_0		ZL_REG(4, 0x1d, 1)
#define ZL_REF_FREQ_MEAS_MASK_3_0(_ref)		BIT(_ref)

#define ZL_REG_REF_FREQ_MEAS_MASK_4		ZL_REG(4, 0x1e, 1)
#define ZL_REF_FREQ_MEAS_MASK_4(_ref)		BIT((_ref) - 8)

#define ZL_REG_DPLL_MEAS_REF_FREQ_CTRL		ZL_REG(4, 0x1f, 1)
#define ZL_DPLL_MEAS_REF_FREQ_CTRL_EN		BIT(0)
#define ZL_DPLL_MEAS_REF_FREQ_CTRL_IDX		GENMASK(6, 4)

#define ZL_REG_REF_PHASE(_idx)						\
	ZL_REG_IDX(_idx, 4, 0x20, 6, ZL3073X_NUM_REFS, 6)

/***********************
 * Register Page 5, DPLL
 ***********************/

#define ZL_REG_DPLL_MODE_REFSEL(_idx)					\
	ZL_REG_IDX(_idx, 5, 0x04, 1, ZL3073X_MAX_CHANNELS, 4)
#define ZL_DPLL_MODE_REFSEL_MODE		GENMASK(2, 0)
#define ZL_DPLL_MODE_REFSEL_MODE_FREERUN	0
#define ZL_DPLL_MODE_REFSEL_MODE_HOLDOVER	1
#define ZL_DPLL_MODE_REFSEL_MODE_REFLOCK	2
#define ZL_DPLL_MODE_REFSEL_MODE_AUTO		3
#define ZL_DPLL_MODE_REFSEL_MODE_NCO		4
#define ZL_DPLL_MODE_REFSEL_REF			GENMASK(7, 4)

#define ZL_REG_DPLL_MEAS_CTRL			ZL_REG(5, 0x50, 1)
#define ZL_DPLL_MEAS_CTRL_EN			BIT(0)
#define ZL_DPLL_MEAS_CTRL_AVG_FACTOR		GENMASK(7, 4)

#define ZL_REG_DPLL_MEAS_IDX			ZL_REG(5, 0x51, 1)
#define ZL_DPLL_MEAS_IDX			GENMASK(2, 0)

#define ZL_REG_DPLL_PHASE_ERR_READ_MASK		ZL_REG(5, 0x54, 1)

#define ZL_REG_DPLL_PHASE_ERR_DATA(_idx)				\
	ZL_REG_IDX(_idx, 5, 0x55, 6, ZL3073X_MAX_CHANNELS, 6)

/***********************************
 * Register Page 9, Synth and Output
 ***********************************/

#define ZL_REG_SYNTH_CTRL(_idx)						\
	ZL_REG_IDX(_idx, 9, 0x00, 1, ZL3073X_NUM_SYNTHS, 1)
#define ZL_SYNTH_CTRL_EN			BIT(0)
#define ZL_SYNTH_CTRL_DPLL_SEL			GENMASK(6, 4)

#define ZL_REG_SYNTH_PHASE_SHIFT_CTRL		ZL_REG(9, 0x1e, 1)
#define ZL_REG_SYNTH_PHASE_SHIFT_MASK		ZL_REG(9, 0x1f, 1)
#define ZL_REG_SYNTH_PHASE_SHIFT_INTVL		ZL_REG(9, 0x20, 1)
#define ZL_REG_SYNTH_PHASE_SHIFT_DATA		ZL_REG(9, 0x21, 2)

#define ZL_REG_OUTPUT_CTRL(_idx)					\
	ZL_REG_IDX(_idx, 9, 0x28, 1, ZL3073X_NUM_OUTS, 1)
#define ZL_OUTPUT_CTRL_EN			BIT(0)
#define ZL_OUTPUT_CTRL_SYNTH_SEL		GENMASK(6, 4)

/*******************************
 * Register Page 10, Ref Mailbox
 *******************************/

#define ZL_REG_REF_MB_MASK			ZL_REG(10, 0x02, 2)

#define ZL_REG_REF_MB_SEM			ZL_REG(10, 0x04, 1)
#define ZL_REF_MB_SEM_WR			BIT(0)
#define ZL_REF_MB_SEM_RD			BIT(1)

#define ZL_REG_REF_FREQ_BASE			ZL_REG(10, 0x05, 2)
#define ZL_REG_REF_FREQ_MULT			ZL_REG(10, 0x07, 2)
#define ZL_REG_REF_RATIO_M			ZL_REG(10, 0x09, 2)
#define ZL_REG_REF_RATIO_N			ZL_REG(10, 0x0b, 2)

#define ZL_REG_REF_CONFIG			ZL_REG(10, 0x0d, 1)
#define ZL_REF_CONFIG_ENABLE			BIT(0)
#define ZL_REF_CONFIG_DIFF_EN			BIT(2)

#define ZL_REG_REF_PHASE_OFFSET_COMP		ZL_REG(10, 0x28, 6)

#define ZL_REG_REF_SYNC_CTRL			ZL_REG(10, 0x2e, 1)
#define ZL_REF_SYNC_CTRL_MODE			GENMASK(2, 0)
#define ZL_REF_SYNC_CTRL_MODE_REFSYNC_PAIR_OFF	0
#define ZL_REF_SYNC_CTRL_MODE_50_50_ESYNC_25_75	2

#define ZL_REG_REF_ESYNC_DIV			ZL_REG(10, 0x30, 4)
#define ZL_REF_ESYNC_DIV_1HZ			0

/********************************
 * Register Page 12, DPLL Mailbox
 ********************************/

#define ZL_REG_DPLL_MB_MASK			ZL_REG(12, 0x02, 2)

#define ZL_REG_DPLL_MB_SEM			ZL_REG(12, 0x04, 1)
#define ZL_DPLL_MB_SEM_WR			BIT(0)
#define ZL_DPLL_MB_SEM_RD			BIT(1)

#define ZL_REG_DPLL_REF_PRIO(_idx)					\
	ZL_REG_IDX(_idx, 12, 0x52, 1, ZL3073X_NUM_REFS / 2, 1)
#define ZL_DPLL_REF_PRIO_REF_P			GENMASK(3, 0)
#define ZL_DPLL_REF_PRIO_REF_N			GENMASK(7, 4)
#define ZL_DPLL_REF_PRIO_MAX			14
#define ZL_DPLL_REF_PRIO_NONE			15

/*********************************
 * Register Page 13, Synth Mailbox
 *********************************/

#define ZL_REG_SYNTH_MB_MASK			ZL_REG(13, 0x02, 2)

#define ZL_REG_SYNTH_MB_SEM			ZL_REG(13, 0x04, 1)
#define ZL_SYNTH_MB_SEM_WR			BIT(0)
#define ZL_SYNTH_MB_SEM_RD			BIT(1)

#define ZL_REG_SYNTH_FREQ_BASE			ZL_REG(13, 0x06, 2)
#define ZL_REG_SYNTH_FREQ_MULT			ZL_REG(13, 0x08, 4)
#define ZL_REG_SYNTH_FREQ_M			ZL_REG(13, 0x0c, 2)
#define ZL_REG_SYNTH_FREQ_N			ZL_REG(13, 0x0e, 2)

/**********************************
 * Register Page 14, Output Mailbox
 **********************************/
#define ZL_REG_OUTPUT_MB_MASK			ZL_REG(14, 0x02, 2)

#define ZL_REG_OUTPUT_MB_SEM			ZL_REG(14, 0x04, 1)
#define ZL_OUTPUT_MB_SEM_WR			BIT(0)
#define ZL_OUTPUT_MB_SEM_RD			BIT(1)

#define ZL_REG_OUTPUT_MODE			ZL_REG(14, 0x05, 1)
#define ZL_OUTPUT_MODE_CLOCK_TYPE		GENMASK(2, 0)
#define ZL_OUTPUT_MODE_CLOCK_TYPE_NORMAL	0
#define ZL_OUTPUT_MODE_CLOCK_TYPE_ESYNC		1
#define ZL_OUTPUT_MODE_SIGNAL_FORMAT		GENMASK(7, 4)
#define ZL_OUTPUT_MODE_SIGNAL_FORMAT_DISABLED	0
#define ZL_OUTPUT_MODE_SIGNAL_FORMAT_LVDS	1
#define ZL_OUTPUT_MODE_SIGNAL_FORMAT_DIFF	2
#define ZL_OUTPUT_MODE_SIGNAL_FORMAT_LOWVCM	3
#define ZL_OUTPUT_MODE_SIGNAL_FORMAT_2		4
#define ZL_OUTPUT_MODE_SIGNAL_FORMAT_1P		5
#define ZL_OUTPUT_MODE_SIGNAL_FORMAT_1N		6
#define ZL_OUTPUT_MODE_SIGNAL_FORMAT_2_INV	7
#define ZL_OUTPUT_MODE_SIGNAL_FORMAT_2_NDIV	12
#define ZL_OUTPUT_MODE_SIGNAL_FORMAT_2_NDIV_INV	15

#define ZL_REG_OUTPUT_DIV			ZL_REG(14, 0x0c, 4)
#define ZL_REG_OUTPUT_WIDTH			ZL_REG(14, 0x10, 4)
#define ZL_REG_OUTPUT_ESYNC_PERIOD		ZL_REG(14, 0x14, 4)
#define ZL_REG_OUTPUT_ESYNC_WIDTH		ZL_REG(14, 0x18, 4)
#define ZL_REG_OUTPUT_PHASE_COMP		ZL_REG(14, 0x20, 4)

/*
 * Register Page 255 - HW registers access
 */
#define ZL_REG_HWREG_OP				ZL_REG(0xff, 0x00, 1)
#define ZL_HWREG_OP_WRITE			0x28
#define ZL_HWREG_OP_READ			0x29
#define ZL_HWREG_OP_PENDING			BIT(1)

#define ZL_REG_HWREG_ADDR			ZL_REG(0xff, 0x04, 4)
#define ZL_REG_HWREG_WRITE_DATA			ZL_REG(0xff, 0x08, 4)
#define ZL_REG_HWREG_READ_DATA			ZL_REG(0xff, 0x0c, 4)

/*
 * Registers available in flash mode
 */
#define ZL_REG_FLASH_HASH			ZL_REG(0, 0x78, 4)
#define ZL_REG_FLASH_FAMILY			ZL_REG(0, 0x7c, 1)
#define ZL_REG_FLASH_RELEASE			ZL_REG(0, 0x7d, 1)

#define ZL_REG_HOST_CONTROL			ZL_REG(1, 0x02, 1)
#define ZL_HOST_CONTROL_ENABLE			BIT(0)

#define ZL_REG_IMAGE_START_ADDR			ZL_REG(1, 0x04, 4)
#define ZL_REG_IMAGE_SIZE			ZL_REG(1, 0x08, 4)
#define ZL_REG_FLASH_INDEX_READ			ZL_REG(1, 0x0c, 4)
#define ZL_REG_FLASH_INDEX_WRITE		ZL_REG(1, 0x10, 4)
#define ZL_REG_FILL_PATTERN			ZL_REG(1, 0x14, 4)

#define ZL_REG_WRITE_FLASH			ZL_REG(1, 0x18, 1)
#define ZL_WRITE_FLASH_OP			GENMASK(2, 0)
#define ZL_WRITE_FLASH_OP_DONE			0x0
#define ZL_WRITE_FLASH_OP_SECTORS		0x2
#define ZL_WRITE_FLASH_OP_PAGE			0x3
#define ZL_WRITE_FLASH_OP_COPY_PAGE		0x4

#define ZL_REG_FLASH_INFO			ZL_REG(2, 0x00, 1)
#define ZL_FLASH_INFO_SECTOR_SIZE		GENMASK(3, 0)
#define ZL_FLASH_INFO_SECTOR_4K			0
#define ZL_FLASH_INFO_SECTOR_64K		1

#define ZL_REG_ERROR_COUNT			ZL_REG(2, 0x04, 4)
#define ZL_REG_ERROR_CAUSE			ZL_REG(2, 0x08, 4)

#define ZL_REG_OP_STATE				ZL_REG(2, 0x14, 1)
#define ZL_OP_STATE_NO_COMMAND			0
#define ZL_OP_STATE_PENDING			1
#define ZL_OP_STATE_DONE			2

#endif /* _ZL3073X_REGS_H */
