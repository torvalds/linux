/*
 * Device driver for regulators in HISI PMIC IC
 *
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (c) 2011 Hisilicon.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/mfd/hisi_pmic.h>
#include <linux/irq.h>
#include <linux/spmi.h>
#ifndef NO_IRQ
#define NO_IRQ       0
#endif

/* 8-bit register offset in PMIC */
#define HISI_MASK_STATE			0xff

#define HISI_IRQ_KEY_NUM		0
#define HISI_IRQ_KEY_VALUE		0xc0
#define HISI_IRQ_KEY_DOWN		7
#define HISI_IRQ_KEY_UP			6

/*#define HISI_NR_IRQ			25*/
#define HISI_MASK_FIELD		0xFF
#define HISI_BITS			8
#define PMIC_FPGA_FLAG          1

/*define the first group interrupt register number*/
#define HISI_PMIC_FIRST_GROUP_INT_NUM        2

static struct bit_info g_pmic_vbus = {0};
#ifndef BIT
#define BIT(x)		(0x1U << (x))
#endif

static struct hisi_pmic *g_pmic;
static unsigned int g_extinterrupt_flag  = 0;
static struct of_device_id of_hisi_pmic_match_tbl[] = {
	{
		.compatible = "hisilicon-hisi-pmic-spmi",
	},
	{ /* end */ }
};

/*
 * The PMIC register is only 8-bit.
 * Hisilicon SoC use hardware to map PMIC register into SoC mapping.
 * At here, we are accessing SoC register with 32-bit.
 */
u32 hisi_pmic_read(struct hisi_pmic *pmic, int reg)
{
	u32 ret;
	u8 read_value = 0;
	struct spmi_device *pdev;

	if (NULL == g_pmic) {
		pr_err(" g_pmic  is NULL\n");
		return 0;
	}

	pdev = to_spmi_device(g_pmic->dev);
	if (NULL == pdev) {
		pr_err("%s:pdev get failed!\n", __func__);
		return 0;
	}

	ret = spmi_ext_register_readl(pdev, reg, (unsigned char*)&read_value, 1);/*lint !e734 !e732 */
	if (ret) {
		pr_err("%s:spmi_ext_register_readl failed!\n", __func__);
		return ret;
	}
	return (u32)read_value;
}
EXPORT_SYMBOL(hisi_pmic_read);

void hisi_pmic_write(struct hisi_pmic *pmic, int reg, u32 val)
{
	u32 ret;
	struct spmi_device *pdev;

	if (NULL == g_pmic) {
		pr_err(" g_pmic  is NULL\n");
		return;
	}

	pdev = to_spmi_device(g_pmic->dev);
	if (NULL == pdev) {
		pr_err("%s:pdev get failed!\n", __func__);
		return;
	}

	ret = spmi_ext_register_writel(pdev, reg, (unsigned char*)&val, 1);/*lint !e734 !e732 */
	if (ret) {
		pr_err("%s:spmi_ext_register_writel failed!\n", __func__);
		return ;
	}
}
EXPORT_SYMBOL(hisi_pmic_write);

#ifdef CONFIG_HISI_DIEID
u32 hisi_pmic_read_sub_pmu(u8 sid, int reg)
{
	u32 ret;
	u8 read_value = 0;
	struct spmi_device *pdev;

	if(strstr(saved_command_line, "androidboot.swtype=factory"))
	{
		if (NULL == g_pmic) {
			pr_err(" g_pmic  is NULL\n");
			return -1;/*lint !e570 */
		}

		pdev = to_spmi_device(g_pmic->dev);
		if (NULL == pdev) {
			pr_err("%s:pdev get failed!\n", __func__);
			return -1;/*lint !e570 */
		}

		ret = spmi_ext_register_readl(pdev->ctrl, sid, reg, (unsigned char*)&read_value, 1);/*lint !e734 !e732 */
		if (ret) {
			pr_err("%s:spmi_ext_register_readl failed!\n", __func__);
			return ret;
		}
		return (u32)read_value;
	}
	return  0;
}
EXPORT_SYMBOL(hisi_pmic_read_sub_pmu);

