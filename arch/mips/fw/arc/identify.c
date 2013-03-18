/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * identify.c: identify machine by looking up system identifier
 *
 * Copyright (C) 1998 Thomas Bogendoerfer
 *
 * This code is based on arch/mips/sgi/kernel/system.c, which is
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 */
#include <linux/bug.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>

#include <asm/sgialib.h>
#include <asm/bootinfo.h>

struct smatch {
	char *arcname;
	char *liname;
	int flags;
};

static struct smatch mach_table[] = {
	{
		.arcname	= "SGI-IP22",
		.liname		= "SGI Indy",
		.flags		= PROM_FLAG_ARCS,
	}, {
		.arcname	= "SGI-IP27",
		.liname		= "SGI Origin",
		.flags		= PROM_FLAG_ARCS,
	}, {
		.arcname	= "SGI-IP28",
		.liname		= "SGI IP28",
		.flags		= PROM_FLAG_ARCS,
	}, {
		.arcname	= "SGI-IP30",
		.liname		= "SGI Octane",
		.flags		= PROM_FLAG_ARCS,
	}, {
		.arcname	= "SGI-IP32",
		.liname		= "SGI O2",
		.flags		= PROM_FLAG_ARCS,
	}, {
		.arcname	= "Microsoft-Jazz",
		.liname		= "Jazz MIPS_Magnum_4000",
		.flags		= 0,
	}, {
		.arcname	= "PICA-61",
		.liname		= "Jazz Acer_PICA_61",
		.flags		= 0,
	}, {
		.arcname	= "RM200PCI",
		.liname		= "SNI RM200_PCI",
		.flags		= PROM_FLAG_DONT_FREE_TEMP,
	}, {
		.arcname	= "RM200PCI-R5K",
		.liname		= "SNI RM200_PCI-R5K",
		.flags		= PROM_FLAG_DONT_FREE_TEMP,
	}
};

int prom_flags;

static struct smatch * __init string_to_mach(const char *s)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mach_table); i++) {
		if (!strcmp(s, mach_table[i].arcname))
			return &mach_table[i];
	}

	panic("Yeee, could not determine architecture type <%s>", s);
}

char *system_type;

const char *get_system_type(void)
{
	return system_type;
}

void __init prom_identify_arch(void)
{
	pcomponent *p;
	struct smatch *mach;
	const char *iname;

	/*
	 * The root component tells us what machine architecture we have here.
	 */
	p = ArcGetChild(PROM_NULL_COMPONENT);
	if (p == NULL) {
#ifdef CONFIG_SGI_IP27
		/* IP27 PROM misbehaves, seems to not implement ARC
		   GetChild().	So we just assume it's an IP27.	 */
		iname = "SGI-IP27";
#else
		iname = "Unknown";
#endif
	} else
		iname = (char *) (long) p->iname;

	printk("ARCH: %s\n", iname);
	mach = string_to_mach(iname);
	system_type = mach->liname;

	prom_flags = mach->flags;
}
