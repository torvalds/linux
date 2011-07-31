/* Provides edac interface to mcelog events
 *
 * This file may be distributed under the terms of the
 * GNU General Public License version 2.
 *
 * Copyright (c) 2009 by:
 *	 Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * Red Hat Inc. http://www.redhat.com
 */

#include <linux/module.h>
#include <linux/edac_mce.h>
#include <asm/mce.h>

int edac_mce_enabled;
EXPORT_SYMBOL_GPL(edac_mce_enabled);


/*
 * Extension interface
 */

static LIST_HEAD(edac_mce_list);
static DEFINE_MUTEX(edac_mce_lock);

int edac_mce_register(struct edac_mce *edac_mce)
{
	mutex_lock(&edac_mce_lock);
	list_add_tail(&edac_mce->list, &edac_mce_list);
	mutex_unlock(&edac_mce_lock);
	return 0;
}
EXPORT_SYMBOL(edac_mce_register);

void edac_mce_unregister(struct edac_mce *edac_mce)
{
	mutex_lock(&edac_mce_lock);
	list_del(&edac_mce->list);
	mutex_unlock(&edac_mce_lock);
}
EXPORT_SYMBOL(edac_mce_unregister);

int edac_mce_parse(struct mce *mce)
{
	struct edac_mce *edac_mce;

	list_for_each_entry(edac_mce, &edac_mce_list, list) {
		if (edac_mce->check_error(edac_mce->priv, mce))
			return 1;
	}

	/* Nobody queued the error */
	return 0;
}
EXPORT_SYMBOL_GPL(edac_mce_parse);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_AUTHOR("Red Hat Inc. (http://www.redhat.com)");
MODULE_DESCRIPTION("EDAC Driver for mcelog captured errors");
