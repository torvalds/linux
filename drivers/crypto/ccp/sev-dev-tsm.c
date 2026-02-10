// SPDX-License-Identifier: GPL-2.0-only

// Interface to CCP/SEV-TIO for generic PCIe TDISP module

#include <linux/pci.h>
#include <linux/device.h>
#include <linux/tsm.h>
#include <linux/iommu.h>
#include <linux/pci-doe.h>
#include <linux/bitfield.h>
#include <linux/module.h>

#include <asm/sev-common.h>
#include <asm/sev.h>

#include "psp-dev.h"
#include "sev-dev.h"
#include "sev-dev-tio.h"

MODULE_IMPORT_NS("PCI_IDE");

#define dev_to_sp(dev)		((struct sp_device *)dev_get_drvdata(dev))
#define dev_to_psp(dev)		((struct psp_device *)(dev_to_sp(dev)->psp_data))
#define dev_to_sev(dev)		((struct sev_device *)(dev_to_psp(dev)->sev_data))
#define tsm_dev_to_sev(tsmdev)	dev_to_sev((tsmdev)->dev.parent)

#define pdev_to_tio_dsm(pdev)	(container_of((pdev)->tsm, struct tio_dsm, tsm.base_tsm))

static int sev_tio_spdm_cmd(struct tio_dsm *dsm, int ret)
{
	struct tsm_dsm_tio *dev_data = &dsm->data;
	struct tsm_spdm *spdm = &dev_data->spdm;

	/* Check the main command handler response before entering the loop */
	if (ret == 0 && dev_data->psp_ret != SEV_RET_SUCCESS)
		return -EINVAL;

	if (ret <= 0)
		return ret;

	/* ret > 0 means "SPDM requested" */
	while (ret == PCI_DOE_FEATURE_CMA || ret == PCI_DOE_FEATURE_SSESSION) {
		ret = pci_doe(dsm->tsm.doe_mb, PCI_VENDOR_ID_PCI_SIG, ret,
			      spdm->req, spdm->req_len, spdm->rsp, spdm->rsp_len);
		if (ret < 0)
			break;

		WARN_ON_ONCE(ret == 0); /* The response should never be empty */
		spdm->rsp_len = ret;
		ret = sev_tio_continue(dev_data);
	}

	return ret;
}

static int stream_enable(struct pci_ide *ide)
{
	struct pci_dev *rp = pcie_find_root_port(ide->pdev);
	int ret;

	ret = pci_ide_stream_enable(rp, ide);
	if (ret)
		return ret;

	ret = pci_ide_stream_enable(ide->pdev, ide);
	if (ret)
		pci_ide_stream_disable(rp, ide);

	return ret;
}

static int streams_enable(struct pci_ide **ide)
{
	int ret = 0;

	for (int i = 0; i < TIO_IDE_MAX_TC; ++i) {
		if (ide[i]) {
			ret = stream_enable(ide[i]);
			if (ret)
				break;
		}
	}

	return ret;
}

static void stream_disable(struct pci_ide *ide)
{
	pci_ide_stream_disable(ide->pdev, ide);
	pci_ide_stream_disable(pcie_find_root_port(ide->pdev), ide);
}

static void streams_disable(struct pci_ide **ide)
{
	for (int i = 0; i < TIO_IDE_MAX_TC; ++i)
		if (ide[i])
			stream_disable(ide[i]);
}

static void stream_setup(struct pci_ide *ide)
{
	struct pci_dev *rp = pcie_find_root_port(ide->pdev);

	ide->partner[PCI_IDE_EP].rid_start = 0;
	ide->partner[PCI_IDE_EP].rid_end = 0xffff;
	ide->partner[PCI_IDE_RP].rid_start = 0;
	ide->partner[PCI_IDE_RP].rid_end = 0xffff;

	ide->pdev->ide_cfg = 0;
	ide->pdev->ide_tee_limit = 1;
	rp->ide_cfg = 1;
	rp->ide_tee_limit = 0;

	pci_warn(ide->pdev, "Forcing CFG/TEE for %s", pci_name(rp));
	pci_ide_stream_setup(ide->pdev, ide);
	pci_ide_stream_setup(rp, ide);
}