void hisi_pmic_write_sub_pmu(u8 sid, int reg, u32 val)
{
	u32 ret;
	struct spmi_device *pdev;
	if(strstr(saved_command_line, "androidboot.swtype=factory"))
	{
		if (NULL == g_pmic) {
			pr_err(" g_pmic  is NULL\n");
			return;
		}

		pdev = to_spmi_device(g_pmic->dev);
		if (NULL == pdev) {
			pr_err("%s:pdev get failed!\n", __func__);
			return;
		}

		ret = spmi_ext_register_writel(pdev->ctrl, sid, reg, (unsigned char*)&val, 1);/*lint !e734 !e732 */
		if (ret) {
			pr_err("%s:spmi_ext_register_writel failed!\n", __func__);
			return ;
		}
	}

	return ;
}
EXPORT_SYMBOL(hisi_pmic_write_sub_pmu);
#endif

void hisi_pmic_rmw(struct hisi_pmic *pmic, int reg,
		     u32 mask, u32 bits)
{
	u32 data;
	unsigned long flags;

	if (NULL == g_pmic) {
		pr_err(" g_pmic  is NULL\n");
		return;
	}

	spin_lock_irqsave(&g_pmic->lock, flags);
	data = hisi_pmic_read(pmic, reg) & ~mask;
	data |= mask & bits;
	hisi_pmic_write(pmic, reg, data);
	spin_unlock_irqrestore(&g_pmic->lock, flags);
}
EXPORT_SYMBOL(hisi_pmic_rmw);

unsigned int hisi_pmic_reg_read(int addr)
{
	return (unsigned int)hisi_pmic_read(g_pmic, addr);
}
EXPORT_SYMBOL(hisi_pmic_reg_read);

void hisi_pmic_reg_write(int addr, int val)
{
	hisi_pmic_write(g_pmic, addr, val);
}
EXPORT_SYMBOL(hisi_pmic_reg_write);

void hisi_pmic_reg_write_lock(int addr, int val)
{
	unsigned long flags;

	if (NULL == g_pmic) {
		pr_err(" g_pmic  is NULL\n");
		return;
	}

	spin_lock_irqsave(&g_pmic->lock, flags);
	hisi_pmic_write(g_pmic, g_pmic->normal_lock.addr, g_pmic->normal_lock.val);
	hisi_pmic_write(g_pmic, g_pmic->debug_lock.addr, g_pmic->debug_lock.val);
	hisi_pmic_write(g_pmic, addr, val);
	hisi_pmic_write(g_pmic, g_pmic->normal_lock.addr, 0);
	hisi_pmic_write(g_pmic, g_pmic->debug_lock.addr, 0);
	spin_unlock_irqrestore(&g_pmic->lock, flags);
}

int hisi_pmic_array_read(int addr, char *buff, unsigned int len)
{
	unsigned int i;

	if ((len > 32) || (NULL == buff)) {
		return -EINVAL;
	}

	/*
	 * Here is a bug in the pmu die.
	 * the coul driver will read 4 bytes,
	 * but the ssi bus only read 1 byte, and the pmu die
	 * will make sampling 1/10669us about vol cur,so the driver
	 * read the data is not the same sampling
	 */
	for (i = 0; i < len; i++)
	{
		*(buff + i) = hisi_pmic_reg_read(addr+i);
	}

	return 0;
}

int hisi_pmic_array_write(int addr, char *buff, unsigned int len)
{
    unsigned int i;

	if ((len > 32) || (NULL == buff)) {
		return -EINVAL;
	}

	for (i = 0; i < len; i++)
	{
		hisi_pmic_reg_write(addr+i, *(buff + i));
	}

	return 0;
}

