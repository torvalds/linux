/*
 * Copyright (C) 2004 IBM Corporation
 *
 * Authors:
 * Leendert van Doorn <leendert@watson.ibm.com>
 * Dave Safford <safford@watson.ibm.com>
 * Reiner Sailer <sailer@watson.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * Maintained by: <tpmdd_devel@lists.sourceforge.net>
 *
 * Device driver for TCG/TCPA TPM (trusted platform module).
 * Specifications at www.trustedcomputinggroup.org	 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 * 
 * Note, the TPM chip is not interrupt driven (only polling)
 * and can have very long timeouts (minutes!). Hence the unusual
 * calls to schedule_timeout.
 *
 */

#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include "tpm.h"

#define	TPM_MINOR			224	/* officially assigned */

#define	TPM_BUFSIZE			2048

/* PCI configuration addresses */
#define	PCI_GEN_PMCON_1			0xA0
#define	PCI_GEN1_DEC			0xE4
#define	PCI_LPC_EN			0xE6
#define	PCI_GEN2_DEC			0xEC

static LIST_HEAD(tpm_chip_list);
static DEFINE_SPINLOCK(driver_lock);
static int dev_mask[32];

static void user_reader_timeout(unsigned long ptr)
{
	struct tpm_chip *chip = (struct tpm_chip *) ptr;

	down(&chip->buffer_mutex);
	atomic_set(&chip->data_pending, 0);
	memset(chip->data_buffer, 0, TPM_BUFSIZE);
	up(&chip->buffer_mutex);
}

void tpm_time_expired(unsigned long ptr)
{
	int *exp = (int *) ptr;
	*exp = 1;
}

EXPORT_SYMBOL_GPL(tpm_time_expired);

/*
 * Initialize the LPC bus and enable the TPM ports
 */
int tpm_lpc_bus_init(struct pci_dev *pci_dev, u16 base)
{
	u32 lpcenable, tmp;
	int is_lpcm = 0;

	switch (pci_dev->vendor) {
	case PCI_VENDOR_ID_INTEL:
		switch (pci_dev->device) {
		case PCI_DEVICE_ID_INTEL_82801CA_12:
		case PCI_DEVICE_ID_INTEL_82801DB_12:
			is_lpcm = 1;
			break;
		}
		/* init ICH (enable LPC) */
		pci_read_config_dword(pci_dev, PCI_GEN1_DEC, &lpcenable);
		lpcenable |= 0x20000000;
		pci_write_config_dword(pci_dev, PCI_GEN1_DEC, lpcenable);

		if (is_lpcm) {
			pci_read_config_dword(pci_dev, PCI_GEN1_DEC,
					      &lpcenable);
			if ((lpcenable & 0x20000000) == 0) {
				dev_err(&pci_dev->dev,
					"cannot enable LPC\n");
				return -ENODEV;
			}
		}

		/* initialize TPM registers */
		pci_read_config_dword(pci_dev, PCI_GEN2_DEC, &tmp);

		if (!is_lpcm)
			tmp = (tmp & 0xFFFF0000) | (base & 0xFFF0);
		else
			tmp =
			    (tmp & 0xFFFF0000) | (base & 0xFFF0) |
			    0x00000001;

		pci_write_config_dword(pci_dev, PCI_GEN2_DEC, tmp);

		if (is_lpcm) {
			pci_read_config_dword(pci_dev, PCI_GEN_PMCON_1,
					      &tmp);
			tmp |= 0x00000004;	/* enable CLKRUN */
			pci_write_config_dword(pci_dev, PCI_GEN_PMCON_1,
					       tmp);
		}
		tpm_write_index(0x0D, 0x55);	/* unlock 4F */
		tpm_write_index(0x0A, 0x00);	/* int disable */
		tpm_write_index(0x08, base);	/* base addr lo */
		tpm_write_index(0x09, (base & 0xFF00) >> 8);	/* base addr hi */
		tpm_write_index(0x0D, 0xAA);	/* lock 4F */
		break;
	case PCI_VENDOR_ID_AMD:
		/* nothing yet */
		break;
	}

	return 0;
}

EXPORT_SYMBOL_GPL(tpm_lpc_bus_init);

/*
 * Internal kernel interface to transmit TPM commands
 */
