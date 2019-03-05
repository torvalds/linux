/*
 * IOSF-SB MailBox Interface Driver
 * Copyright (c) 2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 *
 * The IOSF-SB is a fabric bus available on Atom based SOC's that uses a
 * mailbox interface (MBI) to communicate with multiple devices. This
 * driver implements access to this interface for those platforms that can
 * enumerate the device using PCI.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/pci.h>
#include <linux/debugfs.h>
#include <linux/capability.h>
#include <linux/pm_qos.h>

#include <asm/iosf_mbi.h>

#define PCI_DEVICE_ID_INTEL_BAYTRAIL		0x0F00
#define PCI_DEVICE_ID_INTEL_BRASWELL		0x2280
#define PCI_DEVICE_ID_INTEL_QUARK_X1000		0x0958
#define PCI_DEVICE_ID_INTEL_TANGIER		0x1170

static struct pci_dev *mbi_pdev;
static DEFINE_SPINLOCK(iosf_mbi_lock);

/**************** Generic iosf_mbi access helpers ****************/

static inline u32 iosf_mbi_form_mcr(u8 op, u8 port, u8 offset)
{
	return (op << 24) | (port << 16) | (offset << 8) | MBI_ENABLE;
}

static int iosf_mbi_pci_read_mdr(u32 mcrx, u32 mcr, u32 *mdr)
{
	int result;

	if (!mbi_pdev)
		return -ENODEV;

	if (mcrx) {
		result = pci_write_config_dword(mbi_pdev, MBI_MCRX_OFFSET,
						mcrx);
		if (result < 0)
			goto fail_read;
	}

	result = pci_write_config_dword(mbi_pdev, MBI_MCR_OFFSET, mcr);
	if (result < 0)
		goto fail_read;

	result = pci_read_config_dword(mbi_pdev, MBI_MDR_OFFSET, mdr);
	if (result < 0)
		goto fail_read;

	return 0;

fail_read:
	dev_err(&mbi_pdev->dev, "PCI config access failed with %d\n", result);
	return result;
}

static int iosf_mbi_pci_write_mdr(u32 mcrx, u32 mcr, u32 mdr)
{
	int result;

	if (!mbi_pdev)
		return -ENODEV;

	result = pci_write_config_dword(mbi_pdev, MBI_MDR_OFFSET, mdr);
	if (result < 0)
		goto fail_write;

	if (mcrx) {
		result = pci_write_config_dword(mbi_pdev, MBI_MCRX_OFFSET,
						mcrx);
		if (result < 0)
			goto fail_write;
	}

	result = pci_write_config_dword(mbi_pdev, MBI_MCR_OFFSET, mcr);
	if (result < 0)
		goto fail_write;

	return 0;

fail_write:
	dev_err(&mbi_pdev->dev, "PCI config access failed with %d\n", result);
	return result;
}

int iosf_mbi_read(u8 port, u8 opcode, u32 offset, u32 *mdr)
{
	u32 mcr, mcrx;
	unsigned long flags;
	int ret;

	/* Access to the GFX unit is handled by GPU code */
	if (port == BT_MBI_UNIT_GFX) {
		WARN_ON(1);
		return -EPERM;
	}

	mcr = iosf_mbi_form_mcr(opcode, port, offset & MBI_MASK_LO);
	mcrx = offset & MBI_MASK_HI;

	spin_lock_irqsave(&iosf_mbi_lock, flags);
	ret = iosf_mbi_pci_read_mdr(mcrx, mcr, mdr);
	spin_unlock_irqrestore(&iosf_mbi_lock, flags);

	return ret;
}
EXPORT_SYMBOL(iosf_mbi_read);

int iosf_mbi_write(u8 port, u8 opcode, u32 offset, u32 mdr)
{
	u32 mcr, mcrx;
	unsigned long flags;
	int ret;

	/* Access to the GFX unit is handled by GPU code */
	if (port == BT_MBI_UNIT_GFX) {
		WARN_ON(1);
		return -EPERM;
	}

	mcr = iosf_mbi_form_mcr(opcode, port, offset & MBI_MASK_LO);
	mcrx = offset & MBI_MASK_HI;

	spin_lock_irqsave(&iosf_mbi_lock, flags);
	ret = iosf_mbi_pci_write_mdr(mcrx, mcr, mdr);
	spin_unlock_irqrestore(&iosf_mbi_lock, flags);

	return ret;
}
EXPORT_SYMBOL(iosf_mbi_write);

