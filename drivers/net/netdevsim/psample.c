// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Mellanox Technologies. All rights reserved */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/etherdevice.h>
#include <linux/inet.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <net/devlink.h>
#include <net/ip.h>
#include <net/psample.h>
#include <uapi/linux/ip.h>
#include <uapi/linux/udp.h>

#include "netdevsim.h"

#define NSIM_PSAMPLE_REPORT_INTERVAL_MS	100
#define NSIM_PSAMPLE_INVALID_TC		0xFFFF
#define NSIM_PSAMPLE_L4_DATA_LEN	100

struct nsim_dev_psample {
	struct delayed_work psample_dw;
	struct dentry *ddir;
	struct psample_group *group;
	u32 rate;
	u32 group_num;
	u32 trunc_size;
	int in_ifindex;
	int out_ifindex;
	u16 out_tc;
	u64 out_tc_occ_max;
	u64 latency_max;
	bool is_active;
};

static struct sk_buff *nsim_dev_psample_skb_build(void)
{
	int tot_len, data_len = NSIM_PSAMPLE_L4_DATA_LEN;
	struct sk_buff *skb;
	struct udphdr *udph;
	struct ethhdr *eth;
	struct iphdr *iph;

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb)
		return NULL;
	tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + data_len;

	skb_reset_mac_header(skb);
	eth = skb_put(skb, sizeof(struct ethhdr));
	eth_random_addr(eth->h_dest);
	eth_random_addr(eth->h_source);
	eth->h_proto = htons(ETH_P_IP);
	skb->protocol = htons(ETH_P_IP);

	skb_set_network_header(skb, skb->len);
	iph = skb_put(skb, sizeof(struct iphdr));
	iph->protocol = IPPROTO_UDP;
	iph->saddr = in_aton("192.0.2.1");
	iph->daddr = in_aton("198.51.100.1");
	iph->version = 0x4;
	iph->frag_off = 0;
	iph->ihl = 0x5;
	iph->tot_len = htons(tot_len);
	iph->id = 0;
	iph->ttl = 100;
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);

	skb_set_transport_header(skb, skb->len);
	udph = skb_put_zero(skb, sizeof(struct udphdr) + data_len);
	get_random_bytes(&udph->source, sizeof(u16));
	get_random_bytes(&udph->dest, sizeof(u16));
	udph->len = htons(sizeof(struct udphdr) + data_len);

	return skb;
}

static void nsim_dev_psample_md_prepare(const struct nsim_dev_psample *psample,
					struct psample_metadata *md,
					unsigned int len)
{
	md->trunc_size = psample->trunc_size ? psample->trunc_size : len;
	md->in_ifindex = psample->in_ifindex;
	md->out_ifindex = psample->out_ifindex;

	if (psample->out_tc != NSIM_PSAMPLE_INVALID_TC) {
		md->out_tc = psample->out_tc;
		md->out_tc_valid = 1;
	}

	if (psample->out_tc_occ_max) {
		u64 out_tc_occ;

		get_random_bytes(&out_tc_occ, sizeof(u64));
		md->out_tc_occ = out_tc_occ & (psample->out_tc_occ_max - 1);
		md->out_tc_occ_valid = 1;
	}

	if (psample->latency_max) {
		u64 latency;

		get_random_bytes(&latency, sizeof(u64));
		md->latency = latency & (psample->latency_max - 1);
		md->latency_valid = 1;
	}
}

static void nsim_dev_psample_report_work(struct work_struct *work)
{
	struct nsim_dev_psample *psample;
	struct psample_metadata md = {};
	struct sk_buff *skb;
	unsigned long delay;

	psample = container_of(work, struct nsim_dev_psample, psample_dw.work);

	skb = nsim_dev_psample_skb_build();
	if (!skb)
		goto out;

	nsim_dev_psample_md_prepare(psample, &md, skb->len);
	psample_sample_packet(psample->group, skb, psample->rate, &md);
	consume_skb(skb);

out:
	delay = msecs_to_jiffies(NSIM_PSAMPLE_REPORT_INTERVAL_MS);
	schedule_delayed_work(&psample->psample_dw, delay);
}

