// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) Advanced Micro Devices, Inc */

#include <linux/module.h>
#include <linux/auxiliary_bus.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/bitfield.h>
#include <linux/string.h>

#include <uapi/fwctl/fwctl.h>
#include <uapi/fwctl/pds.h>
#include <linux/fwctl.h>

#include <linux/pds/pds_common.h>
#include <linux/pds/pds_core_if.h>
#include <linux/pds/pds_adminq.h>
#include <linux/pds/pds_auxbus.h>

struct pdsfc_uctx {
	struct fwctl_uctx uctx;
	u32 uctx_caps;
};

struct pdsfc_rpc_endpoint_info {
	u32 endpoint;
	dma_addr_t operations_pa;
	struct pds_fwctl_query_data *operations;
	struct mutex lock;	/* lock for endpoint info management */
};

struct pdsfc_dev {
	struct fwctl_device fwctl;
	struct pds_auxiliary_dev *padev;
	u32 caps;
	struct pds_fwctl_ident ident;
	dma_addr_t endpoints_pa;
	struct pds_fwctl_query_data *endpoints;
	struct pdsfc_rpc_endpoint_info *endpoint_info;
};

static int pdsfc_open_uctx(struct fwctl_uctx *uctx)
{
	struct pdsfc_dev *pdsfc = container_of(uctx->fwctl, struct pdsfc_dev, fwctl);
	struct pdsfc_uctx *pdsfc_uctx = container_of(uctx, struct pdsfc_uctx, uctx);

	pdsfc_uctx->uctx_caps = pdsfc->caps;

	return 0;
}

static void pdsfc_close_uctx(struct fwctl_uctx *uctx)
{
}

static void *pdsfc_info(struct fwctl_uctx *uctx, size_t *length)
{
	struct pdsfc_uctx *pdsfc_uctx = container_of(uctx, struct pdsfc_uctx, uctx);
	struct fwctl_info_pds *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return ERR_PTR(-ENOMEM);

	info->uctx_caps = pdsfc_uctx->uctx_caps;

	return info;
}

static int pdsfc_identify(struct pdsfc_dev *pdsfc)
{
	struct device *dev = &pdsfc->fwctl.dev;
	union pds_core_adminq_comp comp = {0};
	union pds_core_adminq_cmd cmd;
	struct pds_fwctl_ident *ident;
	dma_addr_t ident_pa;
	int err;

	ident = dma_alloc_coherent(dev->parent, sizeof(*ident), &ident_pa, GFP_KERNEL);
	if (!ident) {
		dev_err(dev, "Failed to map ident buffer\n");
		return -ENOMEM;
	}

	cmd = (union pds_core_adminq_cmd) {
		.fwctl_ident = {
			.opcode = PDS_FWCTL_CMD_IDENT,
			.version = 0,
			.len = cpu_to_le32(sizeof(*ident)),
			.ident_pa = cpu_to_le64(ident_pa),
		}
	};

	err = pds_client_adminq_cmd(pdsfc->padev, &cmd, sizeof(cmd), &comp, 0);
	if (err)
		dev_err(dev, "Failed to send adminq cmd opcode: %u err: %d\n",
			cmd.fwctl_ident.opcode, err);
	else
		pdsfc->ident = *ident;

	dma_free_coherent(dev->parent, sizeof(*ident), ident, ident_pa);

	return err;
}

static void pdsfc_free_endpoints(struct pdsfc_dev *pdsfc)
{
	struct device *dev = &pdsfc->fwctl.dev;
	u32 num_endpoints;
	int i;

	if (!pdsfc->endpoints)
		return;

	num_endpoints = le32_to_cpu(pdsfc->endpoints->num_entries);
	for (i = 0; pdsfc->endpoint_info && i < num_endpoints; i++)
		mutex_destroy(&pdsfc->endpoint_info[i].lock);
	vfree(pdsfc->endpoint_info);
	pdsfc->endpoint_info = NULL;
	dma_free_coherent(dev->parent, PAGE_SIZE,
			  pdsfc->endpoints, pdsfc->endpoints_pa);
	pdsfc->endpoints = NULL;
	pdsfc->endpoints_pa = DMA_MAPPING_ERROR;
}

