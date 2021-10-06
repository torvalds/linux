// SPDX-License-Identifier: GPL-2.0-only
/*
 *  dcdbas.c: Dell Systems Management Base Driver
 *
 *  The Dell Systems Management Base Driver provides a sysfs interface for
 *  systems management software to perform System Management Interrupts (SMIs)
 *  and Host Control Actions (power cycle or power off after OS shutdown) on
 *  Dell systems.
 *
 *  See Documentation/driver-api/dcdbas.rst for more information.
 *
 *  Copyright (C) 1995-2006 Dell Inc.
 */

#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/dma-mapping.h>
#include <linux/dmi.h>
#include <linux/errno.h>
#include <linux/cpu.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mc146818rtc.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/mutex.h>

#include "dcdbas.h"

#define DRIVER_NAME		"dcdbas"
#define DRIVER_VERSION		"5.6.0-3.4"
#define DRIVER_DESCRIPTION	"Dell Systems Management Base Driver"

static struct platform_device *dcdbas_pdev;

static u8 *smi_data_buf;
static dma_addr_t smi_data_buf_handle;
static unsigned long smi_data_buf_size;
static unsigned long max_smi_data_buf_size = MAX_SMI_DATA_BUF_SIZE;
static u32 smi_data_buf_phys_addr;
static DEFINE_MUTEX(smi_data_lock);
static u8 *bios_buffer;

static unsigned int host_control_action;
static unsigned int host_control_smi_type;
static unsigned int host_control_on_shutdown;

static bool wsmt_enabled;

/**
 * smi_data_buf_free: free SMI data buffer
 */
static void smi_data_buf_free(void)
{
	if (!smi_data_buf || wsmt_enabled)
		return;

	dev_dbg(&dcdbas_pdev->dev, "%s: phys: %x size: %lu\n",
		__func__, smi_data_buf_phys_addr, smi_data_buf_size);

	dma_free_coherent(&dcdbas_pdev->dev, smi_data_buf_size, smi_data_buf,
			  smi_data_buf_handle);
	smi_data_buf = NULL;
	smi_data_buf_handle = 0;
	smi_data_buf_phys_addr = 0;
	smi_data_buf_size = 0;
}

/**
 * smi_data_buf_realloc: grow SMI data buffer if needed
 */
static int smi_data_buf_realloc(unsigned long size)
{
	void *buf;
	dma_addr_t handle;

	if (smi_data_buf_size >= size)
		return 0;

	if (size > max_smi_data_buf_size)
		return -EINVAL;

	/* new buffer is needed */
	buf = dma_alloc_coherent(&dcdbas_pdev->dev, size, &handle, GFP_KERNEL);
	if (!buf) {
		dev_dbg(&dcdbas_pdev->dev,
			"%s: failed to allocate memory size %lu\n",
			__func__, size);
		return -ENOMEM;
	}
	/* memory zeroed by dma_alloc_coherent */

	if (smi_data_buf)
		memcpy(buf, smi_data_buf, smi_data_buf_size);

	/* free any existing buffer */
	smi_data_buf_free();

	/* set up new buffer for use */
	smi_data_buf = buf;
	smi_data_buf_handle = handle;
	smi_data_buf_phys_addr = (u32) virt_to_phys(buf);
	smi_data_buf_size = size;

	dev_dbg(&dcdbas_pdev->dev, "%s: phys: %x size: %lu\n",
		__func__, smi_data_buf_phys_addr, smi_data_buf_size);

	return 0;
}

static ssize_t smi_data_buf_phys_addr_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	return sprintf(buf, "%x\n", smi_data_buf_phys_addr);
}

static ssize_t smi_data_buf_size_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return sprintf(buf, "%lu\n", smi_data_buf_size);
}

static ssize_t smi_data_buf_size_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	unsigned long buf_size;
	ssize_t ret;

	buf_size = simple_strtoul(buf, NULL, 10);

	/* make sure SMI data buffer is at least buf_size */
	mutex_lock(&smi_data_lock);
	ret = smi_data_buf_realloc(buf_size);
	mutex_unlock(&smi_data_lock);
	if (ret)
		return ret;

	return count;
}

