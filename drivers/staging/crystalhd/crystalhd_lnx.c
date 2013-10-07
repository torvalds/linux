/***************************************************************************
  BCM70010 Linux driver
  Copyright (c) 2005-2009, Broadcom Corporation.

  This driver is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, version 2 of the License.

  This driver is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this driver.  If not, see <http://www.gnu.org/licenses/>.
***************************************************************************/

#include "crystalhd.h"

#include <linux/mutex.h>
#include <linux/slab.h>


static DEFINE_MUTEX(chd_dec_mutex);
static struct class *crystalhd_class;

static struct crystalhd_adp *g_adp_info;

static irqreturn_t chd_dec_isr(int irq, void *arg)
{
	struct crystalhd_adp *adp = (struct crystalhd_adp *) arg;
	int rc = 0;
	if (adp)
		rc = crystalhd_cmd_interrupt(&adp->cmds);

	return IRQ_RETVAL(rc);
}

static int chd_dec_enable_int(struct crystalhd_adp *adp)
{
	int rc = 0;

	if (!adp || !adp->pdev) {
		BCMLOG_ERR("Invalid arg!!\n");
		return -EINVAL;
	}

	if (adp->pdev->msi_enabled)
		adp->msi = 1;
	else
		adp->msi = pci_enable_msi(adp->pdev);

	rc = request_irq(adp->pdev->irq, chd_dec_isr, IRQF_SHARED,
			 adp->name, (void *)adp);
	if (rc) {
		BCMLOG_ERR("Interrupt request failed..\n");
		pci_disable_msi(adp->pdev);
	}

	return rc;
}

static int chd_dec_disable_int(struct crystalhd_adp *adp)
{
	if (!adp || !adp->pdev) {
		BCMLOG_ERR("Invalid arg!!\n");
		return -EINVAL;
	}

	free_irq(adp->pdev->irq, adp);

	if (adp->msi)
		pci_disable_msi(adp->pdev);

	return 0;
}

static struct
crystalhd_ioctl_data *chd_dec_alloc_iodata(struct crystalhd_adp *adp,
					   bool isr)
{
	unsigned long flags = 0;
	struct crystalhd_ioctl_data *temp;

	if (!adp)
		return NULL;

	spin_lock_irqsave(&adp->lock, flags);

	temp = adp->idata_free_head;
	if (temp) {
		adp->idata_free_head = adp->idata_free_head->next;
		memset(temp, 0, sizeof(*temp));
	}

	spin_unlock_irqrestore(&adp->lock, flags);
	return temp;
}

static void chd_dec_free_iodata(struct crystalhd_adp *adp,
				struct crystalhd_ioctl_data *iodata, bool isr)
{
	unsigned long flags = 0;

	if (!adp || !iodata)
		return;

	spin_lock_irqsave(&adp->lock, flags);
	iodata->next = adp->idata_free_head;
	adp->idata_free_head = iodata;
	spin_unlock_irqrestore(&adp->lock, flags);
}

static inline int crystalhd_user_data(unsigned long ud, void *dr,
			 int size, int set)
{
	int rc;

	if (!ud || !dr) {
		BCMLOG_ERR("Invalid arg\n");
		return -EINVAL;
	}

	if (set)
		rc = copy_to_user((void *)ud, dr, size);
	else
		rc = copy_from_user(dr, (void *)ud, size);

	if (rc) {
		BCMLOG_ERR("Invalid args for command\n");
		rc = -EFAULT;
	}

	return rc;
}

static int chd_dec_fetch_cdata(struct crystalhd_adp *adp,
	 struct crystalhd_ioctl_data *io, uint32_t m_sz, unsigned long ua)
{
	unsigned long ua_off;
	int rc = 0;

	if (!adp || !io || !ua || !m_sz) {
		BCMLOG_ERR("Invalid Arg!!\n");
		return -EINVAL;
	}

	io->add_cdata = vmalloc(m_sz);
	if (!io->add_cdata) {
		BCMLOG_ERR("kalloc fail for sz:%x\n", m_sz);
		return -ENOMEM;
	}

