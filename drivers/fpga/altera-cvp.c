/*
 * FPGA Manager Driver for Altera Arria/Cyclone/Stratix CvP
 *
 * Copyright (C) 2017 DENX Software Engineering
 *
 * Anatolij Gustschin <agust@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * Manage Altera FPGA firmware using PCIe CvP.
 * Firmware must be in binary "rbf" format.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sizes.h>

#define CVP_BAR		0	/* BAR used for data transfer in memory mode */
#define CVP_DUMMY_WR	244	/* dummy writes to clear CvP state machine */
#define TIMEOUT_US	2000	/* CVP STATUS timeout for USERMODE polling */

/* Vendor Specific Extended Capability Registers */
#define VSE_PCIE_EXT_CAP_ID		0x200
#define VSE_PCIE_EXT_CAP_ID_VAL		0x000b	/* 16bit */

#define VSE_CVP_STATUS			0x21c	/* 32bit */
#define VSE_CVP_STATUS_CFG_RDY		BIT(18)	/* CVP_CONFIG_READY */
#define VSE_CVP_STATUS_CFG_ERR		BIT(19)	/* CVP_CONFIG_ERROR */
#define VSE_CVP_STATUS_CVP_EN		BIT(20)	/* ctrl block is enabling CVP */
#define VSE_CVP_STATUS_USERMODE		BIT(21)	/* USERMODE */
#define VSE_CVP_STATUS_CFG_DONE		BIT(23)	/* CVP_CONFIG_DONE */
#define VSE_CVP_STATUS_PLD_CLK_IN_USE	BIT(24)	/* PLD_CLK_IN_USE */

#define VSE_CVP_MODE_CTRL		0x220	/* 32bit */
#define VSE_CVP_MODE_CTRL_CVP_MODE	BIT(0)	/* CVP (1) or normal mode (0) */
#define VSE_CVP_MODE_CTRL_HIP_CLK_SEL	BIT(1) /* PMA (1) or fabric clock (0) */
#define VSE_CVP_MODE_CTRL_NUMCLKS_OFF	8	/* NUMCLKS bits offset */
#define VSE_CVP_MODE_CTRL_NUMCLKS_MASK	GENMASK(15, 8)

#define VSE_CVP_DATA			0x228	/* 32bit */
#define VSE_CVP_PROG_CTRL		0x22c	/* 32bit */
#define VSE_CVP_PROG_CTRL_CONFIG	BIT(0)
#define VSE_CVP_PROG_CTRL_START_XFER	BIT(1)

#define VSE_UNCOR_ERR_STATUS		0x234	/* 32bit */
#define VSE_UNCOR_ERR_CVP_CFG_ERR	BIT(5)	/* CVP_CONFIG_ERROR_LATCHED */

#define DRV_NAME		"altera-cvp"
#define ALTERA_CVP_MGR_NAME	"Altera CvP FPGA Manager"

/* Optional CvP config error status check for debugging */
static bool altera_cvp_chkcfg;

struct altera_cvp_conf {
	struct fpga_manager	*mgr;
	struct pci_dev		*pci_dev;
	void __iomem		*map;
	void			(*write_data)(struct altera_cvp_conf *, u32);
	char			mgr_name[64];
	u8			numclks;
};

static enum fpga_mgr_states altera_cvp_state(struct fpga_manager *mgr)
{
	struct altera_cvp_conf *conf = mgr->priv;
	u32 status;

	pci_read_config_dword(conf->pci_dev, VSE_CVP_STATUS, &status);

	if (status & VSE_CVP_STATUS_CFG_DONE)
		return FPGA_MGR_STATE_OPERATING;

	if (status & VSE_CVP_STATUS_CVP_EN)
		return FPGA_MGR_STATE_POWER_UP;

	return FPGA_MGR_STATE_UNKNOWN;
}

static void altera_cvp_write_data_iomem(struct altera_cvp_conf *conf, u32 val)
{
	writel(val, conf->map);
}

