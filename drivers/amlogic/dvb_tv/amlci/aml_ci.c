
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <mach/gpio.h>
#include <mach/gpio_data.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"

#include "aml_ci.h"
#include "aml_iobus.h"

static int aml_ci_debug=0;

module_param_named(ci_debug, aml_ci_debug, int, 0644);
MODULE_PARM_DESC(ci_debug, "enable verbose debug messages");

#define pr_dbg(fmt, args...) do{if (aml_ci_debug) printk("CI: " fmt, ## args);}while(0)
#define pr_error(fmt, args...) printk(KERN_ERR "CI: " fmt, ## args)


static int aml_ci_read_attr_mem(struct dvb_ca_en50221 *en50221, int slot, int addr)
{
	//struct aml_ci *ci = en50221->data;
	//struct aml_dvb *dvb = ci->ci_priv;

	//pr_dbg("Slot(%d): Request Attribute Mem Read\n", slot);

	if (slot != 0)
		return -EINVAL;

	return aml_iobus_attr_read(addr);
}

static int aml_ci_write_attr_mem(struct dvb_ca_en50221 *en50221, int slot, int addr, u8 data)
{
	//pr_dbg("Slot(%d): Request Attribute Mem Write\n", slot);

	if (slot != 0)
		return -EINVAL;

	return aml_iobus_attr_write(addr, data);
}

static int aml_ci_read_cam_ctl(struct dvb_ca_en50221 *en50221, int slot, u8 addr)
{
	//pr_dbg("Slot(%d): Request CAM control Read\n", slot);

	if (slot != 0)
		return -EINVAL;

	return aml_iobus_io_read(addr);
}

static int aml_ci_write_cam_ctl(struct dvb_ca_en50221 *en50221, int slot, u8 addr, u8 data)
{

	//pr_dbg("Slot(%d): Request CAM control Write\n", slot);

	if (slot != 0)
		return -EINVAL;

	return aml_iobus_io_write(addr, data);
}

static int aml_ci_slot_reset(struct dvb_ca_en50221 *en50221, int slot)
{
	struct aml_ci *ci = en50221->data;

	pr_dbg("Slot(%d): Slot RESET\n", slot);
	aml_pcmcia_reset(&ci->pc);
	dvb_ca_en50221_camready_irq(en50221, 0);

	return 0;
}

static int aml_ci_slot_shutdown(struct dvb_ca_en50221 *en50221, int slot)
{

	pr_dbg("Slot(%d): Slot shutdown\n", slot);

	return 0;
}

static int aml_ci_ts_control(struct dvb_ca_en50221 *en50221, int slot)
{

	pr_dbg("Slot(%d): TS control\n", slot);

	return 0;
}

static int aml_ci_slot_status(struct dvb_ca_en50221 *en50221, int slot, int open)
{
	struct aml_ci *ci = en50221->data;

	pr_dbg("Slot(%d): Poll Slot status\n", slot);

	if (ci->pc.slot_state == MODULE_INSERTED) {
		pr_dbg("CA Module present and ready\n");
		return DVB_CA_EN50221_POLL_CAM_PRESENT | DVB_CA_EN50221_POLL_CAM_READY;
	} else {
		pr_error("CA Module not present or not ready\n");
	}

	return 0;
}

static int aml_ci_slot_ts(struct dvb_ca_en50221 *en50221, int slot)
{
	struct aml_ci *ci = en50221->data;

	pr_dbg("Slot(%d): Slot ts\n", slot);

}

int aml_ci_init(struct platform_device *pdev, struct aml_dvb *dvb, struct aml_ci **cip)
{
	struct dvb_adapter *dvb_adapter	= &dvb->dvb_adapter;
	struct aml_ci *ci = NULL;
	int ca_flags = 0, result;
	char buf[32];
	int id = 0;
	struct resource *res;

	ci = kzalloc(sizeof(struct aml_ci), GFP_KERNEL);
	if (!ci) {
		pr_error("Out of memory!, exiting ..\n");
		result = -ENOMEM;
		goto err;
	}

	ci->id = 0;
	ci->ts = 0xffff;
	snprintf(buf, sizeof(buf), "ci%d_ts", id);
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, buf);
	if(res){
		int id = res->start;
		aml_ts_source_t ts = AM_TS_SRC_TS0;

		switch(id){
			case 0:
				ts = AM_TS_SRC_TS0;
				break;
			case 1:
				ts = AM_TS_SRC_TS1;
				break;
			case 2:
				ts = AM_TS_SRC_TS2;
				break;
			default:
				break;
		}

		ci->ts = ts;
	}

	ci->priv		= dvb;
	//aml->ci	= ci;
	ca_flags		= DVB_CA_EN50221_FLAG_IRQ_CAMCHANGE;
	/* register CA interface */
	ci->en50221.owner		= THIS_MODULE;
	ci->en50221.read_attribute_mem	= aml_ci_read_attr_mem;
	ci->en50221.write_attribute_mem	= aml_ci_write_attr_mem;
	ci->en50221.read_cam_control	= aml_ci_read_cam_ctl;
	ci->en50221.write_cam_control	= aml_ci_write_cam_ctl;
	ci->en50221.slot_reset		= aml_ci_slot_reset;
	ci->en50221.slot_shutdown	= aml_ci_slot_shutdown;
	ci->en50221.slot_ts_enable	= aml_ci_ts_control;
	ci->en50221.poll_slot_status	= aml_ci_slot_status;
	ci->en50221.data		= ci;
	ci->en50221.get_ts	= aml_ci_slot_ts;

	mutex_init(&ci->ci_lock);

	pr_dbg("Registering EN50221 device\n");
	result = dvb_ca_en50221_init(dvb_adapter, &ci->en50221, ca_flags, 1);
	if (result != 0) {
		pr_error( "EN50221: Initialization failed <%d>\n", result);
		goto err;
	}

	*cip = ci;

	pr_dbg("Registered EN50221 device\n");

	return 0;