	io->add_cdata_sz = m_sz;
	ua_off = ua + sizeof(io->udata);
	rc = crystalhd_user_data(ua_off, io->add_cdata, io->add_cdata_sz, 0);
	if (rc) {
		BCMLOG_ERR("failed to pull add_cdata sz:%x ua_off:%x\n",
			   io->add_cdata_sz, (unsigned int)ua_off);
		kfree(io->add_cdata);
		io->add_cdata = NULL;
		return -ENODATA;
	}

	return rc;
}

static int chd_dec_release_cdata(struct crystalhd_adp *adp,
			 struct crystalhd_ioctl_data *io, unsigned long ua)
{
	unsigned long ua_off;
	int rc;

	if (!adp || !io || !ua) {
		BCMLOG_ERR("Invalid Arg!!\n");
		return -EINVAL;
	}

	if (io->cmd != BCM_IOC_FW_DOWNLOAD) {
		ua_off = ua + sizeof(io->udata);
		rc = crystalhd_user_data(ua_off, io->add_cdata,
					io->add_cdata_sz, 1);
		if (rc) {
			BCMLOG_ERR(
				"failed to push add_cdata sz:%x ua_off:%x\n",
				 io->add_cdata_sz, (unsigned int)ua_off);
			return -ENODATA;
		}
	}

	if (io->add_cdata) {
		vfree(io->add_cdata);
		io->add_cdata = NULL;
	}

	return 0;
}

static int chd_dec_proc_user_data(struct crystalhd_adp *adp,
				  struct crystalhd_ioctl_data *io,
				  unsigned long ua, int set)
{
	int rc;
	uint32_t m_sz = 0;

	if (!adp || !io || !ua) {
		BCMLOG_ERR("Invalid Arg!!\n");
		return -EINVAL;
	}

	rc = crystalhd_user_data(ua, &io->udata, sizeof(io->udata), set);
	if (rc) {
		BCMLOG_ERR("failed to %s iodata\n", (set ? "set" : "get"));
		return rc;
	}

	switch (io->cmd) {
	case BCM_IOC_MEM_RD:
	case BCM_IOC_MEM_WR:
	case BCM_IOC_FW_DOWNLOAD:
		m_sz = io->udata.u.devMem.NumDwords * 4;
		if (set)
			rc = chd_dec_release_cdata(adp, io, ua);
		else
			rc = chd_dec_fetch_cdata(adp, io, m_sz, ua);
		break;
	default:
		break;
	}

	return rc;
}

static int chd_dec_api_cmd(struct crystalhd_adp *adp, unsigned long ua,
			   uint32_t uid, uint32_t cmd, crystalhd_cmd_proc func)
{
	int rc;
	struct crystalhd_ioctl_data *temp;
	enum BC_STATUS sts = BC_STS_SUCCESS;

	temp = chd_dec_alloc_iodata(adp, 0);
	if (!temp) {
		BCMLOG_ERR("Failed to get iodata..\n");
		return -EINVAL;
	}

	temp->u_id = uid;
	temp->cmd  = cmd;

	rc = chd_dec_proc_user_data(adp, temp, ua, 0);
	if (!rc) {
		sts = func(&adp->cmds, temp);
		if (sts == BC_STS_PENDING)
			sts = BC_STS_NOT_IMPL;
		temp->udata.RetSts = sts;
		rc = chd_dec_proc_user_data(adp, temp, ua, 1);
	}

	chd_dec_free_iodata(adp, temp, 0);

	return rc;
}

/* API interfaces */
static long chd_dec_ioctl(struct file *fd, unsigned int cmd, unsigned long ua)
{
	struct crystalhd_adp *adp = chd_get_adp();
	crystalhd_cmd_proc cproc;
	struct crystalhd_user *uc;
	int ret;

	if (!adp || !fd) {
		BCMLOG_ERR("Invalid adp\n");
		return -EINVAL;
	}

	uc = fd->private_data;
	if (!uc) {
		BCMLOG_ERR("Failed to get uc\n");
		return -ENODATA;
	}

	mutex_lock(&chd_dec_mutex);
	cproc = crystalhd_get_cmd_proc(&adp->cmds, cmd, uc);
	if (!cproc) {
		BCMLOG_ERR("Unhandled command: %d\n", cmd);
		mutex_unlock(&chd_dec_mutex);
		return -EINVAL;
	}

	ret = chd_dec_api_cmd(adp, ua, uc->uid, cmd, cproc);
	mutex_unlock(&chd_dec_mutex);
	return ret;
}

