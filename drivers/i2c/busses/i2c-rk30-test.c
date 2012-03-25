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
    unsigned char tx_buf[100];
    unsigned char rx_buf[100];
    unsigned int rx_len;
    unsigned int tx_len;
};

static int test_write(struct i2c_client *client, struct rw_info *info)
{
    int i, ret = 0;
    struct i2c_msg msg;
    
    char *buf = kzalloc(info->reg_bytes + info->tx_len, GFP_KERNEL);
    for(i = 0; i < info->reg_bytes; i++)
        buf[i] = (info->reg >> i)& 0xff;
    for(i = info->reg_bytes; i < info->tx_len; i++)
        buf[i] = info->tx_buf[i - info->reg_bytes];

    msg.addr = client->addr;
    msg.flags = client->flags;
    msg.buf = buf;
    msg.len = info->reg_bytes + info->tx_len;
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
        msgs[0].buf = info->rx_buf;
        msgs[0].len = info->rx_len;
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
        msgs[1].buf = info->rx_buf;
        msgs[1].len = info->rx_len;
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
    unsigned long i = 0;
    struct i2c_client *client = NULL;
    struct rw_info info = {
        .addr = 0x51,
        .reg = 0x01,
        .reg_bytes = 1,
        .tx_len = 8,
        .tx_buf = 
        {
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
        },
        .rx_len = 8,
    };
        
    client = kzalloc(sizeof(struct i2c_client) * I2C_NUM, GFP_KERNEL);

    printk("%s: start\n", __func__);
    while(1) {
        for(nr = 0; nr < I2C_NUM; nr++){
            for(i = 0x51; i < 0x52; i++){
                info.addr = i;
                test_set_client(&client[nr], info.addr, nr);
                ret = test_write(&client[nr], &info);
                printk("i2c-%d: addr[0x%02x] write val [0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x], ret = %d\n",
                        nr, info.addr, info.tx_buf[0], info.tx_buf[1], info.tx_buf[2], info.tx_buf[3], 
                        info.tx_buf[4], info.tx_buf[5], info.tx_buf[6], info.tx_buf[7], ret);
                if(info.addr == 0x51 && ret < 0)
                    goto out;
                ret = test_read(&client[nr], &info);
                printk("i2c-%d: addr[0x%02x] read val [0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x], ret = %d\n",
                        nr, info.addr, info.rx_buf[0], info.rx_buf[1], info.rx_buf[2], info.rx_buf[3], 
                        info.rx_buf[4], info.rx_buf[5], info.rx_buf[6], info.rx_buf[7], ret);
                if(info.addr == 0x51 && ret < 0)
                    goto out;
            }
        }
    }
out:
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