static void altera_cvp_write_data_config(struct altera_cvp_conf *conf, u32 val)
{
	pci_write_config_dword(conf->pci_dev, VSE_CVP_DATA, val);
}

/* switches between CvP clock and internal clock */
static void altera_cvp_dummy_write(struct altera_cvp_conf *conf)
{
	unsigned int i;
	u32 val;

	/* set 1 CVP clock cycle for every CVP Data Register Write */
	pci_read_config_dword(conf->pci_dev, VSE_CVP_MODE_CTRL, &val);
	val &= ~VSE_CVP_MODE_CTRL_NUMCLKS_MASK;
	val |= 1 << VSE_CVP_MODE_CTRL_NUMCLKS_OFF;
	pci_write_config_dword(conf->pci_dev, VSE_CVP_MODE_CTRL, val);

	for (i = 0; i < CVP_DUMMY_WR; i++)
		conf->write_data(conf, 0); /* dummy data, could be any value */
}

static int altera_cvp_wait_status(struct altera_cvp_conf *conf, u32 status_mask,
				  u32 status_val, int timeout_us)
{
	unsigned int retries;
	u32 val;

	retries = timeout_us / 10;
	if (timeout_us % 10)
		retries++;

	do {
		pci_read_config_dword(conf->pci_dev, VSE_CVP_STATUS, &val);
		if ((val & status_mask) == status_val)
			return 0;

		/* use small usleep value to re-check and break early */
		usleep_range(10, 11);
	} while (--retries);

	return -ETIMEDOUT;
}

static int altera_cvp_teardown(struct fpga_manager *mgr,
			       struct fpga_image_info *info)
{
	struct altera_cvp_conf *conf = mgr->priv;
	struct pci_dev *pdev = conf->pci_dev;
	int ret;
	u32 val;

	/* STEP 12 - reset START_XFER bit */
	pci_read_config_dword(pdev, VSE_CVP_PROG_CTRL, &val);
	val &= ~VSE_CVP_PROG_CTRL_START_XFER;
	pci_write_config_dword(pdev, VSE_CVP_PROG_CTRL, val);

	/* STEP 13 - reset CVP_CONFIG bit */
	val &= ~VSE_CVP_PROG_CTRL_CONFIG;
	pci_write_config_dword(pdev, VSE_CVP_PROG_CTRL, val);

	/*
	 * STEP 14
	 * - set CVP_NUMCLKS to 1 and then issue CVP_DUMMY_WR dummy
	 *   writes to the HIP
	 */
	altera_cvp_dummy_write(conf); /* from CVP clock to internal clock */

	/* STEP 15 - poll CVP_CONFIG_READY bit for 0 with 10us timeout */
	ret = altera_cvp_wait_status(conf, VSE_CVP_STATUS_CFG_RDY, 0, 10);
	if (ret)
		dev_err(&mgr->dev, "CFG_RDY == 0 timeout\n");

	return ret;
}