static void pdsfc_free_operations(struct pdsfc_dev *pdsfc)
{
	struct device *dev = &pdsfc->fwctl.dev;
	u32 num_endpoints;
	int i;

	num_endpoints = le32_to_cpu(pdsfc->endpoints->num_entries);
	for (i = 0; i < num_endpoints; i++) {
		struct pdsfc_rpc_endpoint_info *ei = &pdsfc->endpoint_info[i];

		if (ei->operations) {
			dma_free_coherent(dev->parent, PAGE_SIZE,
					  ei->operations, ei->operations_pa);
			ei->operations = NULL;
			ei->operations_pa = DMA_MAPPING_ERROR;
		}
	}
}

static struct pds_fwctl_query_data *pdsfc_get_endpoints(struct pdsfc_dev *pdsfc,
							dma_addr_t *pa)
{
	struct device *dev = &pdsfc->fwctl.dev;
	union pds_core_adminq_comp comp = {0};
	struct pds_fwctl_query_data *data;
	union pds_core_adminq_cmd cmd;
	dma_addr_t data_pa;
	int err;

	data = dma_alloc_coherent(dev->parent, PAGE_SIZE, &data_pa, GFP_KERNEL);
	if (!data) {
		dev_err(dev, "Failed to map endpoint list\n");
		return ERR_PTR(-ENOMEM);
	}

	cmd = (union pds_core_adminq_cmd) {
		.fwctl_query = {
			.opcode = PDS_FWCTL_CMD_QUERY,
			.entity = PDS_FWCTL_RPC_ROOT,
			.version = 0,
			.query_data_buf_len = cpu_to_le32(PAGE_SIZE),
			.query_data_buf_pa = cpu_to_le64(data_pa),
		}
	};

	err = pds_client_adminq_cmd(pdsfc->padev, &cmd, sizeof(cmd), &comp, 0);
	if (err) {
		dev_err(dev, "Failed to send adminq cmd opcode: %u entity: %u err: %d\n",
			cmd.fwctl_query.opcode, cmd.fwctl_query.entity, err);
		dma_free_coherent(dev->parent, PAGE_SIZE, data, data_pa);
		return ERR_PTR(err);
	}

	*pa = data_pa;

	return data;
}

static int pdsfc_init_endpoints(struct pdsfc_dev *pdsfc)
{
	struct pds_fwctl_query_data_endpoint *ep_entry;
	u32 num_endpoints;
	int i;

	pdsfc->endpoints = pdsfc_get_endpoints(pdsfc, &pdsfc->endpoints_pa);
	if (IS_ERR(pdsfc->endpoints))
		return PTR_ERR(pdsfc->endpoints);

	num_endpoints = le32_to_cpu(pdsfc->endpoints->num_entries);
	pdsfc->endpoint_info = vcalloc(num_endpoints,
				       sizeof(*pdsfc->endpoint_info));
	if (!pdsfc->endpoint_info) {
		pdsfc_free_endpoints(pdsfc);
		return -ENOMEM;
	}

	ep_entry = (struct pds_fwctl_query_data_endpoint *)pdsfc->endpoints->entries;
	for (i = 0; i < num_endpoints; i++) {
		mutex_init(&pdsfc->endpoint_info[i].lock);
		pdsfc->endpoint_info[i].endpoint = le32_to_cpu(ep_entry[i].id);
	}

	return 0;
}