static u8 streams_setup(struct pci_ide **ide, u8 *ids)
{
	bool def = false;
	u8 tc_mask = 0;
	int i;

	for (i = 0; i < TIO_IDE_MAX_TC; ++i) {
		if (!ide[i]) {
			ids[i] = 0xFF;
			continue;
		}

		tc_mask |= BIT(i);
		ids[i] = ide[i]->stream_id;

		if (!def) {
			struct pci_ide_partner *settings;

			settings = pci_ide_to_settings(ide[i]->pdev, ide[i]);
			settings->default_stream = 1;
			def = true;
		}

		stream_setup(ide[i]);
	}

	return tc_mask;
}

static int streams_register(struct pci_ide **ide)
{
	int ret = 0, i;

	for (i = 0; i < TIO_IDE_MAX_TC; ++i) {
		if (ide[i]) {
			ret = pci_ide_stream_register(ide[i]);
			if (ret)
				break;
		}
	}

	return ret;
}

static void streams_unregister(struct pci_ide **ide)
{
	for (int i = 0; i < TIO_IDE_MAX_TC; ++i)
		if (ide[i])
			pci_ide_stream_unregister(ide[i]);
}

static void stream_teardown(struct pci_ide *ide)
{
	pci_ide_stream_teardown(ide->pdev, ide);
	pci_ide_stream_teardown(pcie_find_root_port(ide->pdev), ide);
}

static void streams_teardown(struct pci_ide **ide)
{
	for (int i = 0; i < TIO_IDE_MAX_TC; ++i) {
		if (ide[i]) {
			stream_teardown(ide[i]);
			pci_ide_stream_free(ide[i]);
			ide[i] = NULL;
		}
	}
}

static int stream_alloc(struct pci_dev *pdev, struct pci_ide **ide,
			unsigned int tc)
{
	struct pci_ide *ide1;

	if (ide[tc]) {
		pci_err(pdev, "Stream for class=%d already registered", tc);
		return -EBUSY;
	}

	ide1 = pci_ide_stream_alloc(pdev);
	if (!ide1)
		return -EFAULT;

	ide1->stream_id = ide1->host_bridge_stream;

	ide[tc] = ide1;

	return 0;
}

static struct pci_tsm *tio_pf0_probe(struct pci_dev *pdev, struct sev_device *sev)
{
	struct tio_dsm *dsm __free(kfree) = kzalloc(sizeof(*dsm), GFP_KERNEL);
	int rc;

	if (!dsm)
		return NULL;

	rc = pci_tsm_pf0_constructor(pdev, &dsm->tsm, sev->tsmdev);
	if (rc)
		return NULL;

	pci_dbg(pdev, "TSM enabled\n");
	dsm->sev = sev;
	return &no_free_ptr(dsm)->tsm.base_tsm;
}

static struct pci_tsm *dsm_probe(struct tsm_dev *tsmdev, struct pci_dev *pdev)
{
	struct sev_device *sev = tsm_dev_to_sev(tsmdev);

	if (is_pci_tsm_pf0(pdev))
		return tio_pf0_probe(pdev, sev);
	return NULL;
}

static void dsm_remove(struct pci_tsm *tsm)
{
	struct pci_dev *pdev = tsm->pdev;

	pci_dbg(pdev, "TSM disabled\n");

	if (is_pci_tsm_pf0(pdev)) {
		struct tio_dsm *dsm = container_of(tsm, struct tio_dsm, tsm.base_tsm);

		pci_tsm_pf0_destructor(&dsm->tsm);
		kfree(dsm);
	}
}

static int dsm_create(struct tio_dsm *dsm)
{
	struct pci_dev *pdev = dsm->tsm.base_tsm.pdev;
	u8 segment_id = pdev->bus ? pci_domain_nr(pdev->bus) : 0;
	struct pci_dev *rootport = pcie_find_root_port(pdev);
	u16 device_id = pci_dev_id(pdev);
	u16 root_port_id;
	u32 lnkcap = 0;

	if (pci_read_config_dword(rootport, pci_pcie_cap(rootport) + PCI_EXP_LNKCAP,
				  &lnkcap))
		return -ENODEV;

	root_port_id = FIELD_GET(PCI_EXP_LNKCAP_PN, lnkcap);

	return sev_tio_dev_create(&dsm->data, device_id, root_port_id, segment_id);
}

