/*
 * TI DaVinci Power and Sleep Controller (PSC)
 *
 * Copyright (C) 2006 Texas Instruments.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>

#include <mach/cputype.h>
#include <mach/hardware.h>
#include <mach/psc.h>
#include <mach/mux.h>

/* PSC register offsets */
#define EPCPR		0x070
#define PTCMD		0x120
#define PTSTAT		0x128
#define PDSTAT		0x200
#define PDCTL1		0x304
#define MDSTAT		0x800
#define MDCTL		0xA00

#define MDSTAT_STATE_MASK 0x1f

/* Return nonzero iff the domain's clock is active */
int __init davinci_psc_is_clk_active(unsigned int ctlr, unsigned int id)
{
	void __iomem *psc_base;
	u32 mdstat;
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	if (!soc_info->psc_bases || (ctlr >= soc_info->psc_bases_num)) {
		pr_warning("PSC: Bad psc data: 0x%x[%d]\n",
				(int)soc_info->psc_bases, ctlr);
		return 0;
	}

	psc_base = soc_info->psc_bases[ctlr];
	mdstat = __raw_readl(psc_base + MDSTAT + 4 * id);

	/* if clocked, state can be "Enable" or "SyncReset" */
	return mdstat & BIT(12);
}

/* Enable or disable a PSC domain */
void davinci_psc_config(unsigned int domain, unsigned int ctlr,
		unsigned int id, char enable)
{
	u32 epcpr, ptcmd, ptstat, pdstat, pdctl1, mdstat, mdctl;
	void __iomem *psc_base;
	struct davinci_soc_info *soc_info = &davinci_soc_info;
	u32 next_state = enable ? 0x3 : 0x2; /* 0x3 enables, 0x2 disables */

	if (!soc_info->psc_bases || (ctlr >= soc_info->psc_bases_num)) {
		pr_warning("PSC: Bad psc data: 0x%x[%d]\n",
				(int)soc_info->psc_bases, ctlr);
		return;
	}

	psc_base = soc_info->psc_bases[ctlr];

	mdctl = __raw_readl(psc_base + MDCTL + 4 * id);
	mdctl &= ~MDSTAT_STATE_MASK;
	mdctl |= next_state;
	__raw_writel(mdctl, psc_base + MDCTL + 4 * id);

	pdstat = __raw_readl(psc_base + PDSTAT);
	if ((pdstat & 0x00000001) == 0) {
		pdctl1 = __raw_readl(psc_base + PDCTL1);
		pdctl1 |= 0x1;
		__raw_writel(pdctl1, psc_base + PDCTL1);

		ptcmd = 1 << domain;
		__raw_writel(ptcmd, psc_base + PTCMD);

		do {
			epcpr = __raw_readl(psc_base + EPCPR);
		} while ((((epcpr >> domain) & 1) == 0));

		pdctl1 = __raw_readl(psc_base + PDCTL1);
		pdctl1 |= 0x100;
		__raw_writel(pdctl1, psc_base + PDCTL1);

		do {
			ptstat = __raw_readl(psc_base +
					       PTSTAT);
		} while (!(((ptstat >> domain) & 1) == 0));
	} else {
		ptcmd = 1 << domain;
		__raw_writel(ptcmd, psc_base + PTCMD);

		do {
			ptstat = __raw_readl(psc_base + PTSTAT);
		} while (!(((ptstat >> domain) & 1) == 0));
	}

	do {
		mdstat = __raw_readl(psc_base + MDSTAT + 4 * id);
	} while (!((mdstat & MDSTAT_STATE_MASK) == next_state));
}
