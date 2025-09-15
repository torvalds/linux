/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2016, 2017 Cavium Inc.
 */

#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/property.h>
#include <linux/spinlock.h>

#define GPIO_RX_DAT	0x0
#define GPIO_TX_SET	0x8
#define GPIO_TX_CLR	0x10
#define GPIO_CONST	0x90
#define  GPIO_CONST_GPIOS_MASK 0xff
#define GPIO_BIT_CFG	0x400
#define  GPIO_BIT_CFG_TX_OE		BIT(0)
#define  GPIO_BIT_CFG_PIN_XOR		BIT(1)
#define  GPIO_BIT_CFG_INT_EN		BIT(2)
#define  GPIO_BIT_CFG_INT_TYPE		BIT(3)
#define  GPIO_BIT_CFG_FIL_MASK		GENMASK(11, 4)
#define  GPIO_BIT_CFG_FIL_CNT_SHIFT	4
#define  GPIO_BIT_CFG_FIL_SEL_SHIFT	8
#define  GPIO_BIT_CFG_TX_OD		BIT(12)
#define  GPIO_BIT_CFG_PIN_SEL_MASK	GENMASK(25, 16)
#define GPIO_INTR	0x800
#define  GPIO_INTR_INTR			BIT(0)
#define  GPIO_INTR_INTR_W1S		BIT(1)
#define  GPIO_INTR_ENA_W1C		BIT(2)
#define  GPIO_INTR_ENA_W1S		BIT(3)
#define GPIO_2ND_BANK	0x1400

#define GLITCH_FILTER_400NS ((4u << GPIO_BIT_CFG_FIL_SEL_SHIFT) | \
			     (9u << GPIO_BIT_CFG_FIL_CNT_SHIFT))

struct thunderx_gpio;

struct thunderx_line {
	struct thunderx_gpio	*txgpio;
	unsigned int		line;
	unsigned int		fil_bits;
};

struct thunderx_gpio {
	struct gpio_chip	chip;
	u8 __iomem		*register_base;
	struct msix_entry	*msix_entries;	/* per line MSI-X */
	struct thunderx_line	*line_entries;	/* per line irq info */
	raw_spinlock_t		lock;
	unsigned long		invert_mask[2];
	unsigned long		od_mask[2];
	int			base_msi;
};

static unsigned int bit_cfg_reg(unsigned int line)
{
	return 8 * line + GPIO_BIT_CFG;
}

static unsigned int intr_reg(unsigned int line)
{
	return 8 * line + GPIO_INTR;
}

static bool thunderx_gpio_is_gpio_nowarn(struct thunderx_gpio *txgpio,
					 unsigned int line)
{
	u64 bit_cfg = readq(txgpio->register_base + bit_cfg_reg(line));

	return (bit_cfg & GPIO_BIT_CFG_PIN_SEL_MASK) == 0;
}

/*
 * Check (and WARN) that the pin is available for GPIO.  We will not
 * allow modification of the state of non-GPIO pins from this driver.
 */
static bool thunderx_gpio_is_gpio(struct thunderx_gpio *txgpio,
				  unsigned int line)
{
	bool rv = thunderx_gpio_is_gpio_nowarn(txgpio, line);

	WARN_RATELIMIT(!rv, "Pin %d not available for GPIO\n", line);

	return rv;
}

static int thunderx_gpio_request(struct gpio_chip *chip, unsigned int line)
{
	struct thunderx_gpio *txgpio = gpiochip_get_data(chip);

	return thunderx_gpio_is_gpio(txgpio, line) ? 0 : -EIO;
}

static int thunderx_gpio_dir_in(struct gpio_chip *chip, unsigned int line)
{
	struct thunderx_gpio *txgpio = gpiochip_get_data(chip);

	if (!thunderx_gpio_is_gpio(txgpio, line))
		return -EIO;

	raw_spin_lock(&txgpio->lock);
	clear_bit(line, txgpio->invert_mask);
	clear_bit(line, txgpio->od_mask);
	writeq(txgpio->line_entries[line].fil_bits,
	       txgpio->register_base + bit_cfg_reg(line));
	raw_spin_unlock(&txgpio->lock);
	return 0;
}

