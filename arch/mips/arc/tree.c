/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * PROM component device tree code.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1999 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <linux/init.h>
#include <asm/arc/types.h>
#include <asm/sgialib.h>

#undef DEBUG_PROM_TREE

pcomponent * __init
ArcGetPeer(pcomponent *Current)
{
	if (Current == PROM_NULL_COMPONENT)
		return PROM_NULL_COMPONENT;

	return (pcomponent *) ARC_CALL1(next_component, Current);
}

pcomponent * __init
ArcGetChild(pcomponent *Current)
{
	return (pcomponent *) ARC_CALL1(child_component, Current);
}

pcomponent * __init
ArcGetParent(pcomponent *Current)
{
	if (Current == PROM_NULL_COMPONENT)
		return PROM_NULL_COMPONENT;

	return (pcomponent *) ARC_CALL1(parent_component, Current);
}

LONG __init
ArcGetConfigurationData(VOID *Buffer, pcomponent *Current)
{
	return ARC_CALL2(component_data, Buffer, Current);
}

pcomponent * __init
ArcAddChild(pcomponent *Current, pcomponent *Template, VOID *ConfigurationData)
{
	return (pcomponent *)
	       ARC_CALL3(child_add, Current, Template, ConfigurationData);
}

LONG __init
ArcDeleteComponent(pcomponent *ComponentToDelete)
{
	return ARC_CALL1(comp_del, ComponentToDelete);
}

pcomponent * __init
ArcGetComponent(CHAR *Path)
{
	return (pcomponent *)ARC_CALL1(component_by_path, Path);
}

#ifdef DEBUG_PROM_TREE

static char *classes[] = {
	"system", "processor", "cache", "adapter", "controller", "peripheral",
	"memory"
};

static char *types[] = {
	"arc", "cpu", "fpu", "picache", "pdcache", "sicache", "sdcache",
	"sccache", "memdev", "eisa adapter", "tc adapter", "scsi adapter",
	"dti adapter", "multi-func adapter", "disk controller",
	"tp controller", "cdrom controller", "worm controller",
	"serial controller", "net controller", "display controller",
	"parallel controller", "pointer controller", "keyboard controller",
	"audio controller", "misc controller", "disk peripheral",
	"floppy peripheral", "tp peripheral", "modem peripheral",
	"monitor peripheral", "printer peripheral", "pointer peripheral",
	"keyboard peripheral", "terminal peripheral", "line peripheral",
	"net peripheral", "misc peripheral", "anonymous"
};

static char *iflags[] = {
	"bogus", "read only", "removable", "console in", "console out",
	"input", "output"
};

static void __init
dump_component(pcomponent *p)
{
	prom_printf("[%p]:class<%s>type<%s>flags<%s>ver<%d>rev<%d>",
		    p, classes[p->class], types[p->type],
		    iflags[p->iflags], p->vers, p->rev);
	prom_printf("key<%08lx>\n\tamask<%08lx>cdsize<%d>ilen<%d>iname<%s>\n",
		    p->key, p->amask, (int)p->cdsize, (int)p->ilen, p->iname);
}

static void __init
traverse(pcomponent *p, int op)
{
	dump_component(p);
	if(ArcGetChild(p))
		traverse(ArcGetChild(p), 1);
	if(ArcGetPeer(p) && op)
		traverse(ArcGetPeer(p), 1);
}

void __init
prom_testtree(void)
{
	pcomponent *p;

	p = ArcGetChild(PROM_NULL_COMPONENT);
	dump_component(p);
	p = ArcGetChild(p);
	while(p) {
		dump_component(p);
		p = ArcGetPeer(p);
	}
}

#endif /* DEBUG_PROM_TREE  */
