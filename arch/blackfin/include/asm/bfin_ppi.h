/*
 * bfin_ppi.h - interface to Blackfin PPIs
 *
 * Copyright 2005-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_BFIN_PPI_H__
#define __ASM_BFIN_PPI_H__

#include <linux/types.h>

/*
 * All Blackfin system MMRs are padded to 32bits even if the register
 * itself is only 16bits.  So use a helper macro to streamline this.
 */
#define __BFP(m) u16 m; u16 __pad_##m

/*
 * bfin ppi registers layout
 */
struct bfin_ppi_regs {
	__BFP(control);
	__BFP(status);
	__BFP(count);
	__BFP(delay);
	__BFP(frame);
};

/*
 * bfin eppi registers layout
 */
struct bfin_eppi_regs {
	__BFP(status);
	__BFP(hcount);
	__BFP(hdelay);
	__BFP(vcount);
	__BFP(vdelay);
	__BFP(frame);
	__BFP(line);
	__BFP(clkdiv);
	u32 control;
	u32 fs1w_hbl;
	u32 fs1p_avpl;
	u32 fs2w_lvb;
	u32 fs2p_lavf;
	u32 clip;
};

#undef __BFP

#endif