static int chd_dec_open(struct inode *in, struct file *fd)
{
	struct crystalhd_adp *adp = chd_get_adp();
	int rc = 0;
	enum BC_STATUS sts = BC_STS_SUCCESS;
	struct crystalhd_user *uc = NULL;

	if (!adp) {
		BCMLOG_ERR("Invalid adp\n");
		return -EINVAL;
	}

	if (adp->cfg_users >= BC_LINK_MAX_OPENS) {
		BCMLOG(BCMLOG_INFO, "Already in use.%d\n", adp->cfg_users);
		return -EBUSY;
	}

	sts = crystalhd_user_open(&adp->cmds, &uc);
	if (sts != BC_STS_SUCCESS) {
		BCMLOG_ERR("cmd_user_open - %d\n", sts);
		rc = -EBUSY;
	}

	adp->cfg_users++;

	fd->private_data = uc;

	return rc;
}

static int chd_dec_close(struct inode *in, struct file *fd)
{
	struct crystalhd_adp *adp = chd_get_adp();
	struct crystalhd_user *uc;

	if (!adp) {
		BCMLOG_ERR("Invalid adp\n");
		return -EINVAL;
	}

	uc = fd->private_data;
	if (!uc) {
		BCMLOG_ERR("Failed to get uc\n");
		return -ENODATA;
	}

	crystalhd_user_close(&adp->cmds, uc);

	adp->cfg_users--;

	return 0;
}

static const struct file_operations chd_dec_fops = {
	.owner   = THIS_MODULE,
	.unlocked_ioctl = chd_dec_ioctl,
	.open    = chd_dec_open,
	.release = chd_dec_close,
	.llseek = noop_llseek,
};

static int chd_dec_init_chdev(struct crystalhd_adp *adp)
{
	struct crystalhd_ioctl_data *temp;
	struct device *dev;
	int rc = -ENODEV, i = 0;

	if (!adp)
		goto fail;

	adp->chd_dec_major = register_chrdev(0, CRYSTALHD_API_NAME,
					     &chd_dec_fops);
	if (adp->chd_dec_major < 0) {
		BCMLOG_ERR("Failed to create config dev\n");
		rc = adp->chd_dec_major;
		goto fail;
	}

	/* register crystalhd class */
	crystalhd_class = class_create(THIS_MODULE, "crystalhd");
	if (IS_ERR(crystalhd_class)) {
		rc = PTR_ERR(crystalhd_class);
		BCMLOG_ERR("failed to create class\n");
		goto class_create_fail;
	}

	dev = device_create(crystalhd_class, NULL,
			 MKDEV(adp->chd_dec_major, 0), NULL, "crystalhd");
	if (IS_ERR(dev)) {
		rc = PTR_ERR(dev);
		BCMLOG_ERR("failed to create device\n");
		goto device_create_fail;
	}

	rc = crystalhd_create_elem_pool(adp, BC_LINK_ELEM_POOL_SZ);
	if (rc) {
		BCMLOG_ERR("failed to create device\n");
		goto elem_pool_fail;
	}

	/* Allocate general purpose ioctl pool. */
	for (i = 0; i < CHD_IODATA_POOL_SZ; i++) {
		temp = kzalloc(sizeof(struct crystalhd_ioctl_data),
					 GFP_KERNEL);
		if (!temp) {
			BCMLOG_ERR("ioctl data pool kzalloc failed\n");
			rc = -ENOMEM;
			goto kzalloc_fail;
		}
		/* Add to global pool.. */
		chd_dec_free_iodata(adp, temp, 0);
	}

	return 0;

kzalloc_fail:
	crystalhd_delete_elem_pool(adp);
elem_pool_fail:
	device_destroy(crystalhd_class, MKDEV(adp->chd_dec_major, 0));
device_create_fail:
	class_destroy(crystalhd_class);
class_create_fail:
	unregister_chrdev(adp->chd_dec_major, CRYSTALHD_API_NAME);
fail:
	return rc;
}

