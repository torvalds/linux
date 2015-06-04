#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rk1000.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>

#define RK1000_CORE_DBG 0

#if RK1000_CORE_DBG
#define	DBG(x...)	pr_info(x)
#else
#define	DBG(x...)
#endif

#define	CTRL_ADC	0x00
#define	CTRL_CODEC	0x01
#define	CTRL_I2C	0x02
#define	CTRL_TVE	0x03
#define	RGB2CCIR_RESET	0x04
#define	ADC_START	0x05

struct rk1000 {
	struct i2c_client *client;
	struct device *dev;
	struct dentry *debugfs_dir;
	struct ioctrl io_power;
	struct ioctrl io_reset;
};

static struct rk1000 *rk1000;
int cvbsmode = -1;

void rk1000_reset_ctrl(int enable)
{
	DBG("rk1000_reset_ctrl\n");
	if (rk1000 && gpio_is_valid(rk1000->io_reset.gpio)) {
		if (enable) {
			gpio_set_value(rk1000->io_reset.gpio,
				       !(rk1000->io_reset.active));
		} else {
			DBG("rk1000 reset pull low\n");
			gpio_set_value(rk1000->io_reset.gpio,
				       (rk1000->io_reset.active));
		}
	}
}

int rk1000_i2c_send(const u8 addr, const u8 reg, const u8 value)
{
	struct i2c_adapter *adap;
	struct i2c_msg msg;
	int ret;
	char buf[2];

	if (rk1000 == NULL || rk1000->client == NULL) {
		DBG("rk1000 not init!\n");
		return -1;
	}
	adap = rk1000->client->adapter;
	buf[0] = reg;
	buf[1] = value;
	msg.addr = addr;
	msg.flags = rk1000->client->flags;
	msg.len = 2;
	msg.buf = buf;
	msg.scl_rate = RK1000_I2C_RATE;
	ret = i2c_transfer(adap, &msg, 1);
	if (ret != 1) {
		DBG("rk1000 control i2c write err,ret =%d\n", ret);
		return -1;
	}
	return 0;
}

int rk1000_i2c_recv(const u8 addr, const u8 reg, const char *buf)
{
	struct i2c_adapter *adap;
	struct i2c_msg msgs[2];
	int ret;

	if (rk1000 == NULL || rk1000->client == NULL) {
		DBG("rk1000 not init!\n");
		return -1;
	}
	adap = rk1000->client->adapter;
	msgs[0].addr = addr;
	msgs[0].flags = rk1000->client->flags;
	msgs[0].len = 1;
	msgs[0].buf = (unsigned char *)(&reg);
	msgs[0].scl_rate = RK1000_I2C_RATE;
	msgs[1].addr = addr;
	msgs[1].flags = rk1000->client->flags | I2C_M_RD;
	msgs[1].len = 1;
	msgs[1].buf = (unsigned char *)buf;
	msgs[1].scl_rate = RK1000_I2C_RATE;
	ret = i2c_transfer(adap, msgs, 2);
	return (ret == 2) ? 0 : -1;
}

static ssize_t rk1000_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	int ret = -1;
	int i = 0;
	unsigned char tv_encoder_regs[] = {0x00, 0x00, 0x00, 0x03, 0x00, 0x00};
	unsigned char tv_encoder_control_regs[] = {0x43, 0x01};

	for (i = 0; i < sizeof(tv_encoder_regs); i++) {
		ret = rk1000_i2c_recv(I2C_ADDR_TVE, i, buf);
		pr_info("---%x--\n", buf[0]);
		if (ret < 0) {
			pr_err("rk1000_tv_write_block err!\n");
			return ret;
		}
	}

	for (i = 0; i < sizeof(tv_encoder_control_regs); i++) {
		ret = rk1000_i2c_recv(I2C_ADDR_CTRL, i + 3, buf);
		pr_info("cntrl---%x--\n", buf[0]);
		if (ret < 0) {
			pr_err("rk1000_control_write_block err!\n");
			return ret;
		}
	}
	return 0;
}

static DEVICE_ATTR(rkcontrl, S_IRUGO, rk1000_show, NULL);


static int __init bootloader_cvbs_setup(char *str)
{
	static int ret;

	if (str) {
		pr_info("cvbs init tve.format is %s\n", str);
		ret = kstrtoint(str, 0, &cvbsmode);
	}
	return 0;
}
early_param("tve.format", bootloader_cvbs_setup);

#ifdef CONFIG_PM
static int rk1000_control_suspend(struct device *dev)
{
	int ret;

	DBG("rk1000_control_suspend\n");
	ret = rk1000_i2c_send(I2C_ADDR_CTRL, CTRL_CODEC, 0x22);
	DBG("ret=0x%x\n", ret);
	ret = rk1000_i2c_send(I2C_ADDR_CTRL, CTRL_TVE, 0x00);
	DBG("ret=0x%x\n", ret);
	ret = rk1000_i2c_send(I2C_ADDR_CTRL, CTRL_TVE, 0x07);
	DBG("ret=0x%x\n", ret);
	/* rk1000_reset_ctrl(0); */
	return 0;
}

