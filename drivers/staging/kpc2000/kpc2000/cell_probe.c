// SPDX-License-Identifier: GPL-2.0+
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/mfd/core.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/uio_driver.h>
#include "pcie.h"

/*  Core (Resource) Table Layout:
 *      one Resource per record (8 bytes)
 *                 6         5         4         3         2         1         0
 *              3210987654321098765432109876543210987654321098765432109876543210
 *              IIIIIIIIIIII                                                        Core Type    [up to 4095 types]
 *                          D                                                       S2C DMA Present
 *                           DDD                                                    S2C DMA Channel Number    [up to 8 channels]
 *                              LLLLLLLLLLLLLLLL                                    Register Count (64-bit registers)    [up to 65535 registers]
 *                                              OOOOOOOOOOOOOOOO                    Core Offset (in 4kB blocks)    [up to 65535 cores]
 *                                                              D                   C2S DMA Present
 *                                                               DDD                C2S DMA Channel Number    [up to 8 channels]
 *                                                                  II              IRQ Count [0 to 3 IRQs per core]
 *                                                                    1111111000
 *                                                                    IIIIIII       IRQ Base Number [up to 128 IRQs per card]
 *                                                                           ___    Spare
 *
 */

#define KPC_OLD_DMA_CH_NUM(present, channel)   \
		((present) ? (0x8 | ((channel) & 0x7)) : 0)
#define KPC_OLD_S2C_DMA_CH_NUM(cte)   \
		KPC_OLD_DMA_CH_NUM(cte.s2c_dma_present, cte.s2c_dma_channel_num)
#define KPC_OLD_C2S_DMA_CH_NUM(cte)   \
		KPC_OLD_DMA_CH_NUM(cte.c2s_dma_present, cte.c2s_dma_channel_num)

#define KP_CORE_ID_INVALID      0
#define KP_CORE_ID_I2C          3
#define KP_CORE_ID_SPI          5

struct core_table_entry {
	u16  type;
	u32  offset;
	u32  length;
	bool s2c_dma_present;
	u8   s2c_dma_channel_num;
	bool c2s_dma_present;
	u8   c2s_dma_channel_num;
	u8   irq_count;
	u8   irq_base_num;
};

static
void  parse_core_table_entry_v0(struct core_table_entry *cte, const u64 read_val)
{
	cte->type                = ((read_val & 0xFFF0000000000000UL) >> 52);
	cte->offset              = ((read_val & 0x00000000FFFF0000UL) >> 16) * 4096;
	cte->length              = ((read_val & 0x0000FFFF00000000UL) >> 32) * 8;
	cte->s2c_dma_present     = ((read_val & 0x0008000000000000UL) >> 51);
	cte->s2c_dma_channel_num = ((read_val & 0x0007000000000000UL) >> 48);
	cte->c2s_dma_present     = ((read_val & 0x0000000000008000UL) >> 15);
	cte->c2s_dma_channel_num = ((read_val & 0x0000000000007000UL) >> 12);
	cte->irq_count           = ((read_val & 0x0000000000000C00UL) >> 10);
	cte->irq_base_num        = ((read_val & 0x00000000000003F8UL) >>  3);
}

static
void dbg_cte(struct kp2000_device *pcard, struct core_table_entry *cte)
{
	dev_dbg(&pcard->pdev->dev,
		"CTE: type:%3d  offset:%3d (%3d)  length:%3d (%3d)  s2c:%d  c2s:%d  irq_count:%d  base_irq:%d\n",
		cte->type,
		cte->offset,
		cte->offset / 4096,
		cte->length,
		cte->length / 8,
		(cte->s2c_dma_present ? cte->s2c_dma_channel_num : -1),
		(cte->c2s_dma_present ? cte->c2s_dma_channel_num : -1),
		cte->irq_count,
		cte->irq_base_num
	);
}

static
void parse_core_table_entry(struct core_table_entry *cte, const u64 read_val, const u8 entry_rev)
{
	switch (entry_rev) {
	case 0:
		parse_core_table_entry_v0(cte, read_val);
		break;
	default:
		cte->type = 0;
		break;
	}
}