static ssize_t smi_data_read(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *bin_attr,
			     char *buf, loff_t pos, size_t count)
{
	ssize_t ret;

	mutex_lock(&smi_data_lock);
	ret = memory_read_from_buffer(buf, count, &pos, smi_data_buf,
					smi_data_buf_size);
	mutex_unlock(&smi_data_lock);
	return ret;
}

static ssize_t smi_data_write(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *bin_attr,
			      char *buf, loff_t pos, size_t count)
{
	ssize_t ret;

	if ((pos + count) > max_smi_data_buf_size)
		return -EINVAL;

	mutex_lock(&smi_data_lock);

	ret = smi_data_buf_realloc(pos + count);
	if (ret)
		goto out;

	memcpy(smi_data_buf + pos, buf, count);
	ret = count;
out:
	mutex_unlock(&smi_data_lock);
	return ret;
}

static ssize_t host_control_action_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%u\n", host_control_action);
}

static ssize_t host_control_action_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	ssize_t ret;

	/* make sure buffer is available for host control command */
	mutex_lock(&smi_data_lock);
	ret = smi_data_buf_realloc(sizeof(struct apm_cmd));
	mutex_unlock(&smi_data_lock);
	if (ret)
		return ret;

	host_control_action = simple_strtoul(buf, NULL, 10);
	return count;
}

static ssize_t host_control_smi_type_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	return sprintf(buf, "%u\n", host_control_smi_type);
}

static ssize_t host_control_smi_type_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	host_control_smi_type = simple_strtoul(buf, NULL, 10);
	return count;
}

static ssize_t host_control_on_shutdown_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	return sprintf(buf, "%u\n", host_control_on_shutdown);
}

static ssize_t host_control_on_shutdown_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	host_control_on_shutdown = simple_strtoul(buf, NULL, 10);
	return count;
}

static int raise_smi(void *par)
{
	struct smi_cmd *smi_cmd = par;

	if (smp_processor_id() != 0) {
		dev_dbg(&dcdbas_pdev->dev, "%s: failed to get CPU 0\n",
			__func__);
		return -EBUSY;
	}

	/* generate SMI */
	/* inb to force posted write through and make SMI happen now */
	asm volatile (
		"outb %b0,%w1\n"
		"inb %w1"
		: /* no output args */
		: "a" (smi_cmd->command_code),
		  "d" (smi_cmd->command_address),
		  "b" (smi_cmd->ebx),
		  "c" (smi_cmd->ecx)
		: "memory"
	);

	return 0;
}
/**
 * dcdbas_smi_request: generate SMI request
 *
 * Called with smi_data_lock.
 */
int dcdbas_smi_request(struct smi_cmd *smi_cmd)
{
	int ret;

	if (smi_cmd->magic != SMI_CMD_MAGIC) {
		dev_info(&dcdbas_pdev->dev, "%s: invalid magic value\n",
			 __func__);
		return -EBADR;
	}

	/* SMI requires CPU 0 */
	cpus_read_lock();
	ret = smp_call_on_cpu(0, raise_smi, smi_cmd, true);
	cpus_read_unlock();

	return ret;
}

/**
 * smi_request_store:
 *
 * The valid values are:
 * 0: zero SMI data buffer
 * 1: generate calling interface SMI
 * 2: generate raw SMI
 *
 * User application writes smi_cmd to smi_data before telling driver
 * to generate SMI.
 */