static struct pds_fwctl_query_data *pdsfc_get_operations(struct pdsfc_dev *pdsfc,
							 dma_addr_t *pa, u32 ep)
{
	struct pds_fwctl_query_data_operation *entries;
	struct device *dev = &pdsfc->fwctl.dev;
	union pds_core_adminq_comp comp = {0};
	struct pds_fwctl_query_data *data;
	union pds_core_adminq_cmd cmd;
	dma_addr_t data_pa;
	u32 num_entries;
	int err;
	int i;

	/* Query the operations list for the given endpoint */
	data = dma_alloc_coherent(dev->parent, PAGE_SIZE, &data_pa, GFP_KERNEL);
	if (!data) {
		dev_err(dev, "Failed to map operations list\n");
		return ERR_PTR(-ENOMEM);
	}

	cmd = (union pds_core_adminq_cmd) {
		.fwctl_query = {
			.opcode = PDS_FWCTL_CMD_QUERY,
			.entity = PDS_FWCTL_RPC_ENDPOINT,
			.version = 0,
			.query_data_buf_len = cpu_to_le32(PAGE_SIZE),
			.query_data_buf_pa = cpu_to_le64(data_pa),
			.ep = cpu_to_le32(ep),
		}
	};

	err = pds_client_adminq_cmd(pdsfc->padev, &cmd, sizeof(cmd), &comp, 0);
	if (err) {
		dev_err(dev, "Failed to send adminq cmd opcode: %u entity: %u err: %d\n",
			cmd.fwctl_query.opcode, cmd.fwctl_query.entity, err);
		dma_free_coherent(dev->parent, PAGE_SIZE, data, data_pa);
		return ERR_PTR(err);
	}

	*pa = data_pa;

	entries = (struct pds_fwctl_query_data_operation *)data->entries;
	num_entries = le32_to_cpu(data->num_entries);
	dev_dbg(dev, "num_entries %d\n", num_entries);
	for (i = 0; i < num_entries; i++) {

		/* Translate FW command attribute to fwctl scope */
		switch (entries[i].scope) {
		case PDSFC_FW_CMD_ATTR_READ:
		case PDSFC_FW_CMD_ATTR_WRITE:
		case PDSFC_FW_CMD_ATTR_SYNC:
			entries[i].scope = FWCTL_RPC_CONFIGURATION;
			break;
		case PDSFC_FW_CMD_ATTR_DEBUG_READ:
			entries[i].scope = FWCTL_RPC_DEBUG_READ_ONLY;
			break;
		case PDSFC_FW_CMD_ATTR_DEBUG_WRITE:
			entries[i].scope = FWCTL_RPC_DEBUG_WRITE;
			break;
		default:
			entries[i].scope = FWCTL_RPC_DEBUG_WRITE_FULL;
			break;
		}
		dev_dbg(dev, "endpoint %d operation: id %x scope %d\n",
			ep, le32_to_cpu(entries[i].id), entries[i].scope);
	}

	return data;
}

static int pdsfc_validate_rpc(struct pdsfc_dev *pdsfc,
			      struct fwctl_rpc_pds *rpc,
			      enum fwctl_rpc_scope scope)
{
	struct pds_fwctl_query_data_operation *op_entry;
	struct pdsfc_rpc_endpoint_info *ep_info = NULL;
	struct device *dev = &pdsfc->fwctl.dev;
	u32 num_entries;
	int i;

	/* validate rpc in_len & out_len based
	 * on ident.max_req_sz & max_resp_sz
	 */
	if (rpc->in.len > le32_to_cpu(pdsfc->ident.max_req_sz)) {
		dev_dbg(dev, "Invalid request size %u, max %u\n",
			rpc->in.len, le32_to_cpu(pdsfc->ident.max_req_sz));
		return -EINVAL;
	}

	if (rpc->out.len > le32_to_cpu(pdsfc->ident.max_resp_sz)) {
		dev_dbg(dev, "Invalid response size %u, max %u\n",
			rpc->out.len, le32_to_cpu(pdsfc->ident.max_resp_sz));
		return -EINVAL;
	}

	num_entries = le32_to_cpu(pdsfc->endpoints->num_entries);
	for (i = 0; i < num_entries; i++) {
		if (pdsfc->endpoint_info[i].endpoint == rpc->in.ep) {
			ep_info = &pdsfc->endpoint_info[i];
			break;
		}
	}
	if (!ep_info) {
		dev_dbg(dev, "Invalid endpoint %d\n", rpc->in.ep);
		return -EINVAL;
	}

	/* query and cache this endpoint's operations */
	mutex_lock(&ep_info->lock);
	if (!ep_info->operations) {
		struct pds_fwctl_query_data *operations;

		operations = pdsfc_get_operations(pdsfc,
						  &ep_info->operations_pa,
						  rpc->in.ep);
		if (IS_ERR(operations)) {
			mutex_unlock(&ep_info->lock);
			return -ENOMEM;
		}
		ep_info->operations = operations;
	}
	mutex_unlock(&ep_info->lock);

