/*
 *  FUJITSU Extended Socket Network Device driver
 *  Copyright (c) 2015 FUJITSU LIMITED
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 */

#ifndef FJES_REGS_H_
#define FJES_REGS_H_

#include <linux/bitops.h>

#define XSCT_DEVICE_REGISTER_SIZE 0x1000

/* register offset */
/* Information registers */
#define XSCT_OWNER_EPID     0x0000  /* Owner EPID */
#define XSCT_MAX_EP         0x0004  /* Maximum EP */

/* Device Control registers */
#define XSCT_DCTL           0x0010  /* Device Control */

/* Command Control registers */
#define XSCT_CR             0x0020  /* Command request */
#define XSCT_CS             0x0024  /* Command status */
#define XSCT_SHSTSAL        0x0028  /* Share status address Low */
#define XSCT_SHSTSAH        0x002C  /* Share status address High */

#define XSCT_REQBL          0x0034  /* Request Buffer length */
#define XSCT_REQBAL         0x0038  /* Request Buffer Address Low */
#define XSCT_REQBAH         0x003C  /* Request Buffer Address High */

#define XSCT_RESPBL         0x0044  /* Response Buffer Length */
#define XSCT_RESPBAL        0x0048  /* Response Buffer Address Low */
#define XSCT_RESPBAH        0x004C  /* Response Buffer Address High */

/* Interrupt Control registers */
#define XSCT_IS             0x0080  /* Interrupt status */
#define XSCT_IMS            0x0084  /* Interrupt mask set */
#define XSCT_IMC            0x0088  /* Interrupt mask clear */
#define XSCT_IG             0x008C  /* Interrupt generator */
#define XSCT_ICTL           0x0090  /* Interrupt control */

/* register structure */
/* Information registers */
union REG_OWNER_EPID {
	struct {
		__le32 epid:16;
		__le32:16;
	} bits;
	__le32 reg;
};

union REG_MAX_EP {
	struct {
		__le32 maxep:16;
		__le32:16;
	} bits;
	__le32 reg;
};

/* Device Control registers */
union REG_DCTL {
	struct {
		__le32 reset:1;
		__le32 rsv0:15;
		__le32 rsv1:16;
	} bits;
	__le32 reg;
};

/* Command Control registers */
union REG_CR {
	struct {
		__le32 req_code:16;
		__le32 err_info:14;
		__le32 error:1;
		__le32 req_start:1;
	} bits;
	__le32 reg;
};

union REG_CS {
	struct {
		__le32 req_code:16;
		__le32 rsv0:14;
		__le32 busy:1;
		__le32 complete:1;
	} bits;
	__le32 reg;
};

/* Interrupt Control registers */
union REG_ICTL {
	struct {
		__le32 automak:1;
		__le32 rsv0:31;
	} bits;
	__le32 reg;
};

enum REG_ICTL_MASK {
	REG_ICTL_MASK_INFO_UPDATE     = 1 << 20,
	REG_ICTL_MASK_DEV_STOP_REQ    = 1 << 19,
	REG_ICTL_MASK_TXRX_STOP_REQ   = 1 << 18,
	REG_ICTL_MASK_TXRX_STOP_DONE  = 1 << 17,
	REG_ICTL_MASK_RX_DATA         = 1 << 16,
	REG_ICTL_MASK_ALL             = GENMASK(20, 16),
};

enum REG_IS_MASK {
	REG_IS_MASK_IS_ASSERT	= 1 << 31,
	REG_IS_MASK_EPID	= GENMASK(15, 0),
};

struct fjes_hw;

u32 fjes_hw_rd32(struct fjes_hw *hw, u32 reg);

#define wr32(reg, val) \
do { \
	u8 *base = hw->base; \
	writel((val), &base[(reg)]); \
} while (0)

#define rd32(reg) (fjes_hw_rd32(hw, reg))

#endif /* FJES_REGS_H_ */
