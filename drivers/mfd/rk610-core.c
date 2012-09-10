#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <asm/gpio.h>
#include <linux/mfd/rk610_core.h>
#include <linux/clk.h>
#include <mach/iomux.h>
#include <linux/err.h>
#include <linux/slab.h>

#if defined(CONFIG_ARCH_RK3066B)
#define RK610_RESET_PIN   RK30_PIN2_PC5
#elif defined(CONFIG_ARCH_RK30)
#define RK610_RESET_PIN   RK30_PIN0_PC6
#else
#define RK610_RESET_PIN   RK29_PIN6_PC1
#endif

/*
 * Debug
 */
#if 0
#define	DBG(x...)	printk(KERN_INFO x)
#else
#define	DBG(x...)
#endif

static struct i2c_client *rk610_control_client = NULL;
#ifdef CONFIG_RK610_LVDS
extern int rk610_lcd_init(struct rk610_core_info *rk610_core_info);
#else
int rk610_lcd_init(struct rk610_core_info *rk610_core_info){}
#endif
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
#ifdef RK610_DEBUG
static int rk610_read_p0_reg(struct i2c_client *client, char reg, char *val)
{
	return i2c_master_reg8_recv(client, reg, val, 1, 100*1000) > 0? 0: -EINVAL;
}

static int rk610_write_p0_reg(struct i2c_client *client, char reg, char *val)
{
	return i2c_master_reg8_send(client, reg, val, 1, 100*1000) > 0? 0: -EINVAL;
}
static ssize_t rk610_show_reg_attrs(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{

	int i,size=0;
	char val;
	struct i2c_client *client=rk610_control_client;

	for(i=0;i<256;i++)
	{
		rk610_read_p0_reg(client, i,  &val);
		if(i%16==0)
			size += sprintf(buf+size,"\n>>>rk610_ctl %x:",i);
		size += sprintf(buf+size," %2x",val);
	}

	return size;
}
static ssize_t rk610_store_reg_attrs(struct device *dev,
						struct device_attribute *attr,
			 			const char *buf, size_t size)
{
	struct i2c_client *client=NULL;
	static char val=0,reg=0;
	client = rk610_control_client;
	DBG("/**********rk610 reg config******/");

	sscanf(buf, "%x%x", &val,&reg);
	DBG("reg=%x val=%x\n",reg,val);
	rk610_write_p0_reg(client, reg,  &val);
	DBG("val=%x\n",val);
	return size;
}

static struct device_attribute rk610_attrs[] = {
	__ATTR(reg_ctl, 0777,rk610_show_reg_attrs,rk610_store_reg_attrs),
};
#endif

static int rk610_control_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
    int ret;
    struct clk *iis_clk;
	struct rk610_core_info *core_info = NULL; 
	DBG("[%s] start\n", __FUNCTION__);
	core_info = kmalloc(sizeof(struct rk610_core_info), GFP_KERNEL);
    if(!core_info)
    {
        dev_err(&client->dev, ">> rk610 core inf kmalloc fail!");
        return -ENOMEM;
    }
    memset(core_info, 0, sizeof(struct rk610_core_info));
		#if defined(CONFIG_SND_RK29_SOC_I2S_8CH)        
        	iis_clk = clk_get_sys("rk29_i2s.0", "i2s");
		#elif defined(CONFIG_SND_RK29_SOC_I2S_2CH)
		iis_clk = clk_get_sys("rk29_i2s.1", "i2s");
		#else
        	iis_clk = clk_get_sys("rk29_i2s.2", "i2s");
		#endif
		if (IS_ERR(iis_clk)) {
			printk("failed to get i2s clk\n");
			ret = PTR_ERR(iis_clk);
		}else{
			DBG("got i2s clk ok!\n");
			clk_enable(iis_clk);
			clk_set_rate(iis_clk, 11289600);
			#if defined(CONFIG_ARCH_RK29)
			rk29_mux_api_set(GPIO2D0_I2S0CLK_MIIRXCLKIN_NAME, GPIO2H_I2S0_CLK);
			#elif defined(CONFIG_ARCH_RK3066B)
			rk30_mux_api_set(GPIO1C0_I2SCLK_NAME, GPIO1C_I2SCLK);
			#elif defined(CONFIG_ARCH_RK30)
                        rk30_mux_api_set(GPIO0B0_I2S8CHCLK_NAME, GPIO0B_I2S_8CH_CLK);
			#endif
			clk_put(iis_clk);
		}

    rk610_control_client = client;
    msleep(100);
	if(RK610_RESET_PIN != INVALID_GPIO) {
	    ret = gpio_request(RK610_RESET_PIN, "rk610 reset");
	    if (ret){
	        printk(KERN_ERR "rk610_control_probe request gpio fail\n");
	    }
	    else {
	    	DBG("rk610_control_probe request gpio ok\n");
	    	gpio_direction_output(RK610_RESET_PIN, GPIO_HIGH);
		     msleep(100);
		    gpio_direction_output(RK610_RESET_PIN, GPIO_LOW);
			msleep(100);
		    gpio_set_value(RK610_RESET_PIN, GPIO_HIGH);
		}
	}
    core_info->client = client;
	rk610_lcd_init(core_info);
	#ifdef RK610_DEBUG
	device_create_file(&(client->dev), &rk610_attrs[0]);
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
	DBG("[%s] start\n", __FUNCTION__);
	return i2c_add_driver(&rk610_control_driver);
}

static void __exit rk610_control_exit(void)
{
	i2c_del_driver(&rk610_control_driver);
}

subsys_initcall_sync(rk610_control_init);
//module_init(rk610_control_init);
module_exit(rk610_control_exit);


MODULE_DESCRIPTION("RK610 control driver");
MODULE_AUTHOR("Rock-chips, <www.rock-chips.com>");
MODULE_LICENSE("GPL");

