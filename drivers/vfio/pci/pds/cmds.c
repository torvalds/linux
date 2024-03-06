// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#include <linux/io.h>
#include <linux/types.h>
#include <linux/delay.h>

#include <linux/pds/pds_common.h>
#include <linux/pds/pds_core_if.h>
#include <linux/pds/pds_adminq.h>

#include "vfio_dev.h"
#include "cmds.h"

#define SUSPEND_TIMEOUT_S		5
#define SUSPEND_CHECK_INTERVAL_MS	1

static int pds_vfio_client_adminq_cmd(struct pds_vfio_pci_device *pds_vfio,
				      union pds_core_adminq_cmd *req,
				      union pds_core_adminq_comp *resp,
				      bool fast_poll)
{
	struct pci_dev *pdev = pds_vfio_to_pci_dev(pds_vfio);
	union pds_core_adminq_cmd cmd = {};
	struct pdsc *pdsc;
	int err;

	/* Wrap the client request */
	cmd.client_request.opcode = PDS_AQ_CMD_CLIENT_CMD;
	cmd.client_request.client_id = cpu_to_le16(pds_vfio->client_id);
	memcpy(cmd.client_request.client_cmd, req,
	       sizeof(cmd.client_request.client_cmd));

	pdsc = pdsc_get_pf_struct(pdev);
	if (IS_ERR(pdsc))
		return PTR_ERR(pdsc);

	err = pdsc_adminq_post(pdsc, &cmd, resp, fast_poll);
	if (err && err != -EAGAIN)
		dev_err(pds_vfio_to_dev(pds_vfio),
			"client admin cmd failed: %pe\n", ERR_PTR(err));

	return err;
}

int pds_vfio_register_client_cmd(struct pds_vfio_pci_device *pds_vfio)
{
	struct pci_dev *pdev = pds_vfio_to_pci_dev(pds_vfio);
	char devname[PDS_DEVNAME_LEN];
	struct pdsc *pdsc;
	int ci;

	snprintf(devname, sizeof(devname), "%s.%d-%u", PDS_VFIO_LM_DEV_NAME,
		 pci_domain_nr(pdev->bus),
		 PCI_DEVID(pdev->bus->number, pdev->devfn));

	pdsc = pdsc_get_pf_struct(pdev);
	if (IS_ERR(pdsc))
		return PTR_ERR(pdsc);

	ci = pds_client_register(pdsc, devname);
	if (ci < 0)
		return ci;

	pds_vfio->client_id = ci;

	return 0;
}

void pds_vfio_unregister_client_cmd(struct pds_vfio_pci_device *pds_vfio)
{
	struct pci_dev *pdev = pds_vfio_to_pci_dev(pds_vfio);
	struct pdsc *pdsc;
	int err;

	pdsc = pdsc_get_pf_struct(pdev);
	if (IS_ERR(pdsc))
		return;

	err = pds_client_unregister(pdsc, pds_vfio->client_id);
	if (err)
		dev_err(&pdev->dev, "unregister from DSC failed: %pe\n",
			ERR_PTR(err));

	pds_vfio->client_id = 0;
}

static int
pds_vfio_suspend_wait_device_cmd(struct pds_vfio_pci_device *pds_vfio, u8 type)
{
	union pds_core_adminq_cmd cmd = {
		.lm_suspend_status = {
			.opcode = PDS_LM_CMD_SUSPEND_STATUS,
			.vf_id = cpu_to_le16(pds_vfio->vf_id),
			.type = type,
		},
	};
	struct device *dev = pds_vfio_to_dev(pds_vfio);
	union pds_core_adminq_comp comp = {};
	unsigned long time_limit;
	unsigned long time_start;
	unsigned long time_done;
	int err;

	time_start = jiffies;
	time_limit = time_start + HZ * SUSPEND_TIMEOUT_S;
	do {
		err = pds_vfio_client_adminq_cmd(pds_vfio, &cmd, &comp, true);
		if (err != -EAGAIN)
			break;

		msleep(SUSPEND_CHECK_INTERVAL_MS);
	} while (time_before(jiffies, time_limit));

	time_done = jiffies;
	dev_dbg(dev, "%s: vf%u: Suspend comp received in %d msecs\n", __func__,
		pds_vfio->vf_id, jiffies_to_msecs(time_done - time_start));

	/* Check the results */
	if (time_after_eq(time_done, time_limit)) {
		dev_err(dev, "%s: vf%u: Suspend comp timeout\n", __func__,
			pds_vfio->vf_id);
		err = -ETIMEDOUT;
	}

	return err;
}

