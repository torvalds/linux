/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and /or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/stddef.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include <linux/dma-mapping.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/ethtool.h>
#include <linux/etherdevice.h>
#include <linux/vmalloc.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/qed_ll2_if.h>

#include "qed.h"
#include "qed_sriov.h"
#include "qed_sp.h"
#include "qed_dev_api.h"
#include "qed_ll2.h"
#include "qed_mcp.h"
#include "qed_hw.h"
#include "qed_selftest.h"

#define QED_ROCE_QPS			(8192)
#define QED_ROCE_DPIS			(8)

static char version[] =
	"QLogic FastLinQ 4xxxx Core Module qed " DRV_MODULE_VERSION "\n";

MODULE_DESCRIPTION("QLogic FastLinQ 4xxxx Core Module");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

#define FW_FILE_VERSION				\
	__stringify(FW_MAJOR_VERSION) "."	\
	__stringify(FW_MINOR_VERSION) "."	\
	__stringify(FW_REVISION_VERSION) "."	\
	__stringify(FW_ENGINEERING_VERSION)

#define QED_FW_FILE_NAME	\
	"qed/qed_init_values_zipped-" FW_FILE_VERSION ".bin"

MODULE_FIRMWARE(QED_FW_FILE_NAME);

static int __init qed_init(void)
{
	pr_info("%s", version);

	return 0;
}

static void __exit qed_cleanup(void)
{
	pr_notice("qed_cleanup called\n");
}

module_init(qed_init);
module_exit(qed_cleanup);

/* Check if the DMA controller on the machine can properly handle the DMA
 * addressing required by the device.
*/
static int qed_set_coherency_mask(struct qed_dev *cdev)
{
	struct device *dev = &cdev->pdev->dev;

	if (dma_set_mask(dev, DMA_BIT_MASK(64)) == 0) {
		if (dma_set_coherent_mask(dev, DMA_BIT_MASK(64)) != 0) {
			DP_NOTICE(cdev,
				  "Can't request 64-bit consistent allocations\n");
			return -EIO;
		}
	} else if (dma_set_mask(dev, DMA_BIT_MASK(32)) != 0) {
		DP_NOTICE(cdev, "Can't request 64b/32b DMA addresses\n");
		return -EIO;
	}

	return 0;
}

static void qed_free_pci(struct qed_dev *cdev)
{
	struct pci_dev *pdev = cdev->pdev;

	if (cdev->doorbells)
		iounmap(cdev->doorbells);
	if (cdev->regview)
		iounmap(cdev->regview);
	if (atomic_read(&pdev->enable_cnt) == 1)
		pci_release_regions(pdev);

	pci_disable_device(pdev);
}

#define PCI_REVISION_ID_ERROR_VAL	0xff

/* Performs PCI initializations as well as initializing PCI-related parameters
 * in the device structrue. Returns 0 in case of success.
 */
static int qed_init_pci(struct qed_dev *cdev, struct pci_dev *pdev)
{
	u8 rev_id;
	int rc;

	cdev->pdev = pdev;

	rc = pci_enable_device(pdev);
	if (rc) {
		DP_NOTICE(cdev, "Cannot enable PCI device\n");
		goto err0;
	}

	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM)) {
		DP_NOTICE(cdev, "No memory region found in bar #0\n");
		rc = -EIO;
		goto err1;
	}

	if (IS_PF(cdev) && !(pci_resource_flags(pdev, 2) & IORESOURCE_MEM)) {
		DP_NOTICE(cdev, "No memory region found in bar #2\n");
		rc = -EIO;
		goto err1;
	}

	if (atomic_read(&pdev->enable_cnt) == 1) {
		rc = pci_request_regions(pdev, "qed");
		if (rc) {
			DP_NOTICE(cdev,
				  "Failed to request PCI memory resources\n");
			goto err1;
		}
		pci_set_master(pdev);
		pci_save_state(pdev);
	}

	pci_read_config_byte(pdev, PCI_REVISION_ID, &rev_id);
	if (rev_id == PCI_REVISION_ID_ERROR_VAL) {
		DP_NOTICE(cdev,
			  "Detected PCI device error [rev_id 0x%x]. Probably due to prior indication. Aborting.\n",
			  rev_id);
		rc = -ENODEV;
		goto err2;
	}
	if (!pci_is_pcie(pdev)) {
		DP_NOTICE(cdev, "The bus is not PCI Express\n");
		rc = -EIO;
		goto err2;
	}

	cdev->pci_params.pm_cap = pci_find_capability(pdev, PCI_CAP_ID_PM);
	if (IS_PF(cdev) && !cdev->pci_params.pm_cap)
		DP_NOTICE(cdev, "Cannot find power management capability\n");

	rc = qed_set_coherency_mask(cdev);
	if (rc)
		goto err2;

	cdev->pci_params.mem_start = pci_resource_start(pdev, 0);
	cdev->pci_params.mem_end = pci_resource_end(pdev, 0);
	cdev->pci_params.irq = pdev->irq;

	cdev->regview = pci_ioremap_bar(pdev, 0);
	if (!cdev->regview) {
		DP_NOTICE(cdev, "Cannot map register space, aborting\n");
		rc = -ENOMEM;
		goto err2;
	}

	if (IS_PF(cdev)) {
		cdev->db_phys_addr = pci_resource_start(cdev->pdev, 2);
		cdev->db_size = pci_resource_len(cdev->pdev, 2);
		cdev->doorbells = ioremap_wc(cdev->db_phys_addr, cdev->db_size);
		if (!cdev->doorbells) {
			DP_NOTICE(cdev, "Cannot map doorbell space\n");
			return -ENOMEM;
		}
	}

	return 0;

err2:
	pci_release_regions(pdev);
err1:
	pci_disable_device(pdev);
err0:
	return rc;
}

int qed_fill_dev_info(struct qed_dev *cdev,
		      struct qed_dev_info *dev_info)
{
	struct qed_ptt  *ptt;

	memset(dev_info, 0, sizeof(struct qed_dev_info));

	dev_info->num_hwfns = cdev->num_hwfns;
	dev_info->pci_mem_start = cdev->pci_params.mem_start;
	dev_info->pci_mem_end = cdev->pci_params.mem_end;
	dev_info->pci_irq = cdev->pci_params.irq;
	dev_info->rdma_supported = (cdev->hwfns[0].hw_info.personality ==
				    QED_PCI_ETH_ROCE);
	dev_info->is_mf_default = IS_MF_DEFAULT(&cdev->hwfns[0]);
	ether_addr_copy(dev_info->hw_mac, cdev->hwfns[0].hw_info.hw_mac_addr);