static int rk1000_control_resume(struct device *dev)
{
	int ret;

	/* rk1000_reset_ctrl(1); */
	DBG("rk1000_control_resume\n");
	/* ADC power off */
	ret = rk1000_i2c_send(I2C_ADDR_CTRL, CTRL_ADC, 0x88);
	DBG("ret=0x%x\n", ret);
	#ifdef CONFIG_SND_SOC_RK1000
	ret = rk1000_i2c_send(I2C_ADDR_CTRL, CTRL_CODEC, 0x00);
	#else
	ret = rk1000_i2c_send(I2C_ADDR_CTRL, CTRL_CODEC, 0x0d);
	#endif
	DBG("ret=0x%x\n", ret);
	rk1000_i2c_send(I2C_ADDR_CTRL, CTRL_I2C, 0x22);
	DBG("ret=0x%x\n", ret);
	/* rk1000_codec_reg_set(); */
	return 0;
}
#endif



static int rk1000_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device_node *rk1000_np;
	enum of_gpio_flags flags;
	int ret;

	DBG("[%s] start\n", __func__);
	rk1000 = kmalloc(sizeof(*rk1000), GFP_KERNEL);
	if (!rk1000) {
		dev_err(&client->dev, ">> rk1000 core inf kmalloc fail!");
		return -ENOMEM;
	}

	memset(rk1000, 0, sizeof(struct rk1000));
	rk1000->client = client;
	rk1000->dev = &client->dev;
	rk1000_np = rk1000->dev->of_node;

	if (cvbsmode < 0) {
		/********Get reset pin***********/
		rk1000->io_reset.gpio = of_get_named_gpio_flags(rk1000_np,
								"gpio-reset",
								0, &flags);
		if (!gpio_is_valid(rk1000->io_reset.gpio)) {
			DBG("invalid rk1000->io_reset.gpio: %d\n",
			    rk1000->io_reset.gpio);
			ret = -1;
			goto err;
		}
		ret = gpio_request(rk1000->io_reset.gpio, "rk1000-reset-io");
		if (ret != 0) {
			DBG("gpio_request rk1000->io_reset.gpio invalid: %d\n",
			    rk1000->io_reset.gpio);
			goto err;
		}
		rk1000->io_reset.active = !(flags & OF_GPIO_ACTIVE_LOW);
		gpio_direction_output(rk1000->io_reset.gpio,
				      !(rk1000->io_reset.active));
		usleep_range(500, 1000);
		/********Get power pin***********/
		rk1000->io_power.gpio = of_get_named_gpio_flags(rk1000_np,
								"gpio-power",
								0, &flags);
		if (gpio_is_valid(rk1000->io_power.gpio)) {
			ret = gpio_request(rk1000->io_power.gpio,
					   "rk1000-power-io");
			if (ret != 0) {
				DBG("request gpio for power invalid: %d\n",
				    rk1000->io_power.gpio);
				goto err;
			}
			rk1000->io_power.active =
					!(flags & OF_GPIO_ACTIVE_LOW);
			gpio_direction_output(rk1000->io_power.gpio,
					      rk1000->io_power.active);
		}
		/********rk1000 reset***********/
		gpio_set_value(rk1000->io_reset.gpio,
			       rk1000->io_reset.active);
		usleep_range(5000, 10000);
		gpio_set_value(rk1000->io_reset.gpio,
			       !(rk1000->io_reset.active));
	}
	rk1000_i2c_send(I2C_ADDR_CTRL, CTRL_ADC, 0x88);
	#ifdef CONFIG_SND_SOC_RK1000
	rk1000_i2c_send(I2C_ADDR_CTRL, CTRL_CODEC, 0x00);
	#else
	rk1000_i2c_send(I2C_ADDR_CTRL, CTRL_CODEC, 0x0d);
	#endif
	rk1000_i2c_send(I2C_ADDR_CTRL, CTRL_I2C, 0x22);

	if (cvbsmode < 0)
		rk1000_i2c_send(I2C_ADDR_CTRL, CTRL_TVE, 0x00);

	device_create_file(&client->dev, &dev_attr_rkcontrl);
	DBG("rk1000 probe ok\n");
	return 0;
err:
	kfree(rk1000);
	rk1000 = NULL;
	return ret;
}

static int rk1000_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id rk1000_id[] = {
	{ "rk1000_control", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, rk1000_id);

static const struct dev_pm_ops rockchip_rk1000_pm_ops = {
	.suspend_late = rk1000_control_suspend,
	.resume_early = rk1000_control_resume,
};

static struct i2c_driver rk1000_driver = {
	.driver = {
		.name = "rk1000_control",
		#ifdef CONFIG_PM
		.pm	= &rockchip_rk1000_pm_ops,
		#endif
	},
	.probe = rk1000_probe,
	.remove = rk1000_remove,
	.id_table = rk1000_id,
};


static int __init rk1000_init(void)
{
	return i2c_add_driver(&rk1000_driver);
}

static void __exit rk1000_exit(void)
{
	i2c_del_driver(&rk1000_driver);
}

fs_initcall_sync(rk1000_init);
module_exit(rk1000_exit);


MODULE_DESCRIPTION("RK1000 control driver");
MODULE_AUTHOR("Rock-chips, <www.rock-chips.com>");
MODULE_LICENSE("GPL");
