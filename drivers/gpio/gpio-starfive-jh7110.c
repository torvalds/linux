/*
 ******************************************************************************
 * @file  gpio-starfive-vic7110.c
 * @author  StarFive Technology
 * @version  V1.0
 * @date  08/13/2020
 * @brief
 ******************************************************************************
 * @copy
 *
 * THE PRESENT SOFTWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
 * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
 * TIME. AS A RESULT, STARFIVE SHALL NOT BE HELD LIABLE FOR ANY
 * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
 * FROM THE CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
 * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
 *
 * COPYRIGHT 2020 Shanghai StarFive Technology Co., Ltd.
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/of_irq.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>

#define GPIO_EN		0xe0
#define GPIO_IS_LOW	0xe4
#define GPIO_IS_HIGH	0xe8
#define GPIO_IBE_LOW	0xf4
#define GPIO_IBE_HIGH	0xf8
#define GPIO_IEV_LOW	0xfc
#define GPIO_IEV_HIGH	0x100
#define GPIO_IE_LOW	0x104
#define GPIO_IE_HIGH	0x108
#define GPIO_IC_LOW	0xec
#define GPIO_IC_HIGH	0xf0
//read only
#define GPIO_RIS_LOW	0x10c
#define GPIO_RIS_HIGH	0x110
#define GPIO_MIS_LOW	0x114
#define GPIO_MIS_HIGH	0x118
#define GPIO_DIN_LOW	0x11C
#define GPIO_DIN_HIGH	0x120

#define GPIO_DOUT_X_REG	0x0
#define GPIO_DOEN_X_REG	0x40

#define GPIO_INPUT_ENABLE_X_REG	0x124

#define MAX_GPIO	 64

#define PROC_VIC "vic_gpio7110"

struct sfvic7110_gpio {
	raw_spinlock_t		lock;
	void __iomem		*base;
	struct gpio_chip	gc;
	unsigned long		enabled;
	unsigned		trigger[MAX_GPIO];
	unsigned int		irq_parent[MAX_GPIO];
	struct sfvic7110_gpio	*self_ptr[MAX_GPIO];
};

/* lock for procfs read access */
static DEFINE_MUTEX(read_lock);

/* lock for procfs write access */
static DEFINE_MUTEX(write_lock);

static DEFINE_SPINLOCK(sfg_lock);

static void __iomem *gpio_base = NULL;

