// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/pci.h>

#include "core.h"
#include <linux/pds/pds_auxbus.h>

/**
 * pds_client_register - Link the client to the firmware
 * @pf_pdev:	ptr to the PF driver struct
 * @devname:	name that includes service into, e.g. pds_core.vDPA
 *
 * Return: positive client ID (ci) on success, or
 *         negative for error
 */
int pds_client_register(struct pci_dev *pf_pdev, char *devname)
{
	union pds_core_adminq_comp comp = {};
	union pds_core_adminq_cmd cmd = {};
	struct pdsc *pf;
	int err;
	u16 ci;

	pf = pci_get_drvdata(pf_pdev);
	if (pf->state)
		return -ENXIO;

	cmd.client_reg.opcode = PDS_AQ_CMD_CLIENT_REG;
	strscpy(cmd.client_reg.devname, devname,
		sizeof(cmd.client_reg.devname));

	err = pdsc_adminq_post(pf, &cmd, &comp, false);
	if (err) {
		dev_info(pf->dev, "register dev_name %s with DSC failed, status %d: %pe\n",
			 devname, comp.status, ERR_PTR(err));
		return err;
	}

	ci = le16_to_cpu(comp.client_reg.client_id);
	if (!ci) {
		dev_err(pf->dev, "%s: device returned null client_id\n",
			__func__);
		return -EIO;
	}

	dev_dbg(pf->dev, "%s: device returned client_id %d for %s\n",
		__func__, ci, devname);

	return ci;
}
EXPORT_SYMBOL_GPL(pds_client_register);

/**
 * pds_client_unregister - Unlink the client from the firmware
 * @pf_pdev:	ptr to the PF driver struct
 * @client_id:	id returned from pds_client_register()
 *
 * Return: 0 on success, or
 *         negative for error
 */
int pds_client_unregister(struct pci_dev *pf_pdev, u16 client_id)
{
	union pds_core_adminq_comp comp = {};
	union pds_core_adminq_cmd cmd = {};
	struct pdsc *pf;
	int err;

	pf = pci_get_drvdata(pf_pdev);
	if (pf->state)
		return -ENXIO;

	cmd.client_unreg.opcode = PDS_AQ_CMD_CLIENT_UNREG;
	cmd.client_unreg.client_id = cpu_to_le16(client_id);

	err = pdsc_adminq_post(pf, &cmd, &comp, false);
	if (err)
		dev_info(pf->dev, "unregister client_id %d failed, status %d: %pe\n",
			 client_id, comp.status, ERR_PTR(err));

	return err;
}
EXPORT_SYMBOL_GPL(pds_client_unregister);

/**
 * pds_client_adminq_cmd - Process an adminq request for the client
 * @padev:   ptr to the client device
 * @req:     ptr to buffer with request
 * @req_len: length of actual struct used for request
 * @resp:    ptr to buffer where answer is to be copied
 * @flags:   optional flags from pds_core_adminq_flags
 *
 * Return: 0 on success, or
 *         negative for error
 *
 * Client sends pointers to request and response buffers
 * Core copies request data into pds_core_client_request_cmd
 * Core sets other fields as needed
 * Core posts to AdminQ
 * Core copies completion data into response buffer
 */
int pds_client_adminq_cmd(struct pds_auxiliary_dev *padev,
			  union pds_core_adminq_cmd *req,
			  size_t req_len,
			  union pds_core_adminq_comp *resp,
			  u64 flags)
{
	union pds_core_adminq_cmd cmd = {};
	struct pci_dev *pf_pdev;
	struct pdsc *pf;
	size_t cp_len;
	int err;

	pf_pdev = pci_physfn(padev->vf_pdev);
	pf = pci_get_drvdata(pf_pdev);

	dev_dbg(pf->dev, "%s: %s opcode %d\n",
		__func__, dev_name(&padev->aux_dev.dev), req->opcode);

	if (pf->state)
		return -ENXIO;

	/* Wrap the client's request */
	cmd.client_request.opcode = PDS_AQ_CMD_CLIENT_CMD;
	cmd.client_request.client_id = cpu_to_le16(padev->client_id);
	cp_len = min_t(size_t, req_len, sizeof(cmd.client_request.client_cmd));
	memcpy(cmd.client_request.client_cmd, req, cp_len);

	err = pdsc_adminq_post(pf, &cmd, resp,
			       !!(flags & PDS_AQ_FLAG_FASTPOLL));
	if (err && err != -EAGAIN)
		dev_info(pf->dev, "client admin cmd failed: %pe\n",
			 ERR_PTR(err));

	return err;
}
EXPORT_SYMBOL_GPL(pds_client_adminq_cmd);

