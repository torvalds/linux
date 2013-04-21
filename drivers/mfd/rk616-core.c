
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/mfd/core.h>
#include <linux/slab.h>
#include <linux/mfd/rk616.h>
#include <linux/clk.h>
#include <mach/iomux.h>
#include <linux/err.h>
#include <linux/uaccess.h>
#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif

#ifndef MHZ
#define MHZ (1000*1000)
#endif

static struct mfd_cell rk616_devs[] = {
	{
		.name = "rk616-lvds",
		.id = 0,
	},
	{
		.name = "rk616-codec",
		.id = 1,
	},
	{
		.name = "rk616-hdmi",
		.id = 2,
	},
	{
		.name = "rk616-mipi",
		.id = 3,
	},
};

static int rk616_i2c_read_reg(struct mfd_rk616 *rk616, u16 reg,u32 *pval)
{
	struct i2c_client * client = rk616->client;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msgs[2];
	int ret;
	char reg_buf[2];
	
	memcpy(reg_buf, &reg, 2);

	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags;
	msgs[0].len = 2;
	msgs[0].buf = reg_buf;
	msgs[0].scl_rate = rk616->pdata->scl_rate;
	msgs[0].udelay = client->udelay;

	msgs[1].addr = client->addr;
	msgs[1].flags = client->flags | I2C_M_RD;
	msgs[1].len = 4;
	msgs[1].buf = (char *)pval;
	msgs[1].scl_rate = rk616->pdata->scl_rate;
	msgs[1].udelay = client->udelay;

	ret = i2c_transfer(adap, msgs, 2);

	
	return (ret == 2)? 4 : ret;

}

static int rk616_i2c_write_reg(struct mfd_rk616 *rk616, u16 reg,u32 *pval)
{
	struct i2c_client *client = rk616->client;
	struct i2c_adapter *adap = client->adapter;
	struct i2c_msg msg;
	int ret;
	char *tx_buf = (char *)kmalloc(6, GFP_KERNEL);
	if(!tx_buf)
		return -ENOMEM;
	
	memcpy(tx_buf, &reg, 2); 
	memcpy(tx_buf+2, (char *)pval, 4); 

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = 6;
	msg.buf = (char *)tx_buf;
	msg.scl_rate = rk616->pdata->scl_rate;
	msg.udelay = client->udelay;

	ret = i2c_transfer(adap, &msg, 1);
	kfree(tx_buf);
	
	
	return (ret == 1) ? 4 : ret;
}


#if defined(CONFIG_DEBUG_FS)
static int rk616_reg_show(struct seq_file *s, void *v)
{
	int i = 0;
	u32 val = 0;
	struct mfd_rk616 *rk616 = s->private;
	if(!rk616)
	{
		dev_err(rk616->dev,"no mfd rk616!\n");
		return 0;
	}

	for(i=0;i<= CRU_CFGMISC_CON;i+=4)
	{
		rk616->read_dev(rk616,i,&val);
		seq_printf(s,"0x%04x>>0x%08x\n",i,val);
	}

	return 0;
}

