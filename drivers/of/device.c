#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>

#include <asm/errno.h>
#include "of_private.h"

/**
 * of_match_device - Tell if a struct device matches an of_device_id list
 * @ids: array of of device match structures to search in
 * @dev: the of device structure to match against
 *
 * Used by a driver to check whether an platform_device present in the
 * system is in its list of supported devices.
 */
const struct of_device_id *of_match_device(const struct of_device_id *matches,
					   const struct device *dev)
{
	if ((!matches) || (!dev->of_node))
		return NULL;
	return of_match_node(matches, dev->of_node);
}
EXPORT_SYMBOL(of_match_device);

struct platform_device *of_dev_get(struct platform_device *dev)
{
	struct device *tmp;

	if (!dev)
		return NULL;
	tmp = get_device(&dev->dev);
	if (tmp)
		return to_platform_device(tmp);
	else
		return NULL;
}
EXPORT_SYMBOL(of_dev_get);

void of_dev_put(struct platform_device *dev)
{
	if (dev)
		put_device(&dev->dev);
}
EXPORT_SYMBOL(of_dev_put);

int of_device_add(struct platform_device *ofdev)
{
	BUG_ON(ofdev->dev.of_node == NULL);

	/* name and id have to be set so that the platform bus doesn't get
	 * confused on matching */
	ofdev->name = dev_name(&ofdev->dev);
	ofdev->id = -1;

	/* device_add will assume that this device is on the same node as
	 * the parent. If there is no parent defined, set the node
	 * explicitly */
	if (!ofdev->dev.parent)
		set_dev_node(&ofdev->dev, of_node_to_nid(ofdev->dev.of_node));

	return device_add(&ofdev->dev);
}

int of_device_register(struct platform_device *pdev)
{
	device_initialize(&pdev->dev);
	return of_device_add(pdev);
}
EXPORT_SYMBOL(of_device_register);

void of_device_unregister(struct platform_device *ofdev)
{
	device_unregister(&ofdev->dev);
}
EXPORT_SYMBOL(of_device_unregister);

ssize_t of_device_get_modalias(struct device *dev, char *str, ssize_t len)
{
	const char *compat;
	int cplen, i;
	ssize_t tsize, csize, repend;

	/* Name & Type */
	csize = snprintf(str, len, "of:N%sT%s", dev->of_node->name,
			 dev->of_node->type);

	/* Get compatible property if any */
	compat = of_get_property(dev->of_node, "compatible", &cplen);
	if (!compat)
		return csize;

	/* Find true end (we tolerate multiple \0 at the end */
	for (i = (cplen - 1); i >= 0 && !compat[i]; i--)
		cplen--;
	if (!cplen)
		return csize;
	cplen++;

	/* Check space (need cplen+1 chars including final \0) */
	tsize = csize + cplen;
	repend = tsize;

	if (csize >= len)		/* @ the limit, all is already filled */
		return tsize;

	if (tsize >= len) {		/* limit compat list */
		cplen = len - csize - 1;
		repend = len;
	}

	/* Copy and do char replacement */
	memcpy(&str[csize + 1], compat, cplen);
	for (i = csize; i < repend; i++) {
		char c = str[i];
		if (c == '\0')
			str[i] = 'C';
		else if (c == ' ')
			str[i] = '_';
	}

	return tsize;
}

/**
 * of_device_uevent - Display OF related uevent information
 */
void of_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	const char *compat;
	struct alias_prop *app;
	int seen = 0, cplen, sl;

	if ((!dev) || (!dev->of_node))
		return;

	add_uevent_var(env, "OF_NAME=%s", dev->of_node->name);
	add_uevent_var(env, "OF_FULLNAME=%s", dev->of_node->full_name);
	if (dev->of_node->type && strcmp("<NULL>", dev->of_node->type) != 0)
		add_uevent_var(env, "OF_TYPE=%s", dev->of_node->type);

	/* Since the compatible field can contain pretty much anything
	 * it's not really legal to split it out with commas. We split it
	 * up using a number of environment variables instead. */
	compat = of_get_property(dev->of_node, "compatible", &cplen);
	while (compat && *compat && cplen > 0) {
		add_uevent_var(env, "OF_COMPATIBLE_%d=%s", seen, compat);
		sl = strlen(compat) + 1;
		compat += sl;
		cplen -= sl;
		seen++;
	}
	add_uevent_var(env, "OF_COMPATIBLE_N=%d", seen);

	seen = 0;
	mutex_lock(&of_aliases_mutex);
	list_for_each_entry(app, &aliases_lookup, link) {
		if (dev->of_node == app->np) {
			add_uevent_var(env, "OF_ALIAS_%d=%s", seen,
				       app->alias);
			seen++;
		}
	}
	mutex_unlock(&of_aliases_mutex);
}

int of_device_uevent_modalias(struct device *dev, struct kobj_uevent_env *env)
{
	int sl;

	if ((!dev) || (!dev->of_node))
		return -ENODEV;

	/* Devicetree modalias is tricky, we add it in 2 steps */
	if (add_uevent_var(env, "MODALIAS="))
		return -ENOMEM;

	sl = of_device_get_modalias(dev, &env->buf[env->buflen-1],
				    sizeof(env->buf) - env->buflen);
	if (sl >= (sizeof(env->buf) - env->buflen))
		return -ENOMEM;
	env->buflen += sl;

	return 0;
}