static void chd_dec_release_chdev(struct crystalhd_adp *adp)
{
	struct crystalhd_ioctl_data *temp = NULL;
	if (!adp)
		return;

	if (adp->chd_dec_major > 0) {
		/* unregister crystalhd class */
		device_destroy(crystalhd_class, MKDEV(adp->chd_dec_major, 0));
		unregister_chrdev(adp->chd_dec_major, CRYSTALHD_API_NAME);
		BCMLOG(BCMLOG_INFO, "released api device - %d\n",
		       adp->chd_dec_major);
		class_destroy(crystalhd_class);
	}
	adp->chd_dec_major = 0;

	/* Clear iodata pool.. */
	do {
		temp = chd_dec_alloc_iodata(adp, 0);
		kfree(temp);
	} while (temp);

	crystalhd_delete_elem_pool(adp);
}

static int chd_pci_reserve_mem(struct crystalhd_adp *pinfo)
{
	int rc;
	unsigned long bar2 = pci_resource_start(pinfo->pdev, 2);
	uint32_t mem_len   = pci_resource_len(pinfo->pdev, 2);
	unsigned long bar0 = pci_resource_start(pinfo->pdev, 0);
	uint32_t i2o_len   = pci_resource_len(pinfo->pdev, 0);

	BCMLOG(BCMLOG_SSTEP, "bar2:0x%lx-0x%08x  bar0:0x%lx-0x%08x\n",
	       bar2, mem_len, bar0, i2o_len);

	rc = check_mem_region(bar2, mem_len);
	if (rc) {
		BCMLOG_ERR("No valid mem region...\n");
		return -ENOMEM;
	}

	pinfo->addr = ioremap_nocache(bar2, mem_len);
	if (!pinfo->addr) {
		BCMLOG_ERR("Failed to remap mem region...\n");
		return -ENOMEM;
	}

	pinfo->pci_mem_start = bar2;
	pinfo->pci_mem_len   = mem_len;

	rc = check_mem_region(bar0, i2o_len);
	if (rc) {
		BCMLOG_ERR("No valid mem region...\n");
		return -ENOMEM;
	}

	pinfo->i2o_addr = ioremap_nocache(bar0, i2o_len);
	if (!pinfo->i2o_addr) {
		BCMLOG_ERR("Failed to remap mem region...\n");
		return -ENOMEM;
	}

	pinfo->pci_i2o_start = bar0;
	pinfo->pci_i2o_len   = i2o_len;

	rc = pci_request_regions(pinfo->pdev, pinfo->name);
	if (rc < 0) {
		BCMLOG_ERR("Region request failed: %d\n", rc);
		return rc;
	}

	BCMLOG(BCMLOG_SSTEP, "Mapped addr:0x%08lx  i2o_addr:0x%08lx\n",
	       (unsigned long)pinfo->addr, (unsigned long)pinfo->i2o_addr);

	return 0;
}

static void chd_pci_release_mem(struct crystalhd_adp *pinfo)
{
	if (!pinfo)
		return;

	if (pinfo->addr)
		iounmap(pinfo->addr);

	if (pinfo->i2o_addr)
		iounmap(pinfo->i2o_addr);

	pci_release_regions(pinfo->pdev);
}


static void chd_dec_pci_remove(struct pci_dev *pdev)
{
	struct crystalhd_adp *pinfo;
	enum BC_STATUS sts = BC_STS_SUCCESS;

	pinfo = pci_get_drvdata(pdev);
	if (!pinfo) {
		BCMLOG_ERR("could not get adp\n");
		return;
	}

	sts = crystalhd_delete_cmd_context(&pinfo->cmds);
	if (sts != BC_STS_SUCCESS)
		BCMLOG_ERR("cmd delete :%d\n", sts);

	chd_dec_release_chdev(pinfo);

	chd_dec_disable_int(pinfo);

	chd_pci_release_mem(pinfo);
	pci_disable_device(pinfo->pdev);

	kfree(pinfo);
	g_adp_info = NULL;
}

