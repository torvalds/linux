/*
 * Procedures for creating, accessing and interpreting the device tree.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 * 
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com 
 *
 *  Adapted for sparc64 by David S. Miller davem@davemloft.net
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/module.h>

#include <asm/prom.h>
#include <asm/oplib.h>

static struct device_node *allnodes;

struct device_node *of_get_parent(const struct device_node *node)
{
	struct device_node *np;

	if (!node)
		return NULL;

	np = node->parent;

	return np;
}

struct device_node *of_get_next_child(const struct device_node *node,
	struct device_node *prev)
{
	struct device_node *next;

	next = prev ? prev->sibling : node->child;
	for (; next != 0; next = next->sibling) {
		break;
	}

	return next;
}

struct device_node *of_find_node_by_path(const char *path)
{
	struct device_node *np = allnodes;

	for (; np != 0; np = np->allnext) {
		if (np->full_name != 0 && strcmp(np->full_name, path) == 0)
			break;
	}

	return np;
}
EXPORT_SYMBOL(of_find_node_by_path);

struct device_node *of_find_node_by_phandle(phandle handle)
{
	struct device_node *np;

	for (np = allnodes; np != 0; np = np->allnext)
		if (np->node == handle)
			break;

	return np;
}

struct device_node *of_find_node_by_name(struct device_node *from,
	const char *name)
{
	struct device_node *np;

	np = from ? from->allnext : allnodes;
	for (; np != NULL; np = np->allnext)
		if (np->name != NULL && strcmp(np->name, name) == 0)
			break;

	return np;
}

struct device_node *of_find_node_by_type(struct device_node *from,
	const char *type)
{
	struct device_node *np;

	np = from ? from->allnext : allnodes;
	for (; np != 0; np = np->allnext)
		if (np->type != 0 && strcmp(np->type, type) == 0)
			break;

	return np;
}

struct property *of_find_property(struct device_node *np, const char *name,
				  int *lenp)
{
	struct property *pp;

	for (pp = np->properties; pp != 0; pp = pp->next) {
		if (strcmp(pp->name, name) == 0) {
			if (lenp != 0)
				*lenp = pp->length;
			break;
		}
	}
	return pp;
}
EXPORT_SYMBOL(of_find_property);

/*
 * Find a property with a given name for a given node
 * and return the value.
 */
void *of_get_property(struct device_node *np, const char *name, int *lenp)
{
	struct property *pp = of_find_property(np,name,lenp);
	return pp ? pp->value : NULL;
}
EXPORT_SYMBOL(of_get_property);

int of_getintprop_default(struct device_node *np, const char *name, int def)
{
	struct property *prop;
	int len;

	prop = of_find_property(np, name, &len);
	if (!prop || len != 4)
		return def;

	return *(int *) prop->value;
}
EXPORT_SYMBOL(of_getintprop_default);

static unsigned int prom_early_allocated;

static void * __init prom_early_alloc(unsigned long size)
{
	void *ret;

	ret = __alloc_bootmem(size, SMP_CACHE_BYTES, 0UL);
	if (ret != NULL)
		memset(ret, 0, size);

	prom_early_allocated += size;

	return ret;
}

static int is_root_node(const struct device_node *dp)
{
	if (!dp)
		return 0;

	return (dp->parent == NULL);
}

/* The following routines deal with the black magic of fully naming a
 * node.
 *
 * Certain well known named nodes are just the simple name string.
 *
 * Actual devices have an address specifier appended to the base name
 * string, like this "foo@addr".  The "addr" can be in any number of
 * formats, and the platform plus the type of the node determine the
 * format and how it is constructed.
 *
 * For children of the ROOT node, the naming convention is fixed and
 * determined by whether this is a sun4u or sun4v system.
 *
 * For children of other nodes, it is bus type specific.  So
 * we walk up the tree until we discover a "device_type" property
 * we recognize and we go from there.
 *
 * As an example, the boot device on my workstation has a full path:
 *
 *	/pci@1e,600000/ide@d/disk@0,0:c
 */
static void __init sun4v_path_component(struct device_node *dp, char *tmp_buf)
{
	struct linux_prom64_registers *regs;
	struct property *rprop;
	u32 high_bits, low_bits, type;

	rprop = of_find_property(dp, "reg", NULL);
	if (!rprop)
		return;

	regs = rprop->value;
	if (!is_root_node(dp->parent)) {
		sprintf(tmp_buf, "%s@%x,%x",
			dp->name,
			(unsigned int) (regs->phys_addr >> 32UL),
			(unsigned int) (regs->phys_addr & 0xffffffffUL));
		return;
	}

	type = regs->phys_addr >> 60UL;
	high_bits = (regs->phys_addr >> 32UL) & 0x0fffffffUL;
	low_bits = (regs->phys_addr & 0xffffffffUL);

	if (type == 0 || type == 8) {
		const char *prefix = (type == 0) ? "m" : "i";

		if (low_bits)
			sprintf(tmp_buf, "%s@%s%x,%x",
				dp->name, prefix,
				high_bits, low_bits);
		else
			sprintf(tmp_buf, "%s@%s%x",
				dp->name,
				prefix,
				high_bits);
	} else if (type == 12) {
		sprintf(tmp_buf, "%s@%x",
			dp->name, high_bits);
	}
}

