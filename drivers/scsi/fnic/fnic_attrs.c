/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
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
#include <scsi/scsi_host.h>
#include "fnic.h"

static ssize_t fnic_show_state(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct fc_lport *lp = shost_priv(class_to_shost(dev));
	struct fnic *fnic = lport_priv(lp);

	return snprintf(buf, PAGE_SIZE, "%s\n", fnic_state_str[fnic->state]);
}

static ssize_t fnic_show_drv_version(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", DRV_VERSION);
}

static ssize_t fnic_show_link_state(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct fc_lport *lp = shost_priv(class_to_shost(dev));

	return snprintf(buf, PAGE_SIZE, "%s\n", (lp->link_up)
			? "Link Up" : "Link Down");
}

static DEVICE_ATTR(fnic_state, S_IRUGO, fnic_show_state, NULL);
static DEVICE_ATTR(drv_version, S_IRUGO, fnic_show_drv_version, NULL);
static DEVICE_ATTR(link_state, S_IRUGO, fnic_show_link_state, NULL);

static struct attribute *fnic_host_attrs[] = {
	&dev_attr_fnic_state.attr,
	&dev_attr_drv_version.attr,
	&dev_attr_link_state.attr,
	NULL,
};

static const struct attribute_group fnic_host_attr_group = {
	.attrs = fnic_host_attrs
};

const struct attribute_group *fnic_host_groups[] = {
	&fnic_host_attr_group,
	NULL
};