static int chd_dec_pci_probe(struct pci_dev *pdev,
			     const struct pci_device_id *entry)
{
	struct crystalhd_adp *pinfo;
	int rc;
	enum BC_STATUS sts = BC_STS_SUCCESS;

	BCMLOG(BCMLOG_DBG, "PCI_INFO: Vendor:0x%04x Device:0x%04x s_vendor:0x%04x s_device: 0x%04x\n",
	       pdev->vendor, pdev->device, pdev->subsystem_vendor,
	       pdev->subsystem_device);

	pinfo = kzalloc(sizeof(struct crystalhd_adp), GFP_KERNEL);
	if (!pinfo) {
		BCMLOG_ERR("Failed to allocate memory\n");
		return -ENOMEM;
	}

	pinfo->pdev = pdev;

	rc = pci_enable_device(pdev);
	if (rc) {
		BCMLOG_ERR("Failed to enable PCI device\n");
		goto err;
	}

	snprintf(pinfo->name, sizeof(pinfo->name), "crystalhd_pci_e:%d:%d:%d",
		 pdev->bus->number, PCI_SLOT(pdev->devfn),
		 PCI_FUNC(pdev->devfn));

	rc = chd_pci_reserve_mem(pinfo);
	if (rc) {
		BCMLOG_ERR("Failed to setup memory regions.\n");
		pci_disable_device(pdev);
		rc = -ENOMEM;
		goto err;
	}

	pinfo->present	= 1;
	pinfo->drv_data = entry->driver_data;

	/* Setup adapter level lock.. */
	spin_lock_init(&pinfo->lock);

	/* setup api stuff.. */
	chd_dec_init_chdev(pinfo);
	rc = chd_dec_enable_int(pinfo);
	if (rc) {
		BCMLOG_ERR("_enable_int err:%d\n", rc);
		pci_disable_device(pdev);
		rc = -ENODEV;
		goto err;
	}

	/* Set dma mask... */
	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) {
		pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
		pinfo->dmabits = 64;
	} else if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
		pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		pinfo->dmabits = 32;
	} else {
		BCMLOG_ERR("Unabled to setup DMA %d\n", rc);
		pci_disable_device(pdev);
		rc = -ENODEV;
		goto err;
	}

	sts = crystalhd_setup_cmd_context(&pinfo->cmds, pinfo);
	if (sts != BC_STS_SUCCESS) {
		BCMLOG_ERR("cmd setup :%d\n", sts);
		pci_disable_device(pdev);
		rc = -ENODEV;
		goto err;
	}

	pci_set_master(pdev);

	pci_set_drvdata(pdev, pinfo);

	g_adp_info = pinfo;

	return 0;

err:
	kfree(pinfo);
	return rc;
}

#ifdef CONFIG_PM
static int chd_dec_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct crystalhd_adp *adp;
	struct crystalhd_ioctl_data *temp;
	enum BC_STATUS sts = BC_STS_SUCCESS;

	adp = pci_get_drvdata(pdev);
	if (!adp) {
		BCMLOG_ERR("could not get adp\n");
		return -ENODEV;
	}

	temp = chd_dec_alloc_iodata(adp, false);
	if (!temp) {
		BCMLOG_ERR("could not get ioctl data\n");
		return -ENODEV;
	}

	sts = crystalhd_suspend(&adp->cmds, temp);
	if (sts != BC_STS_SUCCESS) {
		BCMLOG_ERR("BCM70012 Suspend %d\n", sts);
		return -ENODEV;
	}

	chd_dec_free_iodata(adp, temp, false);
	chd_dec_disable_int(adp);
	pci_save_state(pdev);

	/* Disable IO/bus master/irq router */
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));
	return 0;
}