	if (IS_PF(cdev)) {
		dev_info->fw_major = FW_MAJOR_VERSION;
		dev_info->fw_minor = FW_MINOR_VERSION;
		dev_info->fw_rev = FW_REVISION_VERSION;
		dev_info->fw_eng = FW_ENGINEERING_VERSION;
		dev_info->mf_mode = cdev->mf_mode;
		dev_info->tx_switching = true;

		if (QED_LEADING_HWFN(cdev)->hw_info.b_wol_support ==
		    QED_WOL_SUPPORT_PME)
			dev_info->wol_support = true;
	} else {
		qed_vf_get_fw_version(&cdev->hwfns[0], &dev_info->fw_major,
				      &dev_info->fw_minor, &dev_info->fw_rev,
				      &dev_info->fw_eng);
	}

	if (IS_PF(cdev)) {
		ptt = qed_ptt_acquire(QED_LEADING_HWFN(cdev));
		if (ptt) {
			qed_mcp_get_mfw_ver(QED_LEADING_HWFN(cdev), ptt,
					    &dev_info->mfw_rev, NULL);

			qed_mcp_get_flash_size(QED_LEADING_HWFN(cdev), ptt,
					       &dev_info->flash_size);

			qed_ptt_release(QED_LEADING_HWFN(cdev), ptt);
		}
	} else {
		qed_mcp_get_mfw_ver(QED_LEADING_HWFN(cdev), NULL,
				    &dev_info->mfw_rev, NULL);
	}

	dev_info->mtu = QED_LEADING_HWFN(cdev)->hw_info.mtu;

	return 0;
}

static void qed_free_cdev(struct qed_dev *cdev)
{
	kfree((void *)cdev);
}

static struct qed_dev *qed_alloc_cdev(struct pci_dev *pdev)
{
	struct qed_dev *cdev;

	cdev = kzalloc(sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return cdev;

	qed_init_struct(cdev);

	return cdev;
}

/* Sets the requested power state */
static int qed_set_power_state(struct qed_dev *cdev, pci_power_t state)
{
	if (!cdev)
		return -ENODEV;

	DP_VERBOSE(cdev, NETIF_MSG_DRV, "Omitting Power state change\n");
	return 0;
}

/* probing */
static struct qed_dev *qed_probe(struct pci_dev *pdev,
				 struct qed_probe_params *params)
{
	struct qed_dev *cdev;
	int rc;

	cdev = qed_alloc_cdev(pdev);
	if (!cdev)
		goto err0;

	cdev->protocol = params->protocol;

	if (params->is_vf)
		cdev->b_is_vf = true;

	qed_init_dp(cdev, params->dp_module, params->dp_level);

	rc = qed_init_pci(cdev, pdev);
	if (rc) {
		DP_ERR(cdev, "init pci failed\n");
		goto err1;
	}
	DP_INFO(cdev, "PCI init completed successfully\n");

	rc = qed_hw_prepare(cdev, QED_PCI_DEFAULT);
	if (rc) {
		DP_ERR(cdev, "hw prepare failed\n");
		goto err2;
	}

	DP_INFO(cdev, "qed_probe completed successffuly\n");

	return cdev;

err2:
	qed_free_pci(cdev);
err1:
	qed_free_cdev(cdev);
err0:
	return NULL;
}

static void qed_remove(struct qed_dev *cdev)
{
	if (!cdev)
		return;

	qed_hw_remove(cdev);

	qed_free_pci(cdev);

	qed_set_power_state(cdev, PCI_D3hot);

	qed_free_cdev(cdev);
}

static void qed_disable_msix(struct qed_dev *cdev)
{
	if (cdev->int_params.out.int_mode == QED_INT_MODE_MSIX) {
		pci_disable_msix(cdev->pdev);
		kfree(cdev->int_params.msix_table);
	} else if (cdev->int_params.out.int_mode == QED_INT_MODE_MSI) {
		pci_disable_msi(cdev->pdev);
	}

	memset(&cdev->int_params.out, 0, sizeof(struct qed_int_param));
}

static int qed_enable_msix(struct qed_dev *cdev,
			   struct qed_int_params *int_params)
{
	int i, rc, cnt;

	cnt = int_params->in.num_vectors;

	for (i = 0; i < cnt; i++)
		int_params->msix_table[i].entry = i;

	rc = pci_enable_msix_range(cdev->pdev, int_params->msix_table,
				   int_params->in.min_msix_cnt, cnt);
	if (rc < cnt && rc >= int_params->in.min_msix_cnt &&
	    (rc % cdev->num_hwfns)) {
		pci_disable_msix(cdev->pdev);

		/* If fastpath is initialized, we need at least one interrupt
		 * per hwfn [and the slow path interrupts]. New requested number
		 * should be a multiple of the number of hwfns.
		 */
		cnt = (rc / cdev->num_hwfns) * cdev->num_hwfns;
		DP_NOTICE(cdev,
			  "Trying to enable MSI-X with less vectors (%d out of %d)\n",
			  cnt, int_params->in.num_vectors);
		rc = pci_enable_msix_exact(cdev->pdev, int_params->msix_table,
					   cnt);
		if (!rc)
			rc = cnt;
	}

	if (rc > 0) {
		/* MSI-x configuration was achieved */
		int_params->out.int_mode = QED_INT_MODE_MSIX;
		int_params->out.num_vectors = rc;
		rc = 0;
	} else {
		DP_NOTICE(cdev,
			  "Failed to enable MSI-X [Requested %d vectors][rc %d]\n",
			  cnt, rc);
	}

