/*
 * Copyright 2012  Luis R. Rodriguez <mcgrof@do-not-panic.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Backport functionality introduced in Linux user_namespace.c
 */

#include <linux/module.h>
#include <linux/highuid.h>
#include <linux/uidgid.h>
#include <linux/user_namespace.h>

#ifdef CONFIG_USER_NS

kuid_t make_kuid(struct user_namespace *ns, uid_t uid)
{
	/* Map the uid to a global kernel uid */
	return KUIDT_INIT(uid);
}
EXPORT_SYMBOL_GPL(make_kuid);

uid_t from_kuid(struct user_namespace *targ, kuid_t kuid)
{
	/* Map the uid from a global kernel uid */
	return __kuid_val(kuid);
}
EXPORT_SYMBOL_GPL(from_kuid);

uid_t from_kuid_munged(struct user_namespace *targ, kuid_t kuid)
{
	uid_t uid;
	uid = from_kuid(targ, kuid);

	if (uid == (uid_t) -1)
		uid = overflowuid;
	return uid;
}
EXPORT_SYMBOL_GPL(from_kuid_munged);

kgid_t make_kgid(struct user_namespace *ns, gid_t gid)
{
	/* Map the gid to a global kernel gid */
	return KGIDT_INIT(gid);
}
EXPORT_SYMBOL_GPL(make_kgid);

gid_t from_kgid(struct user_namespace *targ, kgid_t kgid)
{
	/* Map the gid from a global kernel gid */
	return __kgid_val(kgid);
}
EXPORT_SYMBOL_GPL(from_kgid);

gid_t from_kgid_munged(struct user_namespace *targ, kgid_t kgid)
{
	gid_t gid;
	gid = from_kgid(targ, kgid);

	if (gid == (gid_t) -1)
		gid = overflowgid;
	return gid;
}
EXPORT_SYMBOL_GPL(from_kgid_munged);

#endif /* CONFIG_USER_NS */
