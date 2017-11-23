/*
 * ipmi_si_hotmod.c
 *
 * Handling for dynamically adding/removing IPMI devices through
 * a module parameter (and thus sysfs).
 */
#include <linux/moduleparam.h>
#include <linux/ipmi.h>
#include "ipmi_si.h"

#define PFX "ipmi_hotmod: "

static int hotmod_handler(const char *val, const struct kernel_param *kp);

module_param_call(hotmod, hotmod_handler, NULL, NULL, 0200);
MODULE_PARM_DESC(hotmod, "Add and remove interfaces.  See"
		 " Documentation/IPMI.txt in the kernel sources for the"
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

static int parse_str(const struct hotmod_vals *v, int *val, char *name,
		     char **curr)
{
	char *s;
	int  i;

	s = strchr(*curr, ',');
	if (!s) {
		pr_warn(PFX "No hotmod %s given.\n", name);
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

	pr_warn(PFX "Invalid hotmod %s '%s'\n", name, *curr);
	return -EINVAL;
}

static int check_hotmod_int_op(const char *curr, const char *option,
			       const char *name, int *val)
{
	char *n;

	if (strcmp(curr, name) == 0) {
		if (!option) {
			pr_warn(PFX "No option given for '%s'\n", curr);
			return -EINVAL;
		}
		*val = simple_strtoul(option, &n, 0);
		if ((*n != '\0') || (*option == '\0')) {
			pr_warn(PFX "Bad option given for '%s'\n", curr);
			return -EINVAL;
		}
		return 1;
	}
	return 0;
}

static int hotmod_handler(const char *val, const struct kernel_param *kp)
{
	char *str = kstrdup(val, GFP_KERNEL);
	int  rv;
	char *next, *curr, *s, *n, *o;
	enum hotmod_op op;
	enum si_type si_type;
	int  addr_space;
	unsigned long addr;
	int regspacing;
	int regsize;
	int regshift;
	int irq;
	int ipmb;
	int ival;
	int len;

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
		regspacing = 1;
		regsize = 1;
		regshift = 0;
		irq = 0;
		ipmb = 0; /* Choose the default if not specified */

		next = strchr(curr, ':');
		if (next) {
			*next = '\0';
			next++;
		}

		rv = parse_str(hotmod_ops, &ival, "operation", &curr);
		if (rv)
			break;
		op = ival;

		rv = parse_str(hotmod_si, &ival, "interface type", &curr);
		if (rv)
			break;
		si_type = ival;

		rv = parse_str(hotmod_as, &addr_space, "address space", &curr);
		if (rv)
			break;

		s = strchr(curr, ',');
		if (s) {
			*s = '\0';
			s++;
		}
		addr = simple_strtoul(curr, &n, 0);
		if ((*n != '\0') || (*curr == '\0')) {
			pr_warn(PFX "Invalid hotmod address '%s'\n", curr);
			break;
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
			rv = check_hotmod_int_op(curr, o, "rsp", &regspacing);
			if (rv < 0)
				goto out;
			else if (rv)
				continue;
			rv = check_hotmod_int_op(curr, o, "rsi", &regsize);
			if (rv < 0)
				goto out;
			else if (rv)
				continue;
			rv = check_hotmod_int_op(curr, o, "rsh", &regshift);
			if (rv < 0)
				goto out;
			else if (rv)
				continue;
			rv = check_hotmod_int_op(curr, o, "irq", &irq);
			if (rv < 0)
				goto out;
			else if (rv)
				continue;
			rv = check_hotmod_int_op(curr, o, "ipmb", &ipmb);
			if (rv < 0)
				goto out;
			else if (rv)
				continue;

			rv = -EINVAL;
			pr_warn(PFX "Invalid hotmod option '%s'\n", curr);
			goto out;
		}

		if (op == HM_ADD) {
			struct si_sm_io io;

			memset(&io, 0, sizeof(io));
			io.addr_source = SI_HOTMOD;
			io.si_type = si_type;
			io.addr_data = addr;
			io.addr_type = addr_space;

			io.addr = NULL;
			io.regspacing = regspacing;
			if (!io.regspacing)
				io.regspacing = DEFAULT_REGSPACING;
			io.regsize = regsize;
			if (!io.regsize)
				io.regsize = DEFAULT_REGSIZE;
			io.regshift = regshift;
			io.irq = irq;
			if (io.irq)
				io.irq_setup = ipmi_std_irq_setup;
			io.slave_addr = ipmb;

			rv = ipmi_si_add_smi(&io);
			if (rv)
				goto out;
		} else {
			ipmi_si_remove_by_data(addr_space, si_type, addr);
		}
	}
	rv = len;
out:
	kfree(str);
	return rv;
}