	return rc;
}

/* This function outputs the int mode and the number of enabled msix vector */
static int qed_set_int_mode(struct qed_dev *cdev, bool force_mode)
{
	struct qed_int_params *int_params = &cdev->int_params;
	struct msix_entry *tbl;
	int rc = 0, cnt;

	switch (int_params->in.int_mode) {
	case QED_INT_MODE_MSIX:
		/* Allocate MSIX table */
		cnt = int_params->in.num_vectors;
		int_params->msix_table = kcalloc(cnt, sizeof(*tbl), GFP_KERNEL);
		if (!int_params->msix_table) {
			rc = -ENOMEM;
			goto out;
		}

		/* Enable MSIX */
		rc = qed_enable_msix(cdev, int_params);
		if (!rc)
			goto out;

		DP_NOTICE(cdev, "Failed to enable MSI-X\n");
		kfree(int_params->msix_table);
		if (force_mode)
			goto out;
		/* Fallthrough */

	case QED_INT_MODE_MSI:
		if (cdev->num_hwfns == 1) {
			rc = pci_enable_msi(cdev->pdev);
			if (!rc) {
				int_params->out.int_mode = QED_INT_MODE_MSI;
				goto out;
			}

			DP_NOTICE(cdev, "Failed to enable MSI\n");
			if (force_mode)
				goto out;
		}
		/* Fallthrough */

	case QED_INT_MODE_INTA:
			int_params->out.int_mode = QED_INT_MODE_INTA;
			rc = 0;
			goto out;
	default:
		DP_NOTICE(cdev, "Unknown int_mode value %d\n",
			  int_params->in.int_mode);
		rc = -EINVAL;
	}

out:
	if (!rc)
		DP_INFO(cdev, "Using %s interrupts\n",
			int_params->out.int_mode == QED_INT_MODE_INTA ?
			"INTa" : int_params->out.int_mode == QED_INT_MODE_MSI ?
			"MSI" : "MSIX");
	cdev->int_coalescing_mode = QED_COAL_MODE_ENABLE;

	return rc;
}

static void qed_simd_handler_config(struct qed_dev *cdev, void *token,
				    int index, void(*handler)(void *))
{
	struct qed_hwfn *hwfn = &cdev->hwfns[index % cdev->num_hwfns];
	int relative_idx = index / cdev->num_hwfns;

	hwfn->simd_proto_handler[relative_idx].func = handler;
	hwfn->simd_proto_handler[relative_idx].token = token;
}

static void qed_simd_handler_clean(struct qed_dev *cdev, int index)
{
	struct qed_hwfn *hwfn = &cdev->hwfns[index % cdev->num_hwfns];
	int relative_idx = index / cdev->num_hwfns;

	memset(&hwfn->simd_proto_handler[relative_idx], 0,
	       sizeof(struct qed_simd_fp_handler));
}

static irqreturn_t qed_msix_sp_int(int irq, void *tasklet)
{
	tasklet_schedule((struct tasklet_struct *)tasklet);
	return IRQ_HANDLED;
}

static irqreturn_t qed_single_int(int irq, void *dev_instance)
{
	struct qed_dev *cdev = (struct qed_dev *)dev_instance;
	struct qed_hwfn *hwfn;
	irqreturn_t rc = IRQ_NONE;
	u64 status;
	int i, j;

	for (i = 0; i < cdev->num_hwfns; i++) {
		status = qed_int_igu_read_sisr_reg(&cdev->hwfns[i]);

		if (!status)
			continue;

		hwfn = &cdev->hwfns[i];

		/* Slowpath interrupt */
		if (unlikely(status & 0x1)) {
			tasklet_schedule(hwfn->sp_dpc);
			status &= ~0x1;
			rc = IRQ_HANDLED;
		}

		/* Fastpath interrupts */
		for (j = 0; j < 64; j++) {
			if ((0x2ULL << j) & status) {
				hwfn->simd_proto_handler[j].func(
					hwfn->simd_proto_handler[j].token);
				status &= ~(0x2ULL << j);
				rc = IRQ_HANDLED;
			}
		}

		if (unlikely(status))
			DP_VERBOSE(hwfn, NETIF_MSG_INTR,
				   "got an unknown interrupt status 0x%llx\n",
				   status);
	}

	return rc;
}

int qed_slowpath_irq_req(struct qed_hwfn *hwfn)
{
	struct qed_dev *cdev = hwfn->cdev;
	u32 int_mode;
	int rc = 0;
	u8 id;

	int_mode = cdev->int_params.out.int_mode;
	if (int_mode == QED_INT_MODE_MSIX) {
		id = hwfn->my_id;
		snprintf(hwfn->name, NAME_SIZE, "sp-%d-%02x:%02x.%02x",
			 id, cdev->pdev->bus->number,
			 PCI_SLOT(cdev->pdev->devfn), hwfn->abs_pf_id);
		rc = request_irq(cdev->int_params.msix_table[id].vector,
				 qed_msix_sp_int, 0, hwfn->name, hwfn->sp_dpc);
	} else {
		unsigned long flags = 0;

		snprintf(cdev->name, NAME_SIZE, "%02x:%02x.%02x",
			 cdev->pdev->bus->number, PCI_SLOT(cdev->pdev->devfn),
			 PCI_FUNC(cdev->pdev->devfn));

		if (cdev->int_params.out.int_mode == QED_INT_MODE_INTA)
			flags |= IRQF_SHARED;

		rc = request_irq(cdev->pdev->irq, qed_single_int,
				 flags, cdev->name, cdev);
	}

	if (rc)
		DP_NOTICE(cdev, "request_irq failed, rc = %d\n", rc);
	else
		DP_VERBOSE(hwfn, (NETIF_MSG_INTR | QED_MSG_SP),
			   "Requested slowpath %s\n",
			   (int_mode == QED_INT_MODE_MSIX) ? "MSI-X" : "IRQ");

	return rc;
}

static void qed_slowpath_irq_free(struct qed_dev *cdev)
{
	int i;

	if (cdev->int_params.out.int_mode == QED_INT_MODE_MSIX) {
		for_each_hwfn(cdev, i) {
			if (!cdev->hwfns[i].b_int_requested)
				break;
			synchronize_irq(cdev->int_params.msix_table[i].vector);
			free_irq(cdev->int_params.msix_table[i].vector,
				 cdev->hwfns[i].sp_dpc);
		}
	} else {
		if (QED_LEADING_HWFN(cdev)->b_int_requested)
			free_irq(cdev->pdev->irq, cdev);
	}
	qed_int_disable_post_isr_release(cdev);
}

static int qed_nic_stop(struct qed_dev *cdev)
{
	int i, rc;

	rc = qed_hw_stop(cdev);

	for (i = 0; i < cdev->num_hwfns; i++) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		if (p_hwfn->b_sp_dpc_enabled) {
			tasklet_disable(p_hwfn->sp_dpc);
			p_hwfn->b_sp_dpc_enabled = false;
			DP_VERBOSE(cdev, NETIF_MSG_IFDOWN,
				   "Disabled sp taskelt [hwfn %d] at %p\n",
				   i, p_hwfn->sp_dpc);
		}
	}

	qed_dbg_pf_exit(cdev);

	return rc;
}

static int qed_nic_reset(struct qed_dev *cdev)
{
	int rc;

	rc = qed_hw_reset(cdev);
	if (rc)
		return rc;

	qed_resc_free(cdev);

	return 0;
}

static int qed_nic_setup(struct qed_dev *cdev)
{
	int rc, i;

	/* Determine if interface is going to require LL2 */
	if (QED_LEADING_HWFN(cdev)->hw_info.personality != QED_PCI_ETH) {
		for (i = 0; i < cdev->num_hwfns; i++) {
			struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

			p_hwfn->using_ll2 = true;
		}
	}

	rc = qed_resc_alloc(cdev);
	if (rc)
		return rc;

	DP_INFO(cdev, "Allocated qed resources\n");

	qed_resc_setup(cdev);

	return rc;
}