static int sfvic7110_direction_input(struct gpio_chip *gc, unsigned offset)
{
	struct sfvic7110_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;
	unsigned int v;

	if (offset >= gc->ngpio)
		return -EINVAL;

	raw_spin_lock_irqsave(&chip->lock, flags);
	v = readl_relaxed(chip->base + GPIO_DOEN_X_REG + (offset & ~0x3));
	v &= ~(0x3f << ((offset & 0x3) * 8));
	v |= 1 << ((offset & 0x3) * 8);
	writel_relaxed(v, chip->base + GPIO_DOEN_X_REG + (offset & ~0x3));
	raw_spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int sfvic7110_direction_output(struct gpio_chip *gc, unsigned offset, int value)
{
	struct sfvic7110_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;
	unsigned int v;

	if (offset >= gc->ngpio)
		return -EINVAL;
	raw_spin_lock_irqsave(&chip->lock, flags);
	v = readl_relaxed(chip->base + GPIO_DOEN_X_REG + (offset & ~0x3));
	v &= ~(0x3f << ((offset & 0x3) * 8));
	writel_relaxed(v, chip->base + GPIO_DOEN_X_REG + (offset & ~0x3));

	v = readl_relaxed(chip->base + GPIO_DOUT_X_REG + (offset & ~0x3));
	v &= ~(0x3f << ((offset & 0x3) * 8));
	v |= value << ((offset & 0x3) * 8);
	writel_relaxed(v, chip->base + GPIO_DOUT_X_REG + (offset & ~0x3));
	raw_spin_unlock_irqrestore(&chip->lock, flags);

	return 0;
}

static int sfvic7110_get_direction(struct gpio_chip *gc, unsigned offset)
{
	struct sfvic7110_gpio *chip = gpiochip_get_data(gc);
	unsigned int v;

	if (offset >= gc->ngpio)
		return -EINVAL;

	v = readl_relaxed(chip->base + GPIO_DOEN_X_REG + (offset & ~0x3));
	return !!(v & (0x3f << ((offset & 0x3) * 8)));
}

static int sfvic7110_get_value(struct gpio_chip *gc, unsigned offset)
{
	struct sfvic7110_gpio *chip = gpiochip_get_data(gc);
	int value;

	if (offset >= gc->ngpio)
		return -EINVAL;

	if(offset < 32){
		value = readl_relaxed(chip->base + GPIO_DIN_LOW);
		return (value >> offset) & 0x1;
	} else {
		value = readl_relaxed(chip->base + GPIO_DIN_HIGH);
		return (value >> (offset - 32)) & 0x1;
	}
}

static void sfvic7110_set_value(struct gpio_chip *gc, unsigned offset, int value)
{
	struct sfvic7110_gpio *chip = gpiochip_get_data(gc);
	unsigned long flags;
	unsigned int v;

	if (offset >= gc->ngpio)
		return;

	raw_spin_lock_irqsave(&chip->lock, flags);
	v = readl_relaxed(chip->base + GPIO_DOUT_X_REG + (offset & ~0x3));
	v &= ~(0x3f << ((offset & 0x3) * 8));
	v |= value << ((offset & 0x3) * 8);
	writel_relaxed(v, chip->base + GPIO_DOUT_X_REG + (offset & ~0x3));
	raw_spin_unlock_irqrestore(&chip->lock, flags);
}

static void sfvic7110_set_ie(struct sfvic7110_gpio *chip, int offset)
{
	unsigned long flags;
	int old_value, new_value;
	int reg_offset, index;

	if(offset < 32) {
		reg_offset = 0;
		index = offset;
	} else {
		reg_offset = 4;
		index = offset - 32;
	}
	raw_spin_lock_irqsave(&chip->lock, flags);
	old_value = readl_relaxed(chip->base + GPIO_IE_LOW + reg_offset);
	new_value = old_value | ( 1 << index);
	writel_relaxed(new_value, chip->base + GPIO_IE_LOW + reg_offset);
	raw_spin_unlock_irqrestore(&chip->lock, flags);
}

static int sfvic7110_irq_set_type(struct irq_data *d, unsigned trigger)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sfvic7110_gpio *chip = gpiochip_get_data(gc);
	int offset = irqd_to_hwirq(d);
	unsigned int reg_is, reg_ibe, reg_iev;
	int reg_offset, index;

	if (offset < 0 || offset >= gc->ngpio)
		return -EINVAL;

	if(offset < 32) {
		reg_offset = 0;
		index = offset;
	} else {
		reg_offset = 4;
		index = offset - 32;
	}
	switch(trigger) {
	case IRQ_TYPE_LEVEL_HIGH:
		reg_is = readl_relaxed(chip->base + GPIO_IS_LOW + reg_offset);
		reg_ibe = readl_relaxed(chip->base + GPIO_IBE_LOW + reg_offset);
		reg_iev = readl_relaxed(chip->base + GPIO_IEV_LOW + reg_offset);
		reg_is  &= (~(0x1<< index));
		reg_ibe &= (~(0x1<< index));
		reg_iev |= (0x1<< index);
		writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		reg_is = readl_relaxed(chip->base + GPIO_IS_LOW + reg_offset);
		reg_ibe = readl_relaxed(chip->base + GPIO_IBE_LOW + reg_offset);
		reg_iev = readl_relaxed(chip->base + GPIO_IEV_LOW + reg_offset);
		reg_is  &= (~(0x1<< index));
		reg_ibe &= (~(0x1<< index));
		reg_iev &= (0x1<< index);
		writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		reg_is = readl_relaxed(chip->base + GPIO_IS_LOW + reg_offset);
		reg_ibe = readl_relaxed(chip->base + GPIO_IBE_LOW + reg_offset);
		//reg_iev = readl_relaxed(chip->base + GPIO_IEV_LOW + reg_offset);
		reg_is  |= (~(0x1<< index));
		reg_ibe |= (~(0x1<< index));
		//reg_iev |= (0x1<< index);
		writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		//writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		break;
	case IRQ_TYPE_EDGE_RISING:
		reg_is = readl_relaxed(chip->base + GPIO_IS_LOW + reg_offset);
		reg_ibe = readl_relaxed(chip->base + GPIO_IBE_LOW + reg_offset);
		reg_iev = readl_relaxed(chip->base + GPIO_IEV_LOW + reg_offset);
		reg_is  |= (~(0x1<< index));
		reg_ibe &= (~(0x1<< index));
		reg_iev |= (0x1<< index);
		writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		reg_is = readl_relaxed(chip->base + GPIO_IS_LOW + reg_offset);
		reg_ibe = readl_relaxed(chip->base + GPIO_IBE_LOW + reg_offset);
		reg_iev = readl_relaxed(chip->base + GPIO_IEV_LOW + reg_offset);
		reg_is  |= (~(0x1<< index));
		reg_ibe &= (~(0x1<< index));
		reg_iev &= (0x1<< index);
		writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		writel_relaxed(reg_is, chip->base + GPIO_IS_LOW + reg_offset);
		break;
	}

	chip->trigger[offset] = trigger;
	sfvic7110_set_ie(chip, offset);
	return 0;
}