static void __init sun4u_path_component(struct device_node *dp, char *tmp_buf)
{
	struct linux_prom64_registers *regs;
	struct property *prop;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;
	if (!is_root_node(dp->parent)) {
		sprintf(tmp_buf, "%s@%x,%x",
			dp->name,
			(unsigned int) (regs->phys_addr >> 32UL),
			(unsigned int) (regs->phys_addr & 0xffffffffUL));
		return;
	}

	prop = of_find_property(dp, "upa-portid", NULL);
	if (!prop)
		prop = of_find_property(dp, "portid", NULL);
	if (prop) {
		unsigned long mask = 0xffffffffUL;

		if (tlb_type >= cheetah)
			mask = 0x7fffff;

		sprintf(tmp_buf, "%s@%x,%x",
			dp->name,
			*(u32 *)prop->value,
			(unsigned int) (regs->phys_addr & mask));
	}
}

/* "name@slot,offset"  */
static void __init sbus_path_component(struct device_node *dp, char *tmp_buf)
{
	struct linux_prom_registers *regs;
	struct property *prop;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;
	sprintf(tmp_buf, "%s@%x,%x",
		dp->name,
		regs->which_io,
		regs->phys_addr);
}

/* "name@devnum[,func]" */
static void __init pci_path_component(struct device_node *dp, char *tmp_buf)
{
	struct linux_prom_pci_registers *regs;
	struct property *prop;
	unsigned int devfn;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;
	devfn = (regs->phys_hi >> 8) & 0xff;
	if (devfn & 0x07) {
		sprintf(tmp_buf, "%s@%x,%x",
			dp->name,
			devfn >> 3,
			devfn & 0x07);
	} else {
		sprintf(tmp_buf, "%s@%x",
			dp->name,
			devfn >> 3);
	}
}

/* "name@UPA_PORTID,offset" */
static void __init upa_path_component(struct device_node *dp, char *tmp_buf)
{
	struct linux_prom64_registers *regs;
	struct property *prop;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;

	prop = of_find_property(dp, "upa-portid", NULL);
	if (!prop)
		return;

	sprintf(tmp_buf, "%s@%x,%x",
		dp->name,
		*(u32 *) prop->value,
		(unsigned int) (regs->phys_addr & 0xffffffffUL));
}

/* "name@reg" */
static void __init vdev_path_component(struct device_node *dp, char *tmp_buf)
{
	struct property *prop;
	u32 *regs;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;

	sprintf(tmp_buf, "%s@%x", dp->name, *regs);
}

/* "name@addrhi,addrlo" */
static void __init ebus_path_component(struct device_node *dp, char *tmp_buf)
{
	struct linux_prom64_registers *regs;
	struct property *prop;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;

	sprintf(tmp_buf, "%s@%x,%x",
		dp->name,
		(unsigned int) (regs->phys_addr >> 32UL),
		(unsigned int) (regs->phys_addr & 0xffffffffUL));
}

/* "name@bus,addr" */
static void __init i2c_path_component(struct device_node *dp, char *tmp_buf)
{
	struct property *prop;
	u32 *regs;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;

	/* This actually isn't right... should look at the #address-cells
	 * property of the i2c bus node etc. etc.
	 */
	sprintf(tmp_buf, "%s@%x,%x",
		dp->name, regs[0], regs[1]);
}

/* "name@reg0[,reg1]" */
static void __init usb_path_component(struct device_node *dp, char *tmp_buf)
{
	struct property *prop;
	u32 *regs;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;

	if (prop->length == sizeof(u32) || regs[1] == 1) {
		sprintf(tmp_buf, "%s@%x",
			dp->name, regs[0]);
	} else {
		sprintf(tmp_buf, "%s@%x,%x",
			dp->name, regs[0], regs[1]);
	}
}

/* "name@reg0reg1[,reg2reg3]" */
static void __init ieee1394_path_component(struct device_node *dp, char *tmp_buf)
{
	struct property *prop;
	u32 *regs;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;

	if (regs[2] || regs[3]) {
		sprintf(tmp_buf, "%s@%08x%08x,%04x%08x",
			dp->name, regs[0], regs[1], regs[2], regs[3]);
	} else {
		sprintf(tmp_buf, "%s@%08x%08x",
			dp->name, regs[0], regs[1]);
	}
}