static ssize_t smi_request_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct smi_cmd *smi_cmd;
	unsigned long val = simple_strtoul(buf, NULL, 10);
	ssize_t ret;

	mutex_lock(&smi_data_lock);

	if (smi_data_buf_size < sizeof(struct smi_cmd)) {
		ret = -ENODEV;
		goto out;
	}
	smi_cmd = (struct smi_cmd *)smi_data_buf;

	switch (val) {
	case 2:
		/* Raw SMI */
		ret = dcdbas_smi_request(smi_cmd);
		if (!ret)
			ret = count;
		break;
	case 1:
		/*
		 * Calling Interface SMI
		 *
		 * Provide physical address of command buffer field within
		 * the struct smi_cmd to BIOS.
		 *
		 * Because the address that smi_cmd (smi_data_buf) points to
		 * will be from memremap() of a non-memory address if WSMT
		 * is present, we can't use virt_to_phys() on smi_cmd, so
		 * we have to use the physical address that was saved when
		 * the virtual address for smi_cmd was received.
		 */
		smi_cmd->ebx = smi_data_buf_phys_addr +
				offsetof(struct smi_cmd, command_buffer);
		ret = dcdbas_smi_request(smi_cmd);
		if (!ret)
			ret = count;
		break;
	case 0:
		memset(smi_data_buf, 0, smi_data_buf_size);
		ret = count;
		break;
	default:
		ret = -EINVAL;
		break;
	}

out:
	mutex_unlock(&smi_data_lock);
	return ret;
}
EXPORT_SYMBOL(dcdbas_smi_request);

/**
 * host_control_smi: generate host control SMI
 *
 * Caller must set up the host control command in smi_data_buf.
 */
static int host_control_smi(void)
{
	struct apm_cmd *apm_cmd;
	u8 *data;
	unsigned long flags;
	u32 num_ticks;
	s8 cmd_status;
	u8 index;

	apm_cmd = (struct apm_cmd *)smi_data_buf;
	apm_cmd->status = ESM_STATUS_CMD_UNSUCCESSFUL;

	switch (host_control_smi_type) {
	case HC_SMITYPE_TYPE1:
		spin_lock_irqsave(&rtc_lock, flags);
		/* write SMI data buffer physical address */
		data = (u8 *)&smi_data_buf_phys_addr;
		for (index = PE1300_CMOS_CMD_STRUCT_PTR;
		     index < (PE1300_CMOS_CMD_STRUCT_PTR + 4);
		     index++, data++) {
			outb(index,
			     (CMOS_BASE_PORT + CMOS_PAGE2_INDEX_PORT_PIIX4));
			outb(*data,
			     (CMOS_BASE_PORT + CMOS_PAGE2_DATA_PORT_PIIX4));
		}

		/* first set status to -1 as called by spec */
		cmd_status = ESM_STATUS_CMD_UNSUCCESSFUL;
		outb((u8) cmd_status, PCAT_APM_STATUS_PORT);

		/* generate SMM call */
		outb(ESM_APM_CMD, PCAT_APM_CONTROL_PORT);
		spin_unlock_irqrestore(&rtc_lock, flags);

		/* wait a few to see if it executed */
		num_ticks = TIMEOUT_USEC_SHORT_SEMA_BLOCKING;
		while ((s8)inb(PCAT_APM_STATUS_PORT) == ESM_STATUS_CMD_UNSUCCESSFUL) {
			num_ticks--;
			if (num_ticks == EXPIRED_TIMER)
				return -ETIME;
		}
		break;

	case HC_SMITYPE_TYPE2:
	case HC_SMITYPE_TYPE3:
		spin_lock_irqsave(&rtc_lock, flags);
		/* write SMI data buffer physical address */
		data = (u8 *)&smi_data_buf_phys_addr;
		for (index = PE1400_CMOS_CMD_STRUCT_PTR;
		     index < (PE1400_CMOS_CMD_STRUCT_PTR + 4);
		     index++, data++) {
			outb(index, (CMOS_BASE_PORT + CMOS_PAGE1_INDEX_PORT));
			outb(*data, (CMOS_BASE_PORT + CMOS_PAGE1_DATA_PORT));
		}

		/* generate SMM call */
		if (host_control_smi_type == HC_SMITYPE_TYPE3)
			outb(ESM_APM_CMD, PCAT_APM_CONTROL_PORT);
		else
			outb(ESM_APM_CMD, PE1400_APM_CONTROL_PORT);

		/* restore RTC index pointer since it was written to above */
		CMOS_READ(RTC_REG_C);
		spin_unlock_irqrestore(&rtc_lock, flags);

		/* read control port back to serialize write */
		cmd_status = inb(PE1400_APM_CONTROL_PORT);

		/* wait a few to see if it executed */
		num_ticks = TIMEOUT_USEC_SHORT_SEMA_BLOCKING;
		while (apm_cmd->status == ESM_STATUS_CMD_UNSUCCESSFUL) {
			num_ticks--;
			if (num_ticks == EXPIRED_TIMER)
				return -ETIME;
		}
		break;

	default:
		dev_dbg(&dcdbas_pdev->dev, "%s: invalid SMI type %u\n",
			__func__, host_control_smi_type);
		return -ENOSYS;
	}

	return 0;
}

