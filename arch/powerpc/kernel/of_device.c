#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>

#include <asm/errno.h>
#include <asm/of_device.h>

/**
 * of_match_node - Tell if an device_node has a matching of_match structure
 * @ids: array of of device match structures to search in
 * @node: the of device structure to match against
 *
 * Low level utility function used by device matching.
 */
const struct of_device_id *of_match_node(const struct of_device_id *matches,
					 const struct device_node *node)
{
	while (matches->name[0] || matches->type[0] || matches->compatible[0]) {
		int match = 1;
		if (matches->name[0])
			match &= node->name
				&& !strcmp(matches->name, node->name);
		if (matches->type[0])
			match &= node->type
				&& !strcmp(matches->type, node->type);
		if (matches->compatible[0])
			match &= of_device_is_compatible(node,
						      matches->compatible);
		if (match)
			return matches;
		matches++;
	}
	return NULL;
}

/**
 * of_match_device - Tell if an of_device structure has a matching
 * of_match structure
 * @ids: array of of device match structures to search in
 * @dev: the of device structure to match against
 *
 * Used by a driver to check whether an of_device present in the
 * system is in its list of supported devices.
 */
const struct of_device_id *of_match_device(const struct of_device_id *matches,
					const struct of_device *dev)
{
	if (!dev->node)
		return NULL;
	return of_match_node(matches, dev->node);
}

struct of_device *of_dev_get(struct of_device *dev)
{
	struct device *tmp;

	if (!dev)
		return NULL;
	tmp = get_device(&dev->dev);
	if (tmp)
		return to_of_device(tmp);
	else
		return NULL;
}

void of_dev_put(struct of_device *dev)
{
	if (dev)
		put_device(&dev->dev);
}

static ssize_t dev_show_devspec(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct of_device *ofdev;

	ofdev = to_of_device(dev);
	return sprintf(buf, "%s", ofdev->node->full_name);
}

static DEVICE_ATTR(devspec, S_IRUGO, dev_show_devspec, NULL);

/**
 * of_release_dev - free an of device structure when all users of it are finished.
 * @dev: device that's been disconnected
 *
 * Will be called only by the device core when all users of this of device are
 * done.
 */
void of_release_dev(struct device *dev)
{
	struct of_device *ofdev;

        ofdev = to_of_device(dev);
	of_node_put(ofdev->node);
	kfree(ofdev);
}

int of_device_register(struct of_device *ofdev)
{
	int rc;

	BUG_ON(ofdev->node == NULL);

	rc = device_register(&ofdev->dev);
	if (rc)
		return rc;

	return device_create_file(&ofdev->dev, &dev_attr_devspec);
}

void of_device_unregister(struct of_device *ofdev)
{
	device_remove_file(&ofdev->dev, &dev_attr_devspec);

	device_unregister(&ofdev->dev);
}


ssize_t of_device_get_modalias(struct of_device *ofdev,
				char *str, ssize_t len)
{
	const char *compat;
	int cplen, i;
	ssize_t tsize, csize, repend;

	/* Name & Type */
	csize = snprintf(str, len, "of:N%sT%s",
				ofdev->node->name, ofdev->node->type);

	/* Get compatible property if any */
	compat = of_get_property(ofdev->node, "compatible", &cplen);
	if (!compat)
		return csize;

	/* Find true end (we tolerate multiple \0 at the end */
	for (i=(cplen-1); i>=0 && !compat[i]; i--)
		cplen--;
	if (!cplen)
		return csize;
	cplen++;

	/* Check space (need cplen+1 chars including final \0) */
	tsize = csize + cplen;
	repend = tsize;

	if (csize>=len)		/* @ the limit, all is already filled */
		return tsize;

	if (tsize>=len) {		/* limit compat list */
		cplen = len-csize-1;
		repend = len;
	}

	/* Copy and do char replacement */
	memcpy(&str[csize+1], compat, cplen);
	for (i=csize; i<repend; i++) {
		char c = str[i];
		if (c=='\0')
			str[i] = 'C';
		else if (c==' ')
			str[i] = '_';
	}

	return tsize;
}

int of_device_uevent(struct device *dev,
		char **envp, int num_envp, char *buffer, int buffer_size)
{
	struct of_device *ofdev;
	const char *compat;
	int i = 0, length = 0, seen = 0, cplen, sl;

	if (!dev)
		return -ENODEV;

	ofdev = to_of_device(dev);

	if (add_uevent_var(envp, num_envp, &i,
			   buffer, buffer_size, &length,
			   "OF_NAME=%s", ofdev->node->name))
		return -ENOMEM;

	if (add_uevent_var(envp, num_envp, &i,
			   buffer, buffer_size, &length,
			   "OF_TYPE=%s", ofdev->node->type))
		return -ENOMEM;

        /* Since the compatible field can contain pretty much anything
         * it's not really legal to split it out with commas. We split it
         * up using a number of environment variables instead. */

	compat = of_get_property(ofdev->node, "compatible", &cplen);
	while (compat && *compat && cplen > 0) {
		if (add_uevent_var(envp, num_envp, &i,
				   buffer, buffer_size, &length,
				   "OF_COMPATIBLE_%d=%s", seen, compat))
			return -ENOMEM;

		sl = strlen (compat) + 1;
		compat += sl;
		cplen -= sl;
		seen++;
	}

	if (add_uevent_var(envp, num_envp, &i,
			   buffer, buffer_size, &length,
			   "OF_COMPATIBLE_N=%d", seen))
		return -ENOMEM;

	/* modalias is trickier, we add it in 2 steps */
	if (add_uevent_var(envp, num_envp, &i,
			   buffer, buffer_size, &length,
			   "MODALIAS="))
		return -ENOMEM;

	sl = of_device_get_modalias(ofdev, &buffer[length-1],
					buffer_size-length);
	if (sl >= (buffer_size-length))
		return -ENOMEM;

	length += sl;

	envp[i] = NULL;

	return 0;
}


EXPORT_SYMBOL(of_match_node);
EXPORT_SYMBOL(of_match_device);
EXPORT_SYMBOL(of_device_register);
EXPORT_SYMBOL(of_device_unregister);
EXPORT_SYMBOL(of_dev_get);
EXPORT_SYMBOL(of_dev_put);
EXPORT_SYMBOL(of_release_dev);
EXPORT_SYMBOL(of_device_uevent);
EXPORT_SYMBOL(of_device_get_modalias);