int pds_vfio_suspend_device_cmd(struct pds_vfio_pci_device *pds_vfio, u8 type)
{
	union pds_core_adminq_cmd cmd = {
		.lm_suspend = {
			.opcode = PDS_LM_CMD_SUSPEND,
			.vf_id = cpu_to_le16(pds_vfio->vf_id),
			.type = type,
		},
	};
	struct device *dev = pds_vfio_to_dev(pds_vfio);
	union pds_core_adminq_comp comp = {};
	int err;

	dev_dbg(dev, "vf%u: Suspend device\n", pds_vfio->vf_id);

	/*
	 * The initial suspend request to the firmware starts the device suspend
	 * operation and the firmware returns success if it's started
	 * successfully.
	 */
	err = pds_vfio_client_adminq_cmd(pds_vfio, &cmd, &comp, true);
	if (err) {
		dev_err(dev, "vf%u: Suspend failed: %pe\n", pds_vfio->vf_id,
			ERR_PTR(err));
		return err;
	}

	/*
	 * The subsequent suspend status request(s) check if the firmware has
	 * completed the device suspend process.
	 */
	return pds_vfio_suspend_wait_device_cmd(pds_vfio, type);
}

int pds_vfio_resume_device_cmd(struct pds_vfio_pci_device *pds_vfio, u8 type)
{
	union pds_core_adminq_cmd cmd = {
		.lm_resume = {
			.opcode = PDS_LM_CMD_RESUME,
			.vf_id = cpu_to_le16(pds_vfio->vf_id),
			.type = type,
		},
	};
	struct device *dev = pds_vfio_to_dev(pds_vfio);
	union pds_core_adminq_comp comp = {};

	dev_dbg(dev, "vf%u: Resume device\n", pds_vfio->vf_id);

	return pds_vfio_client_adminq_cmd(pds_vfio, &cmd, &comp, true);
}

int pds_vfio_get_lm_state_size_cmd(struct pds_vfio_pci_device *pds_vfio, u64 *size)
{
	union pds_core_adminq_cmd cmd = {
		.lm_state_size = {
			.opcode = PDS_LM_CMD_STATE_SIZE,
			.vf_id = cpu_to_le16(pds_vfio->vf_id),
		},
	};
	struct device *dev = pds_vfio_to_dev(pds_vfio);
	union pds_core_adminq_comp comp = {};
	int err;

	dev_dbg(dev, "vf%u: Get migration status\n", pds_vfio->vf_id);

	err = pds_vfio_client_adminq_cmd(pds_vfio, &cmd, &comp, false);
	if (err)
		return err;

	*size = le64_to_cpu(comp.lm_state_size.size);
	return 0;
}

static int pds_vfio_dma_map_lm_file(struct device *dev,
				    enum dma_data_direction dir,
				    struct pds_vfio_lm_file *lm_file)
{
	struct pds_lm_sg_elem *sgl, *sge;
	struct scatterlist *sg;
	dma_addr_t sgl_addr;
	size_t sgl_size;
	int err;
	int i;

	if (!lm_file)
		return -EINVAL;

	/* dma map file pages */
	err = dma_map_sgtable(dev, &lm_file->sg_table, dir, 0);
	if (err)
		return err;

	lm_file->num_sge = lm_file->sg_table.nents;

	/* alloc sgl */
	sgl_size = lm_file->num_sge * sizeof(struct pds_lm_sg_elem);
	sgl = kzalloc(sgl_size, GFP_KERNEL);
	if (!sgl) {
		err = -ENOMEM;
		goto out_unmap_sgtable;
	}

	/* fill sgl */
	sge = sgl;
	for_each_sgtable_dma_sg(&lm_file->sg_table, sg, i) {
		sge->addr = cpu_to_le64(sg_dma_address(sg));
		sge->len = cpu_to_le32(sg_dma_len(sg));
		dev_dbg(dev, "addr = %llx, len = %u\n", sge->addr, sge->len);
		sge++;
	}

	sgl_addr = dma_map_single(dev, sgl, sgl_size, DMA_TO_DEVICE);
	if (dma_mapping_error(dev, sgl_addr)) {
		err = -EIO;
		goto out_free_sgl;
	}

	lm_file->sgl = sgl;
	lm_file->sgl_addr = sgl_addr;

	return 0;

out_free_sgl:
	kfree(sgl);
out_unmap_sgtable:
	lm_file->num_sge = 0;
	dma_unmap_sgtable(dev, &lm_file->sg_table, dir, 0);
	return err;
}

static void pds_vfio_dma_unmap_lm_file(struct device *dev,
				       enum dma_data_direction dir,
				       struct pds_vfio_lm_file *lm_file)
{
	if (!lm_file)
		return;

	/* free sgl */
	if (lm_file->sgl) {
		dma_unmap_single(dev, lm_file->sgl_addr,
				 lm_file->num_sge * sizeof(*lm_file->sgl),
				 DMA_TO_DEVICE);
		kfree(lm_file->sgl);
		lm_file->sgl = NULL;
		lm_file->sgl_addr = DMA_MAPPING_ERROR;
		lm_file->num_sge = 0;
	}