static int altera_cvp_write_init(struct fpga_manager *mgr,
				 struct fpga_image_info *info,
				 const char *buf, size_t count)
{
	struct altera_cvp_conf *conf = mgr->priv;
	struct pci_dev *pdev = conf->pci_dev;
	u32 iflags, val;
	int ret;

	iflags = info ? info->flags : 0;

	if (iflags & FPGA_MGR_PARTIAL_RECONFIG) {
		dev_err(&mgr->dev, "Partial reconfiguration not supported.\n");
		return -EINVAL;
	}

	/* Determine allowed clock to data ratio */
	if (iflags & FPGA_MGR_COMPRESSED_BITSTREAM)
		conf->numclks = 8; /* ratio for all compressed images */
	else if (iflags & FPGA_MGR_ENCRYPTED_BITSTREAM)
		conf->numclks = 4; /* for uncompressed and encrypted images */
	else
		conf->numclks = 1; /* for uncompressed and unencrypted images */

	/* STEP 1 - read CVP status and check CVP_EN flag */
	pci_read_config_dword(pdev, VSE_CVP_STATUS, &val);
	if (!(val & VSE_CVP_STATUS_CVP_EN)) {
		dev_err(&mgr->dev, "CVP mode off: 0x%04x\n", val);
		return -ENODEV;
	}

	if (val & VSE_CVP_STATUS_CFG_RDY) {
		dev_warn(&mgr->dev, "CvP already started, teardown first\n");
		ret = altera_cvp_teardown(mgr, info);
		if (ret)
			return ret;
	}

	/*
	 * STEP 2
	 * - set HIP_CLK_SEL and CVP_MODE (must be set in the order mentioned)
	 */
	/* switch from fabric to PMA clock */
	pci_read_config_dword(pdev, VSE_CVP_MODE_CTRL, &val);
	val |= VSE_CVP_MODE_CTRL_HIP_CLK_SEL;
	pci_write_config_dword(pdev, VSE_CVP_MODE_CTRL, val);

	/* set CVP mode */
	pci_read_config_dword(pdev, VSE_CVP_MODE_CTRL, &val);
	val |= VSE_CVP_MODE_CTRL_CVP_MODE;
	pci_write_config_dword(pdev, VSE_CVP_MODE_CTRL, val);

	/*
	 * STEP 3
	 * - set CVP_NUMCLKS to 1 and issue CVP_DUMMY_WR dummy writes to the HIP
	 */
	altera_cvp_dummy_write(conf);

	/* STEP 4 - set CVP_CONFIG bit */
	pci_read_config_dword(pdev, VSE_CVP_PROG_CTRL, &val);
	/* request control block to begin transfer using CVP */
	val |= VSE_CVP_PROG_CTRL_CONFIG;
	pci_write_config_dword(pdev, VSE_CVP_PROG_CTRL, val);

	/* STEP 5 - poll CVP_CONFIG READY for 1 with 10us timeout */
	ret = altera_cvp_wait_status(conf, VSE_CVP_STATUS_CFG_RDY,
				     VSE_CVP_STATUS_CFG_RDY, 10);
	if (ret) {
		dev_warn(&mgr->dev, "CFG_RDY == 1 timeout\n");
		return ret;
	}

	/*
	 * STEP 6
	 * - set CVP_NUMCLKS to 1 and issue CVP_DUMMY_WR dummy writes to the HIP
	 */
	altera_cvp_dummy_write(conf);

	/* STEP 7 - set START_XFER */
	pci_read_config_dword(pdev, VSE_CVP_PROG_CTRL, &val);
	val |= VSE_CVP_PROG_CTRL_START_XFER;
	pci_write_config_dword(pdev, VSE_CVP_PROG_CTRL, val);

	/* STEP 8 - start transfer (set CVP_NUMCLKS for bitstream) */
	pci_read_config_dword(pdev, VSE_CVP_MODE_CTRL, &val);
	val &= ~VSE_CVP_MODE_CTRL_NUMCLKS_MASK;
	val |= conf->numclks << VSE_CVP_MODE_CTRL_NUMCLKS_OFF;
	pci_write_config_dword(pdev, VSE_CVP_MODE_CTRL, val);

	return 0;
}

static inline int altera_cvp_chk_error(struct fpga_manager *mgr, size_t bytes)
{
	struct altera_cvp_conf *conf = mgr->priv;
	u32 val;

	/* STEP 10 (optional) - check CVP_CONFIG_ERROR flag */
	pci_read_config_dword(conf->pci_dev, VSE_CVP_STATUS, &val);
	if (val & VSE_CVP_STATUS_CFG_ERR) {
		dev_err(&mgr->dev, "CVP_CONFIG_ERROR after %zu bytes!\n",
			bytes);
		return -EPROTO;
	}
	return 0;
}