static int thunderx_gpio_set(struct gpio_chip *chip, unsigned int line,
			     int value)
{
	struct thunderx_gpio *txgpio = gpiochip_get_data(chip);
	int bank = line / 64;
	int bank_bit = line % 64;

	void __iomem *reg = txgpio->register_base +
		(bank * GPIO_2ND_BANK) + (value ? GPIO_TX_SET : GPIO_TX_CLR);

	writeq(BIT_ULL(bank_bit), reg);

	return 0;
}

static int thunderx_gpio_dir_out(struct gpio_chip *chip, unsigned int line,
				 int value)
{
	struct thunderx_gpio *txgpio = gpiochip_get_data(chip);
	u64 bit_cfg = txgpio->line_entries[line].fil_bits | GPIO_BIT_CFG_TX_OE;

	if (!thunderx_gpio_is_gpio(txgpio, line))
		return -EIO;

	raw_spin_lock(&txgpio->lock);

	thunderx_gpio_set(chip, line, value);

	if (test_bit(line, txgpio->invert_mask))
		bit_cfg |= GPIO_BIT_CFG_PIN_XOR;

	if (test_bit(line, txgpio->od_mask))
		bit_cfg |= GPIO_BIT_CFG_TX_OD;

	writeq(bit_cfg, txgpio->register_base + bit_cfg_reg(line));

	raw_spin_unlock(&txgpio->lock);
	return 0;
}

static int thunderx_gpio_get_direction(struct gpio_chip *chip, unsigned int line)
{
	struct thunderx_gpio *txgpio = gpiochip_get_data(chip);
	u64 bit_cfg;

	if (!thunderx_gpio_is_gpio_nowarn(txgpio, line))
		/*
		 * Say it is input for now to avoid WARNing on
		 * gpiochip_add_data().  We will WARN if someone
		 * requests it or tries to use it.
		 */
		return 1;

	bit_cfg = readq(txgpio->register_base + bit_cfg_reg(line));

	if (bit_cfg & GPIO_BIT_CFG_TX_OE)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int thunderx_gpio_set_config(struct gpio_chip *chip,
				    unsigned int line,
				    unsigned long cfg)
{
	bool orig_invert, orig_od, orig_dat, new_invert, new_od;
	u32 arg, sel;
	u64 bit_cfg;
	int bank = line / 64;
	int bank_bit = line % 64;
	int ret = -ENOTSUPP;
	struct thunderx_gpio *txgpio = gpiochip_get_data(chip);
	void __iomem *reg = txgpio->register_base + (bank * GPIO_2ND_BANK) + GPIO_TX_SET;

	if (!thunderx_gpio_is_gpio(txgpio, line))
		return -EIO;

	raw_spin_lock(&txgpio->lock);
	orig_invert = test_bit(line, txgpio->invert_mask);
	new_invert  = orig_invert;
	orig_od = test_bit(line, txgpio->od_mask);
	new_od = orig_od;
	orig_dat = ((readq(reg) >> bank_bit) & 1) ^ orig_invert;
	bit_cfg = readq(txgpio->register_base + bit_cfg_reg(line));
	switch (pinconf_to_config_param(cfg)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		/*
		 * Weird, setting open-drain mode causes signal
		 * inversion.  Note this so we can compensate in the
		 * dir_out function.
		 */
		set_bit(line, txgpio->invert_mask);
		new_invert  = true;
		set_bit(line, txgpio->od_mask);
		new_od = true;
		ret = 0;
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		clear_bit(line, txgpio->invert_mask);
		new_invert  = false;
		clear_bit(line, txgpio->od_mask);
		new_od  = false;
		ret = 0;
		break;
	case PIN_CONFIG_INPUT_DEBOUNCE:
		arg = pinconf_to_config_argument(cfg);
		if (arg > 1228) { /* 15 * 2^15 * 2.5nS maximum */
			ret = -EINVAL;
			break;
		}
		arg *= 400; /* scale to 2.5nS clocks. */
		sel = 0;
		while (arg > 15) {
			sel++;
			arg++; /* always round up */
			arg >>= 1;
		}
		txgpio->line_entries[line].fil_bits =
			(sel << GPIO_BIT_CFG_FIL_SEL_SHIFT) |
			(arg << GPIO_BIT_CFG_FIL_CNT_SHIFT);
		bit_cfg &= ~GPIO_BIT_CFG_FIL_MASK;
		bit_cfg |= txgpio->line_entries[line].fil_bits;
		writeq(bit_cfg, txgpio->register_base + bit_cfg_reg(line));
		ret = 0;
		break;
	default:
		break;
	}
	raw_spin_unlock(&txgpio->lock);

	/*
	 * If currently output and OPEN_DRAIN changed, install the new
	 * settings
	 */
	if ((new_invert != orig_invert || new_od != orig_od) &&
	    (bit_cfg & GPIO_BIT_CFG_TX_OE))
		ret = thunderx_gpio_dir_out(chip, line, orig_dat ^ new_invert);

	return ret;
}

static int thunderx_gpio_get(struct gpio_chip *chip, unsigned int line)
{
	struct thunderx_gpio *txgpio = gpiochip_get_data(chip);
	int bank = line / 64;
	int bank_bit = line % 64;
	u64 read_bits = readq(txgpio->register_base + (bank * GPIO_2ND_BANK) + GPIO_RX_DAT);
	u64 masked_bits = read_bits & BIT_ULL(bank_bit);

	if (test_bit(line, txgpio->invert_mask))
		return masked_bits == 0;
	else
		return masked_bits != 0;
}

static int thunderx_gpio_set_multiple(struct gpio_chip *chip,
				      unsigned long *mask,
				      unsigned long *bits)
{
	int bank;
	u64 set_bits, clear_bits;
	struct thunderx_gpio *txgpio = gpiochip_get_data(chip);

	for (bank = 0; bank <= chip->ngpio / 64; bank++) {
		set_bits = bits[bank] & mask[bank];
		clear_bits = ~bits[bank] & mask[bank];
		writeq(set_bits, txgpio->register_base + (bank * GPIO_2ND_BANK) + GPIO_TX_SET);
		writeq(clear_bits, txgpio->register_base + (bank * GPIO_2ND_BANK) + GPIO_TX_CLR);
	}

	return 0;
}

static void thunderx_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct thunderx_gpio *txgpio = gpiochip_get_data(gc);

	writeq(GPIO_INTR_INTR,
	       txgpio->register_base + intr_reg(irqd_to_hwirq(d)));
}