	/* dma unmap file pages */
	dma_unmap_sgtable(dev, &lm_file->sg_table, dir, 0);
}

int pds_vfio_get_lm_state_cmd(struct pds_vfio_pci_device *pds_vfio)
{
	union pds_core_adminq_cmd cmd = {
		.lm_save = {
			.opcode = PDS_LM_CMD_SAVE,
			.vf_id = cpu_to_le16(pds_vfio->vf_id),
		},
	};
	struct pci_dev *pdev = pds_vfio_to_pci_dev(pds_vfio);
	struct device *pdsc_dev = &pci_physfn(pdev)->dev;
	union pds_core_adminq_comp comp = {};
	struct pds_vfio_lm_file *lm_file;
	int err;

	dev_dbg(&pdev->dev, "vf%u: Get migration state\n", pds_vfio->vf_id);

	lm_file = pds_vfio->save_file;

	err = pds_vfio_dma_map_lm_file(pdsc_dev, DMA_FROM_DEVICE, lm_file);
	if (err) {
		dev_err(&pdev->dev, "failed to map save migration file: %pe\n",
			ERR_PTR(err));
		return err;
	}

	cmd.lm_save.sgl_addr = cpu_to_le64(lm_file->sgl_addr);
	cmd.lm_save.num_sge = cpu_to_le32(lm_file->num_sge);

	err = pds_vfio_client_adminq_cmd(pds_vfio, &cmd, &comp, false);
	if (err)
		dev_err(&pdev->dev, "failed to get migration state: %pe\n",
			ERR_PTR(err));

	pds_vfio_dma_unmap_lm_file(pdsc_dev, DMA_FROM_DEVICE, lm_file);

	return err;
}

int pds_vfio_set_lm_state_cmd(struct pds_vfio_pci_device *pds_vfio)
{
	union pds_core_adminq_cmd cmd = {
		.lm_restore = {
			.opcode = PDS_LM_CMD_RESTORE,
			.vf_id = cpu_to_le16(pds_vfio->vf_id),
		},
	};
	struct pci_dev *pdev = pds_vfio_to_pci_dev(pds_vfio);
	struct device *pdsc_dev = &pci_physfn(pdev)->dev;
	union pds_core_adminq_comp comp = {};
	struct pds_vfio_lm_file *lm_file;
	int err;

	dev_dbg(&pdev->dev, "vf%u: Set migration state\n", pds_vfio->vf_id);

	lm_file = pds_vfio->restore_file;

	err = pds_vfio_dma_map_lm_file(pdsc_dev, DMA_TO_DEVICE, lm_file);
	if (err) {
		dev_err(&pdev->dev,
			"failed to map restore migration file: %pe\n",
			ERR_PTR(err));
		return err;
	}

	cmd.lm_restore.sgl_addr = cpu_to_le64(lm_file->sgl_addr);
	cmd.lm_restore.num_sge = cpu_to_le32(lm_file->num_sge);

	err = pds_vfio_client_adminq_cmd(pds_vfio, &cmd, &comp, false);
	if (err)
		dev_err(&pdev->dev, "failed to set migration state: %pe\n",
			ERR_PTR(err));

	pds_vfio_dma_unmap_lm_file(pdsc_dev, DMA_TO_DEVICE, lm_file);

	return err;
}

void pds_vfio_send_host_vf_lm_status_cmd(struct pds_vfio_pci_device *pds_vfio,
					 enum pds_lm_host_vf_status vf_status)
{
	union pds_core_adminq_cmd cmd = {
		.lm_host_vf_status = {
			.opcode = PDS_LM_CMD_HOST_VF_STATUS,
			.vf_id = cpu_to_le16(pds_vfio->vf_id),
			.status = vf_status,
		},
	};
	struct device *dev = pds_vfio_to_dev(pds_vfio);
	union pds_core_adminq_comp comp = {};
	int err;

	dev_dbg(dev, "vf%u: Set host VF LM status: %u", pds_vfio->vf_id,
		vf_status);
	if (vf_status != PDS_LM_STA_IN_PROGRESS &&
	    vf_status != PDS_LM_STA_NONE) {
		dev_warn(dev, "Invalid host VF migration status, %d\n",
			 vf_status);
		return;
	}

	err = pds_vfio_client_adminq_cmd(pds_vfio, &cmd, &comp, false);
	if (err)
		dev_warn(dev, "failed to send host VF migration status: %pe\n",
			 ERR_PTR(err));
}

