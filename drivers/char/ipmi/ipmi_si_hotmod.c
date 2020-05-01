// SPDX-License-Identifier: GPL-2.0+
/*
 * ipmi_si_hotmod.c
 *
 * Handling for dynamically adding/removing IPMI devices through
 * a module parameter (and thus sysfs).
 */

#define pr_fmt(fmt) "ipmi_hotmod: " fmt

#include <linux/moduleparam.h>
#include <linux/ipmi.h>
#include <linux/atomic.h>
#include "ipmi_si.h"
#include "ipmi_plat_data.h"

static int hotmod_handler(const char *val, const struct kernel_param *kp);

module_param_call(hotmod, hotmod_handler, NULL, NULL, 0200);
MODULE_PARM_DESC(hotmod, "Add and remove interfaces.  See"
		 " Documentation/driver-api/ipmi.rst in the kernel sources for the"
		 " gory details.");

/*
 * Parms come in as <op1>[:op2[:op3...]].  ops are:
 *   add|remove,kcs|bt|smic,mem|i/o,<address>[,<opt1>[,<opt2>[,...]]]
 * Options are:
 *   rsp=<regspacing>
 *   rsi=<regsize>
 *   rsh=<regshift>
 *   irq=<irq>
 *   ipmb=<ipmb addr>
 */
enum hotmod_op { HM_ADD, HM_REMOVE };
struct hotmod_vals {
	const char *name;
	const int  val;
};

static const struct hotmod_vals hotmod_ops[] = {
	{ "add",	HM_ADD },
	{ "remove",	HM_REMOVE },
	{ NULL }
};

static const struct hotmod_vals hotmod_si[] = {
	{ "kcs",	SI_KCS },
	{ "smic",	SI_SMIC },
	{ "bt",		SI_BT },
	{ NULL }
};

static const struct hotmod_vals hotmod_as[] = {
	{ "mem",	IPMI_MEM_ADDR_SPACE },
	{ "i/o",	IPMI_IO_ADDR_SPACE },
	{ NULL }
};

static int parse_str(const struct hotmod_vals *v, unsigned int *val, char *name,
		     const char **curr)
{
	char *s;
	int  i;

	s = strchr(*curr, ',');
	if (!s) {
		pr_warn("No hotmod %s given\n", name);
		return -EINVAL;
	}
	*s = '\0';
	s++;
	for (i = 0; v[i].name; i++) {
		if (strcmp(*curr, v[i].name) == 0) {
			*val = v[i].val;
			*curr = s;
			return 0;
		}
	}

	pr_warn("Invalid hotmod %s '%s'\n", name, *curr);
	return -EINVAL;
}

static int check_hotmod_int_op(const char *curr, const char *option,
			       const char *name, unsigned int *val)
{
	char *n;

	if (strcmp(curr, name) == 0) {
		if (!option) {
			pr_warn("No option given for '%s'\n", curr);
			return -EINVAL;
		}
		*val = simple_strtoul(option, &n, 0);
		if ((*n != '\0') || (*option == '\0')) {
			pr_warn("Bad option given for '%s'\n", curr);
			return -EINVAL;
		}
		return 1;
	}
	return 0;
}

static int parse_hotmod_str(const char *curr, enum hotmod_op *op,
			    struct ipmi_plat_data *h)
{
	char *s, *o;
	int rv;
	unsigned int ival;

	h->iftype = IPMI_PLAT_IF_SI;
	rv = parse_str(hotmod_ops, &ival, "operation", &curr);
	if (rv)
		return rv;
	*op = ival;

	rv = parse_str(hotmod_si, &ival, "interface type", &curr);
	if (rv)
		return rv;
	h->type = ival;

	rv = parse_str(hotmod_as, &ival, "address space", &curr);
	if (rv)
		return rv;
	h->space = ival;

	s = strchr(curr, ',');
	if (s) {
		*s = '\0';
		s++;
	}
	rv = kstrtoul(curr, 0, &h->addr);
	if (rv) {
		pr_warn("Invalid hotmod address '%s': %d\n", curr, rv);
		return rv;
	}

	while (s) {
		curr = s;
		s = strchr(curr, ',');
		if (s) {
			*s = '\0';
			s++;
		}
		o = strchr(curr, '=');
		if (o) {
			*o = '\0';
			o++;
		}
		rv = check_hotmod_int_op(curr, o, "rsp", &h->regspacing);
		if (rv < 0)
			return rv;
		else if (rv)
			continue;
		rv = check_hotmod_int_op(curr, o, "rsi", &h->regsize);
		if (rv < 0)
			return rv;
		else if (rv)
			continue;
		rv = check_hotmod_int_op(curr, o, "rsh", &h->regshift);
		if (rv < 0)
			return rv;
		else if (rv)
			continue;
		rv = check_hotmod_int_op(curr, o, "irq", &h->irq);
		if (rv < 0)
			return rv;
		else if (rv)
			continue;
		rv = check_hotmod_int_op(curr, o, "ipmb", &h->slave_addr);
		if (rv < 0)
			return rv;
		else if (rv)
			continue;

		pr_warn("Invalid hotmod option '%s'\n", curr);
		return -EINVAL;
	}

	h->addr_source = SI_HOTMOD;
	return 0;
}

static atomic_t hotmod_nr;

static int hotmod_handler(const char *val, const struct kernel_param *kp)
{
	char *str = kstrdup(val, GFP_KERNEL), *curr, *next;
	int  rv;
	struct ipmi_plat_data h;
	unsigned int len;
	int ival;

	if (!str)
		return -ENOMEM;

	/* Kill any trailing spaces, as we can get a "\n" from echo. */
	len = strlen(str);
	ival = len - 1;
	while ((ival >= 0) && isspace(str[ival])) {
		str[ival] = '\0';
		ival--;
	}

	for (curr = str; curr; curr = next) {
		enum hotmod_op op;

		next = strchr(curr, ':');
		if (next) {
			*next = '\0';
			next++;
		}

		memset(&h, 0, sizeof(h));
		rv = parse_hotmod_str(curr, &op, &h);
		if (rv)
			goto out;

		if (op == HM_ADD) {
			ipmi_platform_add("hotmod-ipmi-si",
					  atomic_inc_return(&hotmod_nr),
					  &h);
		} else {
			struct device *dev;

			dev = ipmi_si_remove_by_data(h.space, h.type, h.addr);
			if (dev && dev_is_platform(dev)) {
				struct platform_device *pdev;

				pdev = to_platform_device(dev);
				if (strcmp(pdev->name, "hotmod-ipmi-si") == 0)
					platform_device_unregister(pdev);
			}
			if (dev)
				put_device(dev);
		}
	}
	rv = len;
out:
	kfree(str);
	return rv;
}

void ipmi_si_hotmod_exit(void)
{
	ipmi_remove_platform_device_by_name("hotmod-ipmi-si");
}
