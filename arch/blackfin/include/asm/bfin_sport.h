/*
 * bfin_sport.h - interface to Blackfin SPORTs
 *
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */
#ifndef __BFIN_SPORT_H__
#define __BFIN_SPORT_H__


#include <linux/types.h>
#include <uapi/asm/bfin_sport.h>

/*
 * All Blackfin system MMRs are padded to 32bits even if the register
 * itself is only 16bits.  So use a helper macro to streamline this.
 */
#define __BFP(m) u16 m; u16 __pad_##m
struct sport_register {
	__BFP(tcr1);
	__BFP(tcr2);
	__BFP(tclkdiv);
	__BFP(tfsdiv);
	union {
		u32 tx32;
		u16 tx16;
	};
	u32 __pad_tx;
	union {
		u32 rx32;	/* use the anomaly wrapper below */
		u16 rx16;
	};
	u32 __pad_rx;
	__BFP(rcr1);
	__BFP(rcr2);
	__BFP(rclkdiv);
	__BFP(rfsdiv);
	__BFP(stat);
	__BFP(chnl);
	__BFP(mcmc1);
	__BFP(mcmc2);
	u32 mtcs0;
	u32 mtcs1;
	u32 mtcs2;
	u32 mtcs3;
	u32 mrcs0;
	u32 mrcs1;
	u32 mrcs2;
	u32 mrcs3;
};
#undef __BFP

struct bfin_snd_platform_data {
	const unsigned short *pin_req;
};

#define bfin_read_sport_rx32(base) \
({ \
	struct sport_register *__mmrs = (void *)base; \
	u32 __ret; \
	unsigned long flags; \
	if (ANOMALY_05000473) \
		local_irq_save(flags); \
	__ret = __mmrs->rx32; \
	if (ANOMALY_05000473) \
		local_irq_restore(flags); \
	__ret; \
})

#endif
