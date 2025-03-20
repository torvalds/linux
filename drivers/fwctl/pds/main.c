// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) Advanced Micro Devices, Inc */

#include <linux/module.h>
#include <linux/auxiliary_bus.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>

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

struct pdsfc_dev {
	struct fwctl_device fwctl;
	struct pds_auxiliary_dev *padev;
	u32 caps;
	struct pds_fwctl_ident ident;
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

static void *pdsfc_fw_rpc(struct fwctl_uctx *uctx, enum fwctl_rpc_scope scope,
			  void *in, size_t in_len, size_t *out_len)
{
	return NULL;
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
		return dev_err_probe(dev, -ENOMEM, "Failed to allocate fwctl device struct\n");
	pdsfc->padev = padev;

	err = pdsfc_identify(pdsfc);
	if (err) {
		fwctl_put(&pdsfc->fwctl);
		return dev_err_probe(dev, err, "Failed to identify device\n");
	}

	err = fwctl_register(&pdsfc->fwctl);
	if (err) {
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