int pds_vfio_dirty_status_cmd(struct pds_vfio_pci_device *pds_vfio,
			      u64 regions_dma, u8 *max_regions, u8 *num_regions)
{
	union pds_core_adminq_cmd cmd = {
		.lm_dirty_status = {
			.opcode = PDS_LM_CMD_DIRTY_STATUS,
			.vf_id = cpu_to_le16(pds_vfio->vf_id),
		},
	};
	struct device *dev = pds_vfio_to_dev(pds_vfio);
	union pds_core_adminq_comp comp = {};
	int err;

	dev_dbg(dev, "vf%u: Dirty status\n", pds_vfio->vf_id);

	cmd.lm_dirty_status.regions_dma = cpu_to_le64(regions_dma);
	cmd.lm_dirty_status.max_regions = *max_regions;

	err = pds_vfio_client_adminq_cmd(pds_vfio, &cmd, &comp, false);
	if (err) {
		dev_err(dev, "failed to get dirty status: %pe\n", ERR_PTR(err));
		return err;
	}

	/* only support seq_ack approach for now */
	if (!(le32_to_cpu(comp.lm_dirty_status.bmp_type_mask) &
	      BIT(PDS_LM_DIRTY_BMP_TYPE_SEQ_ACK))) {
		dev_err(dev, "Dirty bitmap tracking SEQ_ACK not supported\n");
		return -EOPNOTSUPP;
	}

	*num_regions = comp.lm_dirty_status.num_regions;
	*max_regions = comp.lm_dirty_status.max_regions;

	dev_dbg(dev,
		"Page Tracking Status command successful, max_regions: %d, num_regions: %d, bmp_type: %s\n",
		*max_regions, *num_regions, "PDS_LM_DIRTY_BMP_TYPE_SEQ_ACK");

	return 0;
}

int pds_vfio_dirty_enable_cmd(struct pds_vfio_pci_device *pds_vfio,
			      u64 regions_dma, u8 num_regions)
{
	union pds_core_adminq_cmd cmd = {
		.lm_dirty_enable = {
			.opcode = PDS_LM_CMD_DIRTY_ENABLE,
			.vf_id = cpu_to_le16(pds_vfio->vf_id),
			.regions_dma = cpu_to_le64(regions_dma),
			.bmp_type = PDS_LM_DIRTY_BMP_TYPE_SEQ_ACK,
			.num_regions = num_regions,
		},
	};
	struct device *dev = pds_vfio_to_dev(pds_vfio);
	union pds_core_adminq_comp comp = {};
	int err;

	err = pds_vfio_client_adminq_cmd(pds_vfio, &cmd, &comp, false);
	if (err) {
		dev_err(dev, "failed dirty tracking enable: %pe\n",
			ERR_PTR(err));
		return err;
	}

	return 0;
}

int pds_vfio_dirty_disable_cmd(struct pds_vfio_pci_device *pds_vfio)
{
	union pds_core_adminq_cmd cmd = {
		.lm_dirty_disable = {
			.opcode = PDS_LM_CMD_DIRTY_DISABLE,
			.vf_id = cpu_to_le16(pds_vfio->vf_id),
		},
	};
	struct device *dev = pds_vfio_to_dev(pds_vfio);
	union pds_core_adminq_comp comp = {};
	int err;

	err = pds_vfio_client_adminq_cmd(pds_vfio, &cmd, &comp, false);
	if (err || comp.lm_dirty_status.num_regions != 0) {
		/* in case num_regions is still non-zero after disable */
		err = err ? err : -EIO;
		dev_err(dev,
			"failed dirty tracking disable: %pe, num_regions %d\n",
			ERR_PTR(err), comp.lm_dirty_status.num_regions);
		return err;
	}

	return 0;
}

int pds_vfio_dirty_seq_ack_cmd(struct pds_vfio_pci_device *pds_vfio,
			       u64 sgl_dma, u16 num_sge, u32 offset,
			       u32 total_len, bool read_seq)
{
	const char *cmd_type_str = read_seq ? "read_seq" : "write_ack";
	union pds_core_adminq_cmd cmd = {
		.lm_dirty_seq_ack = {
			.vf_id = cpu_to_le16(pds_vfio->vf_id),
			.len_bytes = cpu_to_le32(total_len),
			.off_bytes = cpu_to_le32(offset),
			.sgl_addr = cpu_to_le64(sgl_dma),
			.num_sge = cpu_to_le16(num_sge),
		},
	};
	struct device *dev = pds_vfio_to_dev(pds_vfio);
	union pds_core_adminq_comp comp = {};
	int err;

	if (read_seq)
		cmd.lm_dirty_seq_ack.opcode = PDS_LM_CMD_DIRTY_READ_SEQ;
	else
		cmd.lm_dirty_seq_ack.opcode = PDS_LM_CMD_DIRTY_WRITE_ACK;

	err = pds_vfio_client_adminq_cmd(pds_vfio, &cmd, &comp, false);
	if (err) {
		dev_err(dev, "failed cmd Page Tracking %s: %pe\n", cmd_type_str,
			ERR_PTR(err));
		return err;
	}

	return 0;
}
