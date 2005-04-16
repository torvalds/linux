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
 */
#include <linux/module.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>

#define TPM_TIMEOUT msecs_to_jiffies(5)

/* TPM addresses */
#define	TPM_ADDR			0x4E
#define	TPM_DATA			0x4F

struct tpm_chip;

struct tpm_vendor_specific {
	u8 req_complete_mask;
	u8 req_complete_val;
	u16 base;		/* TPM base address */

	int (*recv) (struct tpm_chip *, u8 *, size_t);
	int (*send) (struct tpm_chip *, u8 *, size_t);
	void (*cancel) (struct tpm_chip *);
	struct miscdevice miscdev;
};

struct tpm_chip {
	struct pci_dev *pci_dev;	/* PCI device stuff */

	int dev_num;		/* /dev/tpm# */
	int num_opens;		/* only one allowed */
	int time_expired;

	/* Data passed to and from the tpm via the read/write calls */
	u8 *data_buffer;
	atomic_t data_pending;
	struct semaphore buffer_mutex;

	struct timer_list user_read_timer;	/* user needs to claim result */
	struct semaphore tpm_mutex;	/* tpm is processing */
	struct timer_list device_timer;	/* tpm is processing */
	struct semaphore timer_manipulation_mutex;

	struct tpm_vendor_specific *vendor;

	struct list_head list;
};

static inline int tpm_read_index(int index)
{
	outb(index, TPM_ADDR);
	return inb(TPM_DATA) & 0xFF;
}

static inline void tpm_write_index(int index, int value)
{
	outb(index, TPM_ADDR);
	outb(value & 0xFF, TPM_DATA);
}

extern void tpm_time_expired(unsigned long);
extern int tpm_lpc_bus_init(struct pci_dev *, u16);

extern int tpm_register_hardware(struct pci_dev *,
				 struct tpm_vendor_specific *);
extern int tpm_open(struct inode *, struct file *);
extern int tpm_release(struct inode *, struct file *);
extern ssize_t tpm_write(struct file *, const char __user *, size_t,
			 loff_t *);
extern ssize_t tpm_read(struct file *, char __user *, size_t, loff_t *);
extern void __devexit tpm_remove(struct pci_dev *);
extern int tpm_pm_suspend(struct pci_dev *, pm_message_t);
extern int tpm_pm_resume(struct pci_dev *);
