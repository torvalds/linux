#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <asm/gpio.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rk610_core.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#endif

#define GPIO_HIGH 1
#define GPIO_LOW 0

/*
 * Debug
 */
#if 0
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

static struct i2c_client *rk610_control_client = NULL;

int i2c_master_reg8_send(const struct i2c_client *client, const char reg, const char *buf, int count, int scl_rate)
{
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;
	int ret;
	char *tx_buf = (char *)kmalloc(count + 1, GFP_KERNEL);
	if(!tx_buf)
		return -ENOMEM;
	tx_buf[0] = reg;
	memcpy(tx_buf+1, buf, count); 

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = count + 1;
	msg.buf = (char *)tx_buf;
	msg.scl_rate = scl_rate;
//	msg.udelay = client->udelay;

	ret = i2c_transfer(adap, &msg, 1);
	kfree(tx_buf);
	return (ret == 1) ? count : ret;

}

int i2c_master_reg8_recv(const struct i2c_client *client, const char reg, char *buf, int count, int scl_rate)
{
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msgs[2];
	int ret;
	char reg_buf = reg;
	
	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].len = 1;
	msgs[0].buf = &reg_buf;
	msgs[0].scl_rate = scl_rate;
//	msgs[0].udelay = client->udelay;

	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = count;
	msgs[1].buf = (char *)buf;
	msgs[1].scl_rate = scl_rate;
//	msgs[1].udelay = client->udelay;

	ret = i2c_transfer(adap, msgs, 2);

	return (ret == 2)? count : ret;
}


static struct mfd_cell rk610_devs[] = {
	{
		.name = "rk610-lcd",
		.id = 0,
	},
};

int rk610_control_send_byte(const char reg, const char data)
{
	int ret;

	DBG("reg = 0x%02x, val=0x%02x\n", reg ,data);

	if(rk610_control_client == NULL)
		return -1;
	//i2c_master_reg8_send
	ret = i2c_master_reg8_send(rk610_control_client, reg, &data, 1, 100*1000);
	if (ret > 0)
		ret = 0;

	return ret;
}

#ifdef CONFIG_SND_SOC_RK610
static unsigned int current_pll_value = 0;
int rk610_codec_pll_set(unsigned int rate)
{
	char N, M, NO, DIV;
	unsigned int F;
	char data;

	if(current_pll_value == rate)
		return 0;

    // Input clock is 12MHz.
	if(rate == 11289600) {
		// For 11.2896MHz, N = 2 M= 75 F = 0.264(0x43958) NO = 8
		N = 2;
		NO = 3;
		M = 75;
		F = 0x43958;
		DIV = 5;
	}
	else if(rate == 12288000) {
		// For 12.2888MHz, N = 2 M= 75 F = 0.92(0xEB851) NO = 8
		N = 2;
		NO = 3;
		M = 75;
		F = 0xEB851;
		DIV = 5;
	}
	else {
		printk(KERN_ERR "[%s] not support such frequency\n", __FUNCTION__);
		return -1;
	}

	//Enable codec pll fractional number and power down.
    data = 0x00;
    rk610_control_send_byte(RK610_CONTROL_REG_C_PLL_CON5, data);
	msleep(10);

    data = (N << 4) | NO;
    rk610_control_send_byte(RK610_CONTROL_REG_C_PLL_CON0, data);
    // M
    data = M;
    rk610_control_send_byte(RK610_CONTROL_REG_C_PLL_CON1, data);
    // F
    data = F & 0xFF;
    rk610_control_send_byte(RK610_CONTROL_REG_C_PLL_CON2, data);
    data = (F >> 8) & 0xFF;
    rk610_control_send_byte(RK610_CONTROL_REG_C_PLL_CON3, data);
    data = (F >> 16) & 0xFF;
    rk610_control_send_byte(RK610_CONTROL_REG_C_PLL_CON4, data);

    // i2s mclk = codec_pll/5;
    i2c_master_reg8_recv(rk610_control_client, RK610_CONTROL_REG_CLOCK_CON1, &data, 1, 100*1000);
    data &= ~CLOCK_CON1_I2S_DVIDER_MASK;
    data |= (DIV - 1);
    rk610_control_send_byte(RK610_CONTROL_REG_CLOCK_CON1, data);

    // Power up codec pll.
    data |= C_PLL_POWER_ON;
    rk610_control_send_byte(RK610_CONTROL_REG_C_PLL_CON5, data);

    current_pll_value = rate;
    DBG("[%s] rate %u\n", __FUNCTION__, rate);

    return 0;
}

