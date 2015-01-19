#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <mach/am_regs.h>
#include <linux/vmalloc.h>
#include <linux/of.h>
#include <linux/amlogic/aml_gpio_consumer.h>
//#include <linux/amlogic/input/common.h>
#include <linux/amlogic/nfc/nfc_common.h>
#define AML_I2C_BUS_AO     0
#define AML_I2C_BUS_A       1
#define AML_I2C_BUS_B       2
#define AML_I2C_BUS_C       3
#define AML_I2C_BUS_D       4

#define MAX_NFC  4



static int nfc_dt_setup(struct device_node* of_node, struct nfc_pdata *pdata)
{
    const char *str;
    int err;

	if (!of_node) {
		printk("%s: dev.of_node == NULL!\n", pdata->owner);
		return -1;
	}

    err = of_property_read_string(of_node, "nfc_name", (const char **)&pdata->owner);
	if (err) {
		pdata->owner = "amlogic";
		printk("waring: get nfc name failed,set name amlogic!\n");
	}
	err = of_property_read_u32(of_node,"reg",&pdata->addr);
	if (err) {
	    printk("%s warnning: faild to get ic addr!\n", pdata->owner);
	    pdata->addr = 0;
    }
    printk("%s: addr=%x\n", pdata->owner, pdata->addr);
    
	err = of_property_read_string(of_node, "i2c_bus", &str);
	if (err) {
		printk("%s warnning: faild to get i2c_bus str,use default i2c bus!\n", pdata->owner);
		pdata->bus_type = AML_I2C_BUS_A;
	}
	else {
		if (!strncmp(str, "i2c_bus_a", 9))
			pdata->bus_type = AML_I2C_BUS_A;
		else if (!strncmp(str, "i2c_bus_b", 9))
			pdata->bus_type = AML_I2C_BUS_B;
		else if (!strncmp(str, "i2c_bus_c", 9))
			pdata->bus_type = AML_I2C_BUS_C;
		else if (!strncmp(str, "i2c_bus_d", 9))
			pdata->bus_type = AML_I2C_BUS_D;
		else if (!strncmp(str, "i2c_bus_ao", 10))
			pdata->bus_type = AML_I2C_BUS_AO;
		else
			pdata->bus_type = AML_I2C_BUS_A; 
	}
	printk("%s: bus_type=%d\n", pdata->owner, pdata->bus_type);

	err = of_property_read_u32(of_node,"irq",&pdata->irq);
	if (err) {
	    printk("%s warnning£ºto get IRQ number!\n", pdata->owner);
        pdata->irq = 0;
    }
    //pdata->irq += INT_GPIO_0;
	printk("%s: IRQ number=%d\n",pdata->owner, pdata->irq);

	err = of_property_read_string(of_node, "gpio_interrupt", &str);
	if (err) {
	    printk("%s: faild to get gpio interrupt!\n", pdata->owner);
	    pdata->irq_gpio = 0;
	    return -1;
    }
    else {
        pdata->irq_gpio = amlogic_gpio_name_map_num(str);
        printk("%s: alloc gpio_interrupt(%s)!\n", pdata->owner, str);
        if (pdata->irq_gpio <= 0) {
        	pdata->irq_gpio = 0;
        	printk("%s: faild to alloc gpio_interrupt(%s)!\n", pdata->owner, str);
            return -1;
        }
    }

	err = of_property_read_string(of_node, "gpio_en", &str);
	if (!err){
        pdata->en_gpio = amlogic_gpio_name_map_num(str);
        printk("%s: alloc gpio_reset(%s)!\n", pdata->owner, str);
        if (pdata->en_gpio <= 0) {
        	pdata->en_gpio = 0;
        	printk("%s warning: faild to alloc gpio_en(%s)!\n", pdata->owner, str);
        }
    }
    else {
  	    pdata->en_gpio = 0;
    }

	err = of_property_read_string(of_node, "gpio_wake", &str);
	if (!err){
        pdata->wake_gpio = amlogic_gpio_name_map_num(str);
        printk("%s: alloc gpio_wake(%s)!\n", pdata->owner, str);
        if (pdata->wake_gpio <= 0) {
        	pdata->wake_gpio = 0;
        	printk("%s warning: faild to alloc gpio_wake(%s)!\n", pdata->owner, str);
        }
    }
    else {
        pdata->wake_gpio = 0;
    }
 
    return 0;
}

static int aml_nfc_probe(struct platform_device *pdev)
{
	
	struct device_node* nfc_node = pdev->dev.of_node;
	struct device_node* child;
	struct i2c_board_info board_info;
	struct i2c_adapter *adapter;
	struct i2c_client *i2c_client;
	int i = -1;
	struct nfc_pdata *pdata = NULL;

	printk("##############aml_nfc_probe start############\n");

    pdata = kzalloc(sizeof(*pdata)*MAX_NFC, GFP_KERNEL);
	if (!pdata)	{
		printk(KERN_ERR "fail alloc data!\n");
		return -1;
	}

	pdev->dev.platform_data = pdata;
	for_each_child_of_node(nfc_node, child) {
	    i++;
		if (i >= MAX_NFC) {
			printk("warnning: nfc num out of range max nfc num(%d)\n", MAX_NFC);
			break;
		}
		if (nfc_dt_setup(child, pdata+i) < 0) {
			printk(KERN_WARNING "fail get dt data!\n");
			continue;
		}
		adapter = i2c_get_adapter((pdata+i)->bus_type);
		if (!adapter) {
			printk(KERN_WARNING "warnning£ºfail get adapter!\n");
			continue;
		}
		memset(&board_info, 0, sizeof(board_info));
		strncpy(board_info.type, (pdata+i)->owner, I2C_NAME_SIZE);
		board_info.addr = (pdata+i)->addr;
		board_info.platform_data = (pdata+i);
		//printk("%s: adapter = %d\n", (pdata+i)->owner, adapter);
		i2c_client = i2c_new_device(adapter, &board_info);
		i2c_client->irq = (pdata+i)->irq + INT_GPIO_0;
		if (!i2c_client) {
			printk("%s :fail new i2c device\n", (pdata+i)->owner);
			continue;
		}
		else{
			printk("%s: new i2c device successed\n",((struct nfc_pdata *)(i2c_client->dev.platform_data))->owner);
			//printk("pdata addr = %x\n", pdata+i);
		}
		
	}
	printk("==%s==end==\n", __func__);
	return 0;
	
}

static int aml_nfc_remove(struct platform_device *pdev)
{
    if (pdev->dev.platform_data)
	 	kfree (pdev->dev.platform_data);
    return 0; 
}

static const struct of_device_id nfc_prober_dt_match[]={
	{	
		.compatible = "amlogic,aml_nfc",
	},
	{},
};

static struct platform_driver aml_nfc_prober_driver = {
	.probe		= aml_nfc_probe,
	.remove		= aml_nfc_remove,
	.driver		= {
		.name	= "aml_nfc",
		.owner	= THIS_MODULE,
		.of_match_table = nfc_prober_dt_match,
	},
};

static int __init aml_nfc_prober_init(void)
{
    int ret;

    ret = platform_driver_register(&aml_nfc_prober_driver);
    if (ret){
        printk(KERN_ERR"aml_nfc_probre_driver register failed\n");
        return ret;
    }

    return ret;
}


static void __exit aml_nfc_prober_exit(void)
{
    platform_driver_unregister(&aml_nfc_prober_driver);
}

module_init(aml_nfc_prober_init);
module_exit(aml_nfc_prober_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Amlogic nfc prober driver");

