/*
 * Procedures for creating, accessing and interpreting the device tree.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 * 
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com 
 *
 *  Adapted for sparc32 by David S. Miller davem@davemloft.net
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

/* use when traversing tree through the allnext, child, sibling,
 * or parent members of struct device_node.
 */
static DEFINE_RWLOCK(devtree_lock);

int of_device_is_compatible(struct device_node *device, const char *compat)
{
	const char* cp;
	int cplen, l;

	cp = (char *) of_get_property(device, "compatible", &cplen);
	if (cp == NULL)
		return 0;
	while (cplen > 0) {
		if (strncmp(cp, compat, strlen(compat)) == 0)
			return 1;
		l = strlen(cp) + 1;
		cp += l;
		cplen -= l;
	}

	return 0;
}
EXPORT_SYMBOL(of_device_is_compatible);

struct device_node *of_get_parent(const struct device_node *node)
{
	struct device_node *np;

	if (!node)
		return NULL;

	np = node->parent;

	return np;
}
EXPORT_SYMBOL(of_get_parent);

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
EXPORT_SYMBOL(of_get_next_child);

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
EXPORT_SYMBOL(of_find_node_by_phandle);

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
EXPORT_SYMBOL(of_find_node_by_name);

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
EXPORT_SYMBOL(of_find_node_by_type);

struct device_node *of_find_compatible_node(struct device_node *from,
	const char *type, const char *compatible)
{
	struct device_node *np;

	np = from ? from->allnext : allnodes;
	for (; np != 0; np = np->allnext) {
		if (type != NULL
		    && !(np->type != 0 && strcmp(np->type, type) == 0))
			continue;
		if (of_device_is_compatible(np, compatible))
			break;
	}

	return np;
}
EXPORT_SYMBOL(of_find_compatible_node);

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

int of_n_addr_cells(struct device_node *np)
{
	int* ip;
	do {
		if (np->parent)
			np = np->parent;
		ip = of_get_property(np, "#address-cells", NULL);
		if (ip != NULL)
			return *ip;
	} while (np->parent);
	/* No #address-cells property for the root node, default to 2 */
	return 2;
}
EXPORT_SYMBOL(of_n_addr_cells);

int of_n_size_cells(struct device_node *np)
{
	int* ip;
	do {
		if (np->parent)
			np = np->parent;
		ip = of_get_property(np, "#size-cells", NULL);
		if (ip != NULL)
			return *ip;
	} while (np->parent);
	/* No #size-cells property for the root node, default to 1 */
	return 1;
}
EXPORT_SYMBOL(of_n_size_cells);

int of_set_property(struct device_node *dp, const char *name, void *val, int len)
{
	struct property **prevp;
	void *new_val;
	int err;

	new_val = kmalloc(len, GFP_KERNEL);
	if (!new_val)
		return -ENOMEM;

	memcpy(new_val, val, len);

	err = -ENODEV;

	write_lock(&devtree_lock);
	prevp = &dp->properties;
	while (*prevp) {
		struct property *prop = *prevp;

		if (!strcmp(prop->name, name)) {
			void *old_val = prop->value;
			int ret;

			ret = prom_setprop(dp->node, (char *) name, val, len);
			err = -EINVAL;
			if (ret >= 0) {
				prop->value = new_val;
				prop->length = len;

				if (OF_IS_DYNAMIC(prop))
					kfree(old_val);

				OF_MARK_DYNAMIC(prop);

				err = 0;
			}
			break;
		}
		prevp = &(*prevp)->next;
	}
	write_unlock(&devtree_lock);

	/* XXX Upate procfs if necessary... */

	return err;
}
EXPORT_SYMBOL(of_set_property);

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
 */
static void __init sparc32_path_component(struct device_node *dp, char *tmp_buf)
{
	struct linux_prom_registers *regs;
	struct property *rprop;

	rprop = of_find_property(dp, "reg", NULL);
	if (!rprop)
		return;

	regs = rprop->value;
	sprintf(tmp_buf, "%s@%x,%x",
		dp->name,
		regs->which_io, regs->phys_addr);
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

/* "name@addrhi,addrlo" */
static void __init ebus_path_component(struct device_node *dp, char *tmp_buf)
{
	struct linux_prom_registers *regs;
	struct property *prop;

	prop = of_find_property(dp, "reg", NULL);
	if (!prop)
		return;

	regs = prop->value;

	sprintf(tmp_buf, "%s@%x,%x",
		dp->name,
		regs->which_io, regs->phys_addr);
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
		if (!strcmp(parent->type, "ebus"))
			return ebus_path_component(dp, tmp_buf);

		/* "isa" is handled with platform naming */
	}

	/* Use platform naming convention.  */
	return sparc32_path_component(dp, tmp_buf);
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

static unsigned int unique_id;

static struct property * __init build_one_prop(phandle node, char *prev, char *special_name, void *special_val, int special_len)
{
	static struct property *tmp = NULL;
	struct property *p;
	int len;
	const char *name;

	if (tmp) {
		p = tmp;
		memset(p, 0, sizeof(*p) + 32);
		tmp = NULL;
	} else {
		p = prom_early_alloc(sizeof(struct property) + 32);
		p->unique_id = unique_id++;
	}

	p->name = (char *) (p + 1);
	if (special_name) {
		strcpy(p->name, special_name);
		p->length = special_len;
		p->value = prom_early_alloc(special_len);
		memcpy(p->value, special_val, special_len);
	} else {
		if (prev == NULL) {
			name = prom_firstprop(node, NULL);
		} else {
			name = prom_nextprop(node, prev, NULL);
		}
		if (strlen(name) == 0) {
			tmp = p;
			return NULL;
		}
		strcpy(p->name, name);
		p->length = prom_getproplen(node, p->name);
		if (p->length <= 0) {
			p->length = 0;
		} else {
			p->value = prom_early_alloc(p->length + 1);
			len = prom_getproperty(node, p->name, p->value,
					       p->length);
			if (len <= 0)
				p->length = 0;
			((unsigned char *)p->value)[p->length] = '\0';
		}
	}
	return p;
}

static struct property * __init build_prop_list(phandle node)
{
	struct property *head, *tail;

	head = tail = build_one_prop(node, NULL,
				     ".node", &node, sizeof(node));

	tail->next = build_one_prop(node, NULL, NULL, NULL, 0);
	tail = tail->next;
	while(tail) {
		tail->next = build_one_prop(node, tail->name,
					    NULL, NULL, 0);
		tail = tail->next;
	}

	return head;
}

static char * __init get_one_property(phandle node, char *name)
{
	char *buf = "<NULL>";
	int len;

	len = prom_getproplen(node, name);
	if (len > 0) {
		buf = prom_early_alloc(len);
		len = prom_getproperty(node, name, buf, len);
	}

	return buf;
}

static struct device_node * __init create_node(phandle node)
{
	struct device_node *dp;

	if (!node)
		return NULL;

	dp = prom_early_alloc(sizeof(*dp));
	dp->unique_id = unique_id++;

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