void rk610_control_init_codec(void)
{
    struct i2c_client *client = rk610_control_client;
    char data = 0;
    int ret;

    if(rk610_control_client == NULL)
    	return;
	DBG("[%s] start\n", __FUNCTION__);

    //gpio_set_value(RK610_RESET_PIN, GPIO_LOW); //reset rk601
   // mdelay(100);
    //gpio_set_value(RK610_RESET_PIN, GPIO_HIGH);
    //mdelay(100);

   	// Set i2c glitch timeout.
	data = 0x22;
	ret = i2c_master_reg8_send(client, RK610_CONTROL_REG_I2C_CON, &data, 1, 20*1000);

//    rk610_codec_pll_set(11289600);

    //use internal codec, enable DAC ADC LRCK output.
//    i2c_master_reg8_recv(client, RK610_CONTROL_REG_CODEC_CON, &data, 1, 100*1000);
//    data = CODEC_CON_BIT_DAC_LRCL_OUTPUT_DISABLE | CODEC_CON_BIT_ADC_LRCK_OUTPUT_DISABLE;
//	data = CODEC_CON_BIT_ADC_LRCK_OUTPUT_DISABLE;
	data = 0;
   	rk610_control_send_byte(RK610_CONTROL_REG_CODEC_CON, data);

    // Select internal i2s clock from codec_pll.
    i2c_master_reg8_recv(rk610_control_client, RK610_CONTROL_REG_CLOCK_CON1, &data, 1, 100*1000);
//    data |= CLOCK_CON1_I2S_CLK_CODEC_PLL;
	data = 0;
    rk610_control_send_byte(RK610_CONTROL_REG_CLOCK_CON1, data);

    i2c_master_reg8_recv(client, RK610_CONTROL_REG_CODEC_CON, &data, 1, 100*1000);
    DBG("[%s] RK610_CONTROL_REG_CODEC_CON is %x\n", __FUNCTION__, data);

    i2c_master_reg8_recv(client, RK610_CONTROL_REG_CLOCK_CON1, &data, 1, 100*1000);
    DBG("[%s] RK610_CONTROL_REG_CLOCK_CON1 is %x\n", __FUNCTION__, data);
}
#endif

static int rk610_read_p0_reg(struct i2c_client *client, char reg, char *val)
{
	return i2c_master_reg8_recv(client, reg, val, 1, 100*1000) > 0? 0: -EINVAL;
}

static int rk610_write_p0_reg(struct i2c_client *client, char reg, char *val)
{
	return i2c_master_reg8_send(client, reg, val, 1, 100*1000) > 0? 0: -EINVAL;
}


#if defined(CONFIG_DEBUG_FS)
static int rk610_reg_show(struct seq_file *s, void *v)
{
	char reg = 0;
	u8 val = 0;
	struct rk610_core_info *core_info = s->private;
	if(!core_info)
	{
		dev_err(core_info->dev,"no mfd rk610!\n");
		return 0;
	}

	for(reg=C_PLL_CON0;reg<= I2C_CON;reg++)
	{
		rk610_read_p0_reg(core_info->client, reg,  &val);
		if(reg%8==0)
			seq_printf(s,"\n0x%02x:",reg);
		seq_printf(s," %02x",val);
	}
	seq_printf(s,"\n");

	return 0;
}

