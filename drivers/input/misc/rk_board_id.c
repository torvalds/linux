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


#if 0
#define DBG(x...)  printk(x)
#else
#define DBG(x...)
#endif

struct board_id_private_data {
	struct mutex id_mutex;
	int board_id;
	struct board_id_platform_data *pdata;
};

static struct board_id_private_data *g_id;


int rk_get_board_id(void)
{
	struct board_id_private_data *id = g_id;
	DBG("%s:id:0x%x\n",__func__,id->board_id);
	return id->board_id;
}
EXPORT_SYMBOL(rk_get_board_id);

static int _rk_get_board_id(struct board_id_private_data *id)
{
	int result = 0;
	int value = 0;
	int i = 0;
	
	id->board_id = -1;
			
	for(i=0; i<id->pdata->num_gpio; i++)
	{
		gpio_request(id->pdata->gpio_pin[i],"gpio_board_id");
		gpio_direction_input(id->pdata->gpio_pin[i]);
		gpio_pull_updown(id->pdata->gpio_pin[i], PullDisable);
		value = gpio_get_value(id->pdata->gpio_pin[i]);
		if(value < 0)
			return value;
		result = (value << i) | result;
		
		DBG("%s:gpio:%d,value:%d\n",__func__,id->pdata->gpio_pin[i],value);
	}
	
	id->board_id = result;

	
	DBG("%s:num=%d,id=0x%x\n",__func__,id->pdata->num_gpio, id->board_id);

	return result;
}


static int __devinit rk_board_id_probe(struct platform_device *pdev)
{
	struct board_id_platform_data *pdata = pdev->dev.platform_data;
	struct board_id_private_data *id = NULL;
	int result = 0;

	if(!pdata)
		return -ENOMEM;
	
	id = kzalloc(sizeof(struct board_id_private_data), GFP_KERNEL);
	if (id == NULL) {
		dev_err(&pdev->dev, "Unable to allocate private data\n");
		return -ENOMEM;
	}

	id->pdata = pdata;
	
	if(pdata->init_platform_hw)
		pdata->init_platform_hw();
	
	result = _rk_get_board_id(id);

	if(pdata->init_parameter)
		pdata->init_parameter(id->board_id);

	if(pdata->exit_platform_hw)
		pdata->exit_platform_hw();
	
	platform_set_drvdata(pdev, id);	
	g_id = id;
	
	printk("%s:board id :0x%x\n",__func__,result);
	return 0;
}

static int __devexit rk_board_id_remove(struct platform_device *pdev)
{
	//struct board_id_platform_data *pdata = pdev->dev.platform_data;
	struct board_id_private_data *id = platform_get_drvdata(pdev);
	
	kfree(id);
	
	return 0;
}

static struct platform_driver rk_board_id_driver = {
	.probe		= rk_board_id_probe,
	.remove		= __devexit_p(rk_board_id_remove),
	.driver		= {
		.name	= "rk-board-id",
		.owner	= THIS_MODULE,
	},
};

static int __init rk_get_board_init(void)
{
	return platform_driver_register(&rk_board_id_driver);
}

static void __exit rk_get_board_exit(void)
{
	platform_driver_unregister(&rk_board_id_driver);
}

subsys_initcall_sync(rk_get_board_init);
module_exit(rk_get_board_exit);

MODULE_AUTHOR("ROCKCHIP Corporation:lw@rock-chips.com");
MODULE_DESCRIPTION("Interface for get board id");
MODULE_LICENSE("GPL");