/**
 * dcdbas_host_control: initiate host control
 *
 * This function is called by the driver after the system has
 * finished shutting down if the user application specified a
 * host control action to perform on shutdown.  It is safe to
 * use smi_data_buf at this point because the system has finished
 * shutting down and no userspace apps are running.
 */
static void dcdbas_host_control(void)
{
	struct apm_cmd *apm_cmd;
	u8 action;

	if (host_control_action == HC_ACTION_NONE)
		return;

	action = host_control_action;
	host_control_action = HC_ACTION_NONE;

	if (!smi_data_buf) {
		dev_dbg(&dcdbas_pdev->dev, "%s: no SMI buffer\n", __func__);
		return;
	}

	if (smi_data_buf_size < sizeof(struct apm_cmd)) {
		dev_dbg(&dcdbas_pdev->dev, "%s: SMI buffer too small\n",
			__func__);
		return;
	}

	apm_cmd = (struct apm_cmd *)smi_data_buf;

	/* power off takes precedence */
	if (action & HC_ACTION_HOST_CONTROL_POWEROFF) {
		apm_cmd->command = ESM_APM_POWER_CYCLE;
		apm_cmd->reserved = 0;
		*((s16 *)&apm_cmd->parameters.shortreq.parm[0]) = (s16) 0;
		host_control_smi();
	} else if (action & HC_ACTION_HOST_CONTROL_POWERCYCLE) {
		apm_cmd->command = ESM_APM_POWER_CYCLE;
		apm_cmd->reserved = 0;
		*((s16 *)&apm_cmd->parameters.shortreq.parm[0]) = (s16) 20;
		host_control_smi();
	}
}

/* WSMT */

static u8 checksum(u8 *buffer, u8 length)
{
	u8 sum = 0;
	u8 *end = buffer + length;

	while (buffer < end)
		sum += *buffer++;
	return sum;
}

static inline struct smm_eps_table *check_eps_table(u8 *addr)
{
	struct smm_eps_table *eps = (struct smm_eps_table *)addr;

	if (strncmp(eps->smm_comm_buff_anchor, SMM_EPS_SIG, 4) != 0)
		return NULL;

	if (checksum(addr, eps->length) != 0)
		return NULL;

	return eps;
}

