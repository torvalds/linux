// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/pci.h>
#include <linux/vdpa.h>

#include <linux/pds/pds_common.h>
#include <linux/pds/pds_core_if.h>
#include <linux/pds/pds_adminq.h>
#include <linux/pds/pds_auxbus.h>

#include "aux_drv.h"
#include "vdpa_dev.h"
#include "debugfs.h"

static struct dentry *dbfs_dir;

void pds_vdpa_debugfs_create(void)
{
	dbfs_dir = debugfs_create_dir(PDS_VDPA_DRV_NAME, NULL);
}

void pds_vdpa_debugfs_destroy(void)
{
	debugfs_remove_recursive(dbfs_dir);
	dbfs_dir = NULL;
}

#define PRINT_SBIT_NAME(__seq, __f, __name)                     \
	do {                                                    \
		if ((__f) & (__name))                               \
			seq_printf(__seq, " %s", &#__name[16]); \
	} while (0)

static void print_status_bits(struct seq_file *seq, u8 status)
{
	seq_puts(seq, "status:");
	PRINT_SBIT_NAME(seq, status, VIRTIO_CONFIG_S_ACKNOWLEDGE);
	PRINT_SBIT_NAME(seq, status, VIRTIO_CONFIG_S_DRIVER);
	PRINT_SBIT_NAME(seq, status, VIRTIO_CONFIG_S_DRIVER_OK);
	PRINT_SBIT_NAME(seq, status, VIRTIO_CONFIG_S_FEATURES_OK);
	PRINT_SBIT_NAME(seq, status, VIRTIO_CONFIG_S_NEEDS_RESET);
	PRINT_SBIT_NAME(seq, status, VIRTIO_CONFIG_S_FAILED);
	seq_puts(seq, "\n");
}

static void print_feature_bits_all(struct seq_file *seq, u64 features)
{
	int i;

	seq_puts(seq, "features:");

	for (i = 0; i < (sizeof(u64) * 8); i++) {
		u64 mask = BIT_ULL(i);

		switch (features & mask) {
		case BIT_ULL(VIRTIO_NET_F_CSUM):
			seq_puts(seq, " VIRTIO_NET_F_CSUM");
			break;
		case BIT_ULL(VIRTIO_NET_F_GUEST_CSUM):
			seq_puts(seq, " VIRTIO_NET_F_GUEST_CSUM");
			break;
		case BIT_ULL(VIRTIO_NET_F_CTRL_GUEST_OFFLOADS):
			seq_puts(seq, " VIRTIO_NET_F_CTRL_GUEST_OFFLOADS");
			break;
		case BIT_ULL(VIRTIO_NET_F_MTU):
			seq_puts(seq, " VIRTIO_NET_F_MTU");
			break;
		case BIT_ULL(VIRTIO_NET_F_MAC):
			seq_puts(seq, " VIRTIO_NET_F_MAC");
			break;
		case BIT_ULL(VIRTIO_NET_F_GUEST_TSO4):
			seq_puts(seq, " VIRTIO_NET_F_GUEST_TSO4");
			break;
		case BIT_ULL(VIRTIO_NET_F_GUEST_TSO6):
			seq_puts(seq, " VIRTIO_NET_F_GUEST_TSO6");
			break;
		case BIT_ULL(VIRTIO_NET_F_GUEST_ECN):
			seq_puts(seq, " VIRTIO_NET_F_GUEST_ECN");
			break;
		case BIT_ULL(VIRTIO_NET_F_GUEST_UFO):
			seq_puts(seq, " VIRTIO_NET_F_GUEST_UFO");
			break;
		case BIT_ULL(VIRTIO_NET_F_HOST_TSO4):
			seq_puts(seq, " VIRTIO_NET_F_HOST_TSO4");
			break;
		case BIT_ULL(VIRTIO_NET_F_HOST_TSO6):
			seq_puts(seq, " VIRTIO_NET_F_HOST_TSO6");
			break;
		case BIT_ULL(VIRTIO_NET_F_HOST_ECN):
			seq_puts(seq, " VIRTIO_NET_F_HOST_ECN");
			break;
		case BIT_ULL(VIRTIO_NET_F_HOST_UFO):
			seq_puts(seq, " VIRTIO_NET_F_HOST_UFO");
			break;
		case BIT_ULL(VIRTIO_NET_F_MRG_RXBUF):
			seq_puts(seq, " VIRTIO_NET_F_MRG_RXBUF");
			break;
		case BIT_ULL(VIRTIO_NET_F_STATUS):
			seq_puts(seq, " VIRTIO_NET_F_STATUS");
			break;
		case BIT_ULL(VIRTIO_NET_F_CTRL_VQ):
			seq_puts(seq, " VIRTIO_NET_F_CTRL_VQ");
			break;
		case BIT_ULL(VIRTIO_NET_F_CTRL_RX):
			seq_puts(seq, " VIRTIO_NET_F_CTRL_RX");
			break;
		case BIT_ULL(VIRTIO_NET_F_CTRL_VLAN):
			seq_puts(seq, " VIRTIO_NET_F_CTRL_VLAN");
			break;
		case BIT_ULL(VIRTIO_NET_F_CTRL_RX_EXTRA):
			seq_puts(seq, " VIRTIO_NET_F_CTRL_RX_EXTRA");
			break;
		case BIT_ULL(VIRTIO_NET_F_GUEST_ANNOUNCE):
			seq_puts(seq, " VIRTIO_NET_F_GUEST_ANNOUNCE");
			break;
		case BIT_ULL(VIRTIO_NET_F_MQ):
			seq_puts(seq, " VIRTIO_NET_F_MQ");
			break;
		case BIT_ULL(VIRTIO_NET_F_CTRL_MAC_ADDR):
			seq_puts(seq, " VIRTIO_NET_F_CTRL_MAC_ADDR");
			break;
		case BIT_ULL(VIRTIO_NET_F_HASH_REPORT):
			seq_puts(seq, " VIRTIO_NET_F_HASH_REPORT");
			break;
		case BIT_ULL(VIRTIO_NET_F_RSS):
			seq_puts(seq, " VIRTIO_NET_F_RSS");
			break;
		case BIT_ULL(VIRTIO_NET_F_RSC_EXT):
			seq_puts(seq, " VIRTIO_NET_F_RSC_EXT");
			break;
		case BIT_ULL(VIRTIO_NET_F_STANDBY):
			seq_puts(seq, " VIRTIO_NET_F_STANDBY");
			break;
		case BIT_ULL(VIRTIO_NET_F_SPEED_DUPLEX):
			seq_puts(seq, " VIRTIO_NET_F_SPEED_DUPLEX");
			break;
		case BIT_ULL(VIRTIO_F_NOTIFY_ON_EMPTY):
			seq_puts(seq, " VIRTIO_F_NOTIFY_ON_EMPTY");
			break;
		case BIT_ULL(VIRTIO_F_ANY_LAYOUT):
			seq_puts(seq, " VIRTIO_F_ANY_LAYOUT");
			break;
		case BIT_ULL(VIRTIO_F_VERSION_1):
			seq_puts(seq, " VIRTIO_F_VERSION_1");
			break;
		case BIT_ULL(VIRTIO_F_ACCESS_PLATFORM):
			seq_puts(seq, " VIRTIO_F_ACCESS_PLATFORM");
			break;
		case BIT_ULL(VIRTIO_F_RING_PACKED):
			seq_puts(seq, " VIRTIO_F_RING_PACKED");
			break;
		case BIT_ULL(VIRTIO_F_ORDER_PLATFORM):
			seq_puts(seq, " VIRTIO_F_ORDER_PLATFORM");
			break;
		case BIT_ULL(VIRTIO_F_SR_IOV):
			seq_puts(seq, " VIRTIO_F_SR_IOV");
			break;
		case 0:
			break;
		default:
			seq_printf(seq, " bit_%d", i);
			break;
		}
	}

	seq_puts(seq, "\n");
}

void pds_vdpa_debugfs_add_pcidev(struct pds_vdpa_aux *vdpa_aux)
{
	vdpa_aux->dentry = debugfs_create_dir(pci_name(vdpa_aux->padev->vf_pdev), dbfs_dir);
}

static int identity_show(struct seq_file *seq, void *v)
{
	struct pds_vdpa_aux *vdpa_aux = seq->private;
	struct vdpa_mgmt_dev *mgmt;
	u64 hw_features;

	seq_printf(seq, "aux_dev:            %s\n",
		   dev_name(&vdpa_aux->padev->aux_dev.dev));

	mgmt = &vdpa_aux->vdpa_mdev;
	seq_printf(seq, "max_vqs:            %d\n", mgmt->max_supported_vqs);
	seq_printf(seq, "config_attr_mask:   %#llx\n", mgmt->config_attr_mask);
	hw_features = le64_to_cpu(vdpa_aux->ident.hw_features);
	seq_printf(seq, "hw_features:        %#llx\n", hw_features);
	print_feature_bits_all(seq, hw_features);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(identity);

void pds_vdpa_debugfs_add_ident(struct pds_vdpa_aux *vdpa_aux)
{
	debugfs_create_file("identity", 0400, vdpa_aux->dentry,
			    vdpa_aux, &identity_fops);
}

static int config_show(struct seq_file *seq, void *v)
{
	struct pds_vdpa_device *pdsv = seq->private;
	struct virtio_net_config vc;
	u8 status;

	memcpy_fromio(&vc, pdsv->vdpa_aux->vd_mdev.device,
		      sizeof(struct virtio_net_config));

	seq_printf(seq, "mac:                  %pM\n", vc.mac);
	seq_printf(seq, "max_virtqueue_pairs:  %d\n",
		   __virtio16_to_cpu(true, vc.max_virtqueue_pairs));
	seq_printf(seq, "mtu:                  %d\n", __virtio16_to_cpu(true, vc.mtu));
	seq_printf(seq, "speed:                %d\n", le32_to_cpu(vc.speed));
	seq_printf(seq, "duplex:               %d\n", vc.duplex);
	seq_printf(seq, "rss_max_key_size:     %d\n", vc.rss_max_key_size);
	seq_printf(seq, "rss_max_indirection_table_length: %d\n",
		   le16_to_cpu(vc.rss_max_indirection_table_length));
	seq_printf(seq, "supported_hash_types: %#x\n",
		   le32_to_cpu(vc.supported_hash_types));
	seq_printf(seq, "vn_status:            %#x\n",
		   __virtio16_to_cpu(true, vc.status));

	status = vp_modern_get_status(&pdsv->vdpa_aux->vd_mdev);
	seq_printf(seq, "dev_status:           %#x\n", status);
	print_status_bits(seq, status);
	seq_printf(seq, "negotiated_features:  %#llx\n", pdsv->negotiated_features);
	print_feature_bits_all(seq, pdsv->negotiated_features);
	seq_printf(seq, "vdpa_index:           %d\n", pdsv->vdpa_index);
	seq_printf(seq, "num_vqs:              %d\n", pdsv->num_vqs);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(config);

static int vq_show(struct seq_file *seq, void *v)
{
	struct pds_vdpa_vq_info *vq = seq->private;

	seq_printf(seq, "ready:      %d\n", vq->ready);
	seq_printf(seq, "desc_addr:  %#llx\n", vq->desc_addr);
	seq_printf(seq, "avail_addr: %#llx\n", vq->avail_addr);
	seq_printf(seq, "used_addr:  %#llx\n", vq->used_addr);
	seq_printf(seq, "q_len:      %d\n", vq->q_len);
	seq_printf(seq, "qid:        %d\n", vq->qid);

	seq_printf(seq, "doorbell:   %#llx\n", vq->doorbell);
	seq_printf(seq, "avail_idx:  %d\n", vq->avail_idx);
	seq_printf(seq, "used_idx:   %d\n", vq->used_idx);
	seq_printf(seq, "irq:        %d\n", vq->irq);
	seq_printf(seq, "irq-name:   %s\n", vq->irq_name);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(vq);

void pds_vdpa_debugfs_add_vdpadev(struct pds_vdpa_aux *vdpa_aux)
{
	int i;

	debugfs_create_file("config", 0400, vdpa_aux->dentry, vdpa_aux->pdsv, &config_fops);

	for (i = 0; i < vdpa_aux->pdsv->num_vqs; i++) {
		char name[16];

		snprintf(name, sizeof(name), "vq%02d", i);
		debugfs_create_file(name, 0400, vdpa_aux->dentry,
				    &vdpa_aux->pdsv->vqs[i], &vq_fops);
	}
}

void pds_vdpa_debugfs_del_vdpadev(struct pds_vdpa_aux *vdpa_aux)
{
	debugfs_remove_recursive(vdpa_aux->dentry);
	vdpa_aux->dentry = NULL;
}

void pds_vdpa_debugfs_reset_vdpadev(struct pds_vdpa_aux *vdpa_aux)
{
	/* we don't keep track of the entries, so remove it all
	 * then rebuild the basics
	 */
	pds_vdpa_debugfs_del_vdpadev(vdpa_aux);
	pds_vdpa_debugfs_add_pcidev(vdpa_aux);
	pds_vdpa_debugfs_add_ident(vdpa_aux);
}
