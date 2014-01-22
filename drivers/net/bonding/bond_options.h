/*
 * drivers/net/bond/bond_options.h - bonding options
 * Copyright (c) 2013 Nikolay Aleksandrov <nikolay@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _BOND_OPTIONS_H
#define _BOND_OPTIONS_H

#define BOND_OPT_MAX_NAMELEN 32
#define BOND_OPT_VALID(opt) ((opt) < BOND_OPT_LAST)
#define BOND_MODE_ALL_EX(x) (~(x))

/* Option flags:
 * BOND_OPTFLAG_NOSLAVES - check if the bond device is empty before setting
 * BOND_OPTFLAG_IFDOWN - check if the bond device is down before setting
 * BOND_OPTFLAG_RAWVAL - the option parses the value itself
 */
enum {
	BOND_OPTFLAG_NOSLAVES	= BIT(0),
	BOND_OPTFLAG_IFDOWN	= BIT(1),
	BOND_OPTFLAG_RAWVAL	= BIT(2)
};

/* Value type flags:
 * BOND_VALFLAG_DEFAULT - mark the value as default
 * BOND_VALFLAG_(MIN|MAX) - mark the value as min/max
 */
enum {
	BOND_VALFLAG_DEFAULT	= BIT(0),
	BOND_VALFLAG_MIN	= BIT(1),
	BOND_VALFLAG_MAX	= BIT(2)
};

/* Option IDs, their bit positions correspond to their IDs */
enum {
	BOND_OPT_MODE,
	BOND_OPT_LAST
};

/* This structure is used for storing option values and for passing option
 * values when changing an option. The logic when used as an arg is as follows:
 * - if string != NULL -> parse it, if the opt is RAW type then return it, else
 *   return the parse result
 * - if string == NULL -> parse value
 */
struct bond_opt_value {
	char *string;
	u64 value;
	u32 flags;
};

struct bonding;

struct bond_option {
	int id;
	char *name;
	char *desc;
	u32 flags;

	/* unsuppmodes is used to denote modes in which the option isn't
	 * supported.
	 */
	unsigned long unsuppmodes;
	/* supported values which this option can have, can be a subset of
	 * BOND_OPTVAL_RANGE's value range
	 */
	struct bond_opt_value *values;

	int (*set)(struct bonding *bond, struct bond_opt_value *val);
};

int __bond_opt_set(struct bonding *bond, unsigned int option,
		   struct bond_opt_value *val);
int bond_opt_tryset_rtnl(struct bonding *bond, unsigned int option, char *buf);
struct bond_opt_value *bond_opt_parse(const struct bond_option *opt,
				      struct bond_opt_value *val);
struct bond_option *bond_opt_get(unsigned int option);
struct bond_opt_value *bond_opt_get_val(unsigned int option, u64 val);

/* This helper is used to initialize a bond_opt_value structure for parameter
 * passing. There should be either a valid string or value, but not both.
 * When value is ULLONG_MAX then string will be used.
 */
static inline void __bond_opt_init(struct bond_opt_value *optval,
				   char *string, u64 value)
{
	memset(optval, 0, sizeof(*optval));
	optval->value = ULLONG_MAX;
	if (value == ULLONG_MAX)
		optval->string = string;
	else
		optval->value = value;
}
#define bond_opt_initval(optval, value) __bond_opt_init(optval, NULL, value)
#define bond_opt_initstr(optval, str) __bond_opt_init(optval, str, ULLONG_MAX)

int bond_option_mode_set(struct bonding *bond, struct bond_opt_value *newval);
#endif /* _BOND_OPTIONS_H */