int iosf_mbi_modify(u8 port, u8 opcode, u32 offset, u32 mdr, u32 mask)
{
	u32 mcr, mcrx;
	u32 value;
	unsigned long flags;
	int ret;

	/* Access to the GFX unit is handled by GPU code */
	if (port == BT_MBI_UNIT_GFX) {
		WARN_ON(1);
		return -EPERM;
	}

	mcr = iosf_mbi_form_mcr(opcode, port, offset & MBI_MASK_LO);
	mcrx = offset & MBI_MASK_HI;

	spin_lock_irqsave(&iosf_mbi_lock, flags);

	/* Read current mdr value */
	ret = iosf_mbi_pci_read_mdr(mcrx, mcr & MBI_RD_MASK, &value);
	if (ret < 0) {
		spin_unlock_irqrestore(&iosf_mbi_lock, flags);
		return ret;
	}

	/* Apply mask */
	value &= ~mask;
	mdr &= mask;
	value |= mdr;

	/* Write back */
	ret = iosf_mbi_pci_write_mdr(mcrx, mcr | MBI_WR_MASK, value);

	spin_unlock_irqrestore(&iosf_mbi_lock, flags);

	return ret;
}
EXPORT_SYMBOL(iosf_mbi_modify);

bool iosf_mbi_available(void)
{
	/* Mbi isn't hot-pluggable. No remove routine is provided */
	return mbi_pdev;
}
EXPORT_SYMBOL(iosf_mbi_available);

/*
 **************** P-Unit/kernel shared I2C bus arbritration ****************
 *
 * Some Bay Trail and Cherry Trail devices have the P-Unit and us (the kernel)
 * share a single I2C bus to the PMIC. Below are helpers to arbitrate the
 * accesses between the kernel and the P-Unit.
 *
 * See arch/x86/include/asm/iosf_mbi.h for kernel-doc text for each function.
 */

#define SEMAPHORE_TIMEOUT		500
#define PUNIT_SEMAPHORE_BYT		0x7
#define PUNIT_SEMAPHORE_CHT		0x10e
#define PUNIT_SEMAPHORE_BIT		BIT(0)
#define PUNIT_SEMAPHORE_ACQUIRE		BIT(1)

static DEFINE_MUTEX(iosf_mbi_punit_mutex);
static DEFINE_MUTEX(iosf_mbi_block_punit_i2c_access_count_mutex);
static BLOCKING_NOTIFIER_HEAD(iosf_mbi_pmic_bus_access_notifier);
static u32 iosf_mbi_block_punit_i2c_access_count;
static u32 iosf_mbi_sem_address;
static unsigned long iosf_mbi_sem_acquired;
static struct pm_qos_request iosf_mbi_pm_qos;

void iosf_mbi_punit_acquire(void)
{
	mutex_lock(&iosf_mbi_punit_mutex);
}
EXPORT_SYMBOL(iosf_mbi_punit_acquire);

void iosf_mbi_punit_release(void)
{
	mutex_unlock(&iosf_mbi_punit_mutex);
}
EXPORT_SYMBOL(iosf_mbi_punit_release);

static int iosf_mbi_get_sem(u32 *sem)
{
	int ret;

	ret = iosf_mbi_read(BT_MBI_UNIT_PMC, MBI_REG_READ,
			    iosf_mbi_sem_address, sem);
	if (ret) {
		dev_err(&mbi_pdev->dev, "Error P-Unit semaphore read failed\n");
		return ret;
	}

	*sem &= PUNIT_SEMAPHORE_BIT;
	return 0;
}

