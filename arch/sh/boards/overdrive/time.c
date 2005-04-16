/*
 * arch/sh/boards/overdrive/time.c
 *
 * Copyright (C) 2000 Stuart Menefy (stuart.menefy@st.com)
 * Copyright (C) 2002 Paul Mundt (lethal@chaoticdreams.org)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * STMicroelectronics Overdrive Support.
 */

void od_time_init(void)
{
	struct frqcr_data {
		unsigned short frqcr;
		struct {
			unsigned char multiplier;
			unsigned char divisor;
		} factor[3];
	};

	static struct frqcr_data st40_frqcr_table[] = {		
		{ 0x000, {{1,1}, {1,1}, {1,2}}},
		{ 0x002, {{1,1}, {1,1}, {1,4}}},
		{ 0x004, {{1,1}, {1,1}, {1,8}}},
		{ 0x008, {{1,1}, {1,2}, {1,2}}},
		{ 0x00A, {{1,1}, {1,2}, {1,4}}},
		{ 0x00C, {{1,1}, {1,2}, {1,8}}},
		{ 0x011, {{1,1}, {2,3}, {1,6}}},
		{ 0x013, {{1,1}, {2,3}, {1,3}}},
		{ 0x01A, {{1,1}, {1,2}, {1,4}}},
		{ 0x01C, {{1,1}, {1,2}, {1,8}}},
		{ 0x023, {{1,1}, {2,3}, {1,3}}},
		{ 0x02C, {{1,1}, {1,2}, {1,8}}},
		{ 0x048, {{1,2}, {1,2}, {1,4}}},
		{ 0x04A, {{1,2}, {1,2}, {1,6}}},
		{ 0x04C, {{1,2}, {1,2}, {1,8}}},
		{ 0x05A, {{1,2}, {1,3}, {1,6}}},
		{ 0x05C, {{1,2}, {1,3}, {1,6}}},
		{ 0x063, {{1,2}, {1,4}, {1,4}}},
		{ 0x06C, {{1,2}, {1,4}, {1,8}}},
		{ 0x091, {{1,3}, {1,3}, {1,6}}},
		{ 0x093, {{1,3}, {1,3}, {1,6}}},
		{ 0x0A3, {{1,3}, {1,6}, {1,6}}},
		{ 0x0DA, {{1,4}, {1,4}, {1,8}}},
		{ 0x0DC, {{1,4}, {1,4}, {1,8}}},
		{ 0x0EC, {{1,4}, {1,8}, {1,8}}},
		{ 0x123, {{1,4}, {1,4}, {1,8}}},
		{ 0x16C, {{1,4}, {1,8}, {1,8}}},
	};

	struct memclk_data {
		unsigned char multiplier;
		unsigned char divisor;
	};
	static struct memclk_data st40_memclk_table[8] = {
		{1,1},	// 000
		{1,2},	// 001
		{1,3},	// 010
		{2,3},	// 011
		{1,4},	// 100
		{1,6},	// 101
		{1,8},	// 110
		{1,8}	// 111
	};

	unsigned long pvr;

	/* 
	 * This should probably be moved into the SH3 probing code, and then
	 * use the processor structure to determine which CPU we are running
	 * on.
	 */
	pvr = ctrl_inl(CCN_PVR);
	printk("PVR %08x\n", pvr);

	if (((pvr >> CCN_PVR_CHIP_SHIFT) & CCN_PVR_CHIP_MASK) == CCN_PVR_CHIP_ST40STB1) {
		/* 
		 * Unfortunatly the STB1 FRQCR values are different from the
		 * 7750 ones.
		 */
		struct frqcr_data *d;
		int a;
		unsigned long memclkcr;
		struct memclk_data *e;

		for (a=0; a<ARRAY_SIZE(st40_frqcr_table); a++) {
			d = &st40_frqcr_table[a];
			if (d->frqcr == (frqcr & 0x1ff))
				break;
		}
		if (a == ARRAY_SIZE(st40_frqcr_table)) {
			d = st40_frqcr_table;
			printk("ERROR: Unrecognised FRQCR value, using default multipliers\n");
		}

		memclkcr = ctrl_inl(CLOCKGEN_MEMCLKCR);
		e = &st40_memclk_table[memclkcr & MEMCLKCR_RATIO_MASK];

		printk("Clock multipliers: CPU: %d/%d Bus: %d/%d Mem: %d/%d Periph: %d/%d\n",
		       d->factor[0].multiplier, d->factor[0].divisor,
		       d->factor[1].multiplier, d->factor[1].divisor,
		       e->multiplier,           e->divisor,
		       d->factor[2].multiplier, d->factor[2].divisor);
		
		current_cpu_data.master_clock = current_cpu_data.module_clock *
						d->factor[2].divisor /
						d->factor[2].multiplier;
		current_cpu_data.bus_clock    = current_cpu_data.master_clock *
						d->factor[1].multiplier /
						d->factor[1].divisor;
		current_cpu_data.memory_clock = current_cpu_data.master_clock *
						e->multiplier / e->divisor;
		current_cpu_data.cpu_clock    = current_cpu_data.master_clock *
						d->factor[0].multiplier /
						d->factor[0].divisor;
}

