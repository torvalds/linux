#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/delay.h>
#include "i2c-rk30.h"

#if 0
#define TEST_SCL_RATE (100 * 1000)
#define I2C_NUM     1

struct rw_info {
    unsigned short addr;
    unsigned int reg;
    unsigned int reg_bytes;
    unsigned char buf[100];
    unsigned int len;
};

static int test_write(struct i2c_client *client, struct rw_info *info)
{
    int i, ret = 0;
    struct i2c_msg msg;
    
    char *buf = kzalloc(info->reg_bytes + info->len, GFP_KERNEL);
    for(i = 0; i < info->reg_bytes; i++)
        buf[i] = (info->reg >> i)& 0xff;
    for(i = info->reg_bytes; i < info->len; i++)
        buf[i] = info->buf[i - info->reg_bytes];

    msg.addr = client->addr;
    msg.flags = client->flags;
    msg.buf = buf;
    msg.len = info->reg_bytes + info->len;
    msg.scl_rate = TEST_SCL_RATE;
    ret = i2c_transfer(client->adapter, &msg, 1);
    kfree(buf);

    return ret;

}

static int test_read(struct i2c_client *client, struct rw_info *info)
{
    int i, ret = 0, msg_num = 0;
    char buf[4];
    struct i2c_msg msgs[2];
    if(info->reg_bytes == 0){
        msgs[0].addr = client->addr;
        msgs[0].flags = client->flags|I2C_M_RD;
        msgs[0].buf = info->buf;
        msgs[0].len = info->len;
        msgs[0].scl_rate = TEST_SCL_RATE;  
        msg_num = 1;
    }else {
        for(i = 0; i < info->reg_bytes; i++) {
            buf[i] = (info->reg >> i) & 0xff;
        }
        msgs[0].addr = client->addr;
        msgs[0].flags = client->flags;
        msgs[0].buf = buf;
        msgs[0].len = info->reg_bytes;
        msgs[0].scl_rate = TEST_SCL_RATE;  

        msgs[1].addr = client->addr;
        msgs[1].flags = client->flags|I2C_M_RD;
        msgs[1].buf = info->buf;
        msgs[1].len = info->len;
        msgs[1].scl_rate = TEST_SCL_RATE;
        msg_num = 2;
    }


    ret = i2c_transfer(client->adapter, msgs, msg_num);
    return ret;
}

static void test_set_client(struct i2c_client *client, __u16 addr, int nr)
{
    client->flags = 0;
    client->addr = addr;
    client->adapter = i2c_get_adapter(nr);
}
static int __init test_init(void)
{
    int nr = 0, ret = 0;
    struct i2c_client *client = NULL;
    struct rw_info info = {
        .addr = 0x51,
        .reg = 0x01,
        .reg_bytes = 1,
        .len = 8,
        .buf = 
        {
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
        },
    };
        
    client = kzalloc(sizeof(struct i2c_client) * I2C_NUM, GFP_KERNEL);

    printk("%s: start\n", __func__);
    while(1) {
        for(nr = 0; nr < I2C_NUM; nr++){
            test_set_client(&client[nr], info.addr, nr);
            ret = test_write(&client[nr], &info);
            printk("i2c-%d: write val [0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x], ret = %d\n",
                    nr, info.buf[0], info.buf[1], info.buf[2], info.buf[3], info.buf[4], info.buf[5], info.buf[6], info.buf[7], ret);
            if(ret < 0)
                break;
            ret = test_read(&client[nr], &info);
            printk("i2c-%d: read val [0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x], ret = %d\n",
                    nr, info.buf[0], info.buf[1], info.buf[2], info.buf[3], info.buf[4], info.buf[5], info.buf[6], info.buf[7], ret);
            if(ret < 0)
                break;
        }
    }
    kfree(client);
    return 0;
}

static void __exit test_exit(void)
{
    return;
}

subsys_initcall_sync(test_init);
module_exit(test_exit);

#endif