static void iosf_mbi_reset_semaphore(void)
{
	if (iosf_mbi_modify(BT_MBI_UNIT_PMC, MBI_REG_READ,
			    iosf_mbi_sem_address, 0, PUNIT_SEMAPHORE_BIT))
		dev_err(&mbi_pdev->dev, "Error P-Unit semaphore reset failed\n");

	pm_qos_update_request(&iosf_mbi_pm_qos, PM_QOS_DEFAULT_VALUE);

	blocking_notifier_call_chain(&iosf_mbi_pmic_bus_access_notifier,
				     MBI_PMIC_BUS_ACCESS_END, NULL);
}

/*
 * This function blocks P-Unit accesses to the PMIC I2C bus, so that kernel
 * I2C code, such as e.g. a fuel-gauge driver, can access it safely.
 *
 * This function may be called by I2C controller code while an I2C driver has
 * already blocked P-Unit accesses because it wants them blocked over multiple
 * i2c-transfers, for e.g. read-modify-write of an I2C client register.
 *
 * The P-Unit accesses already being blocked is tracked through the
 * iosf_mbi_block_punit_i2c_access_count variable which is protected by the
 * iosf_mbi_block_punit_i2c_access_count_mutex this mutex is hold for the
 * entire duration of the function.
 *
 * If access is not blocked yet, this function takes the following steps:
 *
 * 1) Some code sends request to the P-Unit which make it access the PMIC
 *    I2C bus. Testing has shown that the P-Unit does not check its internal
 *    PMIC bus semaphore for these requests. Callers of these requests call
 *    iosf_mbi_punit_acquire()/_release() around their P-Unit accesses, these
 *    functions lock/unlock the iosf_mbi_punit_mutex.
 *    As the first step we lock the iosf_mbi_punit_mutex, to wait for any in
 *    flight requests to finish and to block any new requests.
 *
 * 2) Some code makes such P-Unit requests from atomic contexts where it
 *    cannot call iosf_mbi_punit_acquire() as that may sleep.
 *    As the second step we call a notifier chain which allows any code
 *    needing P-Unit resources from atomic context to acquire them before
 *    we take control over the PMIC I2C bus.
 *
 * 3) When CPU cores enter C6 or C7 the P-Unit needs to talk to the PMIC
 *    if this happens while the kernel itself is accessing the PMIC I2C bus
 *    the SoC hangs.
 *    As the third step we call pm_qos_update_request() to disallow the CPU
 *    to enter C6 or C7.
 *
 * 4) The P-Unit has a PMIC bus semaphore which we can request to stop
 *    autonomous P-Unit tasks from accessing the PMIC I2C bus while we hold it.
 *    As the fourth and final step we request this semaphore and wait for our
 *    request to be acknowledged.
 */
int iosf_mbi_block_punit_i2c_access(void)
{
	unsigned long start, end;
	int ret = 0;
	u32 sem;

	if (WARN_ON(!mbi_pdev || !iosf_mbi_sem_address))
		return -ENXIO;

	mutex_lock(&iosf_mbi_block_punit_i2c_access_count_mutex);

	if (iosf_mbi_block_punit_i2c_access_count > 0)
		goto success;

	mutex_lock(&iosf_mbi_punit_mutex);
	blocking_notifier_call_chain(&iosf_mbi_pmic_bus_access_notifier,
				     MBI_PMIC_BUS_ACCESS_BEGIN, NULL);

	/*
	 * Disallow the CPU to enter C6 or C7 state, entering these states
	 * requires the P-Unit to talk to the PMIC and if this happens while
	 * we're holding the semaphore, the SoC hangs.
	 */
	pm_qos_update_request(&iosf_mbi_pm_qos, 0);

	/* host driver writes to side band semaphore register */
	ret = iosf_mbi_write(BT_MBI_UNIT_PMC, MBI_REG_WRITE,
			     iosf_mbi_sem_address, PUNIT_SEMAPHORE_ACQUIRE);
	if (ret) {
		dev_err(&mbi_pdev->dev, "Error P-Unit semaphore request failed\n");
		goto error;
	}

	/* host driver waits for bit 0 to be set in semaphore register */
	start = jiffies;
	end = start + msecs_to_jiffies(SEMAPHORE_TIMEOUT);
	do {
		ret = iosf_mbi_get_sem(&sem);
		if (!ret && sem) {
			iosf_mbi_sem_acquired = jiffies;
			dev_dbg(&mbi_pdev->dev, "P-Unit semaphore acquired after %ums\n",
				jiffies_to_msecs(jiffies - start));
			/*
			 * Success, keep iosf_mbi_punit_mutex locked till
			 * iosf_mbi_unblock_punit_i2c_access() gets called.
			 */
			goto success;
		}

		usleep_range(1000, 2000);
	} while (time_before(jiffies, end));

	ret = -ETIMEDOUT;
	dev_err(&mbi_pdev->dev, "Error P-Unit semaphore timed out, resetting\n");
error:
	iosf_mbi_reset_semaphore();
	mutex_unlock(&iosf_mbi_punit_mutex);

	if (!iosf_mbi_get_sem(&sem))
		dev_err(&mbi_pdev->dev, "P-Unit semaphore: %d\n", sem);
success:
	if (!WARN_ON(ret))
		iosf_mbi_block_punit_i2c_access_count++;

	mutex_unlock(&iosf_mbi_block_punit_i2c_access_count_mutex);

	return ret;
}
EXPORT_SYMBOL(iosf_mbi_block_punit_i2c_access);