static int qed_set_int_fp(struct qed_dev *cdev, u16 cnt)
{
	int limit = 0;

	/* Mark the fastpath as free/used */
	cdev->int_params.fp_initialized = cnt ? true : false;

	if (cdev->int_params.out.int_mode != QED_INT_MODE_MSIX)
		limit = cdev->num_hwfns * 63;
	else if (cdev->int_params.fp_msix_cnt)
		limit = cdev->int_params.fp_msix_cnt;

	if (!limit)
		return -ENOMEM;

	return min_t(int, cnt, limit);
}

static int qed_get_int_fp(struct qed_dev *cdev, struct qed_int_info *info)
{
	memset(info, 0, sizeof(struct qed_int_info));

	if (!cdev->int_params.fp_initialized) {
		DP_INFO(cdev,
			"Protocol driver requested interrupt information, but its support is not yet configured\n");
		return -EINVAL;
	}

	/* Need to expose only MSI-X information; Single IRQ is handled solely
	 * by qed.
	 */
	if (cdev->int_params.out.int_mode == QED_INT_MODE_MSIX) {
		int msix_base = cdev->int_params.fp_msix_base;

		info->msix_cnt = cdev->int_params.fp_msix_cnt;
		info->msix = &cdev->int_params.msix_table[msix_base];
	}

	return 0;
}

static int qed_slowpath_setup_int(struct qed_dev *cdev,
				  enum qed_int_mode int_mode)
{
	struct qed_sb_cnt_info sb_cnt_info;
	int num_l2_queues = 0;
	int rc;
	int i;

	if ((int_mode == QED_INT_MODE_MSI) && (cdev->num_hwfns > 1)) {
		DP_NOTICE(cdev, "MSI mode is not supported for CMT devices\n");
		return -EINVAL;
	}

	memset(&cdev->int_params, 0, sizeof(struct qed_int_params));
	cdev->int_params.in.int_mode = int_mode;
	for_each_hwfn(cdev, i) {
		memset(&sb_cnt_info, 0, sizeof(sb_cnt_info));
		qed_int_get_num_sbs(&cdev->hwfns[i], &sb_cnt_info);
		cdev->int_params.in.num_vectors += sb_cnt_info.sb_cnt;
		cdev->int_params.in.num_vectors++; /* slowpath */
	}

	/* We want a minimum of one slowpath and one fastpath vector per hwfn */
	cdev->int_params.in.min_msix_cnt = cdev->num_hwfns * 2;

	rc = qed_set_int_mode(cdev, false);
	if (rc)  {
		DP_ERR(cdev, "qed_slowpath_setup_int ERR\n");
		return rc;
	}

	cdev->int_params.fp_msix_base = cdev->num_hwfns;
	cdev->int_params.fp_msix_cnt = cdev->int_params.out.num_vectors -
				       cdev->num_hwfns;

	if (!IS_ENABLED(CONFIG_QED_RDMA))
		return 0;

	for_each_hwfn(cdev, i)
		num_l2_queues += FEAT_NUM(&cdev->hwfns[i], QED_PF_L2_QUE);

	DP_VERBOSE(cdev, QED_MSG_RDMA,
		   "cdev->int_params.fp_msix_cnt=%d num_l2_queues=%d\n",
		   cdev->int_params.fp_msix_cnt, num_l2_queues);

	if (cdev->int_params.fp_msix_cnt > num_l2_queues) {
		cdev->int_params.rdma_msix_cnt =
			(cdev->int_params.fp_msix_cnt - num_l2_queues)
			/ cdev->num_hwfns;
		cdev->int_params.rdma_msix_base =
			cdev->int_params.fp_msix_base + num_l2_queues;
		cdev->int_params.fp_msix_cnt = num_l2_queues;
	} else {
		cdev->int_params.rdma_msix_cnt = 0;
	}

	DP_VERBOSE(cdev, QED_MSG_RDMA, "roce_msix_cnt=%d roce_msix_base=%d\n",
		   cdev->int_params.rdma_msix_cnt,
		   cdev->int_params.rdma_msix_base);

	return 0;
}

static int qed_slowpath_vf_setup_int(struct qed_dev *cdev)
{
	int rc;

	memset(&cdev->int_params, 0, sizeof(struct qed_int_params));
	cdev->int_params.in.int_mode = QED_INT_MODE_MSIX;

	qed_vf_get_num_rxqs(QED_LEADING_HWFN(cdev),
			    &cdev->int_params.in.num_vectors);
	if (cdev->num_hwfns > 1) {
		u8 vectors = 0;

		qed_vf_get_num_rxqs(&cdev->hwfns[1], &vectors);
		cdev->int_params.in.num_vectors += vectors;
	}

	/* We want a minimum of one fastpath vector per vf hwfn */
	cdev->int_params.in.min_msix_cnt = cdev->num_hwfns;

	rc = qed_set_int_mode(cdev, true);
	if (rc)
		return rc;

	cdev->int_params.fp_msix_base = 0;
	cdev->int_params.fp_msix_cnt = cdev->int_params.out.num_vectors;

	return 0;
}

u32 qed_unzip_data(struct qed_hwfn *p_hwfn, u32 input_len,
		   u8 *input_buf, u32 max_size, u8 *unzip_buf)
{
	int rc;

	p_hwfn->stream->next_in = input_buf;
	p_hwfn->stream->avail_in = input_len;
	p_hwfn->stream->next_out = unzip_buf;
	p_hwfn->stream->avail_out = max_size;

	rc = zlib_inflateInit2(p_hwfn->stream, MAX_WBITS);

	if (rc != Z_OK) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_DRV, "zlib init failed, rc = %d\n",
			   rc);
		return 0;
	}

	rc = zlib_inflate(p_hwfn->stream, Z_FINISH);
	zlib_inflateEnd(p_hwfn->stream);

	if (rc != Z_OK && rc != Z_STREAM_END) {
		DP_VERBOSE(p_hwfn, NETIF_MSG_DRV, "FW unzip error: %s, rc=%d\n",
			   p_hwfn->stream->msg, rc);
		return 0;
	}

	return p_hwfn->stream->total_out / 4;
}

static int qed_alloc_stream_mem(struct qed_dev *cdev)
{
	int i;
	void *workspace;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		p_hwfn->stream = kzalloc(sizeof(*p_hwfn->stream), GFP_KERNEL);
		if (!p_hwfn->stream)
			return -ENOMEM;

		workspace = vzalloc(zlib_inflate_workspacesize());
		if (!workspace)
			return -ENOMEM;
		p_hwfn->stream->workspace = workspace;
	}

	return 0;
}

static void qed_free_stream_mem(struct qed_dev *cdev)
{
	int i;

	for_each_hwfn(cdev, i) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		if (!p_hwfn->stream)
			return;

		vfree(p_hwfn->stream->workspace);
		kfree(p_hwfn->stream);
	}
}

static void qed_update_pf_params(struct qed_dev *cdev,
				 struct qed_pf_params *params)
{
	int i;