err:
	kfree(ci);
	return result;
}
EXPORT_SYMBOL_GPL(aml_ci_init);

void aml_ci_exit(struct aml_ci *ci)
{
	pr_dbg("Unregistering EN50221 device\n");
	if (ci)
		dvb_ca_en50221_release(&ci->en50221);
	kfree(ci);
}
EXPORT_SYMBOL_GPL(aml_ci_exit);



static struct aml_ci *ci;

#include <mach/pinmux.h>
#include <mach/gpio.h>

#define GPIO_CD1  PAD_GPIOX_9
#define GPIO_CD2  PAD_GPIOX_9
#define GPIO_RST  PAD_GPIOB_11
#define GPIO_nPWR  PAD_GPIOX_12

#define IRQ_GPIO_CD1  INT_GPIO_5

static int io_setup(void)
{
	//gpioX09:
		//reg5[10],Reg8[0]
	//gpioX12:
		//Reg3[21]
	static pinmux_item_t ci_gpioX_pins[] = {
	    {
	        .reg = PINMUX_REG(5),
	        .clrmask = 0x1 << 10
	    },
	    {
	        .reg = PINMUX_REG(8),
	        .clrmask = 0x1
	    },
	    {
	        .reg = PINMUX_REG(3),
	        .clrmask = 0x1 << 21
	    },
	    PINMUX_END_ITEM
	};
	static pinmux_set_t ci_gpioX_pinmux_set = {
	    .chip_select = NULL,
	    .pinmux = &ci_gpioX_pins[0]
	};
	pinmux_set(&ci_gpioX_pinmux_set);

	//gpioB11:
		//reg0[2],Reg3[6],Reg5[15]
	static pinmux_item_t ci_gpioB_pins[] = {
	    {
	        .reg = PINMUX_REG(0),
	        .clrmask = 0x1 << 2
	    },
	    {
	        .reg = PINMUX_REG(3),
	        .clrmask = 0x1 << 6
	    },
	    {
	        .reg = PINMUX_REG(5),
	        .clrmask = 0x1 << 15
	    },
	    PINMUX_END_ITEM
	};
	static pinmux_set_t ci_gpioB_pinmux_set = {
	    .chip_select = NULL,
	    .pinmux = &ci_gpioB_pins[0]
	};
	pinmux_set(&ci_gpioB_pinmux_set);

	return 0;
}
static int io_power(int enable)
{
	int ret = 0;
	ret = gpio_out(GPIO_nPWR, enable?1:0);
	return ret;
}
static int io_reset(int enable)
{
	int ret = 0;
	ret = gpio_out(GPIO_RST, enable?1:0);
	return ret;
}
static int io_init_irq(int flag)
{
	gpio_set_status(GPIO_CD1, gpio_status_in);
	if (flag == IRQF_TRIGGER_RISING)
		gpio_irq_set(GPIO_CD1, GPIO_IRQ(IRQ_GPIO_CD1-INT_GPIO_0, GPIO_IRQ_RISING));
	else if (flag == IRQF_TRIGGER_FALLING)
		gpio_irq_set(GPIO_CD1, GPIO_IRQ(IRQ_GPIO_CD1-INT_GPIO_0, GPIO_IRQ_FALLING));
	else if (flag == IRQF_TRIGGER_HIGH)
		gpio_irq_set(GPIO_CD1, GPIO_IRQ(IRQ_GPIO_CD1-INT_GPIO_0, GPIO_IRQ_HIGH));
	else if (flag == IRQF_TRIGGER_LOW)
		gpio_irq_set(GPIO_CD1, GPIO_IRQ(IRQ_GPIO_CD1-INT_GPIO_0, GPIO_IRQ_LOW));
	else
		return -1;
	return 0;
}
static int io_get_cd1(void)
{
	int ret = 0;
	ret = gpio_in_get(GPIO_CD1);
	return ret;
}
static int io_get_cd2(void)
{
	int ret = 0;
	ret = gpio_in_get(GPIO_CD2);
	return ret;
}

