/*
 * Copyright 2014 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/string.h>
#include <linux/device.h>

#include "snic.h"

static ssize_t
snic_show_sym_name(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct snic *snic = shost_priv(class_to_shost(dev));

	return snprintf(buf, PAGE_SIZE, "%s\n", snic->name);
}

static ssize_t
snic_show_state(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct snic *snic = shost_priv(class_to_shost(dev));

	return snprintf(buf, PAGE_SIZE, "%s\n",
			snic_state_str[snic_get_state(snic)]);
}

static ssize_t
snic_show_drv_version(struct device *dev,
		      struct device_attribute *attr,
		      char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", SNIC_DRV_VERSION);
}

static ssize_t
snic_show_link_state(struct device *dev,
		     struct device_attribute *attr,
		     char *buf)
{
	struct snic *snic = shost_priv(class_to_shost(dev));

	if (snic->config.xpt_type == SNIC_DAS)
		snic->link_status = svnic_dev_link_status(snic->vdev);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			(snic->link_status) ? "Link Up" : "Link Down");
}

static DEVICE_ATTR(snic_sym_name, S_IRUGO, snic_show_sym_name, NULL);
static DEVICE_ATTR(snic_state, S_IRUGO, snic_show_state, NULL);
static DEVICE_ATTR(drv_version, S_IRUGO, snic_show_drv_version, NULL);
static DEVICE_ATTR(link_state, S_IRUGO, snic_show_link_state, NULL);

struct device_attribute *snic_attrs[] = {
	&dev_attr_snic_sym_name,
	&dev_attr_snic_state,
	&dev_attr_drv_version,
	&dev_attr_link_state,
	NULL,
};