static int altera_cvp_write(struct fpga_manager *mgr, const char *buf,
			    size_t count)
{
	struct altera_cvp_conf *conf = mgr->priv;
	const u32 *data;
	size_t done, remaining;
	int status = 0;
	u32 mask;

	/* STEP 9 - write 32-bit data from RBF file to CVP data register */
	data = (u32 *)buf;
	remaining = count;
	done = 0;

	while (remaining >= 4) {
		conf->write_data(conf, *data++);
		done += 4;
		remaining -= 4;

		/*
		 * STEP 10 (optional) and STEP 11
		 * - check error flag
		 * - loop until data transfer completed
		 * Config images can be huge (more than 40 MiB), so
		 * only check after a new 4k data block has been written.
		 * This reduces the number of checks and speeds up the
		 * configuration process.
		 */
		if (altera_cvp_chkcfg && !(done % SZ_4K)) {
			status = altera_cvp_chk_error(mgr, done);
			if (status < 0)
				return status;
		}
	}

	/* write up to 3 trailing bytes, if any */
	mask = BIT(remaining * 8) - 1;
	if (mask)
		conf->write_data(conf, *data & mask);

	if (altera_cvp_chkcfg)
		status = altera_cvp_chk_error(mgr, count);

	return status;
}

static int altera_cvp_write_complete(struct fpga_manager *mgr,
				     struct fpga_image_info *info)
{
	struct altera_cvp_conf *conf = mgr->priv;
	struct pci_dev *pdev = conf->pci_dev;
	int ret;
	u32 mask;
	u32 val;

	ret = altera_cvp_teardown(mgr, info);
	if (ret)
		return ret;

	/* STEP 16 - check CVP_CONFIG_ERROR_LATCHED bit */
	pci_read_config_dword(pdev, VSE_UNCOR_ERR_STATUS, &val);
	if (val & VSE_UNCOR_ERR_CVP_CFG_ERR) {
		dev_err(&mgr->dev, "detected CVP_CONFIG_ERROR_LATCHED!\n");
		return -EPROTO;
	}

	/* STEP 17 - reset CVP_MODE and HIP_CLK_SEL bit */
	pci_read_config_dword(pdev, VSE_CVP_MODE_CTRL, &val);
	val &= ~VSE_CVP_MODE_CTRL_HIP_CLK_SEL;
	val &= ~VSE_CVP_MODE_CTRL_CVP_MODE;
	pci_write_config_dword(pdev, VSE_CVP_MODE_CTRL, val);

	/* STEP 18 - poll PLD_CLK_IN_USE and USER_MODE bits */
	mask = VSE_CVP_STATUS_PLD_CLK_IN_USE | VSE_CVP_STATUS_USERMODE;
	ret = altera_cvp_wait_status(conf, mask, mask, TIMEOUT_US);
	if (ret)
		dev_err(&mgr->dev, "PLD_CLK_IN_USE|USERMODE timeout\n");

	return ret;
}

static const struct fpga_manager_ops altera_cvp_ops = {
	.state		= altera_cvp_state,
	.write_init	= altera_cvp_write_init,
	.write		= altera_cvp_write,
	.write_complete	= altera_cvp_write_complete,
};

static ssize_t chkcfg_show(struct device_driver *dev, char *buf)
{
	return snprintf(buf, 3, "%d\n", altera_cvp_chkcfg);
}

static ssize_t chkcfg_store(struct device_driver *drv, const char *buf,
			    size_t count)
{
	int ret;

	ret = kstrtobool(buf, &altera_cvp_chkcfg);
	if (ret)
		return ret;

	return count;
}

static DRIVER_ATTR_RW(chkcfg);

static int altera_cvp_probe(struct pci_dev *pdev,
			    const struct pci_device_id *dev_id);
static void altera_cvp_remove(struct pci_dev *pdev);