	/* reject unsupported and/or out of scope commands */
	op_entry = (struct pds_fwctl_query_data_operation *)ep_info->operations->entries;
	num_entries = le32_to_cpu(ep_info->operations->num_entries);
	for (i = 0; i < num_entries; i++) {
		if (PDS_FWCTL_RPC_OPCODE_CMP(rpc->in.op, le32_to_cpu(op_entry[i].id))) {
			if (scope < op_entry[i].scope)
				return -EPERM;
			return 0;
		}
	}

	dev_dbg(dev, "Invalid operation %d for endpoint %d\n", rpc->in.op, rpc->in.ep);

	return -EINVAL;
}

static void *pdsfc_fw_rpc(struct fwctl_uctx *uctx, enum fwctl_rpc_scope scope,
			  void *in, size_t in_len, size_t *out_len)
{
	struct pdsfc_dev *pdsfc = container_of(uctx->fwctl, struct pdsfc_dev, fwctl);
	struct device *dev = &uctx->fwctl->dev;
	union pds_core_adminq_comp comp = {0};
	dma_addr_t out_payload_dma_addr = 0;
	dma_addr_t in_payload_dma_addr = 0;
	struct fwctl_rpc_pds *rpc = in;
	union pds_core_adminq_cmd cmd;
	void *out_payload = NULL;
	void *in_payload = NULL;
	void *out = NULL;
	int err;

	err = pdsfc_validate_rpc(pdsfc, rpc, scope);
	if (err)
		return ERR_PTR(err);

	if (rpc->in.len > 0) {
		in_payload = memdup_user(u64_to_user_ptr(rpc->in.payload), rpc->in.len);
		if (IS_ERR(in_payload)) {
			dev_dbg(dev, "Failed to copy in_payload from user\n");
			return in_payload;
		}

		in_payload_dma_addr = dma_map_single(dev->parent, in_payload,
						     rpc->in.len, DMA_TO_DEVICE);
		err = dma_mapping_error(dev->parent, in_payload_dma_addr);
		if (err) {
			dev_dbg(dev, "Failed to map in_payload\n");
			goto err_in_payload;
		}
	}

	if (rpc->out.len > 0) {
		out_payload = kzalloc(rpc->out.len, GFP_KERNEL);
		if (!out_payload) {
			dev_dbg(dev, "Failed to allocate out_payload\n");
			err = -ENOMEM;
			goto err_out_payload;
		}

		out_payload_dma_addr = dma_map_single(dev->parent, out_payload,
						      rpc->out.len, DMA_FROM_DEVICE);
		err = dma_mapping_error(dev->parent, out_payload_dma_addr);
		if (err) {
			dev_dbg(dev, "Failed to map out_payload\n");
			goto err_out_payload;
		}
	}

	cmd = (union pds_core_adminq_cmd) {
		.fwctl_rpc = {
			.opcode = PDS_FWCTL_CMD_RPC,
			.flags = cpu_to_le16(PDS_FWCTL_RPC_IND_REQ | PDS_FWCTL_RPC_IND_RESP),
			.ep = cpu_to_le32(rpc->in.ep),
			.op = cpu_to_le32(rpc->in.op),
			.req_pa = cpu_to_le64(in_payload_dma_addr),
			.req_sz = cpu_to_le32(rpc->in.len),
			.resp_pa = cpu_to_le64(out_payload_dma_addr),
			.resp_sz = cpu_to_le32(rpc->out.len),
		}
	};

	err = pds_client_adminq_cmd(pdsfc->padev, &cmd, sizeof(cmd), &comp, 0);
	if (err) {
		dev_dbg(dev, "%s: ep %d op %x req_pa %llx req_sz %d req_sg %d resp_pa %llx resp_sz %d resp_sg %d err %d\n",
			__func__, rpc->in.ep, rpc->in.op,
			cmd.fwctl_rpc.req_pa, cmd.fwctl_rpc.req_sz, cmd.fwctl_rpc.req_sg_elems,
			cmd.fwctl_rpc.resp_pa, cmd.fwctl_rpc.resp_sz, cmd.fwctl_rpc.resp_sg_elems,
			err);
		goto done;
	}

	dynamic_hex_dump("out ", DUMP_PREFIX_OFFSET, 16, 1, out_payload, rpc->out.len, true);

	if (copy_to_user(u64_to_user_ptr(rpc->out.payload), out_payload, rpc->out.len)) {
		dev_dbg(dev, "Failed to copy out_payload to user\n");
		out = ERR_PTR(-EFAULT);
		goto done;
	}

	rpc->out.retval = le32_to_cpu(comp.fwctl_rpc.err);
	*out_len = in_len;
	out = in;

done:
	if (out_payload_dma_addr)
		dma_unmap_single(dev->parent, out_payload_dma_addr,
				 rpc->out.len, DMA_FROM_DEVICE);
err_out_payload:
	kfree(out_payload);

	if (in_payload_dma_addr)
		dma_unmap_single(dev->parent, in_payload_dma_addr,
				 rpc->in.len, DMA_TO_DEVICE);
err_in_payload:
	kfree(in_payload);
	if (err)
		return ERR_PTR(err);

	return out;
}