	if (IS_ENABLED(CONFIG_QED_RDMA)) {
		params->rdma_pf_params.num_qps = QED_ROCE_QPS;
		params->rdma_pf_params.min_dpis = QED_ROCE_DPIS;
		/* divide by 3 the MRs to avoid MF ILT overflow */
		params->rdma_pf_params.num_mrs = RDMA_MAX_TIDS;
		params->rdma_pf_params.gl_pi = QED_ROCE_PROTOCOL_INDEX;
	}

	/* In case we might support RDMA, don't allow qede to be greedy
	 * with the L2 contexts. Allow for 64 queues [rx, tx, xdp] per hwfn.
	 */
	if (QED_LEADING_HWFN(cdev)->hw_info.personality ==
	    QED_PCI_ETH_ROCE) {
		u16 *num_cons;

		num_cons = &params->eth_pf_params.num_cons;
		*num_cons = min_t(u16, *num_cons, 192);
	}

	for (i = 0; i < cdev->num_hwfns; i++) {
		struct qed_hwfn *p_hwfn = &cdev->hwfns[i];

		p_hwfn->pf_params = *params;
	}
}

static int qed_slowpath_start(struct qed_dev *cdev,
			      struct qed_slowpath_params *params)
{
	struct qed_tunn_start_params tunn_info;
	struct qed_mcp_drv_version drv_version;
	const u8 *data = NULL;
	struct qed_hwfn *hwfn;
	struct qed_ptt *p_ptt;
	int rc = -EINVAL;

	if (qed_iov_wq_start(cdev))
		goto err;

	if (IS_PF(cdev)) {
		rc = request_firmware(&cdev->firmware, QED_FW_FILE_NAME,
				      &cdev->pdev->dev);
		if (rc) {
			DP_NOTICE(cdev,
				  "Failed to find fw file - /lib/firmware/%s\n",
				  QED_FW_FILE_NAME);
			goto err;
		}

		p_ptt = qed_ptt_acquire(QED_LEADING_HWFN(cdev));
		if (p_ptt) {
			QED_LEADING_HWFN(cdev)->p_ptp_ptt = p_ptt;
		} else {
			DP_NOTICE(cdev, "Failed to acquire PTT for PTP\n");
			goto err;
		}
	}

	cdev->rx_coalesce_usecs = QED_DEFAULT_RX_USECS;
	rc = qed_nic_setup(cdev);
	if (rc)
		goto err;

	if (IS_PF(cdev))
		rc = qed_slowpath_setup_int(cdev, params->int_mode);
	else
		rc = qed_slowpath_vf_setup_int(cdev);
	if (rc)
		goto err1;

	if (IS_PF(cdev)) {
		/* Allocate stream for unzipping */
		rc = qed_alloc_stream_mem(cdev);
		if (rc)
			goto err2;

		/* First Dword used to diffrentiate between various sources */
		data = cdev->firmware->data + sizeof(u32);

		qed_dbg_pf_init(cdev);
	}

	memset(&tunn_info, 0, sizeof(tunn_info));
	tunn_info.tunn_mode |=  1 << QED_MODE_VXLAN_TUNN |
				1 << QED_MODE_L2GRE_TUNN |
				1 << QED_MODE_IPGRE_TUNN |
				1 << QED_MODE_L2GENEVE_TUNN |
				1 << QED_MODE_IPGENEVE_TUNN;

	tunn_info.tunn_clss_vxlan = QED_TUNN_CLSS_MAC_VLAN;
	tunn_info.tunn_clss_l2gre = QED_TUNN_CLSS_MAC_VLAN;
	tunn_info.tunn_clss_ipgre = QED_TUNN_CLSS_MAC_VLAN;

	/* Start the slowpath */
	rc = qed_hw_init(cdev, &tunn_info, true,
			 cdev->int_params.out.int_mode,
			 true, data);
	if (rc)
		goto err2;

	DP_INFO(cdev,
		"HW initialization and function start completed successfully\n");

	/* Allocate LL2 interface if needed */
	if (QED_LEADING_HWFN(cdev)->using_ll2) {
		rc = qed_ll2_alloc_if(cdev);
		if (rc)
			goto err3;
	}
	if (IS_PF(cdev)) {
		hwfn = QED_LEADING_HWFN(cdev);
		drv_version.version = (params->drv_major << 24) |
				      (params->drv_minor << 16) |
				      (params->drv_rev << 8) |
				      (params->drv_eng);
		strlcpy(drv_version.name, params->name,
			MCP_DRV_VER_STR_SIZE - 4);
		rc = qed_mcp_send_drv_version(hwfn, hwfn->p_main_ptt,
					      &drv_version);
		if (rc) {
			DP_NOTICE(cdev, "Failed sending drv version command\n");
			return rc;
		}
	}

	qed_reset_vport_stats(cdev);

	return 0;

err3:
	qed_hw_stop(cdev);
err2:
	qed_hw_timers_stop_all(cdev);
	if (IS_PF(cdev))
		qed_slowpath_irq_free(cdev);
	qed_free_stream_mem(cdev);
	qed_disable_msix(cdev);
err1:
	qed_resc_free(cdev);
err:
	if (IS_PF(cdev))
		release_firmware(cdev->firmware);

	if (IS_PF(cdev) && QED_LEADING_HWFN(cdev)->p_ptp_ptt)
		qed_ptt_release(QED_LEADING_HWFN(cdev),
				QED_LEADING_HWFN(cdev)->p_ptp_ptt);

	qed_iov_wq_stop(cdev, false);

	return rc;
}

static int qed_slowpath_stop(struct qed_dev *cdev)
{
	if (!cdev)
		return -ENODEV;

	qed_ll2_dealloc_if(cdev);

	if (IS_PF(cdev)) {
		qed_ptt_release(QED_LEADING_HWFN(cdev),
				QED_LEADING_HWFN(cdev)->p_ptp_ptt);
		qed_free_stream_mem(cdev);
		if (IS_QED_ETH_IF(cdev))
			qed_sriov_disable(cdev, true);

		qed_nic_stop(cdev);
		qed_slowpath_irq_free(cdev);
	}

	qed_disable_msix(cdev);
	qed_nic_reset(cdev);

	qed_iov_wq_stop(cdev, true);

	if (IS_PF(cdev))
		release_firmware(cdev->firmware);

	return 0;
}

static void qed_set_id(struct qed_dev *cdev, char name[NAME_SIZE],
		       char ver_str[VER_SIZE])
{
	int i;

	memcpy(cdev->name, name, NAME_SIZE);
	for_each_hwfn(cdev, i)
		snprintf(cdev->hwfns[i].name, NAME_SIZE, "%s-%d", name, i);

	memcpy(cdev->ver_str, ver_str, VER_SIZE);
	cdev->drv_type = DRV_ID_DRV_TYPE_LINUX;
}