static int probe_core_basic(unsigned int core_num, struct kp2000_device *pcard,
			    char *name, const struct core_table_entry cte)
{
	struct mfd_cell  cell = { .id = core_num, .name = name };
	struct resource resources[2];

	struct kpc_core_device_platdata core_pdata = {
		.card_id           = pcard->card_id,
		.build_version     = pcard->build_version,
		.hardware_revision = pcard->hardware_revision,
		.ssid              = pcard->ssid,
		.ddna              = pcard->ddna,
	};

	dev_dbg(&pcard->pdev->dev,
		"Found Basic core: type = %02d  dma = %02x / %02x  offset = 0x%x  length = 0x%x (%d regs)\n",
		cte.type,
		KPC_OLD_S2C_DMA_CH_NUM(cte),
		KPC_OLD_C2S_DMA_CH_NUM(cte),
		cte.offset,
		cte.length,
		cte.length / 8);

	cell.platform_data = &core_pdata;
	cell.pdata_size = sizeof(struct kpc_core_device_platdata);
	cell.num_resources = 2;

	memset(&resources, 0, sizeof(resources));

	resources[0].start = cte.offset;
	resources[0].end   = cte.offset + (cte.length - 1);
	resources[0].flags = IORESOURCE_MEM;

	resources[1].start = pcard->pdev->irq;
	resources[1].end   = pcard->pdev->irq;
	resources[1].flags = IORESOURCE_IRQ;

	cell.resources = resources;

	return mfd_add_devices(PCARD_TO_DEV(pcard),    // parent
			       pcard->card_num * 100,  // id
			       &cell,                  // struct mfd_cell *
			       1,                      // ndevs
			       &pcard->regs_base_resource,
			       0,                      // irq_base
			       NULL);                  // struct irq_domain *
}

struct kpc_uio_device {
	struct list_head list;
	struct kp2000_device *pcard;
	struct device  *dev;
	struct uio_info uioinfo;
	struct core_table_entry cte;
	u16 core_num;
};

static ssize_t offset_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct kpc_uio_device *kudev = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", kudev->cte.offset);
}
static DEVICE_ATTR_RO(offset);

static ssize_t size_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct kpc_uio_device *kudev = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", kudev->cte.length);
}
static DEVICE_ATTR_RO(size);

static ssize_t type_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct kpc_uio_device *kudev = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", kudev->cte.type);
}
static DEVICE_ATTR_RO(type);

static ssize_t s2c_dma_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct kpc_uio_device *kudev = dev_get_drvdata(dev);

	if (!kudev->cte.s2c_dma_present)
		return sprintf(buf, "%s", "not present\n");

	return sprintf(buf, "%u\n", kudev->cte.s2c_dma_channel_num);
}
static DEVICE_ATTR_RO(s2c_dma);

static ssize_t c2s_dma_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct kpc_uio_device *kudev = dev_get_drvdata(dev);

	if (!kudev->cte.c2s_dma_present)
		return sprintf(buf, "%s", "not present\n");

	return sprintf(buf, "%u\n", kudev->cte.c2s_dma_channel_num);
}
static DEVICE_ATTR_RO(c2s_dma);

static ssize_t irq_count_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct kpc_uio_device *kudev = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", kudev->cte.irq_count);
}
static DEVICE_ATTR_RO(irq_count);

static ssize_t irq_base_num_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct kpc_uio_device *kudev = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", kudev->cte.irq_base_num);
}
static DEVICE_ATTR_RO(irq_base_num);

static ssize_t core_num_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct kpc_uio_device *kudev = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", kudev->core_num);
}
static DEVICE_ATTR_RO(core_num);

struct attribute *kpc_uio_class_attrs[] = {
	&dev_attr_offset.attr,
	&dev_attr_size.attr,
	&dev_attr_type.attr,
	&dev_attr_s2c_dma.attr,
	&dev_attr_c2s_dma.attr,
	&dev_attr_irq_count.attr,
	&dev_attr_irq_base_num.attr,
	&dev_attr_core_num.attr,
	NULL,
};

static
int  kp2000_check_uio_irq(struct kp2000_device *pcard, u32 irq_num)
{
	u64 interrupt_active   =  readq(pcard->sysinfo_regs_base + REG_INTERRUPT_ACTIVE);
	u64 interrupt_mask_inv = ~readq(pcard->sysinfo_regs_base + REG_INTERRUPT_MASK);
	u64 irq_check_mask = BIT_ULL(irq_num);

	if (interrupt_active & irq_check_mask) { // if it's active (interrupt pending)
		if (interrupt_mask_inv & irq_check_mask) {    // and if it's not masked off
			return 1;
		}
	}
	return 0;
}