static void thunderx_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct thunderx_gpio *txgpio = gpiochip_get_data(gc);

	writeq(GPIO_INTR_ENA_W1C,
	       txgpio->register_base + intr_reg(irqd_to_hwirq(d)));
}

static void thunderx_gpio_irq_mask_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct thunderx_gpio *txgpio = gpiochip_get_data(gc);

	writeq(GPIO_INTR_ENA_W1C | GPIO_INTR_INTR,
	       txgpio->register_base + intr_reg(irqd_to_hwirq(d)));
}

static void thunderx_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct thunderx_gpio *txgpio = gpiochip_get_data(gc);

	writeq(GPIO_INTR_ENA_W1S,
	       txgpio->register_base + intr_reg(irqd_to_hwirq(d)));
}

static int thunderx_gpio_irq_set_type(struct irq_data *d,
				      unsigned int flow_type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct thunderx_gpio *txgpio = gpiochip_get_data(gc);
	struct thunderx_line *txline =
		&txgpio->line_entries[irqd_to_hwirq(d)];
	u64 bit_cfg;

	irqd_set_trigger_type(d, flow_type);

	bit_cfg = txline->fil_bits | GPIO_BIT_CFG_INT_EN;

	if (flow_type & IRQ_TYPE_EDGE_BOTH) {
		irq_set_handler_locked(d, handle_fasteoi_ack_irq);
		bit_cfg |= GPIO_BIT_CFG_INT_TYPE;
	} else {
		irq_set_handler_locked(d, handle_fasteoi_mask_irq);
	}

	raw_spin_lock(&txgpio->lock);
	if (flow_type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_LEVEL_LOW)) {
		bit_cfg |= GPIO_BIT_CFG_PIN_XOR;
		set_bit(txline->line, txgpio->invert_mask);
	} else {
		clear_bit(txline->line, txgpio->invert_mask);
	}
	clear_bit(txline->line, txgpio->od_mask);
	writeq(bit_cfg, txgpio->register_base + bit_cfg_reg(txline->line));
	raw_spin_unlock(&txgpio->lock);

	return IRQ_SET_MASK_OK;
}

static void thunderx_gpio_irq_enable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);

	gpiochip_enable_irq(gc, irqd_to_hwirq(d));
	irq_chip_enable_parent(d);
	thunderx_gpio_irq_unmask(d);
}

