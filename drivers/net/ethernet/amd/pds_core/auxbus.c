// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc */

#include <linux/pci.h>

#include "core.h"
#include <linux/pds/pds_auxbus.h>

static void pdsc_auxbus_dev_release(struct device *dev)
{
	struct pds_auxiliary_dev *padev =
		container_of(dev, struct pds_auxiliary_dev, aux_dev.dev);

	kfree(padev);
}

static struct pds_auxiliary_dev *pdsc_auxbus_dev_register(struct pdsc *cf,
							  struct pdsc *pf,
							  char *name)
{
	struct auxiliary_device *aux_dev;
	struct pds_auxiliary_dev *padev;
	int err;

	padev = kzalloc(sizeof(*padev), GFP_KERNEL);
	if (!padev)
		return ERR_PTR(-ENOMEM);

	padev->vf_pdev = cf->pdev;

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
		auxiliary_device_delete(&padev->aux_dev);
		auxiliary_device_uninit(&padev->aux_dev);
	}
	pf->vfs[cf->vf_id].padev = NULL;

	mutex_unlock(&pf->config_lock);
	return err;
}

int pdsc_auxbus_dev_add(struct pdsc *cf, struct pdsc *pf)
{
	struct pds_auxiliary_dev *padev;
	enum pds_core_vif_types vt;
	u16 vt_support;
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

	padev = pdsc_auxbus_dev_register(cf, pf,
					 pf->viftype_status[vt].name);
	if (IS_ERR(padev)) {
		err = PTR_ERR(padev);
		goto out_unlock;
	}
	pf->vfs[cf->vf_id].padev = padev;

out_unlock:
	mutex_unlock(&pf->config_lock);
	return err;
}