static irqreturn_t hisi_irq_handler(int irq, void *data)
{
	struct hisi_pmic *pmic = (struct hisi_pmic *)data;
	unsigned long pending;
	int i, offset;

	for (i = 0; i < pmic->irqarray; i++) {
		pending = hisi_pmic_reg_read((i + pmic->irq_addr.start_addr));
		pending &= HISI_MASK_FIELD;
		if (pending != 0) {
			pr_info("pending[%d]=0x%lx\n\r", i, pending);
		}

		hisi_pmic_reg_write((i + pmic->irq_addr.start_addr), pending);

		/*solve powerkey order*/
		if ((HISI_IRQ_KEY_NUM == i) && ((pending & HISI_IRQ_KEY_VALUE) == HISI_IRQ_KEY_VALUE)) {
			generic_handle_irq(pmic->irqs[HISI_IRQ_KEY_DOWN]);
			generic_handle_irq(pmic->irqs[HISI_IRQ_KEY_UP]);
			pending &= (~HISI_IRQ_KEY_VALUE);
		}

		if (pending) {
			for_each_set_bit(offset, &pending, HISI_BITS)
				generic_handle_irq(pmic->irqs[offset + i * HISI_BITS]);/*lint !e679 */
		}
	}

	/*Handle the second group irq if analysis the second group irq from dtsi*/
	if (1 == g_extinterrupt_flag){
		for (i = 0; i < pmic->irqarray1; i++) {
			pending = hisi_pmic_reg_read((i + pmic->irq_addr1.start_addr));
			pending &= HISI_MASK_FIELD;
			if (pending != 0) {
				pr_info("pending[%d]=0x%lx\n\r", i, pending);
			}

			hisi_pmic_reg_write((i + pmic->irq_addr1.start_addr), pending);

			if (pending) {
				for_each_set_bit(offset, &pending, HISI_BITS)
					generic_handle_irq(pmic->irqs[offset + (i+HISI_PMIC_FIRST_GROUP_INT_NUM) * HISI_BITS]);/*lint !e679 */
			}
		}
	}

	return IRQ_HANDLED;
}

static void hisi_irq_mask(struct irq_data *d)
{
	struct hisi_pmic *pmic = irq_data_get_irq_chip_data(d);
	u32 data, offset;
	unsigned long flags;

	if (NULL == g_pmic) {
		pr_err(" g_pmic  is NULL\n");
		return;
	}

	offset = (irqd_to_hwirq(d) >> 3);
	if (1==g_extinterrupt_flag){
		if ( offset < HISI_PMIC_FIRST_GROUP_INT_NUM)
			offset += pmic->irq_mask_addr.start_addr;
		else/*Change addr when irq num larger than 16 because interrupt addr is nonsequence*/
			offset = offset+(pmic->irq_mask_addr1.start_addr)-HISI_PMIC_FIRST_GROUP_INT_NUM;
	}else{
		offset += pmic->irq_mask_addr.start_addr;
	}
	spin_lock_irqsave(&g_pmic->lock, flags);
	data = hisi_pmic_reg_read(offset);
	data |= (1 << (irqd_to_hwirq(d) & 0x07));
	hisi_pmic_reg_write(offset, data);
	spin_unlock_irqrestore(&g_pmic->lock, flags);
}

static void hisi_irq_unmask(struct irq_data *d)
{
	struct hisi_pmic *pmic = irq_data_get_irq_chip_data(d);
	u32 data, offset;
	unsigned long flags;

	if (NULL == g_pmic) {
		pr_err(" g_pmic  is NULL\n");
		return;
	}

	offset = (irqd_to_hwirq(d) >> 3);
	if (1==g_extinterrupt_flag){
		if ( offset < HISI_PMIC_FIRST_GROUP_INT_NUM)
			offset += pmic->irq_mask_addr.start_addr;
		else
			offset = offset+(pmic->irq_mask_addr1.start_addr)-HISI_PMIC_FIRST_GROUP_INT_NUM;
	}else{
		offset += pmic->irq_mask_addr.start_addr;
	}
	spin_lock_irqsave(&g_pmic->lock, flags);
	data = hisi_pmic_reg_read(offset);
	data &= ~(1 << (irqd_to_hwirq(d) & 0x07)); /*lint !e502 */
	hisi_pmic_reg_write(offset, data);
	spin_unlock_irqrestore(&g_pmic->lock, flags);
}

static struct irq_chip hisi_pmu_irqchip = {
	.name		= "hisi-irq",
	.irq_mask	= hisi_irq_mask,
	.irq_unmask	= hisi_irq_unmask,
	.irq_disable	= hisi_irq_mask,
	.irq_enable	= hisi_irq_unmask,
};