void iosf_mbi_unblock_punit_i2c_access(void)
{
	mutex_lock(&iosf_mbi_block_punit_i2c_access_count_mutex);

	iosf_mbi_block_punit_i2c_access_count--;
	if (iosf_mbi_block_punit_i2c_access_count == 0) {
		iosf_mbi_reset_semaphore();
		mutex_unlock(&iosf_mbi_punit_mutex);
		dev_dbg(&mbi_pdev->dev, "punit semaphore held for %ums\n",
			jiffies_to_msecs(jiffies - iosf_mbi_sem_acquired));
	}

	mutex_unlock(&iosf_mbi_block_punit_i2c_access_count_mutex);
}
EXPORT_SYMBOL(iosf_mbi_unblock_punit_i2c_access);

int iosf_mbi_register_pmic_bus_access_notifier(struct notifier_block *nb)
{
	int ret;

	/* Wait for the bus to go inactive before registering */
	mutex_lock(&iosf_mbi_punit_mutex);
	ret = blocking_notifier_chain_register(
				&iosf_mbi_pmic_bus_access_notifier, nb);
	mutex_unlock(&iosf_mbi_punit_mutex);

	return ret;
}
EXPORT_SYMBOL(iosf_mbi_register_pmic_bus_access_notifier);

int iosf_mbi_unregister_pmic_bus_access_notifier_unlocked(
	struct notifier_block *nb)
{
	iosf_mbi_assert_punit_acquired();

	return blocking_notifier_chain_unregister(
				&iosf_mbi_pmic_bus_access_notifier, nb);
}
EXPORT_SYMBOL(iosf_mbi_unregister_pmic_bus_access_notifier_unlocked);

int iosf_mbi_unregister_pmic_bus_access_notifier(struct notifier_block *nb)
{
	int ret;

	/* Wait for the bus to go inactive before unregistering */
	mutex_lock(&iosf_mbi_punit_mutex);
	ret = iosf_mbi_unregister_pmic_bus_access_notifier_unlocked(nb);
	mutex_unlock(&iosf_mbi_punit_mutex);

	return ret;
}
EXPORT_SYMBOL(iosf_mbi_unregister_pmic_bus_access_notifier);

void iosf_mbi_assert_punit_acquired(void)
{
	WARN_ON(!mutex_is_locked(&iosf_mbi_punit_mutex));
}
EXPORT_SYMBOL(iosf_mbi_assert_punit_acquired);

/**************** iosf_mbi debug code ****************/

#ifdef CONFIG_IOSF_MBI_DEBUG
static u32	dbg_mdr;
static u32	dbg_mcr;
static u32	dbg_mcrx;

