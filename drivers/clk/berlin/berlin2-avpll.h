/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2014 Marvell Technology Group Ltd.
 *
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Alexandre Belloni <alexandre.belloni@free-electrons.com>
 */
#ifndef __BERLIN2_AVPLL_H
#define __BERLIN2_AVPLL_H

#define BERLIN2_AVPLL_BIT_QUIRK		BIT(0)
#define BERLIN2_AVPLL_SCRAMBLE_QUIRK	BIT(1)

int berlin2_avpll_vco_register(void __iomem *base, const char *name,
	   const char *parent_name, u8 vco_flags, unsigned long flags);

int berlin2_avpll_channel_register(void __iomem *base, const char *name,
		       u8 index, const char *parent_name, u8 ch_flags,
		       unsigned long flags);

#endif /* __BERLIN2_AVPLL_H */
