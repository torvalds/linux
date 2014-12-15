#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/amlogic/input/common.h>
#include <linux/sensor/sensor_common.h>


#define MAX_SENSOR_ONBOARD  6
static sensor_pdata_t sensor_pdata[MAX_SENSOR_ONBOARD];
static int curr_idx;

void aml_sensor_report_acc(struct i2c_client *client, struct input_dev *dev, int x, int y, int z)
{
    int i;
    for(i = 0; i < curr_idx; i++)
    {
        if(sensor_pdata[i].client && sensor_pdata[i].client == client)
        { 
            if(sensor_pdata[i].acc_swap_xy)
            {
                int temp = x;
                x = y; 
                y = temp;
            }
            x *= sensor_pdata[i].acc_negate_x ? -1 : 1;
            y *= sensor_pdata[i].acc_negate_y ? -1 : 1;
            z *= sensor_pdata[i].acc_negate_z ? -1 : 1;

            input_report_abs(dev, ABS_X, x);
            input_report_abs(dev, ABS_Y, y);
            input_report_abs(dev, ABS_Z, z);
            input_sync(dev);
            return;
        }
    }
}

void aml_sensor_report_mag(struct i2c_client *client, struct input_dev *dev, int x, int y, int z)
{
    int i;
    for(i = 0; i < curr_idx; i++)
    {
        if(sensor_pdata[i].client && sensor_pdata[i].client == client)
        { 

            if(sensor_pdata[i].mag_swap_xy)
            {
                int temp = x;
                x = y; 
                y = temp;
            }
            x *= sensor_pdata[i].mag_negate_x ? -1 : 1;
            y *= sensor_pdata[i].mag_negate_y ? -1 : 1;
            z *= sensor_pdata[i].mag_negate_z ? -1 : 1;

            input_report_abs(dev, ABS_X, x);
            input_report_abs(dev, ABS_Y, y);
            input_report_abs(dev, ABS_Z, z);
            input_sync(dev);
            return;
       }
   }
}

void aml_sensor_report_gyr(struct i2c_client *client, struct input_dev *dev, int x, int y, int z)
{
    int i;
    for(i = 0; i < curr_idx; i++)
    {
        if(sensor_pdata[i].client && sensor_pdata[i].client == client)
        { 
            if(sensor_pdata[i].gyr_swap_xy)
            {
                int temp = x;
                x = y; 
                y = temp;
            }
            x *= sensor_pdata[i].gyr_negate_x ? -1 : 1;
            y *= sensor_pdata[i].gyr_negate_y ? -1 : 1;
            z *= sensor_pdata[i].gyr_negate_z ? -1 : 1;

            input_report_abs(dev, ABS_X, x);
            input_report_abs(dev, ABS_Y, y);
            input_report_abs(dev, ABS_Z, z);
            input_sync(dev);
            return;
         }
    }
}