static
irqreturn_t  kuio_handler(int irq, struct uio_info *uioinfo)
{
	struct kpc_uio_device *kudev = uioinfo->priv;

	if (irq != kudev->pcard->pdev->irq)
		return IRQ_NONE;

	if (kp2000_check_uio_irq(kudev->pcard, kudev->cte.irq_base_num)) {
		/* Clear the active flag */
		writeq(BIT_ULL(kudev->cte.irq_base_num),
		       kudev->pcard->sysinfo_regs_base + REG_INTERRUPT_ACTIVE);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static
int kuio_irqcontrol(struct uio_info *uioinfo, s32 irq_on)
{
	struct kpc_uio_device *kudev = uioinfo->priv;
	struct kp2000_device *pcard = kudev->pcard;
	u64 mask;

	mutex_lock(&pcard->sem);
	mask = readq(pcard->sysinfo_regs_base + REG_INTERRUPT_MASK);
	if (irq_on)
		mask &= ~(BIT_ULL(kudev->cte.irq_base_num));
	else
		mask |= BIT_ULL(kudev->cte.irq_base_num);
	writeq(mask, pcard->sysinfo_regs_base + REG_INTERRUPT_MASK);
	mutex_unlock(&pcard->sem);

	return 0;
}

static int probe_core_uio(unsigned int core_num, struct kp2000_device *pcard,
			  char *name, const struct core_table_entry cte)
{
	struct kpc_uio_device *kudev;
	int rv;

	dev_dbg(&pcard->pdev->dev,
		"Found UIO core:   type = %02d  dma = %02x / %02x  offset = 0x%x  length = 0x%x (%d regs)\n",
		cte.type,
		KPC_OLD_S2C_DMA_CH_NUM(cte),
		KPC_OLD_C2S_DMA_CH_NUM(cte),
		cte.offset,
		cte.length,
		cte.length / 8);

	kudev = kzalloc(sizeof(*kudev), GFP_KERNEL);
	if (!kudev)
		return -ENOMEM;

	INIT_LIST_HEAD(&kudev->list);
	kudev->pcard = pcard;
	kudev->cte = cte;
	kudev->core_num = core_num;

	kudev->uioinfo.priv = kudev;
	kudev->uioinfo.name = name;
	kudev->uioinfo.version = "0.0";
	if (cte.irq_count > 0) {
		kudev->uioinfo.irq_flags = IRQF_SHARED;
		kudev->uioinfo.irq = pcard->pdev->irq;
		kudev->uioinfo.handler = kuio_handler;
		kudev->uioinfo.irqcontrol = kuio_irqcontrol;
	} else {
		kudev->uioinfo.irq = 0;
	}

	kudev->uioinfo.mem[0].name = "uiomap";
	kudev->uioinfo.mem[0].addr = pci_resource_start(pcard->pdev, REG_BAR) + cte.offset;

	// Round up to nearest PAGE_SIZE boundary
	kudev->uioinfo.mem[0].size = (cte.length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	kudev->uioinfo.mem[0].memtype = UIO_MEM_PHYS;

	kudev->dev = device_create(kpc_uio_class,
				   &pcard->pdev->dev, MKDEV(0, 0), kudev, "%s.%d.%d.%d",
				   kudev->uioinfo.name, pcard->card_num, cte.type, kudev->core_num);
	if (IS_ERR(kudev->dev)) {
		dev_err(&pcard->pdev->dev, "%s: device_create failed!\n",
			__func__);
		kfree(kudev);
		return -ENODEV;
	}
	dev_set_drvdata(kudev->dev, kudev);

	rv = uio_register_device(kudev->dev, &kudev->uioinfo);
	if (rv) {
		dev_err(&pcard->pdev->dev, "%s: failed uio_register_device: %d\n",
			__func__, rv);
		put_device(kudev->dev);
		kfree(kudev);
		return rv;
	}

	list_add_tail(&kudev->list, &pcard->uio_devices_list);

	return 0;
}

static int  create_dma_engine_core(struct kp2000_device *pcard,
				   size_t engine_regs_offset,
				   int engine_num, int irq_num)
{
	struct mfd_cell  cell = { .id = engine_num };
	struct resource  resources[2];

	cell.platform_data = NULL;
	cell.pdata_size = 0;
	cell.name = KP_DRIVER_NAME_DMA_CONTROLLER;
	cell.num_resources = 2;

	memset(&resources, 0, sizeof(resources));

	resources[0].start = engine_regs_offset;
	resources[0].end   = engine_regs_offset + (KPC_DMA_ENGINE_SIZE - 1);
	resources[0].flags = IORESOURCE_MEM;

	resources[1].start = irq_num;
	resources[1].end   = irq_num;
	resources[1].flags = IORESOURCE_IRQ;

	cell.resources = resources;

	return mfd_add_devices(PCARD_TO_DEV(pcard),    // parent
			       pcard->card_num * 100,  // id
			       &cell,                  // struct mfd_cell *
			       1,                      // ndevs
			       &pcard->dma_base_resource,
			       0,                      // irq_base
			       NULL);                  // struct irq_domain *
}

static int  kp2000_setup_dma_controller(struct kp2000_device *pcard)
{
	int err;
	unsigned int i;
	u64 capabilities_reg;

	// S2C Engines
	for (i = 0 ; i < 32 ; i++) {
		capabilities_reg = readq(pcard->dma_bar_base +
					 KPC_DMA_S2C_BASE_OFFSET +
					 (KPC_DMA_ENGINE_SIZE * i));

		if (capabilities_reg & ENGINE_CAP_PRESENT_MASK) {
			err = create_dma_engine_core(pcard, (KPC_DMA_S2C_BASE_OFFSET +
							    (KPC_DMA_ENGINE_SIZE * i)),
						     i, pcard->pdev->irq);
			if (err)
				goto err_out;
		}
	}
	// C2S Engines
	for (i = 0 ; i < 32 ; i++) {
		capabilities_reg = readq(pcard->dma_bar_base +
					 KPC_DMA_C2S_BASE_OFFSET +
					 (KPC_DMA_ENGINE_SIZE * i));

		if (capabilities_reg & ENGINE_CAP_PRESENT_MASK) {
			err = create_dma_engine_core(pcard, (KPC_DMA_C2S_BASE_OFFSET +
							    (KPC_DMA_ENGINE_SIZE * i)),
						     32 + i,  pcard->pdev->irq);
			if (err)
				goto err_out;
		}
	}

	return 0;

err_out:
	dev_err(&pcard->pdev->dev, "%s: failed to add a DMA Engine: %d\n",
		__func__, err);
	return err;
}

int  kp2000_probe_cores(struct kp2000_device *pcard)
{
	int err = 0;
	int i;
	int current_type_id;
	u64 read_val;
	unsigned int highest_core_id = 0;
	struct core_table_entry cte;

	err = kp2000_setup_dma_controller(pcard);
	if (err)
		return err;

	INIT_LIST_HEAD(&pcard->uio_devices_list);

	// First, iterate the core table looking for the highest CORE_ID
	for (i = 0 ; i < pcard->core_table_length ; i++) {
		read_val = readq(pcard->sysinfo_regs_base + ((pcard->core_table_offset + i) * 8));
		parse_core_table_entry(&cte, read_val, pcard->core_table_rev);
		dbg_cte(pcard, &cte);
		if (cte.type > highest_core_id)
			highest_core_id = cte.type;
		if (cte.type == KP_CORE_ID_INVALID)
			dev_info(&pcard->pdev->dev, "Found Invalid core: %016llx\n", read_val);
	}
	// Then, iterate over the possible core types.
	for (current_type_id = 1 ; current_type_id <= highest_core_id ; current_type_id++) {
		unsigned int core_num = 0;
		/*
		 * Foreach core type, iterate the whole table and instantiate
		 * subdevices for each core.
		 * Yes, this is O(n*m) but the actual runtime is small enough
		 * that it's an acceptable tradeoff.
		 */
		for (i = 0 ; i < pcard->core_table_length ; i++) {
			read_val = readq(pcard->sysinfo_regs_base +
					 ((pcard->core_table_offset + i) * 8));
			parse_core_table_entry(&cte, read_val, pcard->core_table_rev);

			if (cte.type != current_type_id)
				continue;

			switch (cte.type) {
			case KP_CORE_ID_I2C:
				err = probe_core_basic(core_num, pcard,
						       KP_DRIVER_NAME_I2C, cte);
				break;

			case KP_CORE_ID_SPI:
				err = probe_core_basic(core_num, pcard,
						       KP_DRIVER_NAME_SPI, cte);
				break;

			default:
				err = probe_core_uio(core_num, pcard, "kpc_uio", cte);
				break;
			}
			if (err) {
				dev_err(&pcard->pdev->dev,
					"%s: failed to add core %d: %d\n",
					__func__, i, err);
				goto error;
			}
			core_num++;
		}
	}

	// Finally, instantiate a UIO device for the core_table.
	cte.type                = 0; // CORE_ID_BOARD_INFO
	cte.offset              = 0; // board info is always at the beginning
	cte.length              = 512 * 8;
	cte.s2c_dma_present     = false;
	cte.s2c_dma_channel_num = 0;
	cte.c2s_dma_present     = false;
	cte.c2s_dma_channel_num = 0;
	cte.irq_count           = 0;
	cte.irq_base_num        = 0;
	err = probe_core_uio(0, pcard, "kpc_uio", cte);
	if (err) {
		dev_err(&pcard->pdev->dev, "%s: failed to add board_info core: %d\n",
			__func__, err);
		goto error;
	}

	return 0;

error:
	kp2000_remove_cores(pcard);
	mfd_remove_devices(PCARD_TO_DEV(pcard));
	return err;
}

void  kp2000_remove_cores(struct kp2000_device *pcard)
{
	struct list_head *ptr;
	struct list_head *next;

	list_for_each_safe(ptr, next, &pcard->uio_devices_list) {
		struct kpc_uio_device *kudev = list_entry(ptr, struct kpc_uio_device, list);

		uio_unregister_device(&kudev->uioinfo);
		device_unregister(kudev->dev);
		list_del(&kudev->list);
		kfree(kudev);
	}
}

