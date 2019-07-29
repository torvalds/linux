// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2016 Intel Corporation.
 *
 * Intel Virtio Over PCIe (VOP) driver.
 */
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include "vop_main.h"

static int vop_dp_show(struct seq_file *s, void *pos)
{
	struct mic_device_desc *d;
	struct mic_device_ctrl *dc;
	struct mic_vqconfig *vqconfig;
	__u32 *features;
	__u8 *config;
	struct vop_info *vi = s->private;
	struct vop_device *vpdev = vi->vpdev;
	struct mic_bootparam *bootparam = vpdev->hw_ops->get_dp(vpdev);
	int j, k;

	seq_printf(s, "Bootparam: magic 0x%x\n",
		   bootparam->magic);
	seq_printf(s, "Bootparam: h2c_config_db %d\n",
		   bootparam->h2c_config_db);
	seq_printf(s, "Bootparam: node_id %d\n",
		   bootparam->node_id);
	seq_printf(s, "Bootparam: c2h_scif_db %d\n",
		   bootparam->c2h_scif_db);
	seq_printf(s, "Bootparam: h2c_scif_db %d\n",
		   bootparam->h2c_scif_db);
	seq_printf(s, "Bootparam: scif_host_dma_addr 0x%llx\n",
		   bootparam->scif_host_dma_addr);
	seq_printf(s, "Bootparam: scif_card_dma_addr 0x%llx\n",
		   bootparam->scif_card_dma_addr);

	for (j = sizeof(*bootparam);
		j < MIC_DP_SIZE; j += mic_total_desc_size(d)) {
		d = (void *)bootparam + j;
		dc = (void *)d + mic_aligned_desc_size(d);

		/* end of list */
		if (d->type == 0)
			break;

		if (d->type == -1)
			continue;

		seq_printf(s, "Type %d ", d->type);
		seq_printf(s, "Num VQ %d ", d->num_vq);
		seq_printf(s, "Feature Len %d\n", d->feature_len);
		seq_printf(s, "Config Len %d ", d->config_len);
		seq_printf(s, "Shutdown Status %d\n", d->status);

		for (k = 0; k < d->num_vq; k++) {
			vqconfig = mic_vq_config(d) + k;
			seq_printf(s, "vqconfig[%d]: ", k);
			seq_printf(s, "address 0x%llx ",
				   vqconfig->address);
			seq_printf(s, "num %d ", vqconfig->num);
			seq_printf(s, "used address 0x%llx\n",
				   vqconfig->used_address);
		}

		features = (__u32 *)mic_vq_features(d);
		seq_printf(s, "Features: Host 0x%x ", features[0]);
		seq_printf(s, "Guest 0x%x\n", features[1]);

		config = mic_vq_configspace(d);
		for (k = 0; k < d->config_len; k++)
			seq_printf(s, "config[%d]=%d\n", k, config[k]);

		seq_puts(s, "Device control:\n");
		seq_printf(s, "Config Change %d ", dc->config_change);
		seq_printf(s, "Vdev reset %d\n", dc->vdev_reset);
		seq_printf(s, "Guest Ack %d ", dc->guest_ack);
		seq_printf(s, "Host ack %d\n", dc->host_ack);
		seq_printf(s, "Used address updated %d ",
			   dc->used_address_updated);
		seq_printf(s, "Vdev 0x%llx\n", dc->vdev);
		seq_printf(s, "c2h doorbell %d ", dc->c2h_vdev_db);
		seq_printf(s, "h2c doorbell %d\n", dc->h2c_vdev_db);
	}
	schedule_work(&vi->hotplug_work);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(vop_dp);

static int vop_vdev_info_show(struct seq_file *s, void *unused)
{
	struct vop_info *vi = s->private;
	struct list_head *pos, *tmp;
	struct vop_vdev *vdev;
	int i, j;

	mutex_lock(&vi->vop_mutex);
	list_for_each_safe(pos, tmp, &vi->vdev_list) {
		vdev = list_entry(pos, struct vop_vdev, list);
		seq_printf(s, "VDEV type %d state %s in %ld out %ld in_dma %ld out_dma %ld\n",
			   vdev->virtio_id,
			   vop_vdevup(vdev) ? "UP" : "DOWN",
			   vdev->in_bytes,
			   vdev->out_bytes,
			   vdev->in_bytes_dma,
			   vdev->out_bytes_dma);
		for (i = 0; i < MIC_MAX_VRINGS; i++) {
			struct vring_desc *desc;
			struct vring_avail *avail;
			struct vring_used *used;
			struct vop_vringh *vvr = &vdev->vvr[i];
			struct vringh *vrh = &vvr->vrh;
			int num = vrh->vring.num;

			if (!num)
				continue;
			desc = vrh->vring.desc;
			seq_printf(s, "vring i %d avail_idx %d",
				   i, vvr->vring.info->avail_idx & (num - 1));
			seq_printf(s, " vring i %d avail_idx %d\n",
				   i, vvr->vring.info->avail_idx);
			seq_printf(s, "vrh i %d weak_barriers %d",
				   i, vrh->weak_barriers);
			seq_printf(s, " last_avail_idx %d last_used_idx %d",
				   vrh->last_avail_idx, vrh->last_used_idx);
			seq_printf(s, " completed %d\n", vrh->completed);
			for (j = 0; j < num; j++) {
				seq_printf(s, "desc[%d] addr 0x%llx len %d",
					   j, desc->addr, desc->len);
				seq_printf(s, " flags 0x%x next %d\n",
					   desc->flags, desc->next);
				desc++;
			}
			avail = vrh->vring.avail;
			seq_printf(s, "avail flags 0x%x idx %d\n",
				   vringh16_to_cpu(vrh, avail->flags),
				   vringh16_to_cpu(vrh,
						   avail->idx) & (num - 1));
			seq_printf(s, "avail flags 0x%x idx %d\n",
				   vringh16_to_cpu(vrh, avail->flags),
				   vringh16_to_cpu(vrh, avail->idx));
			for (j = 0; j < num; j++)
				seq_printf(s, "avail ring[%d] %d\n",
					   j, avail->ring[j]);
			used = vrh->vring.used;
			seq_printf(s, "used flags 0x%x idx %d\n",
				   vringh16_to_cpu(vrh, used->flags),
				   vringh16_to_cpu(vrh, used->idx) & (num - 1));
			seq_printf(s, "used flags 0x%x idx %d\n",
				   vringh16_to_cpu(vrh, used->flags),
				   vringh16_to_cpu(vrh, used->idx));
			for (j = 0; j < num; j++)
				seq_printf(s, "used ring[%d] id %d len %d\n",
					   j, vringh32_to_cpu(vrh,
							      used->ring[j].id),
					   vringh32_to_cpu(vrh,
							   used->ring[j].len));
		}
	}
	mutex_unlock(&vi->vop_mutex);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(vop_vdev_info);

void vop_init_debugfs(struct vop_info *vi)
{
	char name[16];

	snprintf(name, sizeof(name), "%s%d", KBUILD_MODNAME, vi->vpdev->dnode);
	vi->dbg = debugfs_create_dir(name, NULL);
	debugfs_create_file("dp", 0444, vi->dbg, vi, &vop_dp_fops);
	debugfs_create_file("vdev_info", 0444, vi->dbg, vi, &vop_vdev_info_fops);
}

void vop_exit_debugfs(struct vop_info *vi)
{
	debugfs_remove_recursive(vi->dbg);
}