static int chd_dec_pci_resume(struct pci_dev *pdev)
{
	struct crystalhd_adp *adp;
	enum BC_STATUS sts = BC_STS_SUCCESS;
	int rc;

	adp = pci_get_drvdata(pdev);
	if (!adp) {
		BCMLOG_ERR("could not get adp\n");
		return -ENODEV;
	}

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);

	/* device's irq possibly is changed, driver should take care */
	if (pci_enable_device(pdev)) {
		BCMLOG_ERR("Failed to enable PCI device\n");
		return 1;
	}

	pci_set_master(pdev);

	rc = chd_dec_enable_int(adp);
	if (rc) {
		BCMLOG_ERR("_enable_int err:%d\n", rc);
		pci_disable_device(pdev);
		return -ENODEV;
	}

	sts = crystalhd_resume(&adp->cmds);
	if (sts != BC_STS_SUCCESS) {
		BCMLOG_ERR("BCM70012 Resume %d\n", sts);
		pci_disable_device(pdev);
		return -ENODEV;
	}

	return 0;
}
#endif

static DEFINE_PCI_DEVICE_TABLE(chd_dec_pci_id_table) = {
	{ PCI_VDEVICE(BROADCOM, 0x1612), 8 },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, chd_dec_pci_id_table);

static struct pci_driver bc_chd_70012_driver = {
	.name     = "Broadcom 70012 Decoder",
	.probe    = chd_dec_pci_probe,
	.remove   = chd_dec_pci_remove,
	.id_table = chd_dec_pci_id_table,
#ifdef CONFIG_PM
	.suspend  = chd_dec_pci_suspend,
	.resume   = chd_dec_pci_resume
#endif
};

void chd_set_log_level(struct crystalhd_adp *adp, char *arg)
{
	if ((!arg) || (strlen(arg) < 3))
		g_linklog_level = BCMLOG_ERROR | BCMLOG_DATA;
	else if (!strncmp(arg, "sstep", 5))
		g_linklog_level = BCMLOG_INFO | BCMLOG_DATA | BCMLOG_DBG |
				  BCMLOG_SSTEP | BCMLOG_ERROR;
	else if (!strncmp(arg, "info", 4))
		g_linklog_level = BCMLOG_ERROR | BCMLOG_DATA | BCMLOG_INFO;
	else if (!strncmp(arg, "debug", 5))
		g_linklog_level = BCMLOG_ERROR | BCMLOG_DATA | BCMLOG_INFO |
				  BCMLOG_DBG;
	else if (!strncmp(arg, "pball", 5))
		g_linklog_level = 0xFFFFFFFF & ~(BCMLOG_SPINLOCK);
	else if (!strncmp(arg, "silent", 6))
		g_linklog_level = 0;
	else
		g_linklog_level = 0;
}

struct crystalhd_adp *chd_get_adp(void)
{
	return g_adp_info;
}

static int __init chd_dec_module_init(void)
{
	int rc;

	chd_set_log_level(NULL, "debug");
	BCMLOG(BCMLOG_DATA, "Loading crystalhd %d.%d.%d\n",
	       crystalhd_kmod_major, crystalhd_kmod_minor, crystalhd_kmod_rev);

	rc = pci_register_driver(&bc_chd_70012_driver);

	if (rc < 0)
		BCMLOG_ERR("Could not find any devices. err:%d\n", rc);

	return rc;
}
module_init(chd_dec_module_init);

static void __exit chd_dec_module_cleanup(void)
{
	BCMLOG(BCMLOG_DATA, "unloading crystalhd %d.%d.%d\n",
	       crystalhd_kmod_major, crystalhd_kmod_minor, crystalhd_kmod_rev);

	pci_unregister_driver(&bc_chd_70012_driver);
}
module_exit(chd_dec_module_cleanup);

MODULE_AUTHOR("Naren Sankar <nsankar@broadcom.com>");
MODULE_AUTHOR("Prasad Bolisetty <prasadb@broadcom.com>");
MODULE_DESCRIPTION(CRYSTAL_HD_NAME);
MODULE_LICENSE("GPL");
MODULE_ALIAS("bcm70012");