static int cam_plugin(struct aml_pcmcia *pc, int plugin)
{
	struct aml_ci *ci = (struct aml_ci *)pc->priv;

	if(plugin)
		dvb_ca_en50221_camchange_irq(&ci->en50221, 0, DVB_CA_EN50221_CAMCHANGE_INSERTED);
	else
		dvb_ca_en50221_camchange_irq(&ci->en50221, 0, DVB_CA_EN50221_CAMCHANGE_REMOVED);
	return 0;
}

static ssize_t aml_ci_ts_show(struct class *class, struct class_attribute *attr,char *buf)
{
	int ret;
	struct aml_ci *ci = container_of(class,struct aml_ci,class);
	ret = sprintf(buf, "ts%d\n", ci->ts);
	return ret;
}

static struct class_attribute amlci_class_attrs[] = {
	__ATTR(ts,  S_IRUGO | S_IWUSR, aml_ci_ts_show, NULL),
	__ATTR_NULL
};

static int aml_ci_register_class(struct aml_ci *ci)
{
#define CLASS_NAME_LEN 48
	int ret;
	struct class *clp;

	clp = &(ci->class);

	clp->name = kzalloc(CLASS_NAME_LEN,GFP_KERNEL);
	if (!clp->name)
		return -ENOMEM;

	snprintf((char *)clp->name, CLASS_NAME_LEN, "amlci-%d", ci->id);
	clp->owner = THIS_MODULE;
	clp->class_attrs = amlci_class_attrs;
	ret = class_register(clp);
	if (ret)
		kfree(clp->name);

	return 0;
}

static int aml_ci_unregister_class(struct aml_ci *ci)
{
	class_unregister(&ci->class);
	kzfree(ci->class.name);
	return 0;
}

void aml_pcmcia_alloc(struct aml_pcmcia **pcmcia)
{
	*pcmcia = &ci->pc;
	(*pcmcia)->irq = IRQ_GPIO_CD1;
	(*pcmcia)->init_irq = io_init_irq;
	(*pcmcia)->get_cd1 = io_get_cd1;
	(*pcmcia)->get_cd2 = io_get_cd2;
	(*pcmcia)->pwr = io_power;
	(*pcmcia)->rst = io_reset;
	(*pcmcia)->pcmcia_plugin = cam_plugin;
	(*pcmcia)->slot_state = MODULE_XTRACTED;
	(*pcmcia)->priv = ci;
}

static int aml_ci_probe(struct platform_device *pdev)
{
	struct aml_dvb *dvb = aml_get_dvb_device();
	struct aml_pcmcia *pc;
	int err=0;

	printk("Amlogic CI Init\n");

	if((err = aml_ci_init(pdev, dvb, &ci))<0)
		return err;
	if((err=aml_iobus_init())<0)
		goto fail1;
	io_setup();
	aml_pcmcia_alloc(&pc);
	if((err=aml_pcmcia_init(pc))<0)
		goto fail2;

	platform_set_drvdata(pdev, ci);
	aml_ci_register_class(ci);

	return 0;

fail2:
	aml_iobus_exit();
fail1:
	aml_ci_exit(ci);
	return err;

}

static int aml_ci_remove(struct platform_device *pdev)
{
	aml_ci_unregister_class(ci);
	platform_set_drvdata(pdev, NULL);

	aml_pcmcia_exit(&ci->pc);
	aml_iobus_exit();

	aml_ci_exit(ci);

	return 0;
}

static int aml_ci_suspend(struct platform_device *pdev, pm_message_t state)
{
	printk("Amlogic CI Suspend!\n");
	aml_pcmcia_exit(&ci->pc);
	aml_iobus_exit();

	return 0;
}

static int aml_ci_resume(struct platform_device *pdev)
{
	struct aml_pcmcia *pc;
	int err=0;

	printk("Amlogic CI Resume!\n");
	if((err=aml_iobus_init())<0)
		goto fail1;
	io_setup();
	aml_pcmcia_alloc(&pc);
	if((err=aml_pcmcia_init(pc))<0)
		goto fail2;
	return 0;

fail2:
	aml_iobus_exit();
fail1:
	aml_ci_exit(ci);
	return err;
}

static struct platform_driver aml_ci_driver = {
	.probe		= aml_ci_probe,
	.remove		= aml_ci_remove,
	.suspend        = aml_ci_suspend,
	.resume         = aml_ci_resume,
	.driver		= {
		.name	= "amlogic-ci",
		.owner	= THIS_MODULE,
	}
};

static int __init aml_ci_mod_init(void)
{
	return platform_driver_register(&aml_ci_driver);
}

static void __exit aml_ci_mod_exit(void)
{
	printk("Amlogic CI Exit\n");
	platform_driver_unregister(&aml_ci_driver);
}



module_init(aml_ci_mod_init);
module_exit(aml_ci_mod_exit);

MODULE_LICENSE("GPL");


