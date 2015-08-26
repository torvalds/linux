/*
 * Freescale IPU PRG Register Definitions
 *
 * Copyright 2014-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/bitops.h>

#ifndef __IPU_PRG_H__
#define __IPU_PRG_H__

#define IPU_PR_CTRL		 0x00
#define IPU_PR_STATUS		 0x04
#define IPU_PR_QOS		 0x08
#define IPU_PR_REG_UPDATE	 0x0c
#define IPU_PR_STRIDE(ch)	(0x10 + (ch) * 4)
#define IPU_PR_CROP_LINE	 0x1c
#define IPU_PR_ADDR_THD		 0x20
#define IPU_PR_CH_BADDR(ch)	(0x24 + (ch) * 4)
#define IPU_PR_CH_OFFSET(ch)	(0x30 + (ch) * 4)
#define IPU_PR_CH_ILO(ch)	(0x3c + (ch) * 4)
#define IPU_PR_CH_HEIGHT(ch)	(0x48 + (ch) * 4)

#define IPU_PR_CTRL_CH_BYPASS(ch)		(0x1 << (ch))
#define IPU_PR_CTRL_SOFT_CH_ARID(ch, n)		((n) << ((ch) * 2 + 8))
#define IPU_PR_CTRL_SOFT_CH_ARID_MASK(ch)	(0x3 << ((ch) * 2 + 8))
#define IPU_PR_CTRL_CH_SO(ch, interlace)	((interlace) << ((ch) + 16))
#define IPU_PR_CTRL_CH_SO_MASK(ch)		(0x1 << ((ch) + 16))
#define IPU_PR_CTRL_CH_VFLIP(ch, vflip)		((vflip) << ((ch) + 19))
#define IPU_PR_CTRL_CH_VFLIP_MASK(ch)		(0x1 << ((ch) + 19))
#define IPU_PR_CTRL_CH_BLOCK_MODE(ch, mode)	((mode) << ((ch) + 22))
#define IPU_PR_CTRL_CH_BLOCK_MODE_MASK(ch)	(0x1 << ((ch) + 22))
#define IPU_PR_CTRL_CH_CNT_LOAD_EN(ch)		(0x1 << ((ch) + 25))
#define IPU_PR_CTRL_CH_CNT_LOAD_EN_MASK		(0x7 << 25)
#define IPU_PR_CTRL_SOFTRST			BIT(30)
#define IPU_PR_CTRL_SHADOW_EN			BIT(31)

#define IPU_PR_STATUS_BUF_RDY(ch, buf)		(1 << ((ch) * 2 + (buf)))

#define IPU_PR_QOS_PRI(id, qos)			((qos) << ((id) * 4))
#define IPU_PR_QOS_MASK(id)			(0xf << ((id) * 4))

#define IPU_PR_REG_UPDATE_EN			BIT(0)

#define IPU_PR_STRIDE_MASK			0x3fff

#define IPU_PR_CROP_LINE_NUM(ch, n)		((n) << ((ch) * 4))
#define IPU_PR_CROP_LINE_MASK(ch)		(0xf << ((ch) * 4))

#define IPU_PR_ADDR_THD_MASK			0xffffffff

#define IPU_PR_CH_BADDR_MASK			0xffffffff

#define IPU_PR_CH_OFFSET_MASK			0xffffffff

#define IPU_PR_CH_ILO_MASK			0x007fffff
#define IPU_PR_CH_ILO_NUM(ilo)			((ilo) & IPU_PR_CH_ILO_MASK)

#define IPU_PR_CH_HEIGHT_MASK			0x00000fff
#define IPU_PR_CH_HEIGHT_NUM(fh)		(((fh) - 1) & IPU_PR_CH_HEIGHT_MASK)
#define IPU_PR_CH_IPU_HEIGHT_MASK		0x0fff0000
#define IPU_PR_CH_IPU_HEIGHT_NUM(fh)		((((fh) - 1) << 16) & IPU_PR_CH_IPU_HEIGHT_MASK)

#endif /* __IPU_PRG_H__ */