static ssize_t tpm_transmit(struct tpm_chip *chip, const char *buf,
			    size_t bufsiz)
{
	ssize_t len;
	u32 count;
	__be32 *native_size;

	native_size = (__force __be32 *) (buf + 2);
	count = be32_to_cpu(*native_size);

	if (count == 0)
		return -ENODATA;
	if (count > bufsiz) {
		dev_err(&chip->pci_dev->dev,
			"invalid count value %x %zx \n", count, bufsiz);
		return -E2BIG;
	}

	down(&chip->tpm_mutex);

	if ((len = chip->vendor->send(chip, (u8 *) buf, count)) < 0) {
		dev_err(&chip->pci_dev->dev,
			"tpm_transmit: tpm_send: error %zd\n", len);
		return len;
	}

	down(&chip->timer_manipulation_mutex);
	chip->time_expired = 0;
	init_timer(&chip->device_timer);
	chip->device_timer.function = tpm_time_expired;
	chip->device_timer.expires = jiffies + 2 * 60 * HZ;
	chip->device_timer.data = (unsigned long) &chip->time_expired;
	add_timer(&chip->device_timer);
	up(&chip->timer_manipulation_mutex);

	do {
		u8 status = inb(chip->vendor->base + 1);
		if ((status & chip->vendor->req_complete_mask) ==
		    chip->vendor->req_complete_val) {
			down(&chip->timer_manipulation_mutex);
			del_singleshot_timer_sync(&chip->device_timer);
			up(&chip->timer_manipulation_mutex);
			goto out_recv;
		}
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(TPM_TIMEOUT);
		rmb();
	} while (!chip->time_expired);


	chip->vendor->cancel(chip);
	dev_err(&chip->pci_dev->dev, "Time expired\n");
	up(&chip->tpm_mutex);
	return -EIO;

out_recv:
	len = chip->vendor->recv(chip, (u8 *) buf, bufsiz);
	if (len < 0)
		dev_err(&chip->pci_dev->dev,
			"tpm_transmit: tpm_recv: error %zd\n", len);
	up(&chip->tpm_mutex);
	return len;
}

#define TPM_DIGEST_SIZE 20
#define CAP_PCR_RESULT_SIZE 18
static u8 cap_pcr[] = {
	0, 193,			/* TPM_TAG_RQU_COMMAND */
	0, 0, 0, 22,		/* length */
	0, 0, 0, 101,		/* TPM_ORD_GetCapability */
	0, 0, 0, 5,
	0, 0, 0, 4,
	0, 0, 1, 1
};

#define READ_PCR_RESULT_SIZE 30
static u8 pcrread[] = {
	0, 193,			/* TPM_TAG_RQU_COMMAND */
	0, 0, 0, 14,		/* length */
	0, 0, 0, 21,		/* TPM_ORD_PcrRead */
	0, 0, 0, 0		/* PCR index */
};

static ssize_t show_pcrs(struct device *dev, char *buf)
{
	u8 data[READ_PCR_RESULT_SIZE];
	ssize_t len;
	int i, j, index, num_pcrs;
	char *str = buf;

	struct tpm_chip *chip =
	    pci_get_drvdata(container_of(dev, struct pci_dev, dev));
	if (chip == NULL)
		return -ENODEV;

	memcpy(data, cap_pcr, sizeof(cap_pcr));
	if ((len = tpm_transmit(chip, data, sizeof(data)))
	    < CAP_PCR_RESULT_SIZE)
		return len;

	num_pcrs = be32_to_cpu(*((__force __be32 *) (data + 14)));

	for (i = 0; i < num_pcrs; i++) {
		memcpy(data, pcrread, sizeof(pcrread));
		index = cpu_to_be32(i);
		memcpy(data + 10, &index, 4);
		if ((len = tpm_transmit(chip, data, sizeof(data)))
		    < READ_PCR_RESULT_SIZE)
			return len;
		str += sprintf(str, "PCR-%02d: ", i);
		for (j = 0; j < TPM_DIGEST_SIZE; j++)
			str += sprintf(str, "%02X ", *(data + 10 + j));
		str += sprintf(str, "\n");
	}
	return str - buf;
}

static DEVICE_ATTR(pcrs, S_IRUGO, show_pcrs, NULL);

#define  READ_PUBEK_RESULT_SIZE 314
static u8 readpubek[] = {
	0, 193,			/* TPM_TAG_RQU_COMMAND */
	0, 0, 0, 30,		/* length */
	0, 0, 0, 124,		/* TPM_ORD_ReadPubek */
};