static int mcr_get(void *data, u64 *val)
{
	*val = *(u32 *)data;
	return 0;
}

static int mcr_set(void *data, u64 val)
{
	u8 command = ((u32)val & 0xFF000000) >> 24,
	   port	   = ((u32)val & 0x00FF0000) >> 16,
	   offset  = ((u32)val & 0x0000FF00) >> 8;
	int err;

	*(u32 *)data = val;

	if (!capable(CAP_SYS_RAWIO))
		return -EACCES;

	if (command & 1u)
		err = iosf_mbi_write(port,
			       command,
			       dbg_mcrx | offset,
			       dbg_mdr);
	else
		err = iosf_mbi_read(port,
			      command,
			      dbg_mcrx | offset,
			      &dbg_mdr);

	return err;
}
DEFINE_SIMPLE_ATTRIBUTE(iosf_mcr_fops, mcr_get, mcr_set , "%llx\n");

static struct dentry *iosf_dbg;

static void iosf_sideband_debug_init(void)
{
	struct dentry *d;

	iosf_dbg = debugfs_create_dir("iosf_sb", NULL);
	if (IS_ERR_OR_NULL(iosf_dbg))
		return;

	/* mdr */
	d = debugfs_create_x32("mdr", 0660, iosf_dbg, &dbg_mdr);
	if (!d)
		goto cleanup;

	/* mcrx */
	d = debugfs_create_x32("mcrx", 0660, iosf_dbg, &dbg_mcrx);
	if (!d)
		goto cleanup;

	/* mcr - initiates mailbox tranaction */
	d = debugfs_create_file("mcr", 0660, iosf_dbg, &dbg_mcr, &iosf_mcr_fops);
	if (!d)
		goto cleanup;

	return;

cleanup:
	debugfs_remove_recursive(d);
}

static void iosf_debugfs_init(void)
{
	iosf_sideband_debug_init();
}

static void iosf_debugfs_remove(void)
{
	debugfs_remove_recursive(iosf_dbg);
}
#else
static inline void iosf_debugfs_init(void) { }
static inline void iosf_debugfs_remove(void) { }
#endif /* CONFIG_IOSF_MBI_DEBUG */

static int iosf_mbi_probe(struct pci_dev *pdev,
			  const struct pci_device_id *dev_id)
{
	int ret;

	ret = pci_enable_device(pdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "error: could not enable device\n");
		return ret;
	}

	mbi_pdev = pci_dev_get(pdev);
	iosf_mbi_sem_address = dev_id->driver_data;

	return 0;
}

static const struct pci_device_id iosf_mbi_pci_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, BAYTRAIL, PUNIT_SEMAPHORE_BYT) },
	{ PCI_DEVICE_DATA(INTEL, BRASWELL, PUNIT_SEMAPHORE_CHT) },
	{ PCI_DEVICE_DATA(INTEL, QUARK_X1000, 0) },
	{ PCI_DEVICE_DATA(INTEL, TANGIER, 0) },
	{ 0, },
};
MODULE_DEVICE_TABLE(pci, iosf_mbi_pci_ids);

static struct pci_driver iosf_mbi_pci_driver = {
	.name		= "iosf_mbi_pci",
	.probe		= iosf_mbi_probe,
	.id_table	= iosf_mbi_pci_ids,
};

static int __init iosf_mbi_init(void)
{
	iosf_debugfs_init();

	pm_qos_add_request(&iosf_mbi_pm_qos, PM_QOS_CPU_DMA_LATENCY,
			   PM_QOS_DEFAULT_VALUE);

	return pci_register_driver(&iosf_mbi_pci_driver);
}

static void __exit iosf_mbi_exit(void)
{
	iosf_debugfs_remove();

	pci_unregister_driver(&iosf_mbi_pci_driver);
	pci_dev_put(mbi_pdev);
	mbi_pdev = NULL;

	pm_qos_remove_request(&iosf_mbi_pm_qos);
}

module_init(iosf_mbi_init);
module_exit(iosf_mbi_exit);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("IOSF Mailbox Interface accessor");
MODULE_LICENSE("GPL v2");