static int nsim_dev_psample_enable(struct nsim_dev *nsim_dev)
{
	struct nsim_dev_psample *psample = nsim_dev->psample;
	struct devlink *devlink;
	unsigned long delay;

	if (psample->is_active)
		return -EBUSY;

	devlink = priv_to_devlink(nsim_dev);
	psample->group = psample_group_get(devlink_net(devlink),
					   psample->group_num);
	if (!psample->group)
		return -EINVAL;

	delay = msecs_to_jiffies(NSIM_PSAMPLE_REPORT_INTERVAL_MS);
	schedule_delayed_work(&psample->psample_dw, delay);

	psample->is_active = true;

	return 0;
}

static int nsim_dev_psample_disable(struct nsim_dev *nsim_dev)
{
	struct nsim_dev_psample *psample = nsim_dev->psample;

	if (!psample->is_active)
		return -EINVAL;

	psample->is_active = false;

	cancel_delayed_work_sync(&psample->psample_dw);
	psample_group_put(psample->group);

	return 0;
}

static ssize_t nsim_dev_psample_enable_write(struct file *file,
					     const char __user *data,
					     size_t count, loff_t *ppos)
{
	struct nsim_dev *nsim_dev = file->private_data;
	bool enable;
	int err;

	err = kstrtobool_from_user(data, count, &enable);
	if (err)
		return err;

	if (enable)
		err = nsim_dev_psample_enable(nsim_dev);
	else
		err = nsim_dev_psample_disable(nsim_dev);

	return err ? err : count;
}

static const struct file_operations nsim_psample_enable_fops = {
	.open = simple_open,
	.write = nsim_dev_psample_enable_write,
	.llseek = generic_file_llseek,
	.owner = THIS_MODULE,
};

int nsim_dev_psample_init(struct nsim_dev *nsim_dev)
{
	struct nsim_dev_psample *psample;
	int err;

	psample = kzalloc(sizeof(*psample), GFP_KERNEL);
	if (!psample)
		return -ENOMEM;
	nsim_dev->psample = psample;

	INIT_DELAYED_WORK(&psample->psample_dw, nsim_dev_psample_report_work);

	psample->ddir = debugfs_create_dir("psample", nsim_dev->ddir);
	if (IS_ERR(psample->ddir)) {
		err = PTR_ERR(psample->ddir);
		goto err_psample_free;
	}

	/* Populate sampling parameters with sane defaults. */
	psample->rate = 100;
	debugfs_create_u32("rate", 0600, psample->ddir, &psample->rate);

	psample->group_num = 10;
	debugfs_create_u32("group_num", 0600, psample->ddir,
			   &psample->group_num);

	psample->trunc_size = 0;
	debugfs_create_u32("trunc_size", 0600, psample->ddir,
			   &psample->trunc_size);

	psample->in_ifindex = 1;
	debugfs_create_u32("in_ifindex", 0600, psample->ddir,
			   &psample->in_ifindex);

	psample->out_ifindex = 2;
	debugfs_create_u32("out_ifindex", 0600, psample->ddir,
			   &psample->out_ifindex);

	psample->out_tc = 0;
	debugfs_create_u16("out_tc", 0600, psample->ddir, &psample->out_tc);

	psample->out_tc_occ_max = 10000;
	debugfs_create_u64("out_tc_occ_max", 0600, psample->ddir,
			   &psample->out_tc_occ_max);

	psample->latency_max = 50;
	debugfs_create_u64("latency_max", 0600, psample->ddir,
			   &psample->latency_max);

	debugfs_create_file("enable", 0200, psample->ddir, nsim_dev,
			    &nsim_psample_enable_fops);

	return 0;

err_psample_free:
	kfree(nsim_dev->psample);
	return err;
}

void nsim_dev_psample_exit(struct nsim_dev *nsim_dev)
{
	debugfs_remove_recursive(nsim_dev->psample->ddir);
	if (nsim_dev->psample->is_active) {
		cancel_delayed_work_sync(&nsim_dev->psample->psample_dw);
		psample_group_put(nsim_dev->psample->group);
	}
	kfree(nsim_dev->psample);
}