static u32 qed_sb_init(struct qed_dev *cdev,
		       struct qed_sb_info *sb_info,
		       void *sb_virt_addr,
		       dma_addr_t sb_phy_addr, u16 sb_id,
		       enum qed_sb_type type)
{
	struct qed_hwfn *p_hwfn;
	int hwfn_index;
	u16 rel_sb_id;
	u8 n_hwfns;
	u32 rc;

	/* RoCE uses single engine and CMT uses two engines. When using both
	 * we force only a single engine. Storage uses only engine 0 too.
	 */
	if (type == QED_SB_TYPE_L2_QUEUE)
		n_hwfns = cdev->num_hwfns;
	else
		n_hwfns = 1;

	hwfn_index = sb_id % n_hwfns;
	p_hwfn = &cdev->hwfns[hwfn_index];
	rel_sb_id = sb_id / n_hwfns;

	DP_VERBOSE(cdev, NETIF_MSG_INTR,
		   "hwfn [%d] <--[init]-- SB %04x [0x%04x upper]\n",
		   hwfn_index, rel_sb_id, sb_id);

	rc = qed_int_sb_init(p_hwfn, p_hwfn->p_main_ptt, sb_info,
			     sb_virt_addr, sb_phy_addr, rel_sb_id);

	return rc;
}

static u32 qed_sb_release(struct qed_dev *cdev,
			  struct qed_sb_info *sb_info, u16 sb_id)
{
	struct qed_hwfn *p_hwfn;
	int hwfn_index;
	u16 rel_sb_id;
	u32 rc;

	hwfn_index = sb_id % cdev->num_hwfns;
	p_hwfn = &cdev->hwfns[hwfn_index];
	rel_sb_id = sb_id / cdev->num_hwfns;

	DP_VERBOSE(cdev, NETIF_MSG_INTR,
		   "hwfn [%d] <--[init]-- SB %04x [0x%04x upper]\n",
		   hwfn_index, rel_sb_id, sb_id);

	rc = qed_int_sb_release(p_hwfn, sb_info, rel_sb_id);

	return rc;
}

static bool qed_can_link_change(struct qed_dev *cdev)
{
	return true;
}

static int qed_set_link(struct qed_dev *cdev, struct qed_link_params *params)
{
	struct qed_hwfn *hwfn;
	struct qed_mcp_link_params *link_params;
	struct qed_ptt *ptt;
	int rc;

	if (!cdev)
		return -ENODEV;

	if (IS_VF(cdev))
		return 0;

	/* The link should be set only once per PF */
	hwfn = &cdev->hwfns[0];

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EBUSY;

	link_params = qed_mcp_get_link_params(hwfn);
	if (params->override_flags & QED_LINK_OVERRIDE_SPEED_AUTONEG)
		link_params->speed.autoneg = params->autoneg;
	if (params->override_flags & QED_LINK_OVERRIDE_SPEED_ADV_SPEEDS) {
		link_params->speed.advertised_speeds = 0;
		if ((params->adv_speeds & QED_LM_1000baseT_Half_BIT) ||
		    (params->adv_speeds & QED_LM_1000baseT_Full_BIT))
			link_params->speed.advertised_speeds |=
			    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G;
		if (params->adv_speeds & QED_LM_10000baseKR_Full_BIT)
			link_params->speed.advertised_speeds |=
			    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G;
		if (params->adv_speeds & QED_LM_25000baseKR_Full_BIT)
			link_params->speed.advertised_speeds |=
			    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G;
		if (params->adv_speeds & QED_LM_40000baseLR4_Full_BIT)
			link_params->speed.advertised_speeds |=
			    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G;
		if (params->adv_speeds & QED_LM_50000baseKR2_Full_BIT)
			link_params->speed.advertised_speeds |=
			    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_50G;
		if (params->adv_speeds & QED_LM_100000baseKR4_Full_BIT)
			link_params->speed.advertised_speeds |=
			    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G;
	}
	if (params->override_flags & QED_LINK_OVERRIDE_SPEED_FORCED_SPEED)
		link_params->speed.forced_speed = params->forced_speed;
	if (params->override_flags & QED_LINK_OVERRIDE_PAUSE_CONFIG) {
		if (params->pause_config & QED_LINK_PAUSE_AUTONEG_ENABLE)
			link_params->pause.autoneg = true;
		else
			link_params->pause.autoneg = false;
		if (params->pause_config & QED_LINK_PAUSE_RX_ENABLE)
			link_params->pause.forced_rx = true;
		else
			link_params->pause.forced_rx = false;
		if (params->pause_config & QED_LINK_PAUSE_TX_ENABLE)
			link_params->pause.forced_tx = true;
		else
			link_params->pause.forced_tx = false;
	}
	if (params->override_flags & QED_LINK_OVERRIDE_LOOPBACK_MODE) {
		switch (params->loopback_mode) {
		case QED_LINK_LOOPBACK_INT_PHY:
			link_params->loopback_mode = ETH_LOOPBACK_INT_PHY;
			break;
		case QED_LINK_LOOPBACK_EXT_PHY:
			link_params->loopback_mode = ETH_LOOPBACK_EXT_PHY;
			break;
		case QED_LINK_LOOPBACK_EXT:
			link_params->loopback_mode = ETH_LOOPBACK_EXT;
			break;
		case QED_LINK_LOOPBACK_MAC:
			link_params->loopback_mode = ETH_LOOPBACK_MAC;
			break;
		default:
			link_params->loopback_mode = ETH_LOOPBACK_NONE;
			break;
		}
	}

	rc = qed_mcp_set_link(hwfn, ptt, params->link_up);

	qed_ptt_release(hwfn, ptt);

	return rc;
}

static int qed_get_port_type(u32 media_type)
{
	int port_type;

	switch (media_type) {
	case MEDIA_SFPP_10G_FIBER:
	case MEDIA_SFP_1G_FIBER:
	case MEDIA_XFP_FIBER:
	case MEDIA_MODULE_FIBER:
	case MEDIA_KR:
		port_type = PORT_FIBRE;
		break;
	case MEDIA_DA_TWINAX:
		port_type = PORT_DA;
		break;
	case MEDIA_BASE_T:
		port_type = PORT_TP;
		break;
	case MEDIA_NOT_PRESENT:
		port_type = PORT_NONE;
		break;
	case MEDIA_UNSPECIFIED:
	default:
		port_type = PORT_OTHER;
		break;
	}
	return port_type;
}

static int qed_get_link_data(struct qed_hwfn *hwfn,
			     struct qed_mcp_link_params *params,
			     struct qed_mcp_link_state *link,
			     struct qed_mcp_link_capabilities *link_caps)
{
	void *p;

	if (!IS_PF(hwfn->cdev)) {
		qed_vf_get_link_params(hwfn, params);
		qed_vf_get_link_state(hwfn, link);
		qed_vf_get_link_caps(hwfn, link_caps);

		return 0;
	}

