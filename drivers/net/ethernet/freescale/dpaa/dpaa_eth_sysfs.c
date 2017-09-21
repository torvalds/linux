/* Copyright 2008-2016 Freescale Semiconductor Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *	 notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *	 notice, this list of conditions and the following disclaimer in the
 *	 documentation and/or other materials provided with the distribution.
 *     * Neither the name of Freescale Semiconductor nor the
 *	 names of its contributors may be used to endorse or promote products
 *	 derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY Freescale Semiconductor ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Freescale Semiconductor BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/of_net.h>
#include "dpaa_eth.h"
#include "mac.h"

static ssize_t dpaa_eth_show_addr(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct dpaa_priv *priv = netdev_priv(to_net_dev(dev));
	struct mac_device *mac_dev = priv->mac_dev;

	if (mac_dev)
		return sprintf(buf, "%llx",
				(unsigned long long)mac_dev->res->start);
	else
		return sprintf(buf, "none");
}

static ssize_t dpaa_eth_show_fqids(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct dpaa_priv *priv = netdev_priv(to_net_dev(dev));
	struct dpaa_fq *prev = NULL;
	char *prevstr = NULL;
	struct dpaa_fq *tmp;
	struct dpaa_fq *fq;
	u32 first_fqid = 0;
	u32 last_fqid = 0;
	ssize_t bytes = 0;
	char *str;
	int i = 0;

	list_for_each_entry_safe(fq, tmp, &priv->dpaa_fq_list, list) {
		switch (fq->fq_type) {
		case FQ_TYPE_RX_DEFAULT:
			str = "Rx default";
			break;
		case FQ_TYPE_RX_ERROR:
			str = "Rx error";
			break;
		case FQ_TYPE_RX_PCD:
			str = "Rx PCD";
			break;
		case FQ_TYPE_TX_CONFIRM:
			str = "Tx default confirmation";
			break;
		case FQ_TYPE_TX_CONF_MQ:
			str = "Tx confirmation (mq)";
			break;
		case FQ_TYPE_TX_ERROR:
			str = "Tx error";
			break;
		case FQ_TYPE_TX:
			str = "Tx";
			break;
		default:
			str = "Unknown";
		}

		if (prev && (abs(fq->fqid - prev->fqid) != 1 ||
			     str != prevstr)) {
			if (last_fqid == first_fqid)
				bytes += sprintf(buf + bytes,
					"%s: %d\n", prevstr, prev->fqid);
			else
				bytes += sprintf(buf + bytes,
					"%s: %d - %d\n", prevstr,
					first_fqid, last_fqid);
		}

		if (prev && abs(fq->fqid - prev->fqid) == 1 &&
		    str == prevstr) {
			last_fqid = fq->fqid;
		} else {
			first_fqid = fq->fqid;
			last_fqid = fq->fqid;
		}

		prev = fq;
		prevstr = str;
		i++;
	}

	if (prev) {
		if (last_fqid == first_fqid)
			bytes += sprintf(buf + bytes, "%s: %d\n", prevstr,
					prev->fqid);
		else
			bytes += sprintf(buf + bytes, "%s: %d - %d\n", prevstr,
					first_fqid, last_fqid);
	}

	return bytes;
}

static ssize_t dpaa_eth_show_bpids(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct dpaa_priv *priv = netdev_priv(to_net_dev(dev));
	ssize_t bytes = 0;
	int i = 0;

	for (i = 0; i < DPAA_BPS_NUM; i++)
		bytes += snprintf(buf + bytes, PAGE_SIZE - bytes, "%u\n",
				  priv->dpaa_bps[i]->bpid);

	return bytes;
}

static struct device_attribute dpaa_eth_attrs[] = {
	__ATTR(device_addr, 0444, dpaa_eth_show_addr, NULL),
	__ATTR(fqids, 0444, dpaa_eth_show_fqids, NULL),
	__ATTR(bpids, 0444, dpaa_eth_show_bpids, NULL),
};

void dpaa_eth_sysfs_init(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dpaa_eth_attrs); i++)
		if (device_create_file(dev, &dpaa_eth_attrs[i])) {
			dev_err(dev, "Error creating sysfs file\n");
			while (i > 0)
				device_remove_file(dev, &dpaa_eth_attrs[--i]);
			return;
		}
}

void dpaa_eth_sysfs_remove(struct device *dev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(dpaa_eth_attrs); i++)
		device_remove_file(dev, &dpaa_eth_attrs[i]);
}
