/*
 * PCI Backend - Handles the virtual fields in the configuration space headers.
 *
 * Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include "pciback.h"
#include "conf_space.h"

struct pci_bar_info {
	u32 val;
	u32 len_val;
	int which;
};

#define is_enable_cmd(value) ((value)&(PCI_COMMAND_MEMORY|PCI_COMMAND_IO))
#define is_master_cmd(value) ((value)&PCI_COMMAND_MASTER)

static int command_write(struct pci_dev *dev, int offset, u16 value, void *data)
{
	int err;

	if (!pci_is_enabled(dev) && is_enable_cmd(value)) {
		if (unlikely(verbose_request))
			printk(KERN_DEBUG "pciback: %s: enable\n",
			       pci_name(dev));
		err = pci_enable_device(dev);
		if (err)
			return err;
	} else if (pci_is_enabled(dev) && !is_enable_cmd(value)) {
		if (unlikely(verbose_request))
			printk(KERN_DEBUG "pciback: %s: disable\n",
			       pci_name(dev));
		pci_disable_device(dev);
	}

	if (!dev->is_busmaster && is_master_cmd(value)) {
		if (unlikely(verbose_request))
			printk(KERN_DEBUG "pciback: %s: set bus master\n",
			       pci_name(dev));
		pci_set_master(dev);
	}

	if (value & PCI_COMMAND_INVALIDATE) {
		if (unlikely(verbose_request))
			printk(KERN_DEBUG
			       "pciback: %s: enable memory-write-invalidate\n",
			       pci_name(dev));
		err = pci_set_mwi(dev);
		if (err) {
			printk(KERN_WARNING
			       "pciback: %s: cannot enable "
			       "memory-write-invalidate (%d)\n",
			       pci_name(dev), err);
			value &= ~PCI_COMMAND_INVALIDATE;
		}
	}

	return pci_write_config_word(dev, offset, value);
}

static int rom_write(struct pci_dev *dev, int offset, u32 value, void *data)
{
	struct pci_bar_info *bar = data;

	if (unlikely(!bar)) {
		printk(KERN_WARNING "pciback: driver data not found for %s\n",
		       pci_name(dev));
		return XEN_PCI_ERR_op_failed;
	}

	/* A write to obtain the length must happen as a 32-bit write.
	 * This does not (yet) support writing individual bytes
	 */
	if (value == ~PCI_ROM_ADDRESS_ENABLE)
		bar->which = 1;
	else {
		u32 tmpval;
		pci_read_config_dword(dev, offset, &tmpval);
		if (tmpval != bar->val && value == bar->val) {
			/* Allow restoration of bar value. */
			pci_write_config_dword(dev, offset, bar->val);
		}
		bar->which = 0;
	}

	/* Do we need to support enabling/disabling the rom address here? */

	return 0;
}

/* For the BARs, only allow writes which write ~0 or
 * the correct resource information
 * (Needed for when the driver probes the resource usage)
 */
static int bar_write(struct pci_dev *dev, int offset, u32 value, void *data)
{
	struct pci_bar_info *bar = data;

	if (unlikely(!bar)) {
		printk(KERN_WARNING "pciback: driver data not found for %s\n",
		       pci_name(dev));
		return XEN_PCI_ERR_op_failed;
	}

	/* A write to obtain the length must happen as a 32-bit write.
	 * This does not (yet) support writing individual bytes
	 */
	if (value == ~0)
		bar->which = 1;
	else {
		u32 tmpval;
		pci_read_config_dword(dev, offset, &tmpval);
		if (tmpval != bar->val && value == bar->val) {
			/* Allow restoration of bar value. */
			pci_write_config_dword(dev, offset, bar->val);
		}
		bar->which = 0;
	}

	return 0;
}

static int bar_read(struct pci_dev *dev, int offset, u32 * value, void *data)
{
	struct pci_bar_info *bar = data;

	if (unlikely(!bar)) {
		printk(KERN_WARNING "pciback: driver data not found for %s\n",
		       pci_name(dev));
		return XEN_PCI_ERR_op_failed;
	}

	*value = bar->which ? bar->len_val : bar->val;

	return 0;
}

static inline void read_dev_bar(struct pci_dev *dev,
				struct pci_bar_info *bar_info, int offset,
				u32 len_mask)
{
	pci_read_config_dword(dev, offset, &bar_info->val);
	pci_write_config_dword(dev, offset, len_mask);
	pci_read_config_dword(dev, offset, &bar_info->len_val);
	pci_write_config_dword(dev, offset, bar_info->val);
}

static void *bar_init(struct pci_dev *dev, int offset)
{
	struct pci_bar_info *bar = kmalloc(sizeof(*bar), GFP_KERNEL);

	if (!bar)
		return ERR_PTR(-ENOMEM);

	read_dev_bar(dev, bar, offset, ~0);
	bar->which = 0;

	return bar;
}