static void __init __build_path_component(struct device_node *dp, char *tmp_buf)
{
	struct device_node *parent = dp->parent;

	if (parent != NULL) {
		if (!strcmp(parent->type, "pci") ||
		    !strcmp(parent->type, "pciex"))
			return pci_path_component(dp, tmp_buf);
		if (!strcmp(parent->type, "sbus"))
			return sbus_path_component(dp, tmp_buf);
		if (!strcmp(parent->type, "upa"))
			return upa_path_component(dp, tmp_buf);
		if (!strcmp(parent->type, "ebus"))
			return ebus_path_component(dp, tmp_buf);
		if (!strcmp(parent->name, "usb") ||
		    !strcmp(parent->name, "hub"))
			return usb_path_component(dp, tmp_buf);
		if (!strcmp(parent->type, "i2c"))
			return i2c_path_component(dp, tmp_buf);
		if (!strcmp(parent->type, "firewire"))
			return ieee1394_path_component(dp, tmp_buf);
		if (!strcmp(parent->type, "virtual-devices"))
			return vdev_path_component(dp, tmp_buf);

		/* "isa" is handled with platform naming */
	}

	/* Use platform naming convention.  */
	if (tlb_type == hypervisor)
		return sun4v_path_component(dp, tmp_buf);
	else
		return sun4u_path_component(dp, tmp_buf);
}

static char * __init build_path_component(struct device_node *dp)
{
	char tmp_buf[64], *n;

	tmp_buf[0] = '\0';
	__build_path_component(dp, tmp_buf);
	if (tmp_buf[0] == '\0')
		strcpy(tmp_buf, dp->name);

	n = prom_early_alloc(strlen(tmp_buf) + 1);
	strcpy(n, tmp_buf);

	return n;
}

static char * __init build_full_name(struct device_node *dp)
{
	int len, ourlen, plen;
	char *n;

	plen = strlen(dp->parent->full_name);
	ourlen = strlen(dp->path_component_name);
	len = ourlen + plen + 2;

	n = prom_early_alloc(len);
	strcpy(n, dp->parent->full_name);
	if (!is_root_node(dp->parent)) {
		strcpy(n + plen, "/");
		plen++;
	}
	strcpy(n + plen, dp->path_component_name);

	return n;
}

static struct property * __init build_one_prop(phandle node, char *prev)
{
	static struct property *tmp = NULL;
	struct property *p;

	if (tmp) {
		p = tmp;
		memset(p, 0, sizeof(*p) + 32);
		tmp = NULL;
	} else
		p = prom_early_alloc(sizeof(struct property) + 32);

	p->name = (char *) (p + 1);
	if (prev == NULL) {
		prom_firstprop(node, p->name);
	} else {
		prom_nextprop(node, prev, p->name);
	}
	if (strlen(p->name) == 0) {
		tmp = p;
		return NULL;
	}
	p->length = prom_getproplen(node, p->name);
	if (p->length <= 0) {
		p->length = 0;
	} else {
		p->value = prom_early_alloc(p->length);
		prom_getproperty(node, p->name, p->value, p->length);
	}
	return p;
}

static struct property * __init build_prop_list(phandle node)
{
	struct property *head, *tail;

	head = tail = build_one_prop(node, NULL);
	while(tail) {
		tail->next = build_one_prop(node, tail->name);
		tail = tail->next;
	}

	return head;
}

static char * __init get_one_property(phandle node, const char *name)
{
	char *buf = "<NULL>";
	int len;

	len = prom_getproplen(node, name);
	if (len > 0) {
		buf = prom_early_alloc(len);
		prom_getproperty(node, name, buf, len);
	}

	return buf;
}

static struct device_node * __init create_node(phandle node)
{
	struct device_node *dp;

	if (!node)
		return NULL;

	dp = prom_early_alloc(sizeof(*dp));

	kref_init(&dp->kref);

	dp->name = get_one_property(node, "name");
	dp->type = get_one_property(node, "device_type");
	dp->node = node;

	/* Build interrupts later... */

	dp->properties = build_prop_list(node);

	return dp;
}

static struct device_node * __init build_tree(struct device_node *parent, phandle node, struct device_node ***nextp)
{
	struct device_node *dp;

	dp = create_node(node);
	if (dp) {
		*(*nextp) = dp;
		*nextp = &dp->allnext;

		dp->parent = parent;
		dp->path_component_name = build_path_component(dp);
		dp->full_name = build_full_name(dp);

		dp->child = build_tree(dp, prom_getchild(node), nextp);

		dp->sibling = build_tree(parent, prom_getsibling(node), nextp);
	}

	return dp;
}

void __init prom_build_devicetree(void)
{
	struct device_node **nextp;

	allnodes = create_node(prom_root_node);
	allnodes->path_component_name = "";
	allnodes->full_name = "/";

	nextp = &allnodes->allnext;
	allnodes->child = build_tree(allnodes,
				     prom_getchild(allnodes->node),
				     &nextp);
	printk("PROM: Built device tree with %u bytes of memory.\n",
	       prom_early_allocated);
}