/* chained_irq_{enter,exit} already mask the parent */
static void sfvic7110_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sfvic7110_gpio *chip = gpiochip_get_data(gc);
	unsigned int value;
	int offset = irqd_to_hwirq(d);
	int reg_offset, index;

	if (offset < 0 || offset >= gc->ngpio)
		return;

	if(offset < 32) {
		reg_offset = 0;
		index = offset;
	} else {
		reg_offset = 4;
		index = offset - 32;
	}

	value = readl_relaxed(chip->base + GPIO_IE_LOW + reg_offset);
	value &= ~(0x1 << index);
	writel_relaxed(value,chip->base + GPIO_IE_LOW + reg_offset);
}

static void sfvic7110_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sfvic7110_gpio *chip = gpiochip_get_data(gc);
	unsigned int value;
	int offset = irqd_to_hwirq(d);
	int reg_offset, index;

	if (offset < 0 || offset >= gc->ngpio)
		return;

	if(offset < 32) {
		reg_offset = 0;
		index = offset;
	} else {
		reg_offset = 4;
		index = offset - 32;
	}

	value = readl_relaxed(chip->base + GPIO_IE_LOW + reg_offset);
	value |= (0x1 << index);
	writel_relaxed(value,chip->base + GPIO_IE_LOW + reg_offset);
}

static void sfvic7110_irq_enable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sfvic7110_gpio *chip = gpiochip_get_data(gc);
	int offset = irqd_to_hwirq(d);

	sfvic7110_irq_unmask(d);
	assign_bit(offset, &chip->enabled, 1);
}

static void sfvic7110_irq_disable(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct sfvic7110_gpio *chip = gpiochip_get_data(gc);
	int offset = irqd_to_hwirq(d) % MAX_GPIO; // must not fail

	assign_bit(offset, &chip->enabled, 0);
	sfvic7110_set_ie(chip, offset);
}

static struct irq_chip sfvic7110_irqchip = {
	.name		= "sfvic7110-gpio",
	.irq_set_type	= sfvic7110_irq_set_type,
	.irq_mask	= sfvic7110_irq_mask,
	.irq_unmask	= sfvic7110_irq_unmask,
	.irq_enable	= sfvic7110_irq_enable,
	.irq_disable	= sfvic7110_irq_disable,
};


static int starfive_gpio_child_to_parent_hwirq(struct gpio_chip *gc,
					     unsigned int child,
					     unsigned int child_type,
					     unsigned int *parent,
					     unsigned int *parent_type)
{
	struct sfvic7110_gpio *chip = gpiochip_get_data(gc);
	struct irq_data *d = irq_get_irq_data(chip->irq_parent[child]);

	*parent_type = IRQ_TYPE_NONE;
	*parent = irqd_to_hwirq(d);

	return 0;
}

static irqreturn_t sfvic7110_irq_handler(int irq, void *gc)
{
	int offset;
	// = self_ptr - &chip->self_ptr[0];
	int reg_offset, index;
	unsigned int value;
	unsigned long flags;
	struct sfvic7110_gpio *chip = gc;

	for (offset = 0; offset < 64; offset++) {
		if(offset < 32) {
			reg_offset = 0;
			index = offset;
		} else {
			reg_offset = 4;
			index = offset - 32;
		}

		raw_spin_lock_irqsave(&chip->lock, flags);
		value = readl_relaxed(chip->base + GPIO_MIS_LOW + reg_offset);
		if(value & BIT(index))
			writel_relaxed(BIT(index), chip->base + GPIO_IC_LOW +
						   reg_offset);

		//generic_handle_irq(irq_find_mapping(chip->gc.irq.domain,
		//				      offset));
		raw_spin_unlock_irqrestore(&chip->lock, flags);
	}

	return IRQ_HANDLED;
}

