#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/skbuff.h>

#include <mach/board.h>
#include <mach/io.h>
#include <mach/gpio.h>
#include <mach/iomux.h>

#include <linux/board-id.h>

#if 0
#define DBG(x...)  printk(x)
#else
#define DBG(x...)
#endif

extern void kernel_restart(char *cmd);

struct board_id_hw_private_data {
	struct mutex id_mutex;
	int last_value[16];
	int board_id;
	struct board_id_platform_data *pdata;
};

static struct board_id_hw_private_data *g_id;


enum board_id_hw get_board_id_hw(void)
{
	struct board_id_hw_private_data *id = g_id;
	DBG("%s:id:0x%x\n",__func__,id->board_id);
	return id->board_id;
}
EXPORT_SYMBOL(get_board_id_hw);

static int _get_board_id_hw(struct board_id_hw_private_data *id)
{
	int result = 0;
	int value1 = 0, value2 = 0, value3 = 0;
	int i = 0, j = 0;
	
	id->board_id = -1;
			
	for(i=0; i<id->pdata->num_gpio; i++)
	{
		gpio_request(id->pdata->gpio_pin[i],"gpio_board_id");
		gpio_direction_input(id->pdata->gpio_pin[i]);
		gpio_pull_updown(id->pdata->gpio_pin[i], PullDisable);
		for(j=0; j<1000; j++)
		{
			value1 = gpio_get_value(id->pdata->gpio_pin[i]);
			if(value1 < 0)
				continue;
			mdelay(1);
			value2 = gpio_get_value(id->pdata->gpio_pin[i]);
			if(value2 < 0)
				continue;
			mdelay(1);
			value3 = gpio_get_value(id->pdata->gpio_pin[i]);
			if(value3 < 0)
				continue;
			if((value1 == value2) && (value2 == value3))
				break;
		}
		if(j >= 1000)
		{
			printk("%s:hareware error,gpio level changed always!\n",__func__);			
			kernel_restart(NULL);
		}
		
		result = (value1 << i) | result;
		
		DBG("%s:gpio:%d,value:%d\n",__func__,id->pdata->gpio_pin[i],value1);
	}

	id->board_id = result;

	
	DBG("%s:num=%d,id=0x%x\n",__func__,id->pdata->num_gpio, id->board_id);

	return result;
}


static int __devinit board_id_hw_probe(struct platform_device *pdev)
{
	struct board_id_platform_data *pdata = pdev->dev.platform_data;
	struct board_id_hw_private_data *id = NULL;
	int result = 0;

	if(!pdata)
		return -ENOMEM;
	
	id = kzalloc(sizeof(struct board_id_hw_private_data), GFP_KERNEL);
	if (id == NULL) {
		dev_err(&pdev->dev, "Unable to allocate private data\n");
		return -ENOMEM;
	}

	id->pdata = pdata;
	
	if(pdata->init_platform_hw)
		pdata->init_platform_hw();
	
	result = _get_board_id_hw(id);

	if(pdata->init_parameter)
		pdata->init_parameter(id->board_id);

	if(pdata->exit_platform_hw)
		pdata->exit_platform_hw();
	
	platform_set_drvdata(pdev, id);	
	g_id = id;
	
	printk("%s:board id :0x%x\n",__func__,result);
	return 0;
}

static int __devexit board_id_hw_remove(struct platform_device *pdev)
{
	//struct board_id_platform_data *pdata = pdev->dev.platform_data;
	struct board_id_hw_private_data *id = platform_get_drvdata(pdev);
	
	kfree(id);
	
	return 0;
}

static struct platform_driver board_id_hw_driver = {
	.probe		= board_id_hw_probe,
	.remove		= __devexit_p(board_id_hw_remove),
	.driver		= {
		.name	= "board_id_hw",
		.owner	= THIS_MODULE,
	},
};

static int __init board_id_hw_init(void)
{
	return platform_driver_register(&board_id_hw_driver);
}

static void __exit board_id_hw_exit(void)
{
	platform_driver_unregister(&board_id_hw_driver);
}

arch_initcall_sync(board_id_hw_init);
module_exit(board_id_hw_exit);

MODULE_AUTHOR("ROCKCHIP Corporation:lw@rock-chips.com");
MODULE_DESCRIPTION("Interface for get board id");
MODULE_LICENSE("GPL");