static void pdsc_auxbus_dev_release(struct device *dev)
{
	struct pds_auxiliary_dev *padev =
		container_of(dev, struct pds_auxiliary_dev, aux_dev.dev);

	kfree(padev);
}

static struct pds_auxiliary_dev *pdsc_auxbus_dev_register(struct pdsc *cf,
							  struct pdsc *pf,
							  u16 client_id,
							  char *name)
{
	struct auxiliary_device *aux_dev;
	struct pds_auxiliary_dev *padev;
	int err;

	padev = kzalloc(sizeof(*padev), GFP_KERNEL);
	if (!padev)
		return ERR_PTR(-ENOMEM);

	padev->vf_pdev = cf->pdev;
	padev->client_id = client_id;

	aux_dev = &padev->aux_dev;
	aux_dev->name = name;
	aux_dev->id = cf->uid;
	aux_dev->dev.parent = cf->dev;
	aux_dev->dev.release = pdsc_auxbus_dev_release;

	err = auxiliary_device_init(aux_dev);
	if (err < 0) {
		dev_warn(cf->dev, "auxiliary_device_init of %s failed: %pe\n",
			 name, ERR_PTR(err));
		goto err_out;
	}

	err = auxiliary_device_add(aux_dev);
	if (err) {
		dev_warn(cf->dev, "auxiliary_device_add of %s failed: %pe\n",
			 name, ERR_PTR(err));
		goto err_out_uninit;
	}

	return padev;

err_out_uninit:
	auxiliary_device_uninit(aux_dev);
err_out:
	kfree(padev);
	return ERR_PTR(err);
}

int pdsc_auxbus_dev_del(struct pdsc *cf, struct pdsc *pf)
{
	struct pds_auxiliary_dev *padev;
	int err = 0;

	mutex_lock(&pf->config_lock);

	padev = pf->vfs[cf->vf_id].padev;
	if (padev) {
		pds_client_unregister(pf->pdev, padev->client_id);
		auxiliary_device_delete(&padev->aux_dev);
		auxiliary_device_uninit(&padev->aux_dev);
		padev->client_id = 0;
	}
	pf->vfs[cf->vf_id].padev = NULL;

	mutex_unlock(&pf->config_lock);
	return err;
}

int pdsc_auxbus_dev_add(struct pdsc *cf, struct pdsc *pf)
{
	struct pds_auxiliary_dev *padev;
	enum pds_core_vif_types vt;
	char devname[PDS_DEVNAME_LEN];
	u16 vt_support;
	int client_id;
	int err = 0;

	mutex_lock(&pf->config_lock);

	/* We only support vDPA so far, so it is the only one to
	 * be verified that it is available in the Core device and
	 * enabled in the devlink param.  In the future this might
	 * become a loop for several VIF types.
	 */

	/* Verify that the type is supported and enabled.  It is not
	 * an error if there is no auxbus device support for this
	 * VF, it just means something else needs to happen with it.
	 */
	vt = PDS_DEV_TYPE_VDPA;
	vt_support = !!le16_to_cpu(pf->dev_ident.vif_types[vt]);
	if (!(vt_support &&
	      pf->viftype_status[vt].supported &&
	      pf->viftype_status[vt].enabled))
		goto out_unlock;

	/* Need to register with FW and get the client_id before
	 * creating the aux device so that the aux client can run
	 * adminq commands as part its probe
	 */
	snprintf(devname, sizeof(devname), "%s.%s.%d",
		 PDS_CORE_DRV_NAME, pf->viftype_status[vt].name, cf->uid);
	client_id = pds_client_register(pf->pdev, devname);
	if (client_id < 0) {
		err = client_id;
		goto out_unlock;
	}

	padev = pdsc_auxbus_dev_register(cf, pf, client_id,
					 pf->viftype_status[vt].name);
	if (IS_ERR(padev)) {
		pds_client_unregister(pf->pdev, client_id);
		err = PTR_ERR(padev);
		goto out_unlock;
	}
	pf->vfs[cf->vf_id].padev = padev;

out_unlock:
	mutex_unlock(&pf->config_lock);
	return err;
}