void sf_vic_gpio_dout_reverse(int gpio,int en)
{
	unsigned int value;
	int offset;

	if(!gpio_base)
		return;

	offset = GPIO_DOUT_X_REG + (gpio & ~0x3);

	spin_lock(&sfg_lock);
	value = ioread32(gpio_base + offset);
	value &= ~(0x3f << ((offset & 0x3) * 8));
	value |= (en & 0x1) << ((offset & 0x3) * 8);
	iowrite32(value, gpio_base + offset);
	spin_unlock(&sfg_lock);
}
EXPORT_SYMBOL_GPL(sf_vic_gpio_dout_reverse);

void sf_vic_gpio_dout_value(int gpio,int v)
{
	unsigned int value;
	int offset;

	if(!gpio_base)
		return;

	offset = GPIO_DOUT_X_REG + (gpio & ~0x3);

	spin_lock(&sfg_lock);
	value = ioread32(gpio_base + offset);
	value &= ~(0x3f << ((offset & 0x3) * 8));
	value |= (v & 0x3f) << ((offset & 0x3) * 8);
	iowrite32(value,gpio_base + offset);
	spin_unlock(&sfg_lock);
}
EXPORT_SYMBOL_GPL(sf_vic_gpio_dout_value);

void sf_vic_gpio_dout_low(int gpio)
{
	sf_vic_gpio_dout_value(gpio, 0);
}
EXPORT_SYMBOL_GPL(sf_vic_gpio_dout_low);

void sf_vic_gpio_dout_high(int gpio)
{
	sf_vic_gpio_dout_value(gpio, 1);
}
EXPORT_SYMBOL_GPL(sf_vic_gpio_dout_high);

void sf_vic_gpio_doen_reverse(int gpio,int en)
{
	unsigned int value;
	int offset;

	if(!gpio_base)
		return;

	offset = GPIO_DOEN_X_REG + (gpio & ~0x3);

	spin_lock(&sfg_lock);
	value = ioread32(gpio_base + offset);
	value &= ~(0x3f << ((offset & 0x3) * 8));
	value |= (en & 0x1) << ((offset & 0x3) * 8);
	iowrite32(value,gpio_base + offset);
	spin_unlock(&sfg_lock);
}
EXPORT_SYMBOL_GPL(sf_vic_gpio_doen_reverse);

void sf_vic_gpio_doen_value(int gpio,int v)
{
	unsigned int value;
	int offset;

	if(!gpio_base)
		return;

	offset = GPIO_DOEN_X_REG + (gpio & ~0x3);

	spin_lock(&sfg_lock);
	value = ioread32(gpio_base + offset);
	value &= ~(0x3f << ((offset & 0x3) * 8));
	value |= (v & 0x3f) << ((offset & 0x3) * 8);
	iowrite32(value,gpio_base + offset);
	spin_unlock(&sfg_lock);
}
EXPORT_SYMBOL_GPL(sf_vic_gpio_doen_value);

void sf_vic_gpio_doen_low(int gpio)
{
	sf_vic_gpio_doen_value(gpio, 0);
}
EXPORT_SYMBOL_GPL(sf_vic_gpio_doen_low);

void sf_vic_gpio_doen_high(int gpio)
{
	sf_vic_gpio_doen_value(gpio, 1);
}
EXPORT_SYMBOL_GPL(sf_vic_gpio_doen_high);

void sf_vic_gpio_manual(int offset,int v)
{
	unsigned int value;

	if(!gpio_base)
		return ;

	spin_lock(&sfg_lock);
	value = ioread32(gpio_base + offset);
	value &= ~(0xFF);
	value |= (v&0xFF);
	iowrite32(value,gpio_base + offset);
	spin_unlock(&sfg_lock);
}
EXPORT_SYMBOL_GPL(sf_vic_gpio_manual);

static int str_to_num(char *str)
{
	char *p = str;
	int value = 0;

	if((*p == '0') && (*(p + 1) == 'x' || *(p + 1) == 'X')) {
		p = p + 2;
		while(((*p >= '0') && (*p <= '9')) ||
		      ((*p >= 'a') && (*p <= 'f')) ||
		      ((*p >= 'A') && (*p <= 'F'))) {
			if((*p >= '0') && (*p <= '9'))
				value = value * 16 + (*p - '0');
			if((*p >= 'a') && (*p <= 'f'))
				value = value * 16 + 10 + (*p - 'a');
			if((*p >= 'A') && (*p <= 'F'))
				value = value * 16 + 10 + (*p - 'A');
			p = p + 1;
		}
	} else {
		while((*p >= '0') && (*p <= '9')) {
			value = value * 10 + (*p - '0');
			p = p + 1;
		}
	}

	if(*p != '\0')
		return -EFAULT;

	return value;
}