static void *rom_init(struct pci_dev *dev, int offset)
{
	struct pci_bar_info *bar = kmalloc(sizeof(*bar), GFP_KERNEL);

	if (!bar)
		return ERR_PTR(-ENOMEM);

	read_dev_bar(dev, bar, offset, ~PCI_ROM_ADDRESS_ENABLE);
	bar->which = 0;

	return bar;
}

static void bar_reset(struct pci_dev *dev, int offset, void *data)
{
	struct pci_bar_info *bar = data;

	bar->which = 0;
}

static void bar_release(struct pci_dev *dev, int offset, void *data)
{
	kfree(data);
}

static int interrupt_read(struct pci_dev *dev, int offset, u8 * value,
			  void *data)
{
	*value = (u8) dev->irq;

	return 0;
}

static int bist_write(struct pci_dev *dev, int offset, u8 value, void *data)
{
	u8 cur_value;
	int err;

	err = pci_read_config_byte(dev, offset, &cur_value);
	if (err)
		goto out;

	if ((cur_value & ~PCI_BIST_START) == (value & ~PCI_BIST_START)
	    || value == PCI_BIST_START)
		err = pci_write_config_byte(dev, offset, value);

out:
	return err;
}

static const struct config_field header_common[] = {
	{
	 .offset    = PCI_COMMAND,
	 .size      = 2,
	 .u.w.read  = pciback_read_config_word,
	 .u.w.write = command_write,
	},
	{
	 .offset    = PCI_INTERRUPT_LINE,
	 .size      = 1,
	 .u.b.read  = interrupt_read,
	},
	{
	 .offset    = PCI_INTERRUPT_PIN,
	 .size      = 1,
	 .u.b.read  = pciback_read_config_byte,
	},
	{
	 /* Any side effects of letting driver domain control cache line? */
	 .offset    = PCI_CACHE_LINE_SIZE,
	 .size      = 1,
	 .u.b.read  = pciback_read_config_byte,
	 .u.b.write = pciback_write_config_byte,
	},
	{
	 .offset    = PCI_LATENCY_TIMER,
	 .size      = 1,
	 .u.b.read  = pciback_read_config_byte,
	},
	{
	 .offset    = PCI_BIST,
	 .size      = 1,
	 .u.b.read  = pciback_read_config_byte,
	 .u.b.write = bist_write,
	},
	{}
};

#define CFG_FIELD_BAR(reg_offset)			\
	{						\
	.offset     = reg_offset,			\
	.size       = 4,				\
	.init       = bar_init,				\
	.reset      = bar_reset,			\
	.release    = bar_release,			\
	.u.dw.read  = bar_read,				\
	.u.dw.write = bar_write,			\
	}

#define CFG_FIELD_ROM(reg_offset)			\
	{						\
	.offset     = reg_offset,			\
	.size       = 4,				\
	.init       = rom_init,				\
	.reset      = bar_reset,			\
	.release    = bar_release,			\
	.u.dw.read  = bar_read,				\
	.u.dw.write = rom_write,			\
	}

static const struct config_field header_0[] = {
	CFG_FIELD_BAR(PCI_BASE_ADDRESS_0),
	CFG_FIELD_BAR(PCI_BASE_ADDRESS_1),
	CFG_FIELD_BAR(PCI_BASE_ADDRESS_2),
	CFG_FIELD_BAR(PCI_BASE_ADDRESS_3),
	CFG_FIELD_BAR(PCI_BASE_ADDRESS_4),
	CFG_FIELD_BAR(PCI_BASE_ADDRESS_5),
	CFG_FIELD_ROM(PCI_ROM_ADDRESS),
	{}
};

static const struct config_field header_1[] = {
	CFG_FIELD_BAR(PCI_BASE_ADDRESS_0),
	CFG_FIELD_BAR(PCI_BASE_ADDRESS_1),
	CFG_FIELD_ROM(PCI_ROM_ADDRESS1),
	{}
};

int pciback_config_header_add_fields(struct pci_dev *dev)
{
	int err;

	err = pciback_config_add_fields(dev, header_common);
	if (err)
		goto out;

	switch (dev->hdr_type) {
	case PCI_HEADER_TYPE_NORMAL:
		err = pciback_config_add_fields(dev, header_0);
		break;

	case PCI_HEADER_TYPE_BRIDGE:
		err = pciback_config_add_fields(dev, header_1);
		break;

	default:
		err = -EINVAL;
		printk(KERN_ERR "pciback: %s: Unsupported header type %d!\n",
		       pci_name(dev), dev->hdr_type);
		break;
	}

out:
	return err;
}
