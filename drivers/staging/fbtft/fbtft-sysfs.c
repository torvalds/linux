#include "fbtft.h"
#include "internal.h"

static int get_next_ulong(char **str_p, unsigned long *val, char *sep, int base)
{
	char *p_val;

	if (!str_p || !(*str_p))
		return -EINVAL;

	p_val = strsep(str_p, sep);

	if (!p_val)
		return -EINVAL;

	return kstrtoul(p_val, base, val);
}

int fbtft_gamma_parse_str(struct fbtft_par *par, u32 *curves,
			  const char *str, int size)
{
	char *str_p, *curve_p = NULL;
	char *tmp;
	unsigned long val = 0;
	int ret = 0;
	int curve_counter, value_counter;

	fbtft_par_dbg(DEBUG_SYSFS, par, "%s() str=\n", __func__);

	if (!str || !curves)
		return -EINVAL;

	fbtft_par_dbg(DEBUG_SYSFS, par, "%s\n", str);

	tmp = kmemdup(str, size + 1, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	/* replace optional separators */
	str_p = tmp;
	while (*str_p) {
		if (*str_p == ',')
			*str_p = ' ';
		if (*str_p == ';')
			*str_p = '\n';
		str_p++;
	}

	str_p = strim(tmp);

	curve_counter = 0;
	while (str_p) {
		if (curve_counter == par->gamma.num_curves) {
			dev_err(par->info->device, "Gamma: Too many curves\n");
			ret = -EINVAL;
			goto out;
		}
		curve_p = strsep(&str_p, "\n");
		value_counter = 0;
		while (curve_p) {
			if (value_counter == par->gamma.num_values) {
				dev_err(par->info->device,
					"Gamma: Too many values\n");
				ret = -EINVAL;
				goto out;
			}
			ret = get_next_ulong(&curve_p, &val, " ", 16);
			if (ret)
				goto out;
			curves[curve_counter * par->gamma.num_values + value_counter] = val;
			value_counter++;
		}
		if (value_counter != par->gamma.num_values) {
			dev_err(par->info->device, "Gamma: Too few values\n");
			ret = -EINVAL;
			goto out;
		}
		curve_counter++;
	}
	if (curve_counter != par->gamma.num_curves) {
		dev_err(par->info->device, "Gamma: Too few curves\n");
		ret = -EINVAL;
		goto out;
	}

out:
	kfree(tmp);
	return ret;
}

static ssize_t
sprintf_gamma(struct fbtft_par *par, u32 *curves, char *buf)
{
	ssize_t len = 0;
	unsigned int i, j;

	mutex_lock(&par->gamma.lock);
	for (i = 0; i < par->gamma.num_curves; i++) {
		for (j = 0; j < par->gamma.num_values; j++)
			len += scnprintf(&buf[len], PAGE_SIZE,
			     "%04x ", curves[i * par->gamma.num_values + j]);
		buf[len - 1] = '\n';
	}
	mutex_unlock(&par->gamma.lock);

	return len;
}

static ssize_t store_gamma_curve(struct device *device,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fbtft_par *par = fb_info->par;
	u32 tmp_curves[FBTFT_GAMMA_MAX_VALUES_TOTAL];
	int ret;

	ret = fbtft_gamma_parse_str(par, tmp_curves, buf, count);
	if (ret)
		return ret;

	ret = par->fbtftops.set_gamma(par, tmp_curves);
	if (ret)
		return ret;

	mutex_lock(&par->gamma.lock);
	memcpy(par->gamma.curves, tmp_curves,
	       par->gamma.num_curves * par->gamma.num_values * sizeof(tmp_curves[0]));
	mutex_unlock(&par->gamma.lock);

	return count;
}

static ssize_t show_gamma_curve(struct device *device,
				struct device_attribute *attr, char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fbtft_par *par = fb_info->par;

	return sprintf_gamma(par, par->gamma.curves, buf);
}

static struct device_attribute gamma_device_attrs[] = {
	__ATTR(gamma, 0660, show_gamma_curve, store_gamma_curve),
};

void fbtft_expand_debug_value(unsigned long *debug)
{
	switch (*debug & 0x7) {
	case 1:
		*debug |= DEBUG_LEVEL_1;
		break;
	case 2:
		*debug |= DEBUG_LEVEL_2;
		break;
	case 3:
		*debug |= DEBUG_LEVEL_3;
		break;
	case 4:
		*debug |= DEBUG_LEVEL_4;
		break;
	case 5:
		*debug |= DEBUG_LEVEL_5;
		break;
	case 6:
		*debug |= DEBUG_LEVEL_6;
		break;
	case 7:
		*debug = 0xFFFFFFFF;
		break;
	}
}

static ssize_t store_debug(struct device *device,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fbtft_par *par = fb_info->par;
	int ret;

	ret = kstrtoul(buf, 10, &par->debug);
	if (ret)
		return ret;
	fbtft_expand_debug_value(&par->debug);

	return count;
}

static ssize_t show_debug(struct device *device,
			  struct device_attribute *attr, char *buf)
{
	struct fb_info *fb_info = dev_get_drvdata(device);
	struct fbtft_par *par = fb_info->par;

	return snprintf(buf, PAGE_SIZE, "%lu\n", par->debug);
}

static struct device_attribute debug_device_attr =
	__ATTR(debug, 0660, show_debug, store_debug);

void fbtft_sysfs_init(struct fbtft_par *par)
{
	device_create_file(par->info->dev, &debug_device_attr);
	if (par->gamma.curves && par->fbtftops.set_gamma)
		device_create_file(par->info->dev, &gamma_device_attrs[0]);
}

void fbtft_sysfs_exit(struct fbtft_par *par)
{
	device_remove_file(par->info->dev, &debug_device_attr);
	if (par->gamma.curves && par->fbtftops.set_gamma)
		device_remove_file(par->info->dev, &gamma_device_attrs[0]);
}
