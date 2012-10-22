#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>


static int check_tp_param(void)
{
        if(en == 0 || i2c == -1 || addr == -1 || x_max == -1 || y_max == -1){
                printk("touchpad: en: %d, i2c: %d, addr: 0x%x, x_max: %d, y_max: %d\n",
                                en, i2c, addr, x_max, y_max);
                return -EINVAL;
        }else{
                return 0;
        }
}

struct i2c_board_info __initdata tp_info = {
        .type = TP_MODULE_NAME,
        .flags = 0,
};

static int tp_board_init(void)
{
        int ret = 0;

        ret = check_tp_param();
        if(ret < 0)
                return ret;

        tp_info.addr = addr;
        ret = i2c_add_device(i2c, &tp_info);

        return ret;
}