static int dcdbas_check_wsmt(void)
{
	const struct dmi_device *dev = NULL;
	struct acpi_table_wsmt *wsmt = NULL;
	struct smm_eps_table *eps = NULL;
	u64 bios_buf_paddr;
	u64 remap_size;
	u8 *addr;

	acpi_get_table(ACPI_SIG_WSMT, 0, (struct acpi_table_header **)&wsmt);
	if (!wsmt)
		return 0;

	/* Check if WSMT ACPI table shows that protection is enabled */
	if (!(wsmt->protection_flags & ACPI_WSMT_FIXED_COMM_BUFFERS) ||
	    !(wsmt->protection_flags & ACPI_WSMT_COMM_BUFFER_NESTED_PTR_PROTECTION))
		return 0;

	/*
	 * BIOS could provide the address/size of the protected buffer
	 * in an SMBIOS string or in an EPS structure in 0xFxxxx.
	 */

	/* Check SMBIOS for buffer address */
	while ((dev = dmi_find_device(DMI_DEV_TYPE_OEM_STRING, NULL, dev)))
		if (sscanf(dev->name, "30[%16llx;%8llx]", &bios_buf_paddr,
		    &remap_size) == 2)
			goto remap;

	/* Scan for EPS (entry point structure) */
	for (addr = (u8 *)__va(0xf0000);
	     addr < (u8 *)__va(0x100000 - sizeof(struct smm_eps_table));
	     addr += 16) {
		eps = check_eps_table(addr);
		if (eps)
			break;
	}

	if (!eps) {
		dev_dbg(&dcdbas_pdev->dev, "found WSMT, but no firmware buffer found\n");
		return -ENODEV;
	}
	bios_buf_paddr = eps->smm_comm_buff_addr;
	remap_size = eps->num_of_4k_pages * PAGE_SIZE;

remap:
	/*
	 * Get physical address of buffer and map to virtual address.
	 * Table gives size in 4K pages, regardless of actual system page size.
	 */
	if (upper_32_bits(bios_buf_paddr + 8)) {
		dev_warn(&dcdbas_pdev->dev, "found WSMT, but buffer address is above 4GB\n");
		return -EINVAL;
	}
	/*
	 * Limit remap size to MAX_SMI_DATA_BUF_SIZE + 8 (since the first 8
	 * bytes are used for a semaphore, not the data buffer itself).
	 */
	if (remap_size > MAX_SMI_DATA_BUF_SIZE + 8)
		remap_size = MAX_SMI_DATA_BUF_SIZE + 8;

	bios_buffer = memremap(bios_buf_paddr, remap_size, MEMREMAP_WB);
	if (!bios_buffer) {
		dev_warn(&dcdbas_pdev->dev, "found WSMT, but failed to map buffer\n");
		return -ENOMEM;
	}

	/* First 8 bytes is for a semaphore, not part of the smi_data_buf */
	smi_data_buf_phys_addr = bios_buf_paddr + 8;
	smi_data_buf = bios_buffer + 8;
	smi_data_buf_size = remap_size - 8;
	max_smi_data_buf_size = smi_data_buf_size;
	wsmt_enabled = true;
	dev_info(&dcdbas_pdev->dev,
		 "WSMT found, using firmware-provided SMI buffer.\n");
	return 1;
}

/**
 * dcdbas_reboot_notify: handle reboot notification for host control
 */