static int dsm_connect(struct pci_dev *pdev)
{
	struct tio_dsm *dsm = pdev_to_tio_dsm(pdev);
	struct tsm_dsm_tio *dev_data = &dsm->data;
	u8 ids[TIO_IDE_MAX_TC];
	u8 tc_mask;
	int ret;

	if (pci_find_doe_mailbox(pdev, PCI_VENDOR_ID_PCI_SIG,
				 PCI_DOE_FEATURE_SSESSION) != dsm->tsm.doe_mb) {
		pci_err(pdev, "CMA DOE MB must support SSESSION\n");
		return -EFAULT;
	}

	ret = stream_alloc(pdev, dev_data->ide, 0);
	if (ret)
		return ret;

	ret = dsm_create(dsm);
	if (ret)
		goto ide_free_exit;

	tc_mask = streams_setup(dev_data->ide, ids);

	ret = sev_tio_dev_connect(dev_data, tc_mask, ids, dev_data->cert_slot);
	ret = sev_tio_spdm_cmd(dsm, ret);
	if (ret)
		goto free_exit;

	streams_enable(dev_data->ide);

	ret = streams_register(dev_data->ide);
	if (ret)
		goto free_exit;

	return 0;

free_exit:
	sev_tio_dev_reclaim(dev_data);

	streams_disable(dev_data->ide);
ide_free_exit:

	streams_teardown(dev_data->ide);

	return ret;
}

static void dsm_disconnect(struct pci_dev *pdev)
{
	bool force = SYSTEM_HALT <= system_state && system_state <= SYSTEM_RESTART;
	struct tio_dsm *dsm = pdev_to_tio_dsm(pdev);
	struct tsm_dsm_tio *dev_data = &dsm->data;
	int ret;

	ret = sev_tio_dev_disconnect(dev_data, force);
	ret = sev_tio_spdm_cmd(dsm, ret);
	if (ret && !force) {
		ret = sev_tio_dev_disconnect(dev_data, true);
		sev_tio_spdm_cmd(dsm, ret);
	}

	sev_tio_dev_reclaim(dev_data);

	streams_disable(dev_data->ide);
	streams_unregister(dev_data->ide);
	streams_teardown(dev_data->ide);
}

static struct pci_tsm_ops sev_tsm_ops = {
	.probe = dsm_probe,
	.remove = dsm_remove,
	.connect = dsm_connect,
	.disconnect = dsm_disconnect,
};

void sev_tsm_init_locked(struct sev_device *sev, void *tio_status_page)
{
	struct sev_tio_status *t = kzalloc(sizeof(*t), GFP_KERNEL);
	struct tsm_dev *tsmdev;
	int ret;

	WARN_ON(sev->tio_status);

	if (!t)
		return;

	ret = sev_tio_init_locked(tio_status_page);
	if (ret) {
		pr_warn("SEV-TIO STATUS failed with %d\n", ret);
		goto error_exit;
	}

	tsmdev = tsm_register(sev->dev, &sev_tsm_ops);
	if (IS_ERR(tsmdev))
		goto error_exit;

	memcpy(t, tio_status_page, sizeof(*t));

	pr_notice("SEV-TIO status: EN=%d INIT_DONE=%d rq=%d..%d rs=%d..%d "
		  "scr=%d..%d out=%d..%d dev=%d tdi=%d algos=%x\n",
		  t->tio_en, t->tio_init_done,
		  t->spdm_req_size_min, t->spdm_req_size_max,
		  t->spdm_rsp_size_min, t->spdm_rsp_size_max,
		  t->spdm_scratch_size_min, t->spdm_scratch_size_max,
		  t->spdm_out_size_min, t->spdm_out_size_max,
		  t->devctx_size, t->tdictx_size,
		  t->tio_crypto_alg);

	sev->tsmdev = tsmdev;
	sev->tio_status = t;

	return;

error_exit:
	kfree(t);
	pr_err("Failed to enable SEV-TIO: ret=%d en=%d initdone=%d SEV=%d\n",
	       ret, t->tio_en, t->tio_init_done, boot_cpu_has(X86_FEATURE_SEV));
}

void sev_tsm_uninit(struct sev_device *sev)
{
	if (sev->tsmdev)
		tsm_unregister(sev->tsmdev);

	sev->tsmdev = NULL;
}