static ssize_t show_pubek(struct device *dev, char *buf)
{
	u8 data[READ_PUBEK_RESULT_SIZE];
	ssize_t len;
	__be32 *native_val;
	int i;
	char *str = buf;

	struct tpm_chip *chip =
	    pci_get_drvdata(container_of(dev, struct pci_dev, dev));
	if (chip == NULL)
		return -ENODEV;

	memcpy(data, readpubek, sizeof(readpubek));
	memset(data + sizeof(readpubek), 0, 20);	/* zero nonce */

	if ((len = tpm_transmit(chip, data, sizeof(data))) <
	    READ_PUBEK_RESULT_SIZE)
		return len;

	/* 
	   ignore header 10 bytes
	   algorithm 32 bits (1 == RSA )
	   encscheme 16 bits
	   sigscheme 16 bits
	   parameters (RSA 12->bytes: keybit, #primes, expbit)  
	   keylenbytes 32 bits
	   256 byte modulus
	   ignore checksum 20 bytes
	 */

	native_val = (__force __be32 *) (data + 34);

	str +=
	    sprintf(str,
		    "Algorithm: %02X %02X %02X %02X\nEncscheme: %02X %02X\n"
		    "Sigscheme: %02X %02X\nParameters: %02X %02X %02X %02X"
		    " %02X %02X %02X %02X %02X %02X %02X %02X\n"
		    "Modulus length: %d\nModulus: \n",
		    data[10], data[11], data[12], data[13], data[14],
		    data[15], data[16], data[17], data[22], data[23],
		    data[24], data[25], data[26], data[27], data[28],
		    data[29], data[30], data[31], data[32], data[33],
		    be32_to_cpu(*native_val)
	    );

	for (i = 0; i < 256; i++) {
		str += sprintf(str, "%02X ", data[i + 39]);
		if ((i + 1) % 16 == 0)
			str += sprintf(str, "\n");
	}
	return str - buf;
}

static DEVICE_ATTR(pubek, S_IRUGO, show_pubek, NULL);

#define CAP_VER_RESULT_SIZE 18
static u8 cap_version[] = {
	0, 193,			/* TPM_TAG_RQU_COMMAND */
	0, 0, 0, 18,		/* length */
	0, 0, 0, 101,		/* TPM_ORD_GetCapability */
	0, 0, 0, 6,
	0, 0, 0, 0
};

#define CAP_MANUFACTURER_RESULT_SIZE 18
static u8 cap_manufacturer[] = {
	0, 193,			/* TPM_TAG_RQU_COMMAND */
	0, 0, 0, 22,		/* length */
	0, 0, 0, 101,		/* TPM_ORD_GetCapability */
	0, 0, 0, 5,
	0, 0, 0, 4,
	0, 0, 1, 3
};

static ssize_t show_caps(struct device *dev, char *buf)
{
	u8 data[READ_PUBEK_RESULT_SIZE];
	ssize_t len;
	char *str = buf;

	struct tpm_chip *chip =
	    pci_get_drvdata(container_of(dev, struct pci_dev, dev));
	if (chip == NULL)
		return -ENODEV;

	memcpy(data, cap_manufacturer, sizeof(cap_manufacturer));

	if ((len = tpm_transmit(chip, data, sizeof(data))) <
	    CAP_MANUFACTURER_RESULT_SIZE)
		return len;

	str += sprintf(str, "Manufacturer: 0x%x\n",
		       be32_to_cpu(*(data + 14)));

	memcpy(data, cap_version, sizeof(cap_version));

	if ((len = tpm_transmit(chip, data, sizeof(data))) <
	    CAP_VER_RESULT_SIZE)
		return len;

	str +=
	    sprintf(str, "TCG version: %d.%d\nFirmware version: %d.%d\n",
		    (int) data[14], (int) data[15], (int) data[16],
		    (int) data[17]);

	return str - buf;
}

static DEVICE_ATTR(caps, S_IRUGO, show_caps, NULL);

/*
 * Device file system interface to the TPM
 */
int tpm_open(struct inode *inode, struct file *file)
{
	int rc = 0, minor = iminor(inode);
	struct tpm_chip *chip = NULL, *pos;

	spin_lock(&driver_lock);

	list_for_each_entry(pos, &tpm_chip_list, list) {
		if (pos->vendor->miscdev.minor == minor) {
			chip = pos;
			break;
		}
	}

	if (chip == NULL) {
		rc = -ENODEV;
		goto err_out;
	}

	if (chip->num_opens) {
		dev_dbg(&chip->pci_dev->dev,
			"Another process owns this TPM\n");
		rc = -EBUSY;
		goto err_out;
	}

	chip->num_opens++;
	pci_dev_get(chip->pci_dev);

	spin_unlock(&driver_lock);

	chip->data_buffer = kmalloc(TPM_BUFSIZE * sizeof(u8), GFP_KERNEL);
	if (chip->data_buffer == NULL) {
		chip->num_opens--;
		pci_dev_put(chip->pci_dev);
		return -ENOMEM;
	}

	atomic_set(&chip->data_pending, 0);

	file->private_data = chip;
	return 0;

err_out:
	spin_unlock(&driver_lock);
	return rc;
}