	p = qed_mcp_get_link_params(hwfn);
	if (!p)
		return -ENXIO;
	memcpy(params, p, sizeof(*params));

	p = qed_mcp_get_link_state(hwfn);
	if (!p)
		return -ENXIO;
	memcpy(link, p, sizeof(*link));

	p = qed_mcp_get_link_capabilities(hwfn);
	if (!p)
		return -ENXIO;
	memcpy(link_caps, p, sizeof(*link_caps));

	return 0;
}

static void qed_fill_link(struct qed_hwfn *hwfn,
			  struct qed_link_output *if_link)
{
	struct qed_mcp_link_params params;
	struct qed_mcp_link_state link;
	struct qed_mcp_link_capabilities link_caps;
	u32 media_type;

	memset(if_link, 0, sizeof(*if_link));

	/* Prepare source inputs */
	if (qed_get_link_data(hwfn, &params, &link, &link_caps)) {
		dev_warn(&hwfn->cdev->pdev->dev, "no link data available\n");
		return;
	}

	/* Set the link parameters to pass to protocol driver */
	if (link.link_up)
		if_link->link_up = true;

	/* TODO - at the moment assume supported and advertised speed equal */
	if_link->supported_caps = QED_LM_FIBRE_BIT;
	if (params.speed.autoneg)
		if_link->supported_caps |= QED_LM_Autoneg_BIT;
	if (params.pause.autoneg ||
	    (params.pause.forced_rx && params.pause.forced_tx))
		if_link->supported_caps |= QED_LM_Asym_Pause_BIT;
	if (params.pause.autoneg || params.pause.forced_rx ||
	    params.pause.forced_tx)
		if_link->supported_caps |= QED_LM_Pause_BIT;

	if_link->advertised_caps = if_link->supported_caps;
	if (params.speed.advertised_speeds &
	    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G)
		if_link->advertised_caps |= QED_LM_1000baseT_Half_BIT |
		    QED_LM_1000baseT_Full_BIT;
	if (params.speed.advertised_speeds &
	    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G)
		if_link->advertised_caps |= QED_LM_10000baseKR_Full_BIT;
	if (params.speed.advertised_speeds &
	    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G)
		if_link->advertised_caps |= QED_LM_25000baseKR_Full_BIT;
	if (params.speed.advertised_speeds &
	    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G)
		if_link->advertised_caps |= QED_LM_40000baseLR4_Full_BIT;
	if (params.speed.advertised_speeds &
	    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_50G)
		if_link->advertised_caps |= QED_LM_50000baseKR2_Full_BIT;
	if (params.speed.advertised_speeds &
	    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G)
		if_link->advertised_caps |= QED_LM_100000baseKR4_Full_BIT;

	if (link_caps.speed_capabilities &
	    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_1G)
		if_link->supported_caps |= QED_LM_1000baseT_Half_BIT |
		    QED_LM_1000baseT_Full_BIT;
	if (link_caps.speed_capabilities &
	    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_10G)
		if_link->supported_caps |= QED_LM_10000baseKR_Full_BIT;
	if (link_caps.speed_capabilities &
	    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_25G)
		if_link->supported_caps |= QED_LM_25000baseKR_Full_BIT;
	if (link_caps.speed_capabilities &
	    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_40G)
		if_link->supported_caps |= QED_LM_40000baseLR4_Full_BIT;
	if (link_caps.speed_capabilities &
	    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_50G)
		if_link->supported_caps |= QED_LM_50000baseKR2_Full_BIT;
	if (link_caps.speed_capabilities &
	    NVM_CFG1_PORT_DRV_SPEED_CAPABILITY_MASK_BB_100G)
		if_link->supported_caps |= QED_LM_100000baseKR4_Full_BIT;

	if (link.link_up)
		if_link->speed = link.speed;

	/* TODO - fill duplex properly */
	if_link->duplex = DUPLEX_FULL;
	qed_mcp_get_media_type(hwfn->cdev, &media_type);
	if_link->port = qed_get_port_type(media_type);

	if_link->autoneg = params.speed.autoneg;

	if (params.pause.autoneg)
		if_link->pause_config |= QED_LINK_PAUSE_AUTONEG_ENABLE;
	if (params.pause.forced_rx)
		if_link->pause_config |= QED_LINK_PAUSE_RX_ENABLE;
	if (params.pause.forced_tx)
		if_link->pause_config |= QED_LINK_PAUSE_TX_ENABLE;

	/* Link partner capabilities */
	if (link.partner_adv_speed & QED_LINK_PARTNER_SPEED_1G_HD)
		if_link->lp_caps |= QED_LM_1000baseT_Half_BIT;
	if (link.partner_adv_speed & QED_LINK_PARTNER_SPEED_1G_FD)
		if_link->lp_caps |= QED_LM_1000baseT_Full_BIT;
	if (link.partner_adv_speed & QED_LINK_PARTNER_SPEED_10G)
		if_link->lp_caps |= QED_LM_10000baseKR_Full_BIT;
	if (link.partner_adv_speed & QED_LINK_PARTNER_SPEED_25G)
		if_link->lp_caps |= QED_LM_25000baseKR_Full_BIT;
	if (link.partner_adv_speed & QED_LINK_PARTNER_SPEED_40G)
		if_link->lp_caps |= QED_LM_40000baseLR4_Full_BIT;
	if (link.partner_adv_speed & QED_LINK_PARTNER_SPEED_50G)
		if_link->lp_caps |= QED_LM_50000baseKR2_Full_BIT;
	if (link.partner_adv_speed & QED_LINK_PARTNER_SPEED_100G)
		if_link->lp_caps |= QED_LM_100000baseKR4_Full_BIT;

	if (link.an_complete)
		if_link->lp_caps |= QED_LM_Autoneg_BIT;

	if (link.partner_adv_pause)
		if_link->lp_caps |= QED_LM_Pause_BIT;
	if (link.partner_adv_pause == QED_LINK_PARTNER_ASYMMETRIC_PAUSE ||
	    link.partner_adv_pause == QED_LINK_PARTNER_BOTH_PAUSE)
		if_link->lp_caps |= QED_LM_Asym_Pause_BIT;
}

static void qed_get_current_link(struct qed_dev *cdev,
				 struct qed_link_output *if_link)
{
	int i;

	qed_fill_link(&cdev->hwfns[0], if_link);

	for_each_hwfn(cdev, i)
		qed_inform_vf_link_state(&cdev->hwfns[i]);
}

void qed_link_update(struct qed_hwfn *hwfn)
{
	void *cookie = hwfn->cdev->ops_cookie;
	struct qed_common_cb_ops *op = hwfn->cdev->protocol_ops.common;
	struct qed_link_output if_link;

	qed_fill_link(hwfn, &if_link);
	qed_inform_vf_link_state(hwfn);

	if (IS_LEAD_HWFN(hwfn) && cookie)
		op->link_update(cookie, &if_link);
}