static ssize_t rk610_reg_write (struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{ 
	struct rk610_core_info *core_info = file->f_path.dentry->d_inode->i_private;
	u32 reg,val;
	
	char kbuf[25];
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	sscanf(kbuf, "%x%x", &reg,&val);
	rk610_write_p0_reg(core_info->client, reg,  (u8*)&val);
	return count;
}

static int rk610_reg_open(struct inode *inode, struct file *file)
{
	struct rk610_core_info *core_info = inode->i_private;
	return single_open(file,rk610_reg_show,core_info);
}

static const struct file_operations rk610_reg_fops = {
	.owner		= THIS_MODULE,
	.open		= rk610_reg_open,
	.read		= seq_read,
	.write          = rk610_reg_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

static int rk610_control_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct rk610_core_info *core_info = NULL; 
	struct device_node *rk610_np;

	DBG("[%s] start\n", __FUNCTION__);
	core_info = kmalloc(sizeof(struct rk610_core_info), GFP_KERNEL);
	if(!core_info)
	{
		dev_err(&client->dev, ">> rk610 core inf kmalloc fail!");
		return -ENOMEM;
	}
	memset(core_info, 0, sizeof(struct rk610_core_info));

	rk610_control_client = client;
	
	core_info->client = client;
	core_info->dev = &client->dev;
	i2c_set_clientdata(client,core_info);
	
	rk610_np = core_info->dev->of_node;
	core_info->reset_gpio = of_get_named_gpio(rk610_np,"rk610-reset-io", 0);
	if (!gpio_is_valid(core_info->reset_gpio)){
		printk("invalid core_info->reset_gpio: %d\n",core_info->reset_gpio);
		return -1;
	}
	ret = gpio_request(core_info->reset_gpio, "rk610-reset-io");
	if( ret != 0){
		printk("gpio_request core_info->reset_gpio invalid: %d\n",core_info->reset_gpio);
		return ret;
	}
	gpio_direction_output(core_info->reset_gpio, GPIO_HIGH);
	msleep(100);
	gpio_direction_output(core_info->reset_gpio, GPIO_LOW);
	msleep(100);
	gpio_set_value(core_info->reset_gpio, GPIO_HIGH);

	core_info->i2s_clk= clk_get(&client->dev, "i2s_clk");
	if (IS_ERR(core_info->i2s_clk)) {
		dev_err(&client->dev, "Can't retrieve i2s clock\n");
		ret = PTR_ERR(core_info->i2s_clk);
		return ret;
	}
	clk_set_rate(core_info->i2s_clk, 11289600);
	clk_prepare_enable(core_info->i2s_clk);

	ret = mfd_add_devices(&client->dev, -1,
				      rk610_devs, ARRAY_SIZE(rk610_devs),
				      NULL,0,NULL);
	
#if defined(CONFIG_DEBUG_FS)
	core_info->debugfs_dir = debugfs_create_dir("rk610", NULL);
	if (IS_ERR(core_info->debugfs_dir))
	{
		dev_err(&client->dev,"failed to create debugfs dir for rk610!\n");
	}
	else
		debugfs_create_file("core", S_IRUSR,core_info->debugfs_dir,core_info,&rk610_reg_fops);
#endif

    return 0;
}

static int rk610_control_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id rk610_control_id[] = {
	{ "rk610_ctl", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rk610_control_id);

static struct i2c_driver rk610_control_driver = {
	.driver = {
		.name = "rk610_ctl",
	},
	.probe = rk610_control_probe,
	.remove = rk610_control_remove,
	.id_table = rk610_control_id,
};

static int __init rk610_control_init(void)
{
	return i2c_add_driver(&rk610_control_driver);
}

static void __exit rk610_control_exit(void)
{
	i2c_del_driver(&rk610_control_driver);
}

subsys_initcall_sync(rk610_control_init);
module_exit(rk610_control_exit);


MODULE_DESCRIPTION("RK610 control driver");
MODULE_AUTHOR("Rock-chips, <www.rock-chips.com>");
MODULE_LICENSE("GPL");