static ssize_t vic_gpio_proc_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	int ret;
	char message[64], cmd[8],gnum[8],v[8];
	int gpionum, value;

	if (mutex_lock_interruptible(&write_lock))
		return -ERESTARTSYS;

	ret = copy_from_user(message, buf, count);
	mutex_unlock(&write_lock);
	if(ret)
		return -EFAULT;
	sscanf(message, "%s %s %s", cmd, gnum, v);
	gpionum = str_to_num(gnum);
	if(gpionum < 0)
		return -EFAULT;
	value = str_to_num(v);
	if(value < 0)
		return -EFAULT;

	if(!strcmp(cmd,"dout")) {
		if(gpionum < 0 || gpionum > 63){
			printk(KERN_ERR "vic-gpio: dout gpionum (0-63)  value (0/1) invalid: gpionum = %d value = %d\n",
			       gpionum,value);
			return -EFAULT;
		}
		sf_vic_gpio_dout_value(gpionum, value);
	}else if(!strcmp(cmd,"doen")) {
		if(gpionum < 0 || gpionum > 63){
			printk(KERN_ERR "vic-gpio: doen gpionum (0-63)  value (0/1) invalid: gpionum = %d value = %d\n",
			       gpionum,value);
			return -EFAULT;
		}
		sf_vic_gpio_doen_value(gpionum,value);
	}else if(!strcmp(cmd,"utrv")) {
		if(gpionum < 0 || gpionum > 63){
			printk(KERN_ERR "vic-gpio: utrv gpionum (0-63) is invalid: %d\n",gpionum);
			return -EFAULT;
		}
		sf_vic_gpio_doen_reverse(gpionum,value);
	}else if(!strcmp(cmd,"enrv")) {
		if(gpionum < 0 || gpionum > 63){
			printk(KERN_ERR "vic-gpio: enrv gpionum (0-63) is invalid: %d\n",gpionum);
			return -EFAULT;
		}
		sf_vic_gpio_doen_reverse(gpionum, value);
	}else if(!strcmp(cmd,"manu")) {
		if(gpionum < 0x250 || gpionum > 0x378 || (gpionum & 0x3)){
			printk(KERN_ERR "vic-gpio: manu offset (0x250-0x378 & mod 4) is invalid: %d\n",gpionum);
			return -EFAULT;
		}
		sf_vic_gpio_manual(gpionum, value);
	}else {
		printk(KERN_ERR "vic-gpio: cmd (dout  doen utrv enrv manu) invalid: %s\n",cmd);
	}

	return count;
}

static ssize_t vic_gpio_proc_read(struct file *file, char __user *buf,
				  size_t count, loff_t *ppos)
{
	int ret;
	unsigned int copied;
	char message[256];

	sprintf(message, "Usage: echo 'cmd gpionum value' >/proc/vic_gpio\n\t"
		"cmd: dout  doen utrv enrv or manu\n\t"
		"gpionum: gpionum or address offset for manu\n\t"
		"value: 0/1 for utrv/enrv, value for dout/doen/manual\n");
	copied = strlen(message);

	if(*ppos >= copied)
		return 0;

	if (mutex_lock_interruptible(&read_lock))
		return -ERESTARTSYS;

	ret = copy_to_user(buf, message, copied);
	if(ret) {
		mutex_unlock(&read_lock);
	}
	*ppos += copied;

	mutex_unlock(&read_lock);

	return copied;
}

static const struct file_operations vic_gpio_fops = {
	.owner	= THIS_MODULE,
	.read	= vic_gpio_proc_read,
	.write	= vic_gpio_proc_write,
	.llseek	= noop_llseek,
};