static int hisi_irq_map(struct irq_domain *d, unsigned int virq,
			  irq_hw_number_t hw)
{
	struct hisi_pmic *pmic = d->host_data;

	irq_set_chip_and_handler_name(virq, &hisi_pmu_irqchip,
				      handle_simple_irq, "hisi");
	irq_set_chip_data(virq, pmic);
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static struct irq_domain_ops hisi_domain_ops = {
	.map	= hisi_irq_map,
	.xlate	= irq_domain_xlate_twocell,
};

/*lint -e570 -e64*/
static int get_pmic_device_tree_data(struct device_node *np, struct hisi_pmic *pmic)
{
	int ret = 0;

	/*get pmic irq num*/
	ret = of_property_read_u32_array(np, "hisilicon,hisi-pmic-irq-num",
						&(pmic->irqnum), 1);
	if (ret) {
		pr_err("no hisilicon,hisi-pmic-irq-num property set\n");
		ret = -ENODEV;
		return ret;
	}

	/*get pmic irq array number*/
	ret = of_property_read_u32_array(np, "hisilicon,hisi-pmic-irq-array",
						&(pmic->irqarray), 1);
	if (ret) {
		pr_err("no hisilicon,hisi-pmic-irq-array property set\n");
		ret = -ENODEV;
		return ret;
	}

	/*SOC_PMIC_IRQ_MASK_0_ADDR*/
	ret = of_property_read_u32_array(np, "hisilicon,hisi-pmic-irq-mask-addr",
						(int *)&pmic->irq_mask_addr, 2);
	if (ret) {
		pr_err("no hisilicon,hisi-pmic-irq-mask-addr property set\n");
		ret = -ENODEV;
		return ret;
	}

	/*SOC_PMIC_IRQ0_ADDR*/
	ret = of_property_read_u32_array(np, "hisilicon,hisi-pmic-irq-addr",
						(int *)&pmic->irq_addr, 2);
	if (ret) {
		pr_err("no hisilicon,hisi-pmic-irq-addr property set\n");
		ret = -ENODEV;
		return ret;
	}

	ret = of_property_read_u32_array(np, "hisilicon,hisi-pmic-vbus",
						(u32 *)&g_pmic_vbus, 2);
	if (ret) {
		pr_err("no hisilicon,hisi-pmic-vbus property\n");
		ret = -ENODEV;
		return ret;
	}

	/*pmic lock*/
	ret = of_property_read_u32_array(np, "hisilicon,hisi-pmic-lock",
						(int *)&pmic->normal_lock, 2);
	if (ret) {
		pr_err("no hisilicon,hisi-pmic-lock property set\n");
		ret = -ENODEV;
		return ret;
	}

	/*pmic debug lock*/
	ret = of_property_read_u32_array(np, "hisilicon,hisi-pmic-debug-lock",
						(int *)&pmic->debug_lock, 2);
	if (ret) {
		pr_err("no hisilicon,hisi-pmic-debug-lock property set\n");
		ret = -ENODEV;
		return ret;
	}

	return ret;
}/*lint -restore*/


/*lint -e570 -e64*/
static int get_pmic_device_tree_data1(struct device_node *np, struct hisi_pmic *pmic)
{
	int ret = 0;

	/*get pmic irq num*/
	ret = of_property_read_u32_array(np, "hisilicon,hisi-pmic-irq-num1",
						&(pmic->irqnum1), 1);
	if (ret) {
		pr_err("no hisilicon,hisi-pmic-irq-num1 property set\n");
		ret = -ENODEV;
		pmic->irqnum1 = 0;
		return ret;
	}

	/*get pmic irq array number*/
	ret = of_property_read_u32_array(np, "hisilicon,hisi-pmic-irq-array1",
						&(pmic->irqarray1), 1);
	if (ret) {
		pr_err("no hisilicon,hisi-pmic-irq-array1 property set\n");
		ret = -ENODEV;
		return ret;
	}

	/*SOC_PMIC_IRQ_MASK_0_ADDR*/
	ret = of_property_read_u32_array(np, "hisilicon,hisi-pmic-irq-mask-addr1",
						(int *)&pmic->irq_mask_addr1, 2);
	if (ret) {
		pr_err("no hisilicon,hisi-pmic-irq-mask-addr1 property set\n");
		ret = -ENODEV;
		return ret;
	}

	/*SOC_PMIC_IRQ0_ADDR*/
	ret = of_property_read_u32_array(np, "hisilicon,hisi-pmic-irq-addr1",
						(int *)&pmic->irq_addr1, 2);
	if (ret) {
		pr_err("no hisilicon,hisi-pmic-irq-addr1 property set\n");
		ret = -ENODEV;
		return ret;
	}

	g_extinterrupt_flag = 1;
	return ret;
}/*lint -restore*/

int hisi_get_pmic_irq_byname(unsigned int pmic_irq_list)
{
	if ( NULL == g_pmic ) {
		pr_err("[%s]g_pmic is NULL\n", __func__);
		return -1;
	}

	if (pmic_irq_list > (unsigned int)g_pmic->irqnum) {
		pr_err("[%s]input pmic irq number is error.\n", __func__);
		return -1;
	}
	pr_info("%s:g_pmic->irqs[%d]=%d\n", __func__, pmic_irq_list, g_pmic->irqs[pmic_irq_list]);
	return (int)g_pmic->irqs[pmic_irq_list];
}
EXPORT_SYMBOL(hisi_get_pmic_irq_byname);

int hisi_pmic_get_vbus_status(void)
{
	if (0 == g_pmic_vbus.addr)
		return -1;

	if (hisi_pmic_reg_read(g_pmic_vbus.addr) & BIT(g_pmic_vbus.bit))
		return 1;

	return 0;
}
EXPORT_SYMBOL(hisi_pmic_get_vbus_status);

static void hisi_pmic_irq_prc(struct hisi_pmic *pmic)
{
	int i;
	for (i = 0 ; i < pmic->irq_mask_addr.array; i++) {
		hisi_pmic_write(pmic, pmic->irq_mask_addr.start_addr + i, HISI_MASK_STATE);
	}

	for (i = 0 ; i < pmic->irq_addr.array; i++) {
		unsigned int pending = hisi_pmic_read(pmic, pmic->irq_addr.start_addr + i);
		pr_debug("PMU IRQ address value:irq[0x%x] = 0x%x\n", pmic->irq_addr.start_addr + i, pending);
		hisi_pmic_write(pmic, pmic->irq_addr.start_addr + i, HISI_MASK_STATE);
	}

}

static void hisi_pmic_irq1_prc(struct hisi_pmic *pmic)
{
	int i;
	if(1 == g_extinterrupt_flag){
		for (i = 0 ; i < pmic->irq_mask_addr1.array; i++) {
			hisi_pmic_write(pmic, pmic->irq_mask_addr1.start_addr + i, HISI_MASK_STATE);
		}

		for (i = 0 ; i < pmic->irq_addr1.array; i++) {
			unsigned int pending1 = hisi_pmic_read(pmic, pmic->irq_addr1.start_addr + i);
			pr_debug("PMU IRQ address1 value:irq[0x%x] = 0x%x\n", pmic->irq_addr1.start_addr + i, pending1);
			hisi_pmic_write(pmic, pmic->irq_addr1.start_addr + i, HISI_MASK_STATE);
		}
	}
}

static int hisi_pmic_probe(struct spmi_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct hisi_pmic *pmic = NULL;
	enum of_gpio_flags flags;
	int ret = 0;
	int i;
	unsigned int fpga_flag = 0;
	unsigned int virq;

	pmic = devm_kzalloc(dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic) {
		dev_err(dev, "cannot allocate hisi_pmic device info\n");
		return -ENOMEM;
	}

	/*TODO: get pmic dts info*/
	ret = get_pmic_device_tree_data(np, pmic);
	if (ret) {
		dev_err(&pdev->dev, "Error reading hisi pmic dts \n");
		return ret;
	}

	/*get pmic dts the second group irq*/
	ret = get_pmic_device_tree_data1(np, pmic);
	if (ret) {
		dev_err(&pdev->dev, "the platform don't support ext-interrupt.\n");
	}

	/* TODO: get and enable clk request */
	spin_lock_init(&pmic->lock);

	pmic->dev = dev;
	g_pmic = pmic;
	ret = of_property_read_u32_array(np, "hisilicon,pmic_fpga_flag", &fpga_flag, 1);
	if (ret) {
		pr_err("no hisilicon,pmic_fpga_flag property set\n");
	}
	if (PMIC_FPGA_FLAG == fpga_flag) {
		goto after_irq_register;
	}

	pmic->gpio = of_get_gpio_flags(np, 0, &flags);
	if (pmic->gpio < 0)
		return pmic->gpio;

	if (!gpio_is_valid(pmic->gpio))
		return -EINVAL;

	ret = gpio_request_one(pmic->gpio, GPIOF_IN, "pmic");
	if (ret < 0) {
		dev_err(dev, "failed to request gpio%d\n", pmic->gpio);
		return ret;
	}

	pmic->irq = gpio_to_irq(pmic->gpio);

	/* mask && clear IRQ status */
	hisi_pmic_irq_prc(pmic);
	/*clear && mask the new adding irq*/
	hisi_pmic_irq1_prc(pmic);

	pmic->irqnum += pmic->irqnum1;

	pmic->irqs = (unsigned int *)devm_kmalloc(dev, pmic->irqnum * sizeof(int), GFP_KERNEL);
	if (!pmic->irqs) {
		pr_err("%s:Failed to alloc memory for pmic irq number!\n", __func__);
		goto irq_malloc;
	}
	memset(pmic->irqs, 0, pmic->irqnum);

	pmic->domain = irq_domain_add_simple(np, pmic->irqnum, 0,
					     &hisi_domain_ops, pmic);
	if (!pmic->domain) {
		dev_err(dev, "failed irq domain add simple!\n");
		ret = -ENODEV;
		goto irq_domain;
	}

	for (i = 0; i < pmic->irqnum; i++) {
		virq = irq_create_mapping(pmic->domain, i);
		if (virq == NO_IRQ) {
			pr_debug("Failed mapping hwirq\n");
			ret = -ENOSPC;
			goto irq_create_mapping;
		}
		pmic->irqs[i] = virq;
		pr_info("[%s]. pmic->irqs[%d] = %d\n", __func__, i, pmic->irqs[i]);
	}

	ret = request_threaded_irq(pmic->irq, hisi_irq_handler, NULL,
				IRQF_TRIGGER_LOW | IRQF_SHARED | IRQF_NO_SUSPEND,
				   "pmic", pmic);
	if (ret < 0) {
		dev_err(dev, "could not claim pmic %d\n", ret);
		ret = -ENODEV;
		goto request_theaded_irq;
	}

after_irq_register:
	return 0;


request_theaded_irq:
irq_create_mapping:
irq_domain:
irq_malloc:
	gpio_free(pmic->gpio);
	g_pmic = NULL;
	return ret;
}

static void hisi_pmic_remove(struct spmi_device *pdev)
{

	struct hisi_pmic *pmic = dev_get_drvdata(&pdev->dev);

	free_irq(pmic->irq, pmic);
	gpio_free(pmic->gpio);
	devm_kfree(&pdev->dev, pmic);

}
static int hisi_pmic_suspend(struct device *dev, pm_message_t state)
{
	struct hisi_pmic *pmic = dev_get_drvdata(dev);

	if (NULL == pmic) {
		pr_err("%s:pmic is NULL\n", __func__);
		return -ENOMEM;
	}

	pr_info("%s:+\n", __func__);
	pr_info("%s:-\n", __func__);

	return 0;
}/*lint !e715 */

static int hisi_pmic_resume(struct device *dev)
{
	struct hisi_pmic *pmic = dev_get_drvdata(dev);

	if (NULL == pmic) {
		pr_err("%s:pmic is NULL\n", __func__);
		return -ENOMEM;
	}

	pr_info("%s:+\n", __func__);
	pr_info("%s:-\n", __func__);

	return 0;
}

MODULE_DEVICE_TABLE(spmi, pmic_spmi_id);
static struct spmi_driver hisi_pmic_driver = {
	.driver = {
		.name	= "hisi_pmic",
		.owner  = THIS_MODULE,
		.of_match_table = of_hisi_pmic_match_tbl,
		.suspend = hisi_pmic_suspend,
		.resume = hisi_pmic_resume,
	},
	.probe	= hisi_pmic_probe,
	.remove	= hisi_pmic_remove,
};

static int __init hisi_pmic_init(void)
{
	return spmi_driver_register(&hisi_pmic_driver);
}

static void __exit hisi_pmic_exit(void)
{
	spmi_driver_unregister(&hisi_pmic_driver);
}


subsys_initcall_sync(hisi_pmic_init);
module_exit(hisi_pmic_exit);

MODULE_DESCRIPTION("PMIC driver");
MODULE_LICENSE("GPL v2");

