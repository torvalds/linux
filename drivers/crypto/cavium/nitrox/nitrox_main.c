// SPDX-License-Identifier: GPL-2.0-only
#include <linux/aer.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>

#include "nitrox_dev.h"
#include "nitrox_common.h"
#include "nitrox_csr.h"
#include "nitrox_hal.h"
#include "nitrox_isr.h"
#include "nitrox_debugfs.h"

#define CNN55XX_DEV_ID	0x12
#define UCODE_HLEN 48
#define DEFAULT_SE_GROUP 0
#define DEFAULT_AE_GROUP 0

#define DRIVER_VERSION "1.2"
#define CNN55XX_UCD_BLOCK_SIZE 32768
#define CNN55XX_MAX_UCODE_SIZE (CNN55XX_UCD_BLOCK_SIZE * 2)
#define FW_DIR "cavium/"
/* SE microcode */
#define SE_FW	FW_DIR "cnn55xx_se.fw"
/* AE microcode */
#define AE_FW	FW_DIR "cnn55xx_ae.fw"

static const char nitrox_driver_name[] = "CNN55XX";

static LIST_HEAD(ndevlist);
static DEFINE_MUTEX(devlist_lock);
static unsigned int num_devices;

/**
 * nitrox_pci_tbl - PCI Device ID Table
 */
static const struct pci_device_id nitrox_pci_tbl[] = {
	{PCI_VDEVICE(CAVIUM, CNN55XX_DEV_ID), 0},
	/* required last entry */
	{0, }
};
MODULE_DEVICE_TABLE(pci, nitrox_pci_tbl);

static unsigned int qlen = DEFAULT_CMD_QLEN;
module_param(qlen, uint, 0644);
MODULE_PARM_DESC(qlen, "Command queue length - default 2048");

/**
 * struct ucode - Firmware Header
 * @id: microcode ID
 * @version: firmware version
 * @code_size: code section size
 * @raz: alignment
 * @code: code section
 */
struct ucode {
	u8 id;
	char version[VERSION_LEN - 1];
	__be32 code_size;
	u8 raz[12];
	u64 code[];
};

/**
 * write_to_ucd_unit - Write Firmware to NITROX UCD unit
 */
static void write_to_ucd_unit(struct nitrox_device *ndev, u32 ucode_size,
			      u64 *ucode_data, int block_num)
{
	u32 code_size;
	u64 offset, data;
	int i = 0;

	/*
	 * UCD structure
	 *
	 *  -------------
	 *  |    BLK 7  |
	 *  -------------
	 *  |    BLK 6  |
	 *  -------------
	 *  |    ...    |
	 *  -------------
	 *  |    BLK 0  |
	 *  -------------
	 *  Total of 8 blocks, each size 32KB
	 */

	/* set the block number */
	offset = UCD_UCODE_LOAD_BLOCK_NUM;
	nitrox_write_csr(ndev, offset, block_num);

	code_size = roundup(ucode_size, 16);
	while (code_size) {
		data = ucode_data[i];
		/* write 8 bytes at a time */
		offset = UCD_UCODE_LOAD_IDX_DATAX(i);
		nitrox_write_csr(ndev, offset, data);
		code_size -= 8;
		i++;
	}

	usleep_range(300, 400);
}

