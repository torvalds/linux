#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>

#include <asm/errno.h>
#include <asm/of_device.h>

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
EXPORT_SYMBOL(of_device_uevent);
EXPORT_SYMBOL(of_device_get_modalias);
