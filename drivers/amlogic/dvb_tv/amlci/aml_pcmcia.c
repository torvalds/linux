
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/device.h>

#include <mach/pinmux.h>
#include <mach/gpio.h>
#include <mach/gpio_data.h>

#include "aml_pcmcia.h"

static int aml_pcmcia_debug=0;

module_param_named(pcmcia_debug, aml_pcmcia_debug, int, 0644);
MODULE_PARM_DESC(pcmcia_debug, "enable verbose debug messages");

#define pr_dbg(fmt, args...) do{if (aml_pcmcia_debug) printk("PCMCIA: " fmt, ## args);}while(0)
#define pr_error(fmt, args...) printk(KERN_ERR "PCMCIA: " fmt, ## args)


static int pcmcia_plugin(struct aml_pcmcia *pc)
{
	if (pc->slot_state == MODULE_XTRACTED) {
		pr_dbg(" CAM Plugged IN: Adapter(%d) Slot(0)\n", 0);
		udelay(50);
		aml_pcmcia_reset(pc);
		/*wait unplug*/
		pc->init_irq(IRQF_TRIGGER_RISING);
		udelay(500);
		pc->slot_state = MODULE_INSERTED;
	}
	aml_pcmcia_reset(pc);
	udelay(100);
	pc->pcmcia_plugin(pc, 1);

	return 0;
}

static int pcmcia_unplug(struct aml_pcmcia *pc)
{
	if (pc->slot_state == MODULE_INSERTED) {
		pr_dbg(" CAM Unplugged: Adapter(%d) Slot(0)\n", 0);
		udelay(50);
		aml_pcmcia_reset(pc);
		/*wait plugin*/
		pc->init_irq(IRQF_TRIGGER_FALLING);
		udelay(500);
		pc->slot_state = MODULE_XTRACTED;
	}
	udelay(100);
	pc->pcmcia_plugin(pc, 0);

	return 0;
}


static irqreturn_t pcmcia_irq_handler(int irq, void *dev_id)
{
	struct aml_pcmcia *pc = (struct aml_pcmcia *)dev_id;
	disable_irq_nosync(pc->irq);
	schedule_work(&pc->pcmcia_work);
	enable_irq(pc->irq);
	return IRQ_HANDLED;
}

static void aml_pcmcia_work(struct work_struct *work)
{
	int cd1, cd2;
	struct aml_pcmcia *pc = container_of(work, struct aml_pcmcia, pcmcia_work);

	cd1 = pc->get_cd1();
	cd2 = pc->get_cd2();

	if(cd1 != cd2) {
		pr_error("CAM card not inerted correctly or CAM card not supported.\n");
	} else {
		if (!cd1) {
			pr_error("Adapter(%d) Slot(0): CAM Plugin\n", 0);
			pcmcia_plugin(pc);
		} else {
			pr_error("Adapter(%d) Slot(0): CAM Unplug\n", 0);
			pcmcia_unplug(pc);
		}
	}
}

static struct aml_pcmcia *pc_cur;

int aml_pcmcia_init(struct aml_pcmcia *pc)
{
	int err=0;

	pc->rst(0);
	/*power on*/
	pc->pwr(0);
	/*assuming cam unpluged, config the INT to waiting-for-plugin mode*/
	pc->init_irq(IRQF_TRIGGER_LOW);

	INIT_WORK(&pc->pcmcia_work, aml_pcmcia_work);

	err = request_irq(INT_GPIO_5, pcmcia_irq_handler, IRQF_DISABLED, "aml-pcmcia", pc);

	if (err != 0) {
		pr_error("ERROR: IRQ registration failed ! <%d>", err);
		return -ENODEV;
	}

	pc_cur = pc;
	return 0;
}
EXPORT_SYMBOL(aml_pcmcia_init);

int aml_pcmcia_exit(struct aml_pcmcia *pc)
{
	free_irq(pc->irq, pc);
	return 0;
}
EXPORT_SYMBOL(aml_pcmcia_exit);

int aml_pcmcia_reset(struct aml_pcmcia *pc)
{
	pr_dbg("CAM RESET-->\n");
	udelay(500); /* Wait.. */
	pc->rst(1);
	udelay(500);
	pc->rst(0);
	msleep(100);
	return 0;
}
EXPORT_SYMBOL(aml_pcmcia_reset);



static ssize_t aml_pcmcia_test_cmd(struct class *class,struct class_attribute *attr,
                          const char *buf,
                          size_t size)
{
	printk("pcmcia cmd: %s\n", buf);
	if(pc_cur) {
		if(memcmp(buf, "reset", 5)==0)
			aml_pcmcia_reset(pc_cur);
		else if(memcmp(buf, "on", 2)==0)
			pc_cur->pwr(1);
		else if(memcmp(buf, "off", 3)==0)
			pc_cur->pwr(0);
		else if(memcmp(buf, "poll", 4)==0)
			schedule_work(&pc_cur->pcmcia_work);
		else if(memcmp(buf, "intr", 4)==0)
			pc_cur->init_irq(IRQF_TRIGGER_RISING);
		else if(memcmp(buf, "intf", 4)==0)
			pc_cur->init_irq(IRQF_TRIGGER_FALLING);
	}
	return size;
}

static struct class_attribute aml_pcmcia_class_attrs[] = {
    __ATTR(cmd,  S_IRUGO | S_IWUSR, NULL, aml_pcmcia_test_cmd),
    __ATTR_NULL
};

static struct class aml_pcmcia_class = {
    .name = "aml_pcmcia_test",
    .class_attrs = aml_pcmcia_class_attrs,
};

static int __init aml_pcmcia_mod_init(void)
{
	printk("Amlogic PCMCIA Init\n");

	class_register(&aml_pcmcia_class);

	return 0;
}

static void __exit aml_pcmcia_mod_exit(void)
{
	printk("Amlogic PCMCIA Exit\n");

	class_unregister(&aml_pcmcia_class);
}



module_init(aml_pcmcia_mod_init);
module_exit(aml_pcmcia_mod_exit);

MODULE_LICENSE("GPL");




