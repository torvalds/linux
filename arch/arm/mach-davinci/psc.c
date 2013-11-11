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
#include <linux/init.h>
#include <linux/io.h>

#include <mach/cputype.h>
#include <mach/psc.h>

#include "clock.h"

/* Return nonzero iff the domain's clock is active */
int __init davinci_psc_is_clk_active(unsigned int ctlr, unsigned int id)
{
	void __iomem *psc_base;
	u32 mdstat;
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	if (!soc_info->psc_bases || (ctlr >= soc_info->psc_bases_num)) {
		pr_warn("PSC: Bad psc data: 0x%x[%d]\n",
				(int)soc_info->psc_bases, ctlr);
		return 0;
	}

	psc_base = ioremap(soc_info->psc_bases[ctlr], SZ_4K);
	mdstat = __raw_readl(psc_base + MDSTAT + 4 * id);
	iounmap(psc_base);

	/* if clocked, state can be "Enable" or "SyncReset" */
	return mdstat & BIT(12);
}

/* Control "reset" line associated with PSC domain */
void davinci_psc_reset(unsigned int ctlr, unsigned int id, bool reset)
{
	u32 mdctl;
	void __iomem *psc_base;
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	if (!soc_info->psc_bases || (ctlr >= soc_info->psc_bases_num)) {
		pr_warn("PSC: Bad psc data: 0x%x[%d]\n",
				(int)soc_info->psc_bases, ctlr);
		return;
	}

	psc_base = ioremap(soc_info->psc_bases[ctlr], SZ_4K);

	mdctl = readl(psc_base + MDCTL + 4 * id);
	if (reset)
		mdctl &= ~MDCTL_LRST;
	else
		mdctl |= MDCTL_LRST;
	writel(mdctl, psc_base + MDCTL + 4 * id);

	iounmap(psc_base);
}

/* Enable or disable a PSC domain */
void davinci_psc_config(unsigned int domain, unsigned int ctlr,
		unsigned int id, bool enable, u32 flags)
{
	u32 epcpr, ptcmd, ptstat, pdstat, pdctl, mdstat, mdctl;
	void __iomem *psc_base;
	struct davinci_soc_info *soc_info = &davinci_soc_info;
	u32 next_state = PSC_STATE_ENABLE;

	if (!soc_info->psc_bases || (ctlr >= soc_info->psc_bases_num)) {
		pr_warn("PSC: Bad psc data: 0x%x[%d]\n",
				(int)soc_info->psc_bases, ctlr);
		return;
	}

	psc_base = ioremap(soc_info->psc_bases[ctlr], SZ_4K);

	if (!enable) {
		if (flags & PSC_SWRSTDISABLE)
			next_state = PSC_STATE_SWRSTDISABLE;
		else
			next_state = PSC_STATE_DISABLE;
	}

	mdctl = __raw_readl(psc_base + MDCTL + 4 * id);
	mdctl &= ~MDSTAT_STATE_MASK;
	mdctl |= next_state;
	if (flags & PSC_FORCE)
		mdctl |= MDCTL_FORCE;
	__raw_writel(mdctl, psc_base + MDCTL + 4 * id);

	pdstat = __raw_readl(psc_base + PDSTAT + 4 * domain);
	if ((pdstat & PDSTAT_STATE_MASK) == 0) {
		pdctl = __raw_readl(psc_base + PDCTL + 4 * domain);
		pdctl |= PDCTL_NEXT;
		__raw_writel(pdctl, psc_base + PDCTL + 4 * domain);

		ptcmd = 1 << domain;
		__raw_writel(ptcmd, psc_base + PTCMD);

		do {
			epcpr = __raw_readl(psc_base + EPCPR);
		} while ((((epcpr >> domain) & 1) == 0));

		pdctl = __raw_readl(psc_base + PDCTL + 4 * domain);
		pdctl |= PDCTL_EPCGOOD;
		__raw_writel(pdctl, psc_base + PDCTL + 4 * domain);
	} else {
		ptcmd = 1 << domain;
		__raw_writel(ptcmd, psc_base + PTCMD);
	}

	do {
		ptstat = __raw_readl(psc_base + PTSTAT);
	} while (!(((ptstat >> domain) & 1) == 0));

	do {
		mdstat = __raw_readl(psc_base + MDSTAT + 4 * id);
	} while (!((mdstat & MDSTAT_STATE_MASK) == next_state));

	iounmap(psc_base);
}