static int nitrox_load_fw(struct nitrox_device *ndev)
{
	const struct firmware *fw;
	const char *fw_name;
	struct ucode *ucode;
	u64 *ucode_data;
	u64 offset;
	union ucd_core_eid_ucode_block_num core_2_eid_val;
	union aqm_grp_execmsk_lo aqm_grp_execmask_lo;
	union aqm_grp_execmsk_hi aqm_grp_execmask_hi;
	u32 ucode_size;
	int ret, i = 0;

	fw_name = SE_FW;
	dev_info(DEV(ndev), "Loading firmware \"%s\"\n", fw_name);

	ret = request_firmware(&fw, fw_name, DEV(ndev));
	if (ret < 0) {
		dev_err(DEV(ndev), "failed to get firmware %s\n", fw_name);
		return ret;
	}

	ucode = (struct ucode *)fw->data;

	ucode_size = be32_to_cpu(ucode->code_size) * 2;
	if (!ucode_size || ucode_size > CNN55XX_MAX_UCODE_SIZE) {
		dev_err(DEV(ndev), "Invalid ucode size: %u for firmware %s\n",
			ucode_size, fw_name);
		release_firmware(fw);
		return -EINVAL;
	}
	ucode_data = ucode->code;

	/* copy the firmware version */
	memcpy(&ndev->hw.fw_name[0][0], ucode->version, (VERSION_LEN - 2));
	ndev->hw.fw_name[0][VERSION_LEN - 1] = '\0';

	/* Load SE Firmware on UCD Block 0 */
	write_to_ucd_unit(ndev, ucode_size, ucode_data, 0);

	release_firmware(fw);

	/* put all SE cores in DEFAULT_SE_GROUP */
	offset = POM_GRP_EXECMASKX(DEFAULT_SE_GROUP);
	nitrox_write_csr(ndev, offset, (~0ULL));

	/* write block number and firmware length
	 * bit:<2:0> block number
	 * bit:3 is set SE uses 32KB microcode
	 * bit:3 is clear SE uses 64KB microcode
	 */
	core_2_eid_val.value = 0ULL;
	core_2_eid_val.ucode_blk = 0;
	if (ucode_size <= CNN55XX_UCD_BLOCK_SIZE)
		core_2_eid_val.ucode_len = 1;
	else
		core_2_eid_val.ucode_len = 0;

	for (i = 0; i < ndev->hw.se_cores; i++) {
		offset = UCD_SE_EID_UCODE_BLOCK_NUMX(i);
		nitrox_write_csr(ndev, offset, core_2_eid_val.value);
	}


	fw_name = AE_FW;
	dev_info(DEV(ndev), "Loading firmware \"%s\"\n", fw_name);

	ret = request_firmware(&fw, fw_name, DEV(ndev));
	if (ret < 0) {
		dev_err(DEV(ndev), "failed to get firmware %s\n", fw_name);
		return ret;
	}

	ucode = (struct ucode *)fw->data;

	ucode_size = be32_to_cpu(ucode->code_size) * 2;
	if (!ucode_size || ucode_size > CNN55XX_MAX_UCODE_SIZE) {
		dev_err(DEV(ndev), "Invalid ucode size: %u for firmware %s\n",
			ucode_size, fw_name);
		release_firmware(fw);
		return -EINVAL;
	}
	ucode_data = ucode->code;

	/* copy the firmware version */
	memcpy(&ndev->hw.fw_name[1][0], ucode->version, (VERSION_LEN - 2));
	ndev->hw.fw_name[1][VERSION_LEN - 1] = '\0';

	/* Load AE Firmware on UCD Block 2 */
	write_to_ucd_unit(ndev, ucode_size, ucode_data, 2);

	release_firmware(fw);

	/* put all AE cores in DEFAULT_AE_GROUP */
	offset = AQM_GRP_EXECMSK_LOX(DEFAULT_AE_GROUP);
	aqm_grp_execmask_lo.exec_0_to_39 = 0xFFFFFFFFFFULL;
	nitrox_write_csr(ndev, offset, aqm_grp_execmask_lo.value);
	offset = AQM_GRP_EXECMSK_HIX(DEFAULT_AE_GROUP);
	aqm_grp_execmask_hi.exec_40_to_79 = 0xFFFFFFFFFFULL;
	nitrox_write_csr(ndev, offset, aqm_grp_execmask_hi.value);

	/* write block number and firmware length
	 * bit:<2:0> block number
	 * bit:3 is set AE uses 32KB microcode
	 * bit:3 is clear AE uses 64KB microcode
	 */
	core_2_eid_val.value = 0ULL;
	core_2_eid_val.ucode_blk = 2;
	if (ucode_size <= CNN55XX_UCD_BLOCK_SIZE)
		core_2_eid_val.ucode_len = 1;
	else
		core_2_eid_val.ucode_len = 0;

	for (i = 0; i < ndev->hw.ae_cores; i++) {
		offset = UCD_AE_EID_UCODE_BLOCK_NUMX(i);
		nitrox_write_csr(ndev, offset, core_2_eid_val.value);
	}

	return 0;
}

/**
 * nitrox_add_to_devlist - add NITROX device to global device list
 * @ndev: NITROX device
 */
static int nitrox_add_to_devlist(struct nitrox_device *ndev)
{
	struct nitrox_device *dev;
	int ret = 0;

	INIT_LIST_HEAD(&ndev->list);
	refcount_set(&ndev->refcnt, 1);

	mutex_lock(&devlist_lock);
	list_for_each_entry(dev, &ndevlist, list) {
		if (dev == ndev) {
			ret = -EEXIST;
			goto unlock;
		}
	}
	ndev->idx = num_devices++;
	list_add_tail(&ndev->list, &ndevlist);
unlock:
	mutex_unlock(&devlist_lock);
	return ret;
}

/**
 * nitrox_remove_from_devlist - remove NITROX device from
 *   global device list
 * @ndev: NITROX device
 */
static void nitrox_remove_from_devlist(struct nitrox_device *ndev)
{
	mutex_lock(&devlist_lock);
	list_del(&ndev->list);
	num_devices--;
	mutex_unlock(&devlist_lock);
}

struct nitrox_device *nitrox_get_first_device(void)
{
	struct nitrox_device *ndev;

