#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/stat.h>
#include <linux/io.h>
#include <linux/vmalloc.h>
#include <asm/io.h>
#include <asm/sizes.h>
#include <mach/iomux.h>
#include <mach/gpio.h>
#include <linux/delay.h>

#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <mach/board.h>

#include <linux/platform_device.h>

#include "rk29_modem.h"

static int rk30_modem_probe(struct platform_device *pdev);

static struct platform_driver demo_platform_driver = {
    .probe  = rk30_modem_probe,
    .driver = {
        .name   = "rk30_modem",
    },
};

static struct rk29_modem_t demo_driver = {
    .driver         = &demo_platform_driver,
    .modem_power    = NULL,//&demo_io_power,
    .ap_ready       = NULL,
    .bp_wakeup_ap   = NULL,
    .status         = MODEM_ENABLE,
    .dev_init       = NULL,
    .dev_uninit     = NULL,
    .irq_handler    = NULL,
    
    .enable         = NULL,
    .disable        = NULL,
    .sleep          = NULL,
    .wakeup         = NULL,
};

static int rk30_modem_probe(struct platform_device *pdev)
{
    int ret = 0;
    struct rk29_io_t *rk29_io_info = pdev->dev.platform_data;

    printk("%s\n", __FUNCTION__);
    demo_driver.modem_power = rk29_io_info;
    demo_driver.modem_power->io_init();

    return 0;
}

static int __init demo_init(void)
{
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);

    return rk29_modem_init(&demo_driver);
}

static void __exit demo_exit(void)
{
    printk("%s[%d]: %s\n", __FILE__, __LINE__, __FUNCTION__);
    rk29_modem_exit();
}

module_init(demo_init);
module_exit(demo_exit);

MODULE_AUTHOR("lintao lintao@rock-chips.com, cmy@rock-chips.com");
MODULE_DESCRIPTION("ROCKCHIP modem driver");
MODULE_LICENSE("GPL");