int dt_sensor_setup_i2c_dev(struct device_node *node,  struct i2c_board_info *i2c_info, int *i2c_bus_nr, int *gpio)
{
        int ret = -1;
    
        int r;
        int irq;
        const char *status;
        u32 acc_dir, mag_dir, gyr_dir;

		r = of_property_read_string(node, "status", &status);
        if(r< 0){
            printk("%s: Failed to read status from device tree for dev %s\n", __func__, i2c_info->type);
            return -1;
        }

        if(strncmp("ok", status, 2) == 0)
        {
            u32 addr;
            const char *str;
            const char *name;

            r = of_property_read_string(node, "dev_name", &name);
            if(r< 0){
                printk("%s: Failed to read dev_name from device tree\n", __func__);
                return -1;
            } 

            strncpy(i2c_info->type, name, I2C_NAME_SIZE);

            r = of_property_read_u32(node, "address",&addr);
            if(r < 0)
            {
                printk("%s: faild to get i2c address for dev %s\n", __func__, i2c_info->type);
                return -1;
            }

            ret = 0;

            i2c_info->addr = addr;

            r = of_property_read_string(node, "i2c_bus", &str);
            if (r) {
                printk("%s: faild to get i2c_bus str for dev %s\n", __func__, i2c_info->type);
                *i2c_bus_nr = AML_I2C_BUS_B;
            } else {
                if (!strncmp(str, "i2c_bus_a", 9))
                    *i2c_bus_nr = AML_I2C_BUS_A;
                else if (!strncmp(str, "i2c_bus_b", 9))
                    *i2c_bus_nr = AML_I2C_BUS_B;
                else if (!strncmp(str, "i2c_bus_ao", 9))
                    *i2c_bus_nr = AML_I2C_BUS_AO;
                else
                    *i2c_bus_nr = AML_I2C_BUS_B;
            }

            r = of_property_read_u32(node, "acc_dir",&acc_dir);
            if(!r)
            {
                sensor_pdata[curr_idx].acc_negate_x = acc_dir & 0x0001;
                sensor_pdata[curr_idx].acc_negate_y = acc_dir & 0x0010;
                sensor_pdata[curr_idx].acc_negate_z = acc_dir & 0x0100;
                sensor_pdata[curr_idx].acc_swap_xy = acc_dir & 0x1000;
            }

            r = of_property_read_u32(node, "mag_dir",&mag_dir);
            if(!r)
            {
                sensor_pdata[curr_idx].mag_negate_x = mag_dir & 0x0001;
                sensor_pdata[curr_idx].mag_negate_y = mag_dir & 0x0010;
                sensor_pdata[curr_idx].mag_negate_z = mag_dir & 0x0100;
                sensor_pdata[curr_idx].mag_swap_xy = mag_dir & 0x1000;
            }

            r = of_property_read_u32(node, "gyo_dir",&gyr_dir);
            if(!r)
            {
                sensor_pdata[curr_idx].gyr_negate_x = gyr_dir & 0x0001;
                sensor_pdata[curr_idx].gyr_negate_y = gyr_dir & 0x0010;
                sensor_pdata[curr_idx].gyr_negate_z = gyr_dir & 0x0100;
                sensor_pdata[curr_idx].gyr_swap_xy = gyr_dir & 0x1000;
            }
           
            r = of_property_read_u32(node, "irq",&irq);
            if(r < 0 || irq <= 0)
            {
                *gpio = -1; 
                i2c_info->irq = 0;
            }
            else
            {
                const char *gpio_str;
                r = of_property_read_string(node, "gpio",&gpio_str);
                if(r < 0)
                {
                    printk("%s: faild to get gpio str for dev %s\n", __func__, i2c_info->type);
                    *gpio = -1; 
                    i2c_info->irq = 0;
                }
                else
                {
                    i2c_info->irq = irq;
                    *gpio = amlogic_gpio_name_map_num(gpio_str);
                }
            }
    } 
    
    return ret;
}


static int aml_sensor_probe(struct platform_device *pdev)
{
	
	struct device_node* child;
	struct i2c_board_info i2c_info;
	struct i2c_adapter *adapter;
    int i2c_bus_nr; 
    int gpio;

	struct device_node* node = pdev->dev.of_node;

	printk("##############aml_sensor_probe start############\n");

	for_each_child_of_node(node, child) {

		memset(&i2c_info, 0, sizeof(i2c_info));

        if(!dt_sensor_setup_i2c_dev(child, &i2c_info, &i2c_bus_nr, &gpio))
        {

            adapter = i2c_get_adapter(i2c_bus_nr);
            if(!adapter)
               return -1;

            if(gpio > 0)
            {
                /* get gpio and set up isr */
            }
            sensor_pdata[curr_idx++].client = i2c_new_device(adapter, &i2c_info);
            if(sensor_pdata[curr_idx-1].client == 0)
            {
                pr_err("Failed to create new i2c dev %s\n", i2c_info.type);
            }

            if(curr_idx>=MAX_SENSOR_ONBOARD)
            {
               pr_err("%s: Warning!!! Not enough slots in sensor_pdata array\n", __func__);
               return -1;
            }
        }
	}
	return 0;
}

static int aml_sensor_remove(struct platform_device *pdev)
{
   return 0; 
}

static const struct of_device_id sensor_prober_dt_match[]={
	{	
		.compatible = "amlogic,aml_sensor",
	},
	{},
};

static struct platform_driver aml_sensor_prober_driver = {
	.probe		= aml_sensor_probe,
	.remove		= aml_sensor_remove,
	.driver		= {
		.name	= "aml_sensor",
		.owner	= THIS_MODULE,
		.of_match_table = sensor_prober_dt_match,
	},
};

static int __init aml_sensor_prober_init(void)
{
	int ret;

	ret = platform_driver_register(&aml_sensor_prober_driver);
	if (ret){
		printk(KERN_ERR"aml_cams_probre_driver register failed\n");
		return ret;
	}

	return ret;
}


static void __exit aml_sensor_prober_exit(void)
{
	platform_driver_unregister(&aml_sensor_prober_driver);
}

module_init(aml_sensor_prober_init);
module_exit(aml_sensor_prober_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Amlogic MEMS sensor prober driver");