	mutex_lock(&devlist_lock);
	list_for_each_entry(ndev, &ndevlist, list) {
		if (nitrox_ready(ndev))
			break;
	}
	mutex_unlock(&devlist_lock);
	if (&ndev->list == &ndevlist)
		return NULL;

	refcount_inc(&ndev->refcnt);
	/* barrier to sync with other cpus */
	smp_mb__after_atomic();
	return ndev;
}

void nitrox_put_device(struct nitrox_device *ndev)
{
	if (!ndev)
		return;

	refcount_dec(&ndev->refcnt);
	/* barrier to sync with other cpus */
	smp_mb__after_atomic();
}

static int nitrox_device_flr(struct pci_dev *pdev)
{
	int pos = 0;

	pos = pci_save_state(pdev);
	if (pos) {
		dev_err(&pdev->dev, "Failed to save pci state\n");
		return -ENOMEM;
	}

	/* check flr support */
	if (pcie_has_flr(pdev))
		pcie_flr(pdev);

	pci_restore_state(pdev);

	return 0;
}

static int nitrox_pf_sw_init(struct nitrox_device *ndev)
{
	int err;

	err = nitrox_common_sw_init(ndev);
	if (err)
		return err;

	err = nitrox_register_interrupts(ndev);
	if (err)
		nitrox_common_sw_cleanup(ndev);

	return err;
}

static void nitrox_pf_sw_cleanup(struct nitrox_device *ndev)
{
	nitrox_unregister_interrupts(ndev);
	nitrox_common_sw_cleanup(ndev);
}

/**
 * nitrox_bist_check - Check NITROX BIST registers status
 * @ndev: NITROX device
 */
static int nitrox_bist_check(struct nitrox_device *ndev)
{
	u64 value = 0;
	int i;

	for (i = 0; i < NR_CLUSTERS; i++) {
		value += nitrox_read_csr(ndev, EMU_BIST_STATUSX(i));
		value += nitrox_read_csr(ndev, EFL_CORE_BIST_REGX(i));
	}
	value += nitrox_read_csr(ndev, UCD_BIST_STATUS);
	value += nitrox_read_csr(ndev, NPS_CORE_BIST_REG);
	value += nitrox_read_csr(ndev, NPS_CORE_NPC_BIST_REG);
	value += nitrox_read_csr(ndev, NPS_PKT_SLC_BIST_REG);
	value += nitrox_read_csr(ndev, NPS_PKT_IN_BIST_REG);
	value += nitrox_read_csr(ndev, POM_BIST_REG);
	value += nitrox_read_csr(ndev, BMI_BIST_REG);
	value += nitrox_read_csr(ndev, EFL_TOP_BIST_STAT);
	value += nitrox_read_csr(ndev, BMO_BIST_REG);
	value += nitrox_read_csr(ndev, LBC_BIST_STATUS);
	value += nitrox_read_csr(ndev, PEM_BIST_STATUSX(0));
	if (value)
		return -EIO;
	return 0;
}

static int nitrox_pf_hw_init(struct nitrox_device *ndev)
{
	int err;

	err = nitrox_bist_check(ndev);
	if (err) {
		dev_err(&ndev->pdev->dev, "BIST check failed\n");
		return err;
	}
	/* get cores information */
	nitrox_get_hwinfo(ndev);

	nitrox_config_nps_core_unit(ndev);
	nitrox_config_aqm_unit(ndev);
	nitrox_config_nps_pkt_unit(ndev);
	nitrox_config_pom_unit(ndev);
	nitrox_config_efl_unit(ndev);
	/* configure IO units */
	nitrox_config_bmi_unit(ndev);
	nitrox_config_bmo_unit(ndev);
	/* configure Local Buffer Cache */
	nitrox_config_lbc_unit(ndev);
	nitrox_config_rand_unit(ndev);

	/* load firmware on cores */
	err = nitrox_load_fw(ndev);
	if (err)
		return err;

	nitrox_config_emu_unit(ndev);

	return 0;
}

/**
 * nitrox_probe - NITROX Initialization function.
 * @pdev: PCI device information struct
 * @id: entry in nitrox_pci_tbl
 *
 * Return: 0, if the driver is bound to the device, or
 *         a negative error if there is failure.
 */