static struct pci_device_id altera_cvp_id_tbl[] = {
	{ PCI_VDEVICE(ALTERA, PCI_ANY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, altera_cvp_id_tbl);

static struct pci_driver altera_cvp_driver = {
	.name   = DRV_NAME,
	.id_table = altera_cvp_id_tbl,
	.probe  = altera_cvp_probe,
	.remove = altera_cvp_remove,
};

static int altera_cvp_probe(struct pci_dev *pdev,
			    const struct pci_device_id *dev_id)
{
	struct altera_cvp_conf *conf;
	struct fpga_manager *mgr;
	u16 cmd, val;
	int ret;

	/*
	 * First check if this is the expected FPGA device. PCI config
	 * space access works without enabling the PCI device, memory
	 * space access is enabled further down.
	 */
	pci_read_config_word(pdev, VSE_PCIE_EXT_CAP_ID, &val);
	if (val != VSE_PCIE_EXT_CAP_ID_VAL) {
		dev_err(&pdev->dev, "Wrong EXT_CAP_ID value 0x%x\n", val);
		return -ENODEV;
	}

	conf = devm_kzalloc(&pdev->dev, sizeof(*conf), GFP_KERNEL);
	if (!conf)
		return -ENOMEM;

	/*
	 * Enable memory BAR access. We cannot use pci_enable_device() here
	 * because it will make the driver unusable with FPGA devices that
	 * have additional big IOMEM resources (e.g. 4GiB BARs) on 32-bit
	 * platform. Such BARs will not have an assigned address range and
	 * pci_enable_device() will fail, complaining about not claimed BAR,
	 * even if the concerned BAR is not needed for FPGA configuration
	 * at all. Thus, enable the device via PCI config space command.
	 */
	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	if (!(cmd & PCI_COMMAND_MEMORY)) {
		cmd |= PCI_COMMAND_MEMORY;
		pci_write_config_word(pdev, PCI_COMMAND, cmd);
	}

	ret = pci_request_region(pdev, CVP_BAR, "CVP");
	if (ret) {
		dev_err(&pdev->dev, "Requesting CVP BAR region failed\n");
		goto err_disable;
	}

	conf->pci_dev = pdev;
	conf->write_data = altera_cvp_write_data_iomem;

	conf->map = pci_iomap(pdev, CVP_BAR, 0);
	if (!conf->map) {
		dev_warn(&pdev->dev, "Mapping CVP BAR failed\n");
		conf->write_data = altera_cvp_write_data_config;
	}

	snprintf(conf->mgr_name, sizeof(conf->mgr_name), "%s @%s",
		 ALTERA_CVP_MGR_NAME, pci_name(pdev));

	mgr = fpga_mgr_create(&pdev->dev, conf->mgr_name,
			      &altera_cvp_ops, conf);
	if (!mgr) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	pci_set_drvdata(pdev, mgr);

	ret = fpga_mgr_register(mgr);
	if (ret) {
		fpga_mgr_free(mgr);
		goto err_unmap;
	}

	ret = driver_create_file(&altera_cvp_driver.driver,
				 &driver_attr_chkcfg);
	if (ret) {
		dev_err(&pdev->dev, "Can't create sysfs chkcfg file\n");
		fpga_mgr_unregister(mgr);
		goto err_unmap;
	}

	return 0;

err_unmap:
	pci_iounmap(pdev, conf->map);
	pci_release_region(pdev, CVP_BAR);
err_disable:
	cmd &= ~PCI_COMMAND_MEMORY;
	pci_write_config_word(pdev, PCI_COMMAND, cmd);
	return ret;
}

static void altera_cvp_remove(struct pci_dev *pdev)
{
	struct fpga_manager *mgr = pci_get_drvdata(pdev);
	struct altera_cvp_conf *conf = mgr->priv;
	u16 cmd;

	driver_remove_file(&altera_cvp_driver.driver, &driver_attr_chkcfg);
	fpga_mgr_unregister(mgr);
	pci_iounmap(pdev, conf->map);
	pci_release_region(pdev, CVP_BAR);
	pci_read_config_word(pdev, PCI_COMMAND, &cmd);
	cmd &= ~PCI_COMMAND_MEMORY;
	pci_write_config_word(pdev, PCI_COMMAND, cmd);
}

module_pci_driver(altera_cvp_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Anatolij Gustschin <agust@denx.de>");
MODULE_DESCRIPTION("Module to load Altera FPGA over CvP");