static int dcdbas_reboot_notify(struct notifier_block *nb, unsigned long code,
				void *unused)
{
	switch (code) {
	case SYS_DOWN:
	case SYS_HALT:
	case SYS_POWER_OFF:
		if (host_control_on_shutdown) {
			/* firmware is going to perform host control action */
			printk(KERN_WARNING "Please wait for shutdown "
			       "action to complete...\n");
			dcdbas_host_control();
		}
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block dcdbas_reboot_nb = {
	.notifier_call = dcdbas_reboot_notify,
	.next = NULL,
	.priority = INT_MIN
};

static DCDBAS_BIN_ATTR_RW(smi_data);

static struct bin_attribute *dcdbas_bin_attrs[] = {
	&bin_attr_smi_data,
	NULL
};

static DCDBAS_DEV_ATTR_RW(smi_data_buf_size);
static DCDBAS_DEV_ATTR_RO(smi_data_buf_phys_addr);
static DCDBAS_DEV_ATTR_WO(smi_request);
static DCDBAS_DEV_ATTR_RW(host_control_action);
static DCDBAS_DEV_ATTR_RW(host_control_smi_type);
static DCDBAS_DEV_ATTR_RW(host_control_on_shutdown);

static struct attribute *dcdbas_dev_attrs[] = {
	&dev_attr_smi_data_buf_size.attr,
	&dev_attr_smi_data_buf_phys_addr.attr,
	&dev_attr_smi_request.attr,
	&dev_attr_host_control_action.attr,
	&dev_attr_host_control_smi_type.attr,
	&dev_attr_host_control_on_shutdown.attr,
	NULL
};

static const struct attribute_group dcdbas_attr_group = {
	.attrs = dcdbas_dev_attrs,
	.bin_attrs = dcdbas_bin_attrs,
};

static int dcdbas_probe(struct platform_device *dev)
{
	int error;

	host_control_action = HC_ACTION_NONE;
	host_control_smi_type = HC_SMITYPE_NONE;

	dcdbas_pdev = dev;

	/* Check if ACPI WSMT table specifies protected SMI buffer address */
	error = dcdbas_check_wsmt();
	if (error < 0)
		return error;

	/*
	 * BIOS SMI calls require buffer addresses be in 32-bit address space.
	 * This is done by setting the DMA mask below.
	 */
	error = dma_set_coherent_mask(&dcdbas_pdev->dev, DMA_BIT_MASK(32));
	if (error)
		return error;

	error = sysfs_create_group(&dev->dev.kobj, &dcdbas_attr_group);
	if (error)
		return error;

	register_reboot_notifier(&dcdbas_reboot_nb);

	dev_info(&dev->dev, "%s (version %s)\n",
		 DRIVER_DESCRIPTION, DRIVER_VERSION);

	return 0;
}

static int dcdbas_remove(struct platform_device *dev)
{
	unregister_reboot_notifier(&dcdbas_reboot_nb);
	sysfs_remove_group(&dev->dev.kobj, &dcdbas_attr_group);

	return 0;
}

static struct platform_driver dcdbas_driver = {
	.driver		= {
		.name	= DRIVER_NAME,
	},
	.probe		= dcdbas_probe,
	.remove		= dcdbas_remove,
};

static const struct platform_device_info dcdbas_dev_info __initconst = {
	.name		= DRIVER_NAME,
	.id		= -1,
	.dma_mask	= DMA_BIT_MASK(32),
};

static struct platform_device *dcdbas_pdev_reg;

/**
 * dcdbas_init: initialize driver
 */
static int __init dcdbas_init(void)
{
	int error;

	error = platform_driver_register(&dcdbas_driver);
	if (error)
		return error;

	dcdbas_pdev_reg = platform_device_register_full(&dcdbas_dev_info);
	if (IS_ERR(dcdbas_pdev_reg)) {
		error = PTR_ERR(dcdbas_pdev_reg);
		goto err_unregister_driver;
	}

	return 0;

 err_unregister_driver:
	platform_driver_unregister(&dcdbas_driver);
	return error;
}

/**
 * dcdbas_exit: perform driver cleanup
 */
static void __exit dcdbas_exit(void)
{
	/*
	 * make sure functions that use dcdbas_pdev are called
	 * before platform_device_unregister
	 */
	unregister_reboot_notifier(&dcdbas_reboot_nb);

	/*
	 * We have to free the buffer here instead of dcdbas_remove
	 * because only in module exit function we can be sure that
	 * all sysfs attributes belonging to this module have been
	 * released.
	 */
	if (dcdbas_pdev)
		smi_data_buf_free();
	if (bios_buffer)
		memunmap(bios_buffer);
	platform_device_unregister(dcdbas_pdev_reg);
	platform_driver_unregister(&dcdbas_driver);
}

subsys_initcall_sync(dcdbas_init);
module_exit(dcdbas_exit);

MODULE_DESCRIPTION(DRIVER_DESCRIPTION " (version " DRIVER_VERSION ")");
MODULE_VERSION(DRIVER_VERSION);
MODULE_AUTHOR("Dell Inc.");
MODULE_LICENSE("GPL");
/* Any System or BIOS claiming to be by Dell */
MODULE_ALIAS("dmi:*:[bs]vnD[Ee][Ll][Ll]*:*");
