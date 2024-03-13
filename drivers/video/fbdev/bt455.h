/*
 *	linux/drivers/video/bt455.h
 *
 *	Copyright 2003  Thiemo Seufer <seufer@csv.ica.uni-stuttgart.de>
 *	Copyright 2016  Maciej W. Rozycki <macro@linux-mips.org>
 *
 *	This file is subject to the terms and conditions of the GNU General
 *	Public License. See the file COPYING in the main directory of this
 *	archive for more details.
 */
#include <linux/types.h>

/*
 * Bt455 byte-wide registers, 32-bit aligned.
 */
struct bt455_regs {
	volatile u8 addr_cmap;
	u8 pad0[3];
	volatile u8 addr_cmap_data;
	u8 pad1[3];
	volatile u8 addr_clr;
	u8 pad2[3];
	volatile u8 addr_ovly;
	u8 pad3[3];
};

static inline void bt455_select_reg(struct bt455_regs *regs, int ir)
{
	mb();
	regs->addr_cmap = ir & 0x0f;
}

static inline void bt455_reset_reg(struct bt455_regs *regs)
{
	mb();
	regs->addr_clr = 0;
}

/*
 * Read/write to a Bt455 color map register.
 */
static inline void bt455_read_cmap_next(struct bt455_regs *regs, u8 *grey)
{
	mb();
	regs->addr_cmap_data;
	rmb();
	*grey = regs->addr_cmap_data & 0xf;
	rmb();
	regs->addr_cmap_data;
}

static inline void bt455_write_cmap_next(struct bt455_regs *regs, u8 grey)
{
	wmb();
	regs->addr_cmap_data = 0x0;
	wmb();
	regs->addr_cmap_data = grey & 0xf;
	wmb();
	regs->addr_cmap_data = 0x0;
}

static inline void bt455_write_ovly_next(struct bt455_regs *regs, u8 grey)
{
	wmb();
	regs->addr_ovly = 0x0;
	wmb();
	regs->addr_ovly = grey & 0xf;
	wmb();
	regs->addr_ovly = 0x0;
}

static inline void bt455_read_cmap_entry(struct bt455_regs *regs,
					 int cr, u8 *grey)
{
	bt455_select_reg(regs, cr);
	bt455_read_cmap_next(regs, grey);
}

static inline void bt455_write_cmap_entry(struct bt455_regs *regs,
					  int cr, u8 grey)
{
	bt455_select_reg(regs, cr);
	bt455_write_cmap_next(regs, grey);
}

static inline void bt455_write_ovly_entry(struct bt455_regs *regs, u8 grey)
{
	bt455_reset_reg(regs);
	bt455_write_ovly_next(regs, grey);
}
