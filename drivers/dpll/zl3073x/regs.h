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

#define ZL_REG_ID				ZL_REG(0, 0x01, 2)
#define ZL_REG_REVISION				ZL_REG(0, 0x03, 2)
#define ZL_REG_FW_VER				ZL_REG(0, 0x05, 2)
#define ZL_REG_CUSTOM_CONFIG_VER		ZL_REG(0, 0x07, 4)

#endif /* _ZL3073X_REGS_H */
