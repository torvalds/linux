// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for the external-charger IRQ pass-through function of the
 * Intel Bay Trail Crystal Cove PMIC.
 *
 * Note this is NOT a power_supply class driver, it just deals with IRQ
 * pass-through, this requires a separate driver because the PMIC's
 * level 2 interrupt for this must be explicitly acked.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define CHGRIRQ_REG					0x0a
#define MCHGRIRQ_REG					0x17

struct crystal_cove_charger_data {
	struct mutex buslock; /* irq_bus_lock */
	struct irq_chip irqchip;
	struct regmap *regmap;
	struct irq_domain *irq_domain;
	int irq;
	int charger_irq;
	u8 mask;
	u8 new_mask;
};

static irqreturn_t crystal_cove_charger_irq(int irq, void *data)
{
	struct crystal_cove_charger_data *charger = data;

	/* No need to read CHGRIRQ_REG as there is only 1 IRQ */
	handle_nested_irq(charger->charger_irq);

	/* Ack CHGRIRQ 0 */
	regmap_write(charger->regmap, CHGRIRQ_REG, BIT(0));

	return IRQ_HANDLED;
}

static void crystal_cove_charger_irq_bus_lock(struct irq_data *data)
{
	struct crystal_cove_charger_data *charger = irq_data_get_irq_chip_data(data);

	mutex_lock(&charger->buslock);
}

static void crystal_cove_charger_irq_bus_sync_unlock(struct irq_data *data)
{
	struct crystal_cove_charger_data *charger = irq_data_get_irq_chip_data(data);

	if (charger->mask != charger->new_mask) {
		regmap_write(charger->regmap, MCHGRIRQ_REG, charger->new_mask);
		charger->mask = charger->new_mask;
	}

	mutex_unlock(&charger->buslock);
}

static void crystal_cove_charger_irq_unmask(struct irq_data *data)
{
	struct crystal_cove_charger_data *charger = irq_data_get_irq_chip_data(data);

	charger->new_mask &= ~BIT(data->hwirq);
}

static void crystal_cove_charger_irq_mask(struct irq_data *data)
{
	struct crystal_cove_charger_data *charger = irq_data_get_irq_chip_data(data);

	charger->new_mask |= BIT(data->hwirq);
}

static void crystal_cove_charger_rm_irq_domain(void *data)
{
	struct crystal_cove_charger_data *charger = data;

	irq_domain_remove(charger->irq_domain);
}

static int crystal_cove_charger_probe(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(pdev->dev.parent);
	struct crystal_cove_charger_data *charger;
	int ret;

	charger = devm_kzalloc(&pdev->dev, sizeof(*charger), GFP_KERNEL);
	if (!charger)
		return -ENOMEM;

	charger->regmap = pmic->regmap;
	mutex_init(&charger->buslock);

	charger->irq = platform_get_irq(pdev, 0);
	if (charger->irq < 0)
		return charger->irq;

	charger->irq_domain = irq_domain_create_linear(dev_fwnode(pdev->dev.parent), 1,
						       &irq_domain_simple_ops, NULL);
	if (!charger->irq_domain)
		return -ENOMEM;

	/* Distuingish IRQ domain from others sharing (MFD) the same fwnode */
	irq_domain_update_bus_token(charger->irq_domain, DOMAIN_BUS_WAKEUP);

	ret = devm_add_action_or_reset(&pdev->dev, crystal_cove_charger_rm_irq_domain, charger);
	if (ret)
		return ret;

	charger->charger_irq = irq_create_mapping(charger->irq_domain, 0);
	if (!charger->charger_irq)
		return -ENOMEM;

	charger->irqchip.name = KBUILD_MODNAME;
	charger->irqchip.irq_unmask = crystal_cove_charger_irq_unmask;
	charger->irqchip.irq_mask = crystal_cove_charger_irq_mask;
	charger->irqchip.irq_bus_lock = crystal_cove_charger_irq_bus_lock;
	charger->irqchip.irq_bus_sync_unlock = crystal_cove_charger_irq_bus_sync_unlock;

	irq_set_chip_data(charger->charger_irq, charger);
	irq_set_chip_and_handler(charger->charger_irq, &charger->irqchip, handle_simple_irq);
	irq_set_nested_thread(charger->charger_irq, true);
	irq_set_noprobe(charger->charger_irq);

	/* Mask the single 2nd level IRQ before enabling the 1st level IRQ */
	charger->mask = charger->new_mask = BIT(0);
	regmap_write(charger->regmap, MCHGRIRQ_REG, charger->mask);

	ret = devm_request_threaded_irq(&pdev->dev, charger->irq, NULL,
					crystal_cove_charger_irq,
					IRQF_ONESHOT, KBUILD_MODNAME, charger);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "requesting irq\n");

	return 0;
}

static struct platform_driver crystal_cove_charger_driver = {
	.probe = crystal_cove_charger_probe,
	.driver = {
		.name = "crystal_cove_charger",
	},
};
module_platform_driver(crystal_cove_charger_driver);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com");
MODULE_DESCRIPTION("Intel Bay Trail Crystal Cove external charger IRQ pass-through");
MODULE_LICENSE("GPL");
