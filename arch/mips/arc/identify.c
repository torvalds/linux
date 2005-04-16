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
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>

#include <asm/sgialib.h>
#include <asm/bootinfo.h>

struct smatch {
	char *arcname;
	char *liname;
	int group;
	int type;
	int flags;
};

static struct smatch mach_table[] = {
	{	"SGI-IP22",
		"SGI Indy",
		MACH_GROUP_SGI,
		MACH_SGI_IP22,
		PROM_FLAG_ARCS
	}, {	"SGI-IP27",
		"SGI Origin",
		MACH_GROUP_SGI,
		MACH_SGI_IP27,
		PROM_FLAG_ARCS
	}, {	"SGI-IP28",
		"SGI IP28",
		MACH_GROUP_SGI,
		MACH_SGI_IP28,
		PROM_FLAG_ARCS
	}, {	"SGI-IP32",
		"SGI O2",
		MACH_GROUP_SGI,
		MACH_SGI_IP32,
		PROM_FLAG_ARCS
	}, {	"Microsoft-Jazz",
		"Jazz MIPS_Magnum_4000",
		MACH_GROUP_JAZZ,
		MACH_MIPS_MAGNUM_4000,
		0
	}, {	"PICA-61",
		"Jazz Acer_PICA_61",
		MACH_GROUP_JAZZ,
		MACH_ACER_PICA_61,
		0
	}, {	"RM200PCI",
		"SNI RM200_PCI",
		MACH_GROUP_SNI_RM,
		MACH_SNI_RM200_PCI,
		PROM_FLAG_DONT_FREE_TEMP
	}
};

int prom_flags;

static struct smatch * __init string_to_mach(const char *s)
{
	int i;

	for (i = 0; i < (sizeof(mach_table) / sizeof (mach_table[0])); i++) {
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
		   GetChild().  So we just assume it's an IP27.  */
		iname = "SGI-IP27";
#else
		iname = "Unknown";
#endif
	} else
		iname = (char *) (long) p->iname;

	printk("ARCH: %s\n", iname);
	mach = string_to_mach(iname);
	system_type = mach->liname;

	mips_machgroup = mach->group;
	mips_machtype = mach->type;
	prom_flags = mach->flags;
}
