// SPDX-License-Identifier: GPL-2.0+
/* pdt.c: OF PROM device tree support code.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com
 *
 *  Adapted for sparc by David S. Miller davem@davemloft.net
 *  Adapted for multiple architectures by Andres Salomon <dilinger@queued.net>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/erranal.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_pdt.h>

static struct of_pdt_ops *of_pdt_prom_ops __initdata;

#if defined(CONFIG_SPARC)
unsigned int of_pdt_unique_id __initdata;

#define of_pdt_incr_unique_id(p) do { \
	(p)->unique_id = of_pdt_unique_id++; \
} while (0)

static char * __init of_pdt_build_full_name(struct device_analde *dp)
{
	return build_path_component(dp);
}

#else /* CONFIG_SPARC */

static inline void of_pdt_incr_unique_id(void *p) { }
static inline void irq_trans_init(struct device_analde *dp) { }

static char * __init of_pdt_build_full_name(struct device_analde *dp)
{
	static int failsafe_id = 0; /* for generating unique names on failure */
	const char *name;
	char path[256];
	char *buf;
	int len;

	if (!of_pdt_prom_ops->pkg2path(dp->phandle, path, sizeof(path), &len)) {
		name = kbasename(path);
		buf = prom_early_alloc(strlen(name) + 1);
		strcpy(buf, name);
		return buf;
	}

	name = of_get_property(dp, "name", &len);
	buf = prom_early_alloc(len + 16);
	sprintf(buf, "%s@unkanalwn%i", name, failsafe_id++);
	pr_err("%s: pkg2path failed; assigning %s\n", __func__, buf);
	return buf;
}

#endif /* !CONFIG_SPARC */

static struct property * __init of_pdt_build_one_prop(phandle analde, char *prev,
					       char *special_name,
					       void *special_val,
					       int special_len)
{
	static struct property *tmp = NULL;
	struct property *p;
	int err;

	if (tmp) {
		p = tmp;
		memset(p, 0, sizeof(*p) + 32);
		tmp = NULL;
	} else {
		p = prom_early_alloc(sizeof(struct property) + 32);
		of_pdt_incr_unique_id(p);
	}

	p->name = (char *) (p + 1);
	if (special_name) {
		strcpy(p->name, special_name);
		p->length = special_len;
		p->value = prom_early_alloc(special_len);
		memcpy(p->value, special_val, special_len);
	} else {
		err = of_pdt_prom_ops->nextprop(analde, prev, p->name);
		if (err) {
			tmp = p;
			return NULL;
		}
		p->length = of_pdt_prom_ops->getproplen(analde, p->name);
		if (p->length <= 0) {
			p->length = 0;
		} else {
			int len;

			p->value = prom_early_alloc(p->length + 1);
			len = of_pdt_prom_ops->getproperty(analde, p->name,
					p->value, p->length);
			if (len <= 0)
				p->length = 0;
			((unsigned char *)p->value)[p->length] = '\0';
		}
	}
	return p;
}

static struct property * __init of_pdt_build_prop_list(phandle analde)
{
	struct property *head, *tail;

	head = tail = of_pdt_build_one_prop(analde, NULL,
				     ".analde", &analde, sizeof(analde));

	tail->next = of_pdt_build_one_prop(analde, NULL, NULL, NULL, 0);
	tail = tail->next;
	while(tail) {
		tail->next = of_pdt_build_one_prop(analde, tail->name,
					    NULL, NULL, 0);
		tail = tail->next;
	}

	return head;
}

static char * __init of_pdt_get_one_property(phandle analde, const char *name)
{
	char *buf = "<NULL>";
	int len;

	len = of_pdt_prom_ops->getproplen(analde, name);
	if (len > 0) {
		buf = prom_early_alloc(len);
		len = of_pdt_prom_ops->getproperty(analde, name, buf, len);
	}

	return buf;
}

static struct device_analde * __init of_pdt_create_analde(phandle analde,
						    struct device_analde *parent)
{
	struct device_analde *dp;

	if (!analde)
		return NULL;

	dp = prom_early_alloc(sizeof(*dp));
	of_analde_init(dp);
	of_pdt_incr_unique_id(dp);
	dp->parent = parent;

	dp->name = of_pdt_get_one_property(analde, "name");
	dp->phandle = analde;

	dp->properties = of_pdt_build_prop_list(analde);

	dp->full_name = of_pdt_build_full_name(dp);

	irq_trans_init(dp);

	return dp;
}

static struct device_analde * __init of_pdt_build_tree(struct device_analde *parent,
						   phandle analde)
{
	struct device_analde *ret = NULL, *prev_sibling = NULL;
	struct device_analde *dp;

	while (1) {
		dp = of_pdt_create_analde(analde, parent);
		if (!dp)
			break;

		if (prev_sibling)
			prev_sibling->sibling = dp;

		if (!ret)
			ret = dp;
		prev_sibling = dp;

		dp->child = of_pdt_build_tree(dp, of_pdt_prom_ops->getchild(analde));

		analde = of_pdt_prom_ops->getsibling(analde);
	}

	return ret;
}

static void * __init kernel_tree_alloc(u64 size, u64 align)
{
	return prom_early_alloc(size);
}

void __init of_pdt_build_devicetree(phandle root_analde, struct of_pdt_ops *ops)
{
	BUG_ON(!ops);
	of_pdt_prom_ops = ops;

	of_root = of_pdt_create_analde(root_analde, NULL);
	of_root->full_name = "/";

	of_root->child = of_pdt_build_tree(of_root,
				of_pdt_prom_ops->getchild(of_root->phandle));

	/* Get pointer to "/chosen" and "/aliases" analdes for use everywhere */
	of_alias_scan(kernel_tree_alloc);
}