EXPORT_SYMBOL_GPL(tpm_open);

int tpm_release(struct inode *inode, struct file *file)
{
	struct tpm_chip *chip = file->private_data;
	
	file->private_data = NULL;

	spin_lock(&driver_lock);
	chip->num_opens--;
	spin_unlock(&driver_lock);

	down(&chip->timer_manipulation_mutex);
	if (timer_pending(&chip->user_read_timer))
		del_singleshot_timer_sync(&chip->user_read_timer);
	else if (timer_pending(&chip->device_timer))
		del_singleshot_timer_sync(&chip->device_timer);
	up(&chip->timer_manipulation_mutex);

	kfree(chip->data_buffer);
	atomic_set(&chip->data_pending, 0);

	pci_dev_put(chip->pci_dev);
	return 0;
}

EXPORT_SYMBOL_GPL(tpm_release);

ssize_t tpm_write(struct file * file, const char __user * buf,
		  size_t size, loff_t * off)
{
	struct tpm_chip *chip = file->private_data;
	int in_size = size, out_size;

	/* cannot perform a write until the read has cleared
	   either via tpm_read or a user_read_timer timeout */
	while (atomic_read(&chip->data_pending) != 0) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(TPM_TIMEOUT);
	}

	down(&chip->buffer_mutex);

	if (in_size > TPM_BUFSIZE)
		in_size = TPM_BUFSIZE;

	if (copy_from_user
	    (chip->data_buffer, (void __user *) buf, in_size)) {
		up(&chip->buffer_mutex);
		return -EFAULT;
	}

	/* atomic tpm command send and result receive */
	out_size = tpm_transmit(chip, chip->data_buffer, TPM_BUFSIZE);

	atomic_set(&chip->data_pending, out_size);
	up(&chip->buffer_mutex);

	/* Set a timeout by which the reader must come claim the result */
	down(&chip->timer_manipulation_mutex);
	init_timer(&chip->user_read_timer);
	chip->user_read_timer.function = user_reader_timeout;
	chip->user_read_timer.data = (unsigned long) chip;
	chip->user_read_timer.expires = jiffies + (60 * HZ);
	add_timer(&chip->user_read_timer);
	up(&chip->timer_manipulation_mutex);

	return in_size;
}

EXPORT_SYMBOL_GPL(tpm_write);

ssize_t tpm_read(struct file * file, char __user * buf,
		 size_t size, loff_t * off)
{
	struct tpm_chip *chip = file->private_data;
	int ret_size = -ENODATA;

	if (atomic_read(&chip->data_pending) != 0) {	/* Result available */
		down(&chip->timer_manipulation_mutex);
		del_singleshot_timer_sync(&chip->user_read_timer);
		up(&chip->timer_manipulation_mutex);

		down(&chip->buffer_mutex);

		ret_size = atomic_read(&chip->data_pending);
		atomic_set(&chip->data_pending, 0);

		if (ret_size == 0)	/* timeout just occurred */
			ret_size = -ETIME;
		else if (ret_size > 0) {	/* relay data */
			if (size < ret_size)
				ret_size = size;

			if (copy_to_user((void __user *) buf,
					 chip->data_buffer, ret_size)) {
				ret_size = -EFAULT;
			}
		}
		up(&chip->buffer_mutex);
	}

	return ret_size;
}

EXPORT_SYMBOL_GPL(tpm_read);

void __devexit tpm_remove(struct pci_dev *pci_dev)
{
	struct tpm_chip *chip = pci_get_drvdata(pci_dev);

	if (chip == NULL) {
		dev_err(&pci_dev->dev, "No device data found\n");
		return;
	}

	spin_lock(&driver_lock);

	list_del(&chip->list);

	spin_unlock(&driver_lock);

	pci_set_drvdata(pci_dev, NULL);
	misc_deregister(&chip->vendor->miscdev);

	device_remove_file(&pci_dev->dev, &dev_attr_pubek);
	device_remove_file(&pci_dev->dev, &dev_attr_pcrs);
	device_remove_file(&pci_dev->dev, &dev_attr_caps);

	pci_disable_device(pci_dev);

	dev_mask[chip->dev_num / 32] &= !(1 << (chip->dev_num % 32));

	kfree(chip);

	pci_dev_put(pci_dev);
}