static void thunderx_gpio_irq_disable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);

	thunderx_gpio_irq_mask(d);
	irq_chip_disable_parent(d);
	gpiochip_disable_irq(gc, irqd_to_hwirq(d));
}

/*
 * Interrupts are chained from underlying MSI-X vectors.  We have
 * these irq_chip functions to be able to handle level triggering
 * semantics and other acknowledgment tasks associated with the GPIO
 * mechanism.
 */
static const struct irq_chip thunderx_gpio_irq_chip = {
	.name			= "GPIO",
	.irq_enable		= thunderx_gpio_irq_enable,
	.irq_disable		= thunderx_gpio_irq_disable,
	.irq_ack		= thunderx_gpio_irq_ack,
	.irq_mask		= thunderx_gpio_irq_mask,
	.irq_mask_ack		= thunderx_gpio_irq_mask_ack,
	.irq_unmask		= thunderx_gpio_irq_unmask,
	.irq_eoi		= irq_chip_eoi_parent,
	.irq_set_affinity	= irq_chip_set_affinity_parent,
	.irq_set_type		= thunderx_gpio_irq_set_type,
	.flags			= IRQCHIP_SET_TYPE_MASKED | IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int thunderx_gpio_child_to_parent_hwirq(struct gpio_chip *gc,
					       unsigned int child,
					       unsigned int child_type,
					       unsigned int *parent,
					       unsigned int *parent_type)
{
	struct thunderx_gpio *txgpio = gpiochip_get_data(gc);
	struct irq_data *irqd;
	unsigned int irq;

	irq = txgpio->msix_entries[child].vector;
	irqd = irq_domain_get_irq_data(gc->irq.parent_domain, irq);
	if (!irqd)
		return -EINVAL;
	*parent = irqd_to_hwirq(irqd);
	*parent_type = IRQ_TYPE_LEVEL_HIGH;
	return 0;
}

static int thunderx_gpio_populate_parent_alloc_info(struct gpio_chip *chip,
						    union gpio_irq_fwspec *gfwspec,
						    unsigned int parent_hwirq,
						    unsigned int parent_type)
{
	msi_alloc_info_t *info = &gfwspec->msiinfo;

	info->hwirq = parent_hwirq;
	return 0;
}

static int thunderx_gpio_probe(struct pci_dev *pdev,
			       const struct pci_device_id *id)
{
	void __iomem * const *tbl;
	struct device *dev = &pdev->dev;
	struct thunderx_gpio *txgpio;
	struct gpio_chip *chip;
	struct gpio_irq_chip *girq;
	int ngpio, i;
	int err = 0;

	txgpio = devm_kzalloc(dev, sizeof(*txgpio), GFP_KERNEL);
	if (!txgpio)
		return -ENOMEM;

	raw_spin_lock_init(&txgpio->lock);
	chip = &txgpio->chip;

	pci_set_drvdata(pdev, txgpio);

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(dev, "Failed to enable PCI device: err %d\n", err);
		goto out;
	}

	err = pcim_iomap_regions(pdev, 1 << 0, KBUILD_MODNAME);
	if (err) {
		dev_err(dev, "Failed to iomap PCI device: err %d\n", err);
		goto out;
	}

	tbl = pcim_iomap_table(pdev);
	txgpio->register_base = tbl[0];
	if (!txgpio->register_base) {
		dev_err(dev, "Cannot map PCI resource\n");
		err = -ENOMEM;
		goto out;
	}

	if (pdev->subsystem_device == 0xa10a) {
		/* CN88XX has no GPIO_CONST register*/
		ngpio = 50;
		txgpio->base_msi = 48;
	} else {
		u64 c = readq(txgpio->register_base + GPIO_CONST);

		ngpio = c & GPIO_CONST_GPIOS_MASK;
		txgpio->base_msi = (c >> 8) & 0xff;
	}

	txgpio->msix_entries = devm_kcalloc(dev,
					    ngpio, sizeof(struct msix_entry),
					    GFP_KERNEL);
	if (!txgpio->msix_entries) {
		err = -ENOMEM;
		goto out;
	}

	txgpio->line_entries = devm_kcalloc(dev,
					    ngpio,
					    sizeof(struct thunderx_line),
					    GFP_KERNEL);
	if (!txgpio->line_entries) {
		err = -ENOMEM;
		goto out;
	}

	for (i = 0; i < ngpio; i++) {
		u64 bit_cfg = readq(txgpio->register_base + bit_cfg_reg(i));

		txgpio->msix_entries[i].entry = txgpio->base_msi + (2 * i);
		txgpio->line_entries[i].line = i;
		txgpio->line_entries[i].txgpio = txgpio;
		/*
		 * If something has already programmed the pin, use
		 * the existing glitch filter settings, otherwise go
		 * to 400nS.
		 */
		txgpio->line_entries[i].fil_bits = bit_cfg ?
			(bit_cfg & GPIO_BIT_CFG_FIL_MASK) : GLITCH_FILTER_400NS;

		if ((bit_cfg & GPIO_BIT_CFG_TX_OE) && (bit_cfg & GPIO_BIT_CFG_TX_OD))
			set_bit(i, txgpio->od_mask);
		if (bit_cfg & GPIO_BIT_CFG_PIN_XOR)
			set_bit(i, txgpio->invert_mask);
	}


	/* Enable all MSI-X for interrupts on all possible lines. */
	err = pci_enable_msix_range(pdev, txgpio->msix_entries, ngpio, ngpio);
	if (err < 0)
		goto out;

	chip->label = KBUILD_MODNAME;
	chip->parent = dev;
	chip->owner = THIS_MODULE;
	chip->request = thunderx_gpio_request;
	chip->base = -1; /* System allocated */
	chip->can_sleep = false;
	chip->ngpio = ngpio;
	chip->get_direction = thunderx_gpio_get_direction;
	chip->direction_input = thunderx_gpio_dir_in;
	chip->get = thunderx_gpio_get;
	chip->direction_output = thunderx_gpio_dir_out;
	chip->set = thunderx_gpio_set;
	chip->set_multiple = thunderx_gpio_set_multiple;
	chip->set_config = thunderx_gpio_set_config;
	girq = &chip->irq;
	gpio_irq_chip_set_chip(girq, &thunderx_gpio_irq_chip);
	girq->fwnode = dev_fwnode(dev);
	girq->parent_domain =
		irq_get_irq_data(txgpio->msix_entries[0].vector)->domain;
	girq->child_to_parent_hwirq = thunderx_gpio_child_to_parent_hwirq;
	girq->populate_parent_alloc_arg = thunderx_gpio_populate_parent_alloc_info;
	girq->handler = handle_bad_irq;
	girq->default_type = IRQ_TYPE_NONE;

	err = devm_gpiochip_add_data(dev, chip, txgpio);
	if (err)
		goto out;

	/* Push on irq_data and the domain for each line. */
	for (i = 0; i < ngpio; i++) {
		struct irq_fwspec fwspec;

		fwspec.fwnode = dev_fwnode(dev);
		fwspec.param_count = 2;
		fwspec.param[0] = i;
		fwspec.param[1] = IRQ_TYPE_NONE;
		err = irq_domain_push_irq(girq->domain,
					  txgpio->msix_entries[i].vector,
					  &fwspec);
		if (err < 0)
			dev_err(dev, "irq_domain_push_irq: %d\n", err);
	}

	dev_info(dev, "ThunderX GPIO: %d lines with base %d.\n",
		 ngpio, chip->base);
	return 0;
out:
	pci_set_drvdata(pdev, NULL);
	return err;
}

static void thunderx_gpio_remove(struct pci_dev *pdev)
{
	int i;
	struct thunderx_gpio *txgpio = pci_get_drvdata(pdev);

	for (i = 0; i < txgpio->chip.ngpio; i++)
		irq_domain_pop_irq(txgpio->chip.irq.domain,
				   txgpio->msix_entries[i].vector);

	irq_domain_remove(txgpio->chip.irq.domain);

	pci_set_drvdata(pdev, NULL);
}

static const struct pci_device_id thunderx_gpio_id_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_CAVIUM, 0xA00A) },
	{ 0, }	/* end of table */
};

MODULE_DEVICE_TABLE(pci, thunderx_gpio_id_table);

static struct pci_driver thunderx_gpio_driver = {
	.name = KBUILD_MODNAME,
	.id_table = thunderx_gpio_id_table,
	.probe = thunderx_gpio_probe,
	.remove = thunderx_gpio_remove,
};

module_pci_driver(thunderx_gpio_driver);

MODULE_DESCRIPTION("Cavium Inc. ThunderX/OCTEON-TX GPIO Driver");
MODULE_LICENSE("GPL");