static int nitrox_probe(struct pci_dev *pdev,
			const struct pci_device_id *id)
{
	struct nitrox_device *ndev;
	int err;

	dev_info_once(&pdev->dev, "%s driver version %s\n",
		      nitrox_driver_name, DRIVER_VERSION);

	err = pci_enable_device_mem(pdev);
	if (err)
		return err;

	/* do FLR */
	err = nitrox_device_flr(pdev);
	if (err) {
		dev_err(&pdev->dev, "FLR failed\n");
		pci_disable_device(pdev);
		return err;
	}

	if (!dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64))) {
		dev_dbg(&pdev->dev, "DMA to 64-BIT address\n");
	} else {
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "DMA configuration failed\n");
			pci_disable_device(pdev);
			return err;
		}
	}

	err = pci_request_mem_regions(pdev, nitrox_driver_name);
	if (err) {
		pci_disable_device(pdev);
		dev_err(&pdev->dev, "Failed to request mem regions!\n");
		return err;
	}
	pci_set_master(pdev);

	ndev = kzalloc(sizeof(*ndev), GFP_KERNEL);
	if (!ndev) {
		err = -ENOMEM;
		goto ndev_fail;
	}

	pci_set_drvdata(pdev, ndev);
	ndev->pdev = pdev;

	/* add to device list */
	nitrox_add_to_devlist(ndev);

	ndev->hw.vendor_id = pdev->vendor;
	ndev->hw.device_id = pdev->device;
	ndev->hw.revision_id = pdev->revision;
	/* command timeout in jiffies */
	ndev->timeout = msecs_to_jiffies(CMD_TIMEOUT);
	ndev->node = dev_to_node(&pdev->dev);
	if (ndev->node == NUMA_NO_NODE)
		ndev->node = 0;

	ndev->bar_addr = ioremap(pci_resource_start(pdev, 0),
				 pci_resource_len(pdev, 0));
	if (!ndev->bar_addr) {
		err = -EIO;
		goto ioremap_err;
	}
	/* allocate command queus based on cpus, max queues are 64 */
	ndev->nr_queues = min_t(u32, MAX_PF_QUEUES, num_online_cpus());
	ndev->qlen = qlen;

	err = nitrox_pf_sw_init(ndev);
	if (err)
		goto ioremap_err;

	err = nitrox_pf_hw_init(ndev);
	if (err)
		goto pf_hw_fail;

	nitrox_debugfs_init(ndev);

	/* clear the statistics */
	atomic64_set(&ndev->stats.posted, 0);
	atomic64_set(&ndev->stats.completed, 0);
	atomic64_set(&ndev->stats.dropped, 0);

	atomic_set(&ndev->state, __NDEV_READY);
	/* barrier to sync with other cpus */
	smp_mb__after_atomic();

	err = nitrox_crypto_register();
	if (err)
		goto crypto_fail;

	return 0;

crypto_fail:
	nitrox_debugfs_exit(ndev);
	atomic_set(&ndev->state, __NDEV_NOT_READY);
	/* barrier to sync with other cpus */
	smp_mb__after_atomic();
pf_hw_fail:
	nitrox_pf_sw_cleanup(ndev);
ioremap_err:
	nitrox_remove_from_devlist(ndev);
	kfree(ndev);
	pci_set_drvdata(pdev, NULL);
ndev_fail:
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
	return err;
}

/**
 * nitrox_remove - Unbind the driver from the device.
 * @pdev: PCI device information struct
 */
static void nitrox_remove(struct pci_dev *pdev)
{
	struct nitrox_device *ndev = pci_get_drvdata(pdev);

	if (!ndev)
		return;

	if (!refcount_dec_and_test(&ndev->refcnt)) {
		dev_err(DEV(ndev), "Device refcnt not zero (%d)\n",
			refcount_read(&ndev->refcnt));
		return;
	}

	dev_info(DEV(ndev), "Removing Device %x:%x\n",
		 ndev->hw.vendor_id, ndev->hw.device_id);

	atomic_set(&ndev->state, __NDEV_NOT_READY);
	/* barrier to sync with other cpus */
	smp_mb__after_atomic();

	nitrox_remove_from_devlist(ndev);

	/* disable SR-IOV */
	nitrox_sriov_configure(pdev, 0);
	nitrox_crypto_unregister();
	nitrox_debugfs_exit(ndev);
	nitrox_pf_sw_cleanup(ndev);

	iounmap(ndev->bar_addr);
	kfree(ndev);

	pci_set_drvdata(pdev, NULL);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}

static void nitrox_shutdown(struct pci_dev *pdev)
{
	pci_set_drvdata(pdev, NULL);
	pci_release_mem_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver nitrox_driver = {
	.name = nitrox_driver_name,
	.id_table = nitrox_pci_tbl,
	.probe = nitrox_probe,
	.remove	= nitrox_remove,
	.shutdown = nitrox_shutdown,
	.sriov_configure = nitrox_sriov_configure,
};

module_pci_driver(nitrox_driver);

MODULE_AUTHOR("Srikanth Jampala <Jampala.Srikanth@cavium.com>");
MODULE_DESCRIPTION("Cavium CNN55XX PF Driver" DRIVER_VERSION " ");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);
MODULE_FIRMWARE(SE_FW);