static const struct fwctl_ops pdsfc_ops = {
	.device_type = FWCTL_DEVICE_TYPE_PDS,
	.uctx_size = sizeof(struct pdsfc_uctx),
	.open_uctx = pdsfc_open_uctx,
	.close_uctx = pdsfc_close_uctx,
	.info = pdsfc_info,
	.fw_rpc = pdsfc_fw_rpc,
};

static int pdsfc_probe(struct auxiliary_device *adev,
		       const struct auxiliary_device_id *id)
{
	struct pds_auxiliary_dev *padev =
			container_of(adev, struct pds_auxiliary_dev, aux_dev);
	struct device *dev = &adev->dev;
	struct pdsfc_dev *pdsfc;
	int err;

	pdsfc = fwctl_alloc_device(&padev->vf_pdev->dev, &pdsfc_ops,
				   struct pdsfc_dev, fwctl);
	if (!pdsfc)
		return -ENOMEM;
	pdsfc->padev = padev;

	err = pdsfc_identify(pdsfc);
	if (err) {
		fwctl_put(&pdsfc->fwctl);
		return dev_err_probe(dev, err, "Failed to identify device\n");
	}

	err = pdsfc_init_endpoints(pdsfc);
	if (err) {
		fwctl_put(&pdsfc->fwctl);
		return dev_err_probe(dev, err, "Failed to init endpoints\n");
	}

	pdsfc->caps = PDS_FWCTL_QUERY_CAP | PDS_FWCTL_SEND_CAP;

	err = fwctl_register(&pdsfc->fwctl);
	if (err) {
		pdsfc_free_endpoints(pdsfc);
		fwctl_put(&pdsfc->fwctl);
		return dev_err_probe(dev, err, "Failed to register device\n");
	}

	auxiliary_set_drvdata(adev, pdsfc);

	return 0;
}

static void pdsfc_remove(struct auxiliary_device *adev)
{
	struct pdsfc_dev *pdsfc = auxiliary_get_drvdata(adev);

	fwctl_unregister(&pdsfc->fwctl);
	pdsfc_free_operations(pdsfc);
	pdsfc_free_endpoints(pdsfc);

	fwctl_put(&pdsfc->fwctl);
}

static const struct auxiliary_device_id pdsfc_id_table[] = {
	{.name = PDS_CORE_DRV_NAME "." PDS_DEV_TYPE_FWCTL_STR },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, pdsfc_id_table);

static struct auxiliary_driver pdsfc_driver = {
	.name = "pds_fwctl",
	.probe = pdsfc_probe,
	.remove = pdsfc_remove,
	.id_table = pdsfc_id_table,
};

module_auxiliary_driver(pdsfc_driver);

MODULE_IMPORT_NS("FWCTL");
MODULE_DESCRIPTION("pds fwctl driver");
MODULE_AUTHOR("Shannon Nelson <shannon.nelson@amd.com>");
MODULE_AUTHOR("Brett Creeley <brett.creeley@amd.com>");
MODULE_LICENSE("GPL");