static int qed_drain(struct qed_dev *cdev)
{
	struct qed_hwfn *hwfn;
	struct qed_ptt *ptt;
	int i, rc;

	if (IS_VF(cdev))
		return 0;

	for_each_hwfn(cdev, i) {
		hwfn = &cdev->hwfns[i];
		ptt = qed_ptt_acquire(hwfn);
		if (!ptt) {
			DP_NOTICE(hwfn, "Failed to drain NIG; No PTT\n");
			return -EBUSY;
		}
		rc = qed_mcp_drain(hwfn, ptt);
		if (rc)
			return rc;
		qed_ptt_release(hwfn, ptt);
	}

	return 0;
}

static void qed_get_coalesce(struct qed_dev *cdev, u16 *rx_coal, u16 *tx_coal)
{
	*rx_coal = cdev->rx_coalesce_usecs;
	*tx_coal = cdev->tx_coalesce_usecs;
}

static int qed_set_coalesce(struct qed_dev *cdev, u16 rx_coal, u16 tx_coal,
			    u8 qid, u16 sb_id)
{
	struct qed_hwfn *hwfn;
	struct qed_ptt *ptt;
	int hwfn_index;
	int status = 0;

	hwfn_index = qid % cdev->num_hwfns;
	hwfn = &cdev->hwfns[hwfn_index];
	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	status = qed_set_rxq_coalesce(hwfn, ptt, rx_coal,
				      qid / cdev->num_hwfns, sb_id);
	if (status)
		goto out;
	status = qed_set_txq_coalesce(hwfn, ptt, tx_coal,
				      qid / cdev->num_hwfns, sb_id);
out:
	qed_ptt_release(hwfn, ptt);

	return status;
}

static int qed_set_led(struct qed_dev *cdev, enum qed_led_mode mode)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *ptt;
	int status = 0;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	status = qed_mcp_set_led(hwfn, ptt, mode);

	qed_ptt_release(hwfn, ptt);

	return status;
}

static int qed_update_wol(struct qed_dev *cdev, bool enabled)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *ptt;
	int rc = 0;

	if (IS_VF(cdev))
		return 0;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	rc = qed_mcp_ov_update_wol(hwfn, ptt, enabled ? QED_OV_WOL_ENABLED
				   : QED_OV_WOL_DISABLED);
	if (rc)
		goto out;
	rc = qed_mcp_ov_update_current_config(hwfn, ptt, QED_OV_CLIENT_DRV);

out:
	qed_ptt_release(hwfn, ptt);
	return rc;
}

static int qed_update_drv_state(struct qed_dev *cdev, bool active)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *ptt;
	int status = 0;

	if (IS_VF(cdev))
		return 0;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	status = qed_mcp_ov_update_driver_state(hwfn, ptt, active ?
						QED_OV_DRIVER_STATE_ACTIVE :
						QED_OV_DRIVER_STATE_DISABLED);

	qed_ptt_release(hwfn, ptt);

	return status;
}

static int qed_update_mac(struct qed_dev *cdev, u8 *mac)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *ptt;
	int status = 0;

	if (IS_VF(cdev))
		return 0;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	status = qed_mcp_ov_update_mac(hwfn, ptt, mac);
	if (status)
		goto out;

	status = qed_mcp_ov_update_current_config(hwfn, ptt, QED_OV_CLIENT_DRV);

out:
	qed_ptt_release(hwfn, ptt);
	return status;
}

static int qed_update_mtu(struct qed_dev *cdev, u16 mtu)
{
	struct qed_hwfn *hwfn = QED_LEADING_HWFN(cdev);
	struct qed_ptt *ptt;
	int status = 0;

	if (IS_VF(cdev))
		return 0;

	ptt = qed_ptt_acquire(hwfn);
	if (!ptt)
		return -EAGAIN;

	status = qed_mcp_ov_update_mtu(hwfn, ptt, mtu);
	if (status)
		goto out;

	status = qed_mcp_ov_update_current_config(hwfn, ptt, QED_OV_CLIENT_DRV);

out:
	qed_ptt_release(hwfn, ptt);
	return status;
}

static struct qed_selftest_ops qed_selftest_ops_pass = {
	.selftest_memory = &qed_selftest_memory,
	.selftest_interrupt = &qed_selftest_interrupt,
	.selftest_register = &qed_selftest_register,
	.selftest_clock = &qed_selftest_clock,
	.selftest_nvram = &qed_selftest_nvram,
};

const struct qed_common_ops qed_common_ops_pass = {
	.selftest = &qed_selftest_ops_pass,
	.probe = &qed_probe,
	.remove = &qed_remove,
	.set_power_state = &qed_set_power_state,
	.set_id = &qed_set_id,
	.update_pf_params = &qed_update_pf_params,
	.slowpath_start = &qed_slowpath_start,
	.slowpath_stop = &qed_slowpath_stop,
	.set_fp_int = &qed_set_int_fp,
	.get_fp_int = &qed_get_int_fp,
	.sb_init = &qed_sb_init,
	.sb_release = &qed_sb_release,
	.simd_handler_config = &qed_simd_handler_config,
	.simd_handler_clean = &qed_simd_handler_clean,
	.can_link_change = &qed_can_link_change,
	.set_link = &qed_set_link,
	.get_link = &qed_get_current_link,
	.drain = &qed_drain,
	.update_msglvl = &qed_init_dp,
	.dbg_all_data = &qed_dbg_all_data,
	.dbg_all_data_size = &qed_dbg_all_data_size,
	.chain_alloc = &qed_chain_alloc,
	.chain_free = &qed_chain_free,
	.get_coalesce = &qed_get_coalesce,
	.set_coalesce = &qed_set_coalesce,
	.set_led = &qed_set_led,
	.update_drv_state = &qed_update_drv_state,
	.update_mac = &qed_update_mac,
	.update_mtu = &qed_update_mtu,
	.update_wol = &qed_update_wol,
};

void qed_get_protocol_stats(struct qed_dev *cdev,
			    enum qed_mcp_protocol_type type,
			    union qed_mcp_protocol_stats *stats)
{
	struct qed_eth_stats eth_stats;

	memset(stats, 0, sizeof(*stats));

	switch (type) {
	case QED_MCP_LAN_STATS:
		qed_get_vport_stats(cdev, &eth_stats);
		stats->lan_stats.ucast_rx_pkts = eth_stats.rx_ucast_pkts;
		stats->lan_stats.ucast_tx_pkts = eth_stats.tx_ucast_pkts;
		stats->lan_stats.fcs_err = -1;
		break;
	default:
		DP_ERR(cdev, "Invalid protocol type = %d\n", type);
		return;
	}
}