static ssize_t rk616_reg_write (struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{ 
	struct mfd_rk616 *rk616 = file->f_path.dentry->d_inode->i_private;
	u32 reg;
	u32 val;
	char kbuf[25];
	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;
	sscanf(kbuf, "%x%x", &reg,&val);
	dev_dbg(rk616->dev,"%s:reg:0x%04x val:0x%08x\n",__func__,reg,val);
	rk616->write_dev(rk616,reg,&val);
	return count;
}

static int rk616_reg_open(struct inode *inode, struct file *file)
{
	struct mfd_rk616 *rk616 = inode->i_private;
	return single_open(file,rk616_reg_show,rk616);
}

static const struct file_operations rk616_reg_fops = {
	.owner		= THIS_MODULE,
	.open		= rk616_reg_open,
	.read		= seq_read,
	.write          = rk616_reg_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif


static u32 rk616_clk_gcd(u32 numerator, u32 denominator)
{
	u32 a, b;

	if (!numerator || !denominator)
		return 0;

	if (numerator > denominator) {
		a = numerator;
		b = denominator;
	} else {
		a = denominator;
		b = numerator;
	}

	while (b != 0) {
		int r = b;
		b = a % b;
		a = r;
	}

	return a;
}


static int rk616_pll_clk_get_set(struct mfd_rk616 *rk616,unsigned long fin_hz,unsigned long fout_hz,
					u32 *refdiv, u32 *fbdiv, u32 *postdiv1, u32 *postdiv2, u32 *frac)
{
	// FIXME set postdiv1/2 always 1 	
	u32 gcd;
	u64 fin_64, frac_64;
	u32 f_frac;
	if(!fin_hz || !fout_hz || fout_hz == fin_hz)
		return -1;

	if(fin_hz / MHZ * MHZ == fin_hz && fout_hz /MHZ * MHZ == fout_hz)
	{
		fin_hz /= MHZ;
		fout_hz /= MHZ;
		gcd = rk616_clk_gcd(fin_hz, fout_hz);
		*refdiv = fin_hz / gcd;
		*fbdiv = fout_hz / gcd;
		*postdiv1 = 1;
		*postdiv2 = 1;

		*frac = 0;

		dev_info(rk616->dev,"fin=%lu,fout=%lu,gcd=%u,refdiv=%u,fbdiv=%u,postdiv1=%u,postdiv2=%u,frac=%u\n",
				fin_hz, fout_hz, gcd, *refdiv, *fbdiv, *postdiv1, *postdiv2, *frac);
	} 
	else 
	{
		dev_info(rk616->dev,"******frac div running, fin_hz=%lu, fout_hz=%lu, fin_mhz=%lu, fout_mhz=%lu\n",
				fin_hz, fout_hz, fin_hz / MHZ * MHZ, fout_hz / MHZ * MHZ);
		gcd = rk616_clk_gcd(fin_hz / MHZ, fout_hz / MHZ);
		*refdiv = fin_hz / MHZ / gcd;
		*fbdiv = fout_hz / MHZ / gcd;
		*postdiv1 = 1;
		*postdiv2 = 1;

		*frac = 0;

		f_frac = (fout_hz % MHZ);
		fin_64 = fin_hz;
		do_div(fin_64, (u64)*refdiv);
		frac_64 = (u64)f_frac << 24;
		do_div(frac_64, fin_64);
		*frac = (u32) frac_64;
		dev_dbg(rk616->dev,"frac_64=%llx, frac=%u\n", frac_64, *frac);
	}
	return 0;
}


static  int  rk616_pll_wait_lock(struct mfd_rk616 *rk616,int id)
{
	u32 delay = 50;
	u32 val ;
	int ret;
	int offset;

	if(id == 0)  //PLL0
	{
		offset = 0;
	}
	else // PLL1
	{
		offset = 0x0c;
	}
	while (delay >= 1) 
	{
		ret = rk616->write_dev(rk616,CRU_PLL0_CON1 + offset,&val);
		if (val&PLL0_LOCK)
		{
			break;
		}
		msleep(1);
		printk("0x%08x\n",val);
		delay--;
	}
	if (delay == 1)
	{
		dev_err(rk616->dev,"wait pll bit time out!\n");
		while(1);
	}

	return 0;
}

static int rk616_pll_cfg(struct mfd_rk616 *rk616,int id)
{
	u32 val = 0;
	int ret;
	int offset;
	if(id == 0)  //PLL0
	{
		offset = 0;
	}
	else // PLL1
	{
		offset = 0x0c;
	}
	
	val = PLL0_PWR_DN | (PLL0_PWR_DN << 16);
	ret = rk616->write_dev(rk616,CRU_PLL0_CON1 + offset,&val);

	val = PLL0_POSTDIV1(1) | PLL0_FBDIV(20) | PLL0_POSTDIV1_MASK | 
		PLL0_FBDIV_MASK | (PLL0_BYPASS << 16);
	ret = rk616->write_dev(rk616,CRU_PLL0_CON0 + offset,&val);

	val = PLL0_DIV_MODE | PLL0_POSTDIV2(1) | PLL0_REFDIV(20) |
		(PLL0_DIV_MODE << 16) | PLL0_POSTDIV1_MASK | PLL0_REFDIV_MASK;
	ret = rk616->write_dev(rk616,CRU_PLL0_CON1 + offset,&val);
	
	val = (PLL0_PWR_DN << 16);
	ret = rk616->write_dev(rk616,CRU_PLL0_CON1 + offset,&val);
	rk616_pll_wait_lock(rk616,id);

	return 0;	
	
}
/***********************************
default clk patch settiing:
CLKIN-------->CODEC
LCD_DCLK0--->PLL0--->Dither--->LVDS/MIPI
LCD_DCLK1--->PLL1--->HDMI
************************************/

static int rk616_clk_common_init(struct mfd_rk616 *rk616)
{
	u32 val = 0;
	int ret;

	val = PLL1_CLK_SEL(1) | PLL0_CLK_SEL(0) | LCD1_CLK_DIV(0) | LCD0_CLK_DIV(0) |
		PLL1_CLK_SEL_MASK | PLL0_CLK_SEL_MASK | LCD1_CLK_DIV_MASK | 
		LCD0_CLK_DIV_MASK; //pll1 clk from lcdc1_dclk,pll0 clk from lcdc0_dclk,mux_lcdx = lcdx_clk
	ret = rk616->write_dev(rk616,CRU_CLKSEL0_CON,&val);

	val = CODEC_MCLK_SEL(2) | CODEC_MCLK_SEL_MASK; //codec mclk from clkin
	ret = rk616->write_dev(rk616,CRU_CLKSEL1_CON,&val);
	
	val = 0; //codec mck = clkin
	ret = rk616->write_dev(rk616,CRU_CODEC_DIV,&val);

#if 0
	val = (PLL0_BYPASS) | (PLL0_BYPASS << 16);  //bypass pll0 
	ret = rk616->write_dev(rk616,CRU_PLL0_CON0,&val);
#else
	rk616_pll_cfg(rk616,0);
#endif

	val = (PLL1_BYPASS) | (PLL1_BYPASS << 16);
	ret = rk616->write_dev(rk616,CRU_PLL1_CON0,&val);

	return 0;
}
static int rk616_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	int ret;
	struct mfd_rk616 *rk616 = NULL;
	struct clk *iis_clk;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		ret = -ENODEV;
	}
	rk616 = kzalloc(sizeof(struct mfd_rk616), GFP_KERNEL);
	if (rk616 == NULL)
	{
		printk(KERN_ALERT "alloc for struct rk616 fail\n");
		ret = -ENOMEM;
	}
	
	rk616->dev = &client->dev;
	rk616->pdata = client->dev.platform_data;
	rk616->client = client;
	i2c_set_clientdata(client, rk616);
	dev_set_drvdata(rk616->dev,rk616);
	
	if(rk616->pdata->power_init)
		rk616->pdata->power_init();

#if defined(CONFIG_SND_RK29_SOC_I2S_8CH)        
	iis_clk = clk_get_sys("rk29_i2s.0", "i2s");
#elif defined(CONFIG_SND_RK29_SOC_I2S_2CH)
	iis_clk = clk_get_sys("rk29_i2s.1", "i2s");
#else
	iis_clk = clk_get_sys("rk29_i2s.2", "i2s");
#endif
	if (IS_ERR(iis_clk)) 
	{
		dev_err(&client->dev,"failed to get i2s clk\n");
		ret = PTR_ERR(iis_clk);
	}
	else
	{
		#if defined(CONFIG_ARCH_RK29)
		rk29_mux_api_set(GPIO2D0_I2S0CLK_MIIRXCLKIN_NAME, GPIO2H_I2S0_CLK);
		#else
		iomux_set(I2S0_CLK);
		#endif
		clk_enable(iis_clk);
		clk_set_rate(iis_clk, 11289600);
		clk_put(iis_clk);
	}
	rk616->read_dev = rk616_i2c_read_reg;
	rk616->write_dev = rk616_i2c_write_reg;
#if defined(CONFIG_DEBUG_FS)
	rk616->debugfs_dir = debugfs_create_dir("rk616", NULL);
	if (IS_ERR(rk616->debugfs_dir))
	{
		dev_err(rk616->dev,"failed to create debugfs dir for rk616!\n");
	}
	else
		debugfs_create_file("rk616-core", S_IRUSR,rk616->debugfs_dir,rk616,&rk616_reg_fops);
#endif
	rk616_clk_common_init(rk616);
	ret = mfd_add_devices(rk616->dev, -1,
				      rk616_devs, ARRAY_SIZE(rk616_devs),
				      NULL, rk616->irq_base);
	
	dev_info(&client->dev,"rk616 core probe success!\n");
	return 0;
}

static int __devexit rk616_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id id_table[] = {
	{"rk616", 0 },
	{ }
};

static struct i2c_driver rk616_i2c_driver  = {
	.driver = {
		.name  = "rk616",
		.owner = THIS_MODULE,
	},
	.probe		= &rk616_i2c_probe,
	.remove     	= &rk616_i2c_remove,
	.id_table	= id_table,
};


static int __init rk616_module_init(void)
{
	return i2c_add_driver(&rk616_i2c_driver);
}

static void __exit rk616_module_exit(void)
{
	i2c_del_driver(&rk616_i2c_driver);
}

subsys_initcall_sync(rk616_module_init);
module_exit(rk616_module_exit);