EXPORT_SYMBOL_GPL(tpm_remove);

static u8 savestate[] = {
	0, 193,			/* TPM_TAG_RQU_COMMAND */
	0, 0, 0, 10,		/* blob length (in bytes) */
	0, 0, 0, 152		/* TPM_ORD_SaveState */
};

/*
 * We are about to suspend. Save the TPM state
 * so that it can be restored.
 */
int tpm_pm_suspend(struct pci_dev *pci_dev, pm_message_t pm_state)
{
	struct tpm_chip *chip = pci_get_drvdata(pci_dev);
	if (chip == NULL)
		return -ENODEV;

	tpm_transmit(chip, savestate, sizeof(savestate));
	return 0;
}

EXPORT_SYMBOL_GPL(tpm_pm_suspend);

/*
 * Resume from a power safe. The BIOS already restored
 * the TPM state.
 */
int tpm_pm_resume(struct pci_dev *pci_dev)
{
	struct tpm_chip *chip = pci_get_drvdata(pci_dev);

	if (chip == NULL)
		return -ENODEV;

	spin_lock(&driver_lock);
	tpm_lpc_bus_init(pci_dev, chip->vendor->base);
	spin_unlock(&driver_lock);

	return 0;
}

EXPORT_SYMBOL_GPL(tpm_pm_resume);

/*
 * Called from tpm_<specific>.c probe function only for devices 
 * the driver has determined it should claim.  Prior to calling
 * this function the specific probe function has called pci_enable_device
 * upon errant exit from this function specific probe function should call
 * pci_disable_device
 */
int tpm_register_hardware(struct pci_dev *pci_dev,
			  struct tpm_vendor_specific *entry)
{
	char devname[7];
	struct tpm_chip *chip;
	int i, j;

	/* Driver specific per-device data */
	chip = kmalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	memset(chip, 0, sizeof(struct tpm_chip));

	init_MUTEX(&chip->buffer_mutex);
	init_MUTEX(&chip->tpm_mutex);
	init_MUTEX(&chip->timer_manipulation_mutex);
	INIT_LIST_HEAD(&chip->list);

	chip->vendor = entry;

	chip->dev_num = -1;

	for (i = 0; i < 32; i++)
		for (j = 0; j < 8; j++)
			if ((dev_mask[i] & (1 << j)) == 0) {
				chip->dev_num = i * 32 + j;
				dev_mask[i] |= 1 << j;
				goto dev_num_search_complete;
			}

dev_num_search_complete:
	if (chip->dev_num < 0) {
		dev_err(&pci_dev->dev,
			"No available tpm device numbers\n");
		kfree(chip);
		return -ENODEV;
	} else if (chip->dev_num == 0)
		chip->vendor->miscdev.minor = TPM_MINOR;
	else
		chip->vendor->miscdev.minor = MISC_DYNAMIC_MINOR;

	snprintf(devname, sizeof(devname), "%s%d", "tpm", chip->dev_num);
	chip->vendor->miscdev.name = devname;

	chip->vendor->miscdev.dev = &(pci_dev->dev);
	chip->pci_dev = pci_dev_get(pci_dev);

	if (misc_register(&chip->vendor->miscdev)) {
		dev_err(&chip->pci_dev->dev,
			"unable to misc_register %s, minor %d\n",
			chip->vendor->miscdev.name,
			chip->vendor->miscdev.minor);
		pci_dev_put(pci_dev);
		kfree(chip);
		dev_mask[i] &= !(1 << j);
		return -ENODEV;
	}

	pci_set_drvdata(pci_dev, chip);

	list_add(&chip->list, &tpm_chip_list);

	device_create_file(&pci_dev->dev, &dev_attr_pubek);
	device_create_file(&pci_dev->dev, &dev_attr_pcrs);
	device_create_file(&pci_dev->dev, &dev_attr_caps);

	return 0;
}

EXPORT_SYMBOL_GPL(tpm_register_hardware);

static int __init init_tpm(void)
{
	return 0;
}

static void __exit cleanup_tpm(void)
{

}

module_init(init_tpm);
module_exit(cleanup_tpm);

MODULE_AUTHOR("Leendert van Doorn (leendert@watson.ibm.com)");
MODULE_DESCRIPTION("TPM Driver");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