static int sfvic7110_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sfvic7110_gpio *chip;
	struct resource *res;
	int irq, ret, ngpio;
	int loop;
	struct gpio_irq_chip *girq;
	struct device_node *node = pdev->dev.of_node;
	struct device_node *irq_parent;
	struct irq_domain *parent;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(dev, "out of memory\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	chip->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(chip->base)) {
		dev_err(dev, "failed to allocate device memory\n");
		return PTR_ERR(chip->base);
	}
	gpio_base = chip->base ;

	ngpio = 64;

	raw_spin_lock_init(&chip->lock);
	chip->gc.direction_input = sfvic7110_direction_input;
	chip->gc.direction_output = sfvic7110_direction_output;
	chip->gc.get_direction = sfvic7110_get_direction;
	chip->gc.get = sfvic7110_get_value;
	chip->gc.set = sfvic7110_set_value;
	chip->gc.base = 0;
	chip->gc.ngpio = ngpio;
	chip->gc.label = dev_name(dev);
	chip->gc.parent = dev;
	chip->gc.owner = THIS_MODULE;

	irq_parent = of_irq_find_parent(node);
	if (!irq_parent) {
		dev_err(dev, "no IRQ parent node\n");
		return -ENODEV;
	}
	parent = irq_find_host(irq_parent);
	if (!parent) {
		dev_err(dev, "no IRQ parent domain\n");
		return -ENODEV;
	}

	girq = &chip->gc.irq;
	girq->chip = &sfvic7110_irqchip;
	girq->fwnode = of_node_to_fwnode(node);
	girq->parent_domain = parent;
	girq->child_to_parent_hwirq = starfive_gpio_child_to_parent_hwirq;
	girq->handler = handle_simple_irq;
	girq->default_type = IRQ_TYPE_NONE;

	/* Disable all GPIO interrupts before enabling parent interrupts */
	iowrite32(0, chip->base + GPIO_IE_HIGH);
	iowrite32(0, chip->base + GPIO_IE_LOW);
	chip->enabled = 0;
	
	platform_set_drvdata(pdev, chip);
	ret = gpiochip_add_data(&chip->gc, chip);
	if (ret){
		dev_err(dev, "gpiochip_add_data ret=%d!\n", ret);
		return ret;
	}
	
#if 0
	/* Disable all GPIO interrupts before enabling parent interrupts */
	iowrite32(0, chip->base + GPIO_IE_HIGH);
	iowrite32(0, chip->base + GPIO_IE_LOW);
	chip->enabled = 0;

	ret = gpiochip_gpiochip_add(&chip->gc, &sfvic7110_irqchip, 0,
				   handle_simple_irq, IRQ_TYPE_NONE);
	if (ret) {
		dev_err(dev, "could not add irqchip\n");
		gpiochip_remove(&chip->gc);
		return ret;
	}
#endif

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Cannot get IRQ resource\n");
		return irq;
	}

	chip->irq_parent[0] = irq;
	
	ret = devm_request_irq(dev, irq, sfvic7110_irq_handler, IRQF_SHARED,
			       dev_name(dev), chip);
	if (ret) {
		dev_err(dev, "IRQ handler registering failed (%d)\n", ret);
		return ret;
	}

	writel_relaxed(1, chip->base + GPIO_EN);

	for(loop = 0; loop < MAX_GPIO; loop++) {
		unsigned int v;
		v = readl_relaxed(chip->base + GPIO_INPUT_ENABLE_X_REG + (loop << 2));
		v |= 0x1;
		writel_relaxed(v, chip->base + GPIO_INPUT_ENABLE_X_REG + (loop << 2));
	}

	if (proc_create(PROC_VIC, 0, NULL, (void *)&vic_gpio_fops) == NULL) {
		return -ENOMEM;
	}
	dev_info(dev, "SiFive GPIO chip registered %d GPIOs\n", ngpio);

	return 0;
}

static const struct of_device_id sfvic7110_gpio_match[] = {
	{ .compatible = "starfive,gpio7110", },
	{ },
};

static struct platform_driver sfvic7110_gpio_driver = {
	.probe	= sfvic7110_gpio_probe,
	.driver	= {
		.name		= "sfvic7110_gpio",
		.of_match_table	= of_match_ptr(sfvic7110_gpio_match),
	},
};

static int __init sfvic7110_gpio_init(void)
{
	return platform_driver_register(&sfvic7110_gpio_driver);
}
subsys_initcall(sfvic7110_gpio_init);

static void __exit sfvic7110_gpio_exit(void)
{
	platform_driver_unregister(&sfvic7110_gpio_driver);
}
module_exit(sfvic7110_gpio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Huan Feng <huan.feng@starfivetech.com>");
MODULE_DESCRIPTION("Starfive VIC GPIO generator driver");
