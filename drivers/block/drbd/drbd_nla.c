// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <net/netlink.h>
#include <linux/drbd_genl_api.h>
#include "drbd_nla.h"

static int drbd_nla_check_mandatory(int maxtype, struct nlattr *nla)
{
	struct nlattr *head = nla_data(nla);
	int len = nla_len(nla);
	int rem;

	/*
	 * validate_nla (called from nla_parse_nested) ignores attributes
	 * beyond maxtype, and does not understand the DRBD_GENLA_F_MANDATORY flag.
	 * In order to have it validate attributes with the DRBD_GENLA_F_MANDATORY
	 * flag set also, check and remove that flag before calling
	 * nla_parse_nested.
	 */

	nla_for_each_attr(nla, head, len, rem) {
		if (nla->nla_type & DRBD_GENLA_F_MANDATORY) {
			nla->nla_type &= ~DRBD_GENLA_F_MANDATORY;
			if (nla_type(nla) > maxtype)
				return -EOPNOTSUPP;
		}
	}
	return 0;
}

int drbd_nla_parse_nested(struct nlattr *tb[], int maxtype, struct nlattr *nla,
			  const struct nla_policy *policy)
{
	int err;

	err = drbd_nla_check_mandatory(maxtype, nla);
	if (!err)
		err = nla_parse_nested_deprecated(tb, maxtype, nla, policy,
						  NULL);

	return err;
}

struct nlattr *drbd_nla_find_nested(int maxtype, struct nlattr *nla, int attrtype)
{
	int err;
	/*
	 * If any nested attribute has the DRBD_GENLA_F_MANDATORY flag set and
	 * we don't know about that attribute, reject all the nested
	 * attributes.
	 */
	err = drbd_nla_check_mandatory(maxtype, nla);
	if (err)
		return ERR_PTR(err);
	return nla_find_nested(nla, attrtype);
}
