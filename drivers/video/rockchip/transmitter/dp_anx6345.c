#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/i2c.h>

#if defined(CONFIG_HAS_EARLYSUSPEND)
#include<linux/earlysuspend.h>
#endif
#if defined(CONFIG_OF)
#include <linux/of_gpio.h>
#endif
#include "anx6345.h"
#include "dpcd_edid.h"
#if defined(CONFIG_DEBUG_FS)
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#endif


static struct edp_anx6345 *edp;
//#define BIST_MODE 0
static int i2c_master_reg8_send(const struct i2c_client *client,
		const char reg, const char *buf, int count, int scl_rate)
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

        ret = i2c_transfer(adap, &msg, 1);
        kfree(tx_buf);
        return (ret == 1) ? count : ret;

}

static int i2c_master_reg8_recv(const struct i2c_client *client,
		const char reg, char *buf, int count, int scl_rate)
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

        msgs[1].addr = client->addr;
        msgs[1].flags = client->flags | I2C_M_RD;
        msgs[1].len = count;
        msgs[1].buf = (char *)buf;
        msgs[1].scl_rate = scl_rate;

        ret = i2c_transfer(adap, msgs, 2);

        return (ret == 2)? count : ret;
}

static int anx6345_i2c_read_p0_reg(struct i2c_client *client, char reg, char *val)
{
	int ret;
	client->addr = DP_TX_PORT0_ADDR >> 1;
	ret = i2c_master_reg8_recv(client, reg, val, 1, ANX6345_SCL_RATE) > 0? 0: -EINVAL;
	if(ret < 0)
	{
		printk(KERN_ERR "%s>>err\n",__func__);
	}

	return ret;
}
static int  anx6345_i2c_write_p0_reg(struct i2c_client *client, char reg, char *val)
{
	int ret;
	client->addr = DP_TX_PORT0_ADDR >> 1;
	ret = i2c_master_reg8_send(client, reg, val, 1, ANX6345_SCL_RATE) > 0? 0: -EINVAL;
	if(ret < 0)
	{
		printk(KERN_ERR "%s>>err\n",__func__);
	}

	return ret;
}
static int anx6345_i2c_read_p1_reg(struct i2c_client *client, char reg, char *val)
{
	int ret;
	client->addr = HDMI_TX_PORT0_ADDR >> 1;
	ret = i2c_master_reg8_recv(client, reg, val, 1, ANX6345_SCL_RATE) > 0? 0: -EINVAL;
	if(ret < 0)
	{
		printk(KERN_ERR "%s>>err\n",__func__);
	}

	return ret;
}

static int anx6345_i2c_write_p1_reg(struct i2c_client *client, char reg, char *val)
{
	int ret;
	client->addr = HDMI_TX_PORT0_ADDR >> 1;
	ret = i2c_master_reg8_send(client, reg, val, 1, ANX6345_SCL_RATE) > 0? 0: -EINVAL;
	if(ret < 0)
	{
		printk(KERN_ERR "%s>>err\n",__func__);
	}

	return ret;
}

#if defined(CONFIG_DEBUG_FS)
static int edp_reg_show(struct seq_file *s, void *v)
{
	int i = 0;
	char val;
	struct edp_anx6345 *anx6345 = s->private;
	if(!anx6345)
	{
		printk(KERN_ERR "no edp device!\n");
		return 0;
	}
	
	seq_printf(s,"0x70:\n");
	for(i=0;i< MAX_REG;i++)
	{
		anx6345_i2c_read_p0_reg(anx6345->client, i , &val);
		seq_printf(s,"0x%02x>>0x%02x\n",i,val);
	}
	
	seq_printf(s,"\n0x72:\n");
	for(i=0;i< MAX_REG;i++)
	{
		anx6345_i2c_read_p1_reg(anx6345->client, i , &val);
		seq_printf(s,"0x%02x>>0x%02x\n",i,val);
	}
	return 0;
}

static int edp_reg_open(struct inode *inode, struct file *file)
{
	struct edp_anx6345 *anx6345 = inode->i_private;
	return single_open(file, edp_reg_show, anx6345);
}

static const struct file_operations edp_reg_fops = {
	.owner		= THIS_MODULE,
	.open		= edp_reg_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

//get chip ID. Make sure I2C is OK
static int get_dp_chip_id(struct i2c_client *client)
{
	char c1,c2;
	int id;
	anx6345_i2c_read_p1_reg(client,DEV_IDL_REG,&c1);
    	anx6345_i2c_read_p1_reg(client,DEV_IDH_REG,&c2);
	id = c2;
	return (id<<8)|c1;
}

#if defined(BIST_MODE)
static int anx980x_bist_mode(struct i2c_client *client)
{
	char val,i;
	u8 cnt=0;

	//Power on total and select DP mode
	val = 00;
        anx6345_i2c_write_p1_reg(client, DP_POWERD_CTRL_REG, &val);
	
	//HW reset
	val = DP_TX_RST_HW_RST;
	anx6345_i2c_write_p1_reg(client, DP_TX_RST_CTRL_REG, &val);
	msleep(10);
	val = 0x00;
	anx6345_i2c_write_p1_reg(client, DP_TX_RST_CTRL_REG, &val);


	anx6345_i2c_read_p1_reg(client, DP_POWERD_CTRL_REG, &val);
	val = 0x00;
        anx6345_i2c_write_p1_reg(client, DP_POWERD_CTRL_REG, &val);
	
	
	//get chip ID. Make sure I2C is OK
	anx6345_i2c_read_p1_reg(client, DP_TX_DEV_IDH_REG , &val);
	if (val==0x98)
		printk("Chip found\n");	

	//for clocl detect
	for(i=0;i<100;i++)
	{
		anx6345_i2c_read_p0_reg(client, DP_TX_SYS_CTRL1_REG, &val);
		anx6345_i2c_write_p0_reg(client, DP_TX_SYS_CTRL1_REG, &val);
		anx6345_i2c_read_p0_reg(client, DP_TX_SYS_CTRL1_REG, &val);
		if((val&DP_TX_SYS_CTRL1_DET_STA)!=0)
		{
			printk("clock is detected.\n");
			break;
		}

		msleep(10);
	}
       //check whther clock is stable
	for(i=0;i<50;i++)
	{
		anx6345_i2c_read_p0_reg(client, DP_TX_SYS_CTRL2_REG, &val);
		anx6345_i2c_write_p0_reg(client, DP_TX_SYS_CTRL2_REG, &val);
		anx6345_i2c_read_p0_reg(client, DP_TX_SYS_CTRL2_REG, &val);
		if((val&DP_TX_SYS_CTRL2_CHA_STA)==0)
		{
			printk("clock is stable.\n");
			break;
		}
		msleep(10);
	}

	//VESA range, 8bits BPC, RGB 
	val = 0x10;
	anx6345_i2c_write_p1_reg(client, DP_TX_VID_CTRL2_REG, &val);
	//RK_EDP chip analog setting
	val = 0x07;
	anx6345_i2c_write_p0_reg(client, DP_TX_PLL_CTRL_REG, &val); 
	val = 0x19;
	anx6345_i2c_write_p1_reg(client, DP_TX_PLL_FILTER_CTRL3, &val); 
	val = 0xd9;
	anx6345_i2c_write_p1_reg(client, DP_TX_PLL_CTRL3, &val); 
	
	//Select AC mode
	val = 0x40;
	anx6345_i2c_write_p1_reg(client, DP_TX_RST_CTRL2_REG, &val); 

	//RK_EDP chip analog setting
	val = 0xf0;
	anx6345_i2c_write_p1_reg(client, ANALOG_DEBUG_REG1, &val);
	val = 0x99;
	anx6345_i2c_write_p1_reg(client, ANALOG_DEBUG_REG3, &val);
	val = 0x7b;
	anx6345_i2c_write_p1_reg(client, DP_TX_PLL_FILTER_CTRL1, &val);
	val = 0x30;
	anx6345_i2c_write_p0_reg(client, DP_TX_LINK_DEBUG_REG,&val);
	val = 0x06;
	anx6345_i2c_write_p1_reg(client, DP_TX_PLL_FILTER_CTRL, &val);
	
	//force HPD
	val = 0x30;
	anx6345_i2c_write_p0_reg(client, DP_TX_SYS_CTRL3_REG, &val);
	//power on 4 lanes
	val = 0x00;
	anx6345_i2c_write_p0_reg(client, 0xc8, &val);
	//lanes setting
	anx6345_i2c_write_p0_reg(client, 0xa3, &val);
	anx6345_i2c_write_p0_reg(client, 0xa4, &val);
	anx6345_i2c_write_p0_reg(client, 0xa5,&val);
	anx6345_i2c_write_p0_reg(client, 0xa6, &val);

	//reset AUX CH
	val = 0x44;
	anx6345_i2c_write_p1_reg(client,  DP_TX_RST_CTRL2_REG, &val);
	val = 0x40;
	anx6345_i2c_write_p1_reg(client,  DP_TX_RST_CTRL2_REG, &val);

	//Select 1.62G
	val = 0x06;
	anx6345_i2c_write_p0_reg(client, DP_TX_LINK_BW_SET_REG, &val);
	//Select 4 lanes
	val = 0x04;
	anx6345_i2c_write_p0_reg(client, DP_TX_LANE_COUNT_SET_REG, &val);
	
	//strart link traing
	//DP_TX_LINK_TRAINING_CTRL_EN is self clear. If link training is OK, it will self cleared.
	#if 1
	val = DP_TX_LINK_TRAINING_CTRL_EN;
	anx6345_i2c_write_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG, &val);
	msleep(5);
	anx6345_i2c_read_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG, &val);
	while((val&0x01)&&(cnt++ < 10))
	{
		printk("Waiting...\n");
		msleep(5);
		anx6345_i2c_read_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG, &val);
	}

	if(cnt >= 10)
	{
		printk(KERN_INFO "HW LT fail\n");
	}
	else
	{
		printk(KERN_INFO "HW LT success ...cnt:%d\n",cnt);
	}
	#else
	DP_TX_HW_LT(client,0x0a,0x04); //2.7Gpbs 4lane
	#endif
	//DP_TX_Write_Reg(0x7a, 0x7c, 0x02);  	
	
	//Set bist format 2048x1536
	val = 0x2c;
	anx6345_i2c_write_p1_reg(client, DP_TX_TOTAL_LINEL_REG, &val);
	val = 0x06;
	anx6345_i2c_write_p1_reg(client, DP_TX_TOTAL_LINEH_REG, &val);

	val = 0x00;
	anx6345_i2c_write_p1_reg(client, DP_TX_ACT_LINEL_REG, &val);
	val = 0x06;
	anx6345_i2c_write_p1_reg(client, DP_TX_ACT_LINEH_REG,&val);
	val = 0x02;
	anx6345_i2c_write_p1_reg(client, DP_TX_VF_PORCH_REG, &val);
	val = 0x04;
	anx6345_i2c_write_p1_reg(client, DP_TX_VSYNC_CFG_REG,&val);
	val = 0x26;
	anx6345_i2c_write_p1_reg(client, DP_TX_VB_PORCH_REG, &val);
	val = 0x50;
	anx6345_i2c_write_p1_reg(client, DP_TX_TOTAL_PIXELL_REG, &val);
	val = 0x04;
	anx6345_i2c_write_p1_reg(client, DP_TX_TOTAL_PIXELH_REG, &val);
	val = 0x00;
	anx6345_i2c_write_p1_reg(client, DP_TX_ACT_PIXELL_REG, &val);
	val = 0x04;
	anx6345_i2c_write_p1_reg(client, DP_TX_ACT_PIXELH_REG, &val);

	val = 0x18;
	anx6345_i2c_write_p1_reg(client, DP_TX_HF_PORCHL_REG, &val);
	val = 0x00;
	anx6345_i2c_write_p1_reg(client, DP_TX_HF_PORCHH_REG, &val);

	val = 0x10;
	anx6345_i2c_write_p1_reg(client, DP_TX_HSYNC_CFGL_REG,&val);
	val = 0x00;
	anx6345_i2c_write_p1_reg(client, DP_TX_HSYNC_CFGH_REG,&val);
	val = 0x28;
	anx6345_i2c_write_p1_reg(client, DP_TX_HB_PORCHL_REG, &val);
	val = 0x00;
	anx6345_i2c_write_p1_reg(client, DP_TX_HB_PORCHH_REG, &val);
	val = 0x03;
	anx6345_i2c_write_p1_reg(client, DP_TX_VID_CTRL10_REG, &val);

	//enable BIST
	val = DP_TX_VID_CTRL4_BIST;
	anx6345_i2c_write_p1_reg(client, DP_TX_VID_CTRL4_REG, &val);
	//enable video input
	val = 0x8d;
	anx6345_i2c_write_p1_reg(client, DP_TX_VID_CTRL1_REG, &val);
	//force HPD and stream valid
	val = 0x33;
	anx6345_i2c_write_p0_reg(client, 0x82, &val);

	return 0;
}
#endif
static int anx980x_aux_rst(struct i2c_client *client)
{
	char val;
	anx6345_i2c_read_p1_reg(client, DP_TX_RST_CTRL2_REG, &val);
	val |= DP_TX_AUX_RST;
    	anx6345_i2c_write_p1_reg(client, DP_TX_RST_CTRL2_REG, &val);
	val &= ~DP_TX_AUX_RST;
    	anx6345_i2c_write_p1_reg(client, DP_TX_RST_CTRL2_REG, &val);
	return 0;
}


static int anx980x_wait_aux_finished(struct i2c_client *client)
{
	char val,cnt;
	cnt = 0;
	
	anx6345_i2c_read_p0_reg(client,DP_TX_AUX_CTRL_REG2, &val);
	while(val&0x01)
	{
		//delay_ms(20);
		cnt ++;
		if(cnt == 10)
		{
		   printk("aux break");
		    anx980x_aux_rst(client);
		    //cnt = 0;
		    break;
		}
		anx6345_i2c_read_p0_reg(client, DP_TX_AUX_CTRL_REG2, &val);
	}

	return 0;
}

static int anx980x_aux_dpcdread_bytes(struct i2c_client *client,unsigned long addr, char cCount,char* pBuf)
{
	char val,i;
	
	val = 0x80;
	anx6345_i2c_write_p0_reg(client, DP_TX_BUF_DATA_COUNT_REG, &val);

	//set read cmd and count
	val = (((char)(cCount-1) <<4)&(0xf0))|0x09;
	anx6345_i2c_write_p0_reg(client, DP_TX_AUX_CTRL_REG, &val);

	//set aux address15:0
	val = (char)addr&0xff;
	anx6345_i2c_write_p0_reg(client, DP_TX_AUX_ADDR_7_0_REG, &val);
	val = (char)((addr>>8)&0xff);
	anx6345_i2c_write_p0_reg(client, DP_TX_AUX_ADDR_15_8_REG, &val);

	//set address19:16 and enable aux
	anx6345_i2c_read_p0_reg(client, DP_TX_AUX_ADDR_19_16_REG, &val);
	val &=(0xf0)|(char)((addr>>16)&0xff);
	anx6345_i2c_write_p0_reg(client, DP_TX_AUX_ADDR_19_16_REG, &val);

	//Enable Aux
	anx6345_i2c_read_p0_reg(client, DP_TX_AUX_CTRL_REG2, &val);
	val |= 0x01;
	anx6345_i2c_write_p0_reg(client, DP_TX_AUX_CTRL_REG2, &val);

	//delay_ms(2);
	anx980x_wait_aux_finished(client);

	for(i =0;i<cCount;i++)
	{
		anx6345_i2c_read_p0_reg(client, DP_TX_BUF_DATA_0_REG+i, &val);

		//debug_printf("c = %.2x\n",(WORD)c);
		*(pBuf+i) = val;

		if(i >= MAX_BUF_CNT)
			return 1;
			//break;
	}

	return 0;
	

}

static int anx_video_map_config(struct i2c_client *client)
{
	char val = 0;
 	char i = 0;
	anx6345_i2c_write_p1_reg(client,  0x40, &val);
	anx6345_i2c_write_p1_reg(client,  0x41, &val);
	anx6345_i2c_write_p1_reg(client,  0x48, &val);
	anx6345_i2c_write_p1_reg(client,  0x49, &val);
	anx6345_i2c_write_p1_reg(client,  0x50, &val);
	anx6345_i2c_write_p1_reg(client,  0x51, &val);
	for(i=0; i<6; i++)
	{    
		val = i;
		anx6345_i2c_write_p1_reg(client,  0x42+i, &val);
	}

	for(i=0; i<6; i++)
	{    
		val = 6+i;
		anx6345_i2c_write_p1_reg(client,  0x4a+i, &val);
	}

	for(i=0; i<6; i++)
	{    
		val = 0x0c+i;
		anx6345_i2c_write_p1_reg(client,  0x52+i, &val);
	}

	return 0;
			
}
static int anx980x_eanble_video_input(struct i2c_client *client)
{
	char val;

	anx6345_i2c_read_p1_reg(client,  DP_TX_VID_CTRL1_REG, &val);
	val |= DP_TX_VID_CTRL1_VID_EN;
	anx6345_i2c_write_p1_reg(client,  DP_TX_VID_CTRL1_REG, &val);
	
	anx_video_map_config(client);
	
	return 0;
}

static int anx980x_init(struct i2c_client *client)
{
	char val = 0x00;
	char safe_mode = 0;
	char ByteBuf[2];
	char dp_tx_bw,dp_tx_lane_count;
	char cnt = 10;

#if defined(BIST_MODE)
	return anx980x_bist_mode(client);
#endif
	 //power on all block and select DisplayPort mode
	val |= DP_POWERD_AUDIO_REG;
	anx6345_i2c_write_p1_reg(client, DP_POWERD_CTRL_REG, &val );

	anx6345_i2c_read_p1_reg(client, DP_TX_VID_CTRL1_REG, &val);
	val &= ~DP_TX_VID_CTRL1_VID_EN;
 	anx6345_i2c_read_p1_reg(client, DP_TX_VID_CTRL1_REG, &val);

	//software reset    
	anx6345_i2c_read_p1_reg(client, DP_TX_RST_CTRL_REG, &val);
	val |= DP_TX_RST_SW_RST;
	anx6345_i2c_write_p1_reg(client, DP_TX_RST_CTRL_REG,&val);
	val &= ~DP_TX_RST_SW_RST;
	anx6345_i2c_write_p1_reg(client, DP_TX_RST_CTRL_REG, &val);

	
	val = 0x07;
	anx6345_i2c_write_p0_reg(client, DP_TX_PLL_CTRL_REG, &val);
	val = 0x50;
	anx6345_i2c_write_p0_reg(client, DP_TX_EXTRA_ADDR_REG, &val);
	
	//24bit SDR,negedge latch, and wait video stable
	val = 0x01;
	anx6345_i2c_write_p1_reg(client, DP_TX_VID_CTRL1_REG, &val);//72:08 for 9804 SDR, neg edge 05/04/09 extra pxl
	val = 0x19;
	anx6345_i2c_write_p1_reg(client, DP_TX_PLL_FILTER_CTRL3, &val); 
	val = 0xd9;
	anx6345_i2c_write_p1_reg(client, DP_TX_PLL_CTRL3, &val);

	//serdes ac mode.
	anx6345_i2c_read_p1_reg(client, DP_TX_RST_CTRL2_REG, &val);
	val |= DP_TX_AC_MODE;
	anx6345_i2c_write_p1_reg(client, DP_TX_RST_CTRL2_REG, &val);

	//set termination
	val = 0xf0;
	anx6345_i2c_write_p1_reg(client, ANALOG_DEBUG_REG1, &val);
	//set duty cycle
	val = 0x99;
	anx6345_i2c_write_p1_reg(client, ANALOG_DEBUG_REG3, &val);

	anx6345_i2c_read_p1_reg(client, DP_TX_PLL_FILTER_CTRL1, &val);
	val |= 0x2a; 
	anx6345_i2c_write_p1_reg(client, DP_TX_PLL_FILTER_CTRL1, &val);

	//anx6345_i2c_write_p0_reg(client, DP_TX_HDCP_CTRL, 0x01);
	val = 0x30;
	anx6345_i2c_write_p0_reg(client, DP_TX_LINK_DEBUG_REG,&val);

	//for DP link CTS 
	anx6345_i2c_read_p0_reg(client, DP_TX_GNS_CTRL_REG, &val);
	val |= 0x40;
	anx6345_i2c_write_p0_reg(client, DP_TX_GNS_CTRL_REG, &val);

	//power down  PLL filter
	val = 0x06;
	anx6345_i2c_write_p1_reg(client, DP_TX_PLL_FILTER_CTRL,&val);
	
	anx6345_i2c_write_p0_reg(client, DP_TX_TRAINING_LANE0_SET_REG, &val);
	anx6345_i2c_write_p0_reg(client, DP_TX_TRAINING_LANE1_SET_REG, &val);
	anx6345_i2c_write_p0_reg(client, DP_TX_TRAINING_LANE2_SET_REG, &val);
	anx6345_i2c_write_p0_reg(client, DP_TX_TRAINING_LANE3_SET_REG, &val);

	val = 0x06;
	anx6345_i2c_write_p0_reg(client, DP_TX_LINK_BW_SET_REG, &val);
	val = 0x04;
	anx6345_i2c_write_p0_reg(client, DP_TX_LANE_COUNT_SET_REG, &val);
	
	val = DP_TX_LINK_TRAINING_CTRL_EN;
	anx6345_i2c_write_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG,&val);
	msleep(2);
	anx6345_i2c_read_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG, &val);
	while((val & DP_TX_LINK_TRAINING_CTRL_EN)&&(cnt--))
	{
		anx6345_i2c_read_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG, &val);
		cnt--;
	}
	if(cnt < 0)
	{
		printk(KERN_INFO "HW LT fail\n");
	}
	else
		printk(KERN_INFO "HW LT Success!>>:times:%d\n",(11-cnt));
	//DP_TX_Config_Video(client);
	anx6345_i2c_write_p0_reg(client, DP_TX_SYS_CTRL1_REG, &val);
	anx6345_i2c_read_p0_reg(client,  DP_TX_SYS_CTRL1_REG, &val);
	if(!(val & DP_TX_SYS_CTRL1_DET_STA))
	{
		printk("No pclk\n");
		//return;  //mask by yxj
	}

	anx6345_i2c_read_p0_reg(client,  DP_TX_SYS_CTRL2_REG, &val);
	anx6345_i2c_write_p0_reg(client,  DP_TX_SYS_CTRL2_REG, &val);
	anx6345_i2c_read_p0_reg(client,  DP_TX_SYS_CTRL2_REG, &val);
	if(val & DP_TX_SYS_CTRL2_CHA_STA)
	{
		printk("pclk not stable!\n");
		//return; mask by yxj
	}

	anx980x_aux_dpcdread_bytes(client,(unsigned long)0x00001,2,ByteBuf);
	dp_tx_bw = ByteBuf[0];
	dp_tx_lane_count = ByteBuf[1] & 0x0f;
	printk("%s..lc:%d--bw:%d\n",__func__,dp_tx_lane_count,dp_tx_bw);
	
	if(!safe_mode)
	{
		//set Input BPC mode & color space
		anx6345_i2c_read_p1_reg(client,  DP_TX_VID_CTRL2_REG, &val);
		val &= 0x8c;
		val = val |((char)(0) << 4);  //8bits  ,rgb
		anx6345_i2c_write_p1_reg(client,  DP_TX_VID_CTRL2_REG, &val);
	}
	
	
	
	//enable video input
	 anx980x_eanble_video_input(client);

	return 0;
}

#if defined(BIST_MODE)
static int anx6345_bist_mode(struct i2c_client *client)
{
	struct edp_anx6345 *anx6345 = i2c_get_clientdata(client);
	struct rk_screen *screen = &anx6345->screen;
	u16 x_total ,y_total, x_act;
	char val = 0x00;
	//these register are for bist mode
	x_total = screen->mode.left_margin + screen->mode.right_margin +
			screen->mode.xres + screen->mode.hsync_len;
	y_total = screen->mode.upper_margin + screen->mode.lower_margin +
			screen->mode.yres + screen->mode.vsync_len;
	x_total >>= 1;
	x_act = screen->mode.xres >> 1;
	val = y_total & 0xff;
	anx6345_i2c_write_p1_reg(client,TOTAL_LINEL_REG,&val);
	val = (y_total >> 8);
	anx6345_i2c_write_p1_reg(client,TOTAL_LINEH_REG,&val);
	val = (screen->mode.yres & 0xff);
	anx6345_i2c_write_p1_reg(client,ACT_LINEL_REG,&val);
	val = (screen->mode.yres >> 8);
	anx6345_i2c_write_p1_reg(client,ACT_LINEH_REG,&val);
	val = screen->mode.lower_margin;
	anx6345_i2c_write_p1_reg(client,VF_PORCH_REG,&val);
	val = screen->mode.vsync_len;
	anx6345_i2c_write_p1_reg(client,VSYNC_CFG_REG,&val);
	val = screen->mode.upper_margin;
	anx6345_i2c_write_p1_reg(client,VB_PORCH_REG,&val);
	val = x_total & 0xff;
	anx6345_i2c_write_p1_reg(client,TOTAL_PIXELL_REG,&val);
	val = x_total >> 8;
	anx6345_i2c_write_p1_reg(client,TOTAL_PIXELH_REG,&val);
	val = (x_act & 0xff);
	anx6345_i2c_write_p1_reg(client,ACT_PIXELL_REG,&val);
	val = (x_act >> 8);
	anx6345_i2c_write_p1_reg(client,ACT_PIXELH_REG,&val);
	val = screen->mode.right_margin & 0xff;
	anx6345_i2c_write_p1_reg(client,HF_PORCHL_REG,&val);
	val = screen->mode.right_margin >> 8;
	anx6345_i2c_write_p1_reg(client,HF_PORCHH_REG,&val);
	val = screen->mode.hsync_len & 0xff;
	anx6345_i2c_write_p1_reg(client,HSYNC_CFGL_REG,&val);
	val = screen->mode.hsync_len >> 8;
	anx6345_i2c_write_p1_reg(client,HSYNC_CFGH_REG,&val);
	val = screen->mode.left_margin & 0xff;
	anx6345_i2c_write_p1_reg(client,HB_PORCHL_REG,&val);
	val = screen->mode.left_margin  >> 8;
	anx6345_i2c_write_p1_reg(client,HB_PORCHH_REG,&val);
	val = 0x13;
	anx6345_i2c_write_p1_reg(client,VID_CTRL10_REG,&val);


       //enable BIST. In normal mode, don't need to config this reg
	val = 0x08;
	anx6345_i2c_write_p1_reg(client, VID_CTRL4_REG, &val);
	printk("anx6345 enter bist mode\n");

	return 0;
}
#endif

int anx6345_start_aux_transaction(struct i2c_client  *client)
{
	char val;
	int retval = 0;
	int timeout_loop = 0;
	int aux_timeout = 0;
	

	anx6345_i2c_read_p0_reg(client, DP_AUX_CH_CTL_2, &val);
	val |= AUX_EN;
	anx6345_i2c_write_p0_reg(client, DP_AUX_CH_CTL_2, &val);

	anx6345_i2c_read_p0_reg(client, DP_AUX_CH_CTL_2, &val);
	while (val & AUX_EN) {
		aux_timeout++;
		if ((DP_TIMEOUT_LOOP_CNT * 10) < aux_timeout) {
			dev_err(&client->dev, "AUX CH enable timeout!\n");
			return -ETIMEDOUT;
		}
		anx6345_i2c_read_p0_reg(client, DP_AUX_CH_CTL_2, &val);
		udelay(100);
	}

	/* Is AUX CH command redply received? */
	anx6345_i2c_read_p1_reg(client, DP_INT_STA, &val);
	while (!(val & RPLY_RECEIV)) {
		timeout_loop++;
		if (DP_TIMEOUT_LOOP_CNT < timeout_loop) {
			dev_err(&client->dev, "AUX CH command redply failed!\n");
			return -ETIMEDOUT;
		}
		anx6345_i2c_read_p1_reg(client, DP_INT_STA, &val);
		udelay(10);
	}

	/* Clear interrupt source for AUX CH command redply */
	anx6345_i2c_write_p1_reg(client, DP_INT_STA, &val);

	/* Check AUX CH error access status */
	anx6345_i2c_read_p0_reg(client, AUX_CH_STA, &val);
	if ((val & AUX_STATUS_MASK) != 0) {
		dev_err(&client->dev, "AUX CH error happens: %d\n\n",
			val & AUX_STATUS_MASK);
		return -EREMOTEIO;
	}

	return retval;
}

int anx6345_dpcd_write_bytes(struct i2c_client *client,
				unsigned int val_addr,
				unsigned int count,
				unsigned char data[])
{
	char val;
	unsigned int start_offset;
	unsigned int cur_data_count;
	unsigned int cur_data_idx;
	int retval = 0;

	start_offset = 0;
	while (start_offset < count) {
		/* Buffer size of AUX CH is 16 * 4bytes */
		if ((count - start_offset) > 16)
			cur_data_count = 16;
		else
			cur_data_count = count - start_offset;

		val = BUF_CLR;
		anx6345_i2c_write_p0_reg(client, BUF_DATA_CTL, &val);
		
		val = AUX_ADDR_7_0(val_addr + start_offset);
		anx6345_i2c_write_p0_reg(client, DP_AUX_ADDR_7_0, &val);
		val = AUX_ADDR_15_8(val_addr + start_offset);
		anx6345_i2c_write_p0_reg(client, DP_AUX_ADDR_15_8, &val);
		val = AUX_ADDR_19_16(val_addr + start_offset);
		anx6345_i2c_write_p0_reg(client, DP_AUX_ADDR_19_16, &val);

		for (cur_data_idx = 0; cur_data_idx < cur_data_count;
		     cur_data_idx++) {
			val = data[start_offset + cur_data_idx];
			anx6345_i2c_write_p0_reg(client, BUF_DATA_0 + cur_data_idx, &val);
		}

		/*
		 * Set DisplayPort transaction and write
		 * If bit 3 is 1, DisplayPort transaction.
		 * If Bit 3 is 0, I2C transaction.
		 */
		val = AUX_LENGTH(cur_data_count) |
			AUX_TX_COMM_DP_TRANSACTION | AUX_TX_COMM_WRITE;
		anx6345_i2c_write_p0_reg(client, DP_AUX_CH_CTL_1, &val);

		/* Start AUX transaction */
		retval = anx6345_start_aux_transaction(client);
		if (retval == 0)
			break;
		else
			dev_dbg(&client->dev, "Aux Transaction fail!\n");
		

		start_offset += cur_data_count;
	}

	return retval;
}


int anx6345_dpcd_read_bytes(struct i2c_client *client,
				unsigned int val_addr,
				unsigned int count,
				unsigned char data[])
{
	char val;
	unsigned int start_offset;
	unsigned int cur_data_count;
	unsigned int cur_data_idx;
	int i;
	int retval = 0;

	start_offset = 0;
	while (start_offset < count) {
		/* Buffer size of AUX CH is 16 * 4bytes */
		if ((count - start_offset) > 16)
			cur_data_count = 16;
		else
			cur_data_count = count - start_offset;

		/* AUX CH Request Transaction process */
		for (i = 0; i < 10; i++) {
			/* Select DPCD device address */
			val = AUX_ADDR_7_0(val_addr + start_offset);
			anx6345_i2c_write_p0_reg(client, DP_AUX_ADDR_7_0, &val);
			val = AUX_ADDR_15_8(val_addr + start_offset);
			anx6345_i2c_write_p0_reg(client, DP_AUX_ADDR_15_8, &val);
			val = AUX_ADDR_19_16(val_addr + start_offset);
			anx6345_i2c_write_p0_reg(client, DP_AUX_ADDR_19_16, &val);

			/*
			 * Set DisplayPort transaction and read
			 * If bit 3 is 1, DisplayPort transaction.
			 * If Bit 3 is 0, I2C transaction.
			 */
			val = AUX_LENGTH(cur_data_count) |
				AUX_TX_COMM_DP_TRANSACTION | AUX_TX_COMM_READ;
			anx6345_i2c_write_p0_reg(client, DP_AUX_CH_CTL_1, &val);

			val = BUF_CLR;
			anx6345_i2c_write_p0_reg(client, BUF_DATA_CTL, &val);

			/* Start AUX transaction */
			retval = anx6345_start_aux_transaction(client);
			if (retval == 0)
				break;
			else
				dev_dbg(&client->dev, "Aux Transaction fail!\n");
		}

		for (cur_data_idx = 0; cur_data_idx < cur_data_count;
		    cur_data_idx++) {
			anx6345_i2c_read_p0_reg(client, BUF_DATA_0 + cur_data_idx, &val);
			data[start_offset + cur_data_idx] = val;
			dev_dbg(&client->dev, "0x%05x :0x%02x\n",cur_data_idx, val);
		}

		start_offset += cur_data_count;
	}

	return retval;
}


int anx6345_select_i2c_device(struct i2c_client *client,
				unsigned int device_addr,
				char val_addr)
{
	char val;
	int retval;

	/* Set normal AUX CH command */
	anx6345_i2c_read_p0_reg(client, DP_AUX_CH_CTL_2, &val);
	val &= ~ADDR_ONLY;
	anx6345_i2c_write_p0_reg(client, DP_AUX_CH_CTL_2, &val);
	/* Set EDID device address */
	val = device_addr;
	anx6345_i2c_write_p0_reg(client, DP_AUX_ADDR_7_0, &val);
	val = 0;
	anx6345_i2c_write_p0_reg(client, DP_AUX_ADDR_15_8, &val);
	anx6345_i2c_write_p0_reg(client, DP_AUX_ADDR_19_16, &val);

	/* Set offset from base address of EDID device */
	anx6345_i2c_write_p0_reg(client, BUF_DATA_0, &val_addr);

	/*
	 * Set I2C transaction and write address
	 * If bit 3 is 1, DisplayPort transaction.
	 * If Bit 3 is 0, I2C transaction.
	 */
	val = AUX_TX_COMM_I2C_TRANSACTION | AUX_TX_COMM_MOT |
		AUX_TX_COMM_WRITE;
	anx6345_i2c_write_p0_reg(client, DP_AUX_CH_CTL_1, &val);

	/* Start AUX transaction */
	retval = anx6345_start_aux_transaction(client);
	if (retval != 0)
		dev_dbg(&client->dev, "Aux Transaction fail!\n");

	return retval;
}

int anx6345_edid_read_bytes(struct i2c_client *client,
				unsigned int device_addr,
				unsigned int val_addr,
				unsigned char count,
				unsigned char edid[])
{
	char val;
	unsigned int i;
	unsigned int start_offset;
	unsigned int cur_data_idx;
	unsigned int cur_data_cnt;
	unsigned int defer = 0;
	int retval = 0;

	for (i = 0; i < count; i += 16) {
		start_offset = i;
		if ((count - start_offset) > 16)
				cur_data_cnt = 16;
			else
				cur_data_cnt = count - start_offset;
		/*
		 * If Rx sends defer, Tx sends only reads
		 * request without sending addres
		 */
		if (!defer)
			retval = anx6345_select_i2c_device(client,
					device_addr, val_addr + i);
		else
			defer = 0;

		/*
		 * Set I2C transaction and write data
		 * If bit 3 is 1, DisplayPort transaction.
		 * If Bit 3 is 0, I2C transaction.
		 */
		val = AUX_LENGTH(cur_data_cnt) | AUX_TX_COMM_I2C_TRANSACTION |
			AUX_TX_COMM_READ;
		anx6345_i2c_write_p0_reg(client, DP_AUX_CH_CTL_1, &val);

		/* Start AUX transaction */
		retval = anx6345_start_aux_transaction(client);
		if (retval < 0)
			dev_dbg(&client->dev, "Aux Transaction fail!\n");

		/* Check if Rx sends defer */
		anx6345_i2c_read_p0_reg(client, DP_AUX_RX_COMM, &val);
		if (val == AUX_RX_COMM_AUX_DEFER ||
			val == AUX_RX_COMM_I2C_DEFER) {
			dev_err(&client->dev, "Defer: %d\n\n", val);
			defer = 1;
		}
		

		for (cur_data_idx = 0; cur_data_idx < cur_data_cnt; cur_data_idx++) {
			anx6345_i2c_read_p0_reg(client, BUF_DATA_0 + cur_data_idx, &val);
			edid[i + cur_data_idx] = val;
			dev_dbg(&client->dev, "0x%02x : 0x%02x\n", i + cur_data_idx, val);
		}
	}

	return retval;
}

static int anx6345_read_edid(struct i2c_client *client)
{
	unsigned char edid[EDID_LENGTH * 2];
	unsigned char extend_block = 0;
	unsigned char sum;
	unsigned char test_vector;
	int retval;
	char addr;
	struct edp_anx6345 *anx6345 = i2c_get_clientdata(client);
	

	/* Read Extension Flag, Number of 128-byte EDID extension blocks */
	retval = anx6345_edid_read_bytes(client, EDID_ADDR,
				EDID_EXTENSION_FLAG,1,&extend_block);
	if (retval < 0) {
		dev_err(&client->dev, "EDID extension flag failed!\n");
		return -EIO;
	}

	if (extend_block > 0) {
		dev_dbg(&client->dev, "EDID data includes a single extension!\n");

		/* Read EDID data */
		retval = anx6345_edid_read_bytes(client, EDID_ADDR,
						EDID_HEADER,
						EDID_LENGTH,
						&edid[EDID_HEADER]);
		if (retval != 0) {
			dev_err(&client->dev, "EDID Read failed!\n");
			return -EIO;
		}
		sum = edp_calc_edid_check_sum(edid);
		if (sum != 0) {
			dev_warn(&client->dev, "EDID bad checksum!\n");
			return 0;
		}

		/* Read additional EDID data */
		retval = anx6345_edid_read_bytes(client, EDID_ADDR, EDID_LENGTH,
							EDID_LENGTH, &edid[EDID_LENGTH]);
		if (retval != 0) {
			dev_err(&client->dev, "EDID Read failed!\n");
			return -EIO;
		}
		sum = edp_calc_edid_check_sum(&edid[EDID_LENGTH]);
		if (sum != 0) {
			dev_warn(&client->dev, "EDID bad checksum!\n");
			return 0;
		}

		retval = anx6345_dpcd_read_bytes(client, DPCD_TEST_REQUEST,
						1, &test_vector);
		if (retval < 0) {
			dev_err(&client->dev, "DPCD EDID Read failed!\n");
			return retval;
		}

		if (test_vector & DPCD_TEST_EDID_READ) {
			retval = anx6345_dpcd_write_bytes(client,
					DPCD_TEST_EDID_CHECKSUM,1,
					&edid[EDID_LENGTH + EDID_CHECKSUM]);
			if (retval < 0) {
				dev_err(&client->dev, "DPCD EDID Write failed!\n");
				return retval;
			}

			addr = DPCD_TEST_EDID_CHECKSUM_WRITE;
			retval = anx6345_dpcd_write_bytes(client,
					DPCD_TEST_RESPONSE, 1, &addr);
			if (retval < 0) {
				dev_err(&client->dev, "DPCD EDID checksum failed!\n");
				return retval;
			}
		}
	} else {
		dev_info(&client->dev, "EDID data does not include any extensions.\n");

		/* Read EDID data */
		retval = anx6345_edid_read_bytes(client, EDID_ADDR, EDID_HEADER,
				          EDID_LENGTH, &edid[EDID_HEADER]);
		if (retval != 0) {
			dev_err(&client->dev, "EDID Read failed!\n");
			return -EIO;
		}
		
		sum = edp_calc_edid_check_sum(edid);
		if (sum != 0) {
			dev_warn(&client->dev, "EDID bad checksum!\n");
			return 0;
		}

		retval = anx6345_dpcd_read_bytes(client, DPCD_TEST_REQUEST,
						1,&test_vector);
		if (retval < 0) {
			dev_err(&client->dev, "DPCD EDID Read failed!\n");
			return retval;
		}

		if (test_vector & DPCD_TEST_EDID_READ) {
			retval = anx6345_dpcd_write_bytes(client,
					DPCD_TEST_EDID_CHECKSUM, 1,
					&edid[EDID_CHECKSUM]);
			if (retval < 0) {
				dev_err(&client->dev, "DPCD EDID Write failed!\n");
				return retval;
			}
			addr = DPCD_TEST_EDID_CHECKSUM_WRITE;
			retval = anx6345_dpcd_write_bytes(client, DPCD_TEST_RESPONSE,
					1, &addr);
			if (retval < 0) {
				dev_err(&client->dev, "DPCD EDID checksum failed!\n");
				return retval;
			}
		}
	}
	fb_edid_to_monspecs(edid, &anx6345->specs);
	dev_info(&client->dev, "EDID Read success!\n");
	return 0;
}

static int anx6345_init(struct i2c_client *client)
{
	char val = 0x00;
	char i = 0;
	char lc,bw;
	char cnt = 50;
	u8 buf[12];
	
	val = 0x30;	
	anx6345_i2c_write_p1_reg(client,SP_POWERD_CTRL_REG,&val);

	//clock detect	
	for(i=0;i<50;i++)
	{
		
		anx6345_i2c_read_p0_reg(client, SYS_CTRL1_REG, &val);
		anx6345_i2c_write_p0_reg(client, SYS_CTRL1_REG, &val);
		anx6345_i2c_read_p0_reg(client, SYS_CTRL1_REG, &val);
		if((val&SYS_CTRL1_DET_STA)!=0)
		{
			break;
		}

		mdelay(10);
	}
	if(i>49)
		printk("no clock detected by anx6345\n");
	
	//check whether clock is stable
	for(i=0;i<50;i++)
	{
		anx6345_i2c_read_p0_reg(client, SYS_CTRL2_REG, &val);
		anx6345_i2c_write_p0_reg(client,SYS_CTRL2_REG, &val);
		anx6345_i2c_read_p0_reg(client, SYS_CTRL2_REG, &val);
		if((val&SYS_CTRL2_CHA_STA)==0)
		{
			break;
		}
		mdelay(10);
	}
	if(i>49)
		printk("clk is not stable\n");

	anx6345_dpcd_read_bytes(client, DPCD_REV, 12, buf);
	anx6345_read_edid(client);
	
    	//VESA range, 6bits BPC, RGB 
	val = 0x00;
	anx6345_i2c_write_p1_reg(client, VID_CTRL2_REG, &val);
	
	//ANX6345 chip pll setting 
	val = 0x00;
	anx6345_i2c_write_p0_reg(client, PLL_CTRL_REG, &val);                  //UPDATE: FROM 0X07 TO 0X00
	
	
	//ANX chip analog setting
	val = 0x70;
	anx6345_i2c_write_p1_reg(client, ANALOG_DEBUG_REG1, &val);               //UPDATE: FROM 0XF0 TO 0X70
	val = 0x30;
	anx6345_i2c_write_p0_reg(client, LINK_DEBUG_REG, &val);

	//force HPD
	//anx6345_i2c_write_p0_reg(client, SYS_CTRL3_REG, &val);

	
	//reset AUX
	anx6345_i2c_read_p1_reg(client, RST_CTRL2_REG, &val);
	val |= AUX_RST;
	anx6345_i2c_write_p1_reg(client, RST_CTRL2_REG, &val);
	val &= ~AUX_RST;
	anx6345_i2c_write_p1_reg(client, RST_CTRL2_REG, &val);
	
	//Select 2.7G
	val = 0x0a;
	anx6345_i2c_write_p0_reg(client, LINK_BW_SET_REG, &val);  
	//Select 2 lanes
	val = 0x02;
	anx6345_i2c_write_p0_reg(client,LANE_COUNT_SET_REG,&val);
	
	val = LINK_TRAINING_CTRL_EN;  
	anx6345_i2c_write_p0_reg(client, LINK_TRAINING_CTRL_REG, &val);
	mdelay(5);
	anx6345_i2c_read_p0_reg(client, LINK_TRAINING_CTRL_REG, &val);
	while((val&0x80)&&(cnt))                                                                                //UPDATE: FROM 0X01 TO 0X80
	{
		printk("Waiting...\n");
		mdelay(5);
		anx6345_i2c_read_p0_reg(client,LINK_TRAINING_CTRL_REG,&val);
		cnt--;
	} 
	if(cnt <= 0)
	{
		printk(KERN_INFO "HW LT fail\n");
	}
	else
		printk("HW LT Success>>:times:%d\n",(51-cnt));


	
	//enable video input, set DDR mode, the input DCLK should be 102.5MHz; 
	//In normal mode, set this reg to 0x81, SDR mode, the input DCLK should be 205MHz

#if defined(BIST_MODE)
	anx6345_bist_mode(client);
	val = 0x8f;
#else
	val = 0x81;
#endif
	anx6345_i2c_write_p1_reg(client,VID_CTRL1_REG,&val);

	anx_video_map_config(client);
	//force HPD and stream valid
	val = 0x33;
	anx6345_i2c_write_p0_reg(client,SYS_CTRL3_REG,&val);

	anx6345_i2c_read_p0_reg(client,LANE_COUNT_SET_REG, &lc);
	anx6345_i2c_read_p0_reg(client,LINK_BW_SET_REG, &bw);
	printk("%s..lc:%d--bw:%d\n",__func__,lc,bw);

	return 0;
}


static int  anx6345_disable(void)
{
	struct edp_anx6345 *anx6345 = edp;

	if (!anx6345->pdata->pwron)
		return 0;
	gpio_set_value(anx6345->pdata->dvdd33_en_pin,!anx6345->pdata->dvdd33_en_val);
	gpio_set_value(anx6345->pdata->dvdd18_en_pin,!anx6345->pdata->dvdd18_en_val);
	anx6345->pdata->pwron = false;

	return 0;
	
}
	

static int anx6345_enable(void)
{
	struct edp_anx6345 *anx6345 = edp;

	if (!anx6345->pdata->pwron) {
		gpio_set_value(anx6345->pdata->dvdd33_en_pin,anx6345->pdata->dvdd33_en_val);
		msleep(5);
		gpio_set_value(anx6345->pdata->dvdd18_en_pin,anx6345->pdata->dvdd18_en_val);
		gpio_set_value(anx6345->pdata->edp_rst_pin,0);
		msleep(50);
		gpio_set_value(anx6345->pdata->edp_rst_pin,1);
		anx6345->pdata->pwron = true;
	}
	anx6345->edp_anx_init(anx6345->client);
	return 0;
}
#ifdef CONFIG_HAS_EARLYSUSPEND
static void anx6345_early_suspend(struct early_suspend *h)
{
	anx6345_disable();
}

static void anx6345_late_resume(struct early_suspend *h)
{
	anx6345_enable();
}
#endif				

#if defined(CONFIG_OF)

static int anx6345_power_ctl(struct anx6345_platform_data  *pdata)
{
       int ret;
       ret = gpio_request(pdata->dvdd33_en_pin, "dvdd33_en_pin");
       if (ret != 0) {
	       gpio_free(pdata->dvdd33_en_pin);
	       printk(KERN_ERR "request dvdd33 en pin fail!\n");
	       return -1;
       } else {
	       gpio_direction_output(pdata->dvdd33_en_pin, pdata->dvdd33_en_val);
       }
       mdelay(5);

       ret = gpio_request(pdata->dvdd18_en_pin, "dvdd18_en_pin");
       if (ret != 0) {
	       gpio_free(pdata->dvdd18_en_pin);
	       printk(KERN_ERR "request dvdd18 en pin fail!\n");
	       return -1;
       } else {
	       gpio_direction_output(pdata->dvdd18_en_pin, pdata->dvdd18_en_pin);
       }

       ret = gpio_request(pdata->edp_rst_pin, "edp_rst_pin");
       if (ret != 0) {
	       gpio_free(pdata->edp_rst_pin);
	       printk(KERN_ERR "request rst pin fail!\n");
	       return -1;
       } else {
	       gpio_direction_output(pdata->edp_rst_pin, 0);
	       msleep(50);
	       gpio_direction_output(pdata->edp_rst_pin, 1);
       }
       pdata->pwron = true;
       return 0;

}


struct rk_fb_trsm_ops  trsm_edp_ops = {
	.enable = anx6345_enable,
	.disable = anx6345_disable,
	
};
static void anx6345_parse_dt(struct edp_anx6345 *anx6345)
{
	struct device_node *np = anx6345->client->dev.of_node;
	struct anx6345_platform_data *pdata;
	enum of_gpio_flags dvdd33_flags,dvdd18_flags,rst_flags;
	pdata = devm_kzalloc(&anx6345->client->dev,
			sizeof(struct anx6345_platform_data ), GFP_KERNEL);
	if (!pdata) {
		dev_err(&anx6345->client->dev, 
			"failed to allocate platform data\n");
		return ;
	}
	pdata->dvdd33_en_pin = of_get_named_gpio_flags(np, "dvdd33-gpio", 0, &dvdd33_flags);
	pdata->dvdd18_en_pin = of_get_named_gpio_flags(np, "dvdd18-gpio", 0, &dvdd18_flags);
	pdata->edp_rst_pin = of_get_named_gpio_flags(np, "reset-gpio", 0, &rst_flags);
	pdata->dvdd33_en_val = (dvdd33_flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	pdata->dvdd18_en_val = (dvdd18_flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	pdata->power_ctl = anx6345_power_ctl;
	anx6345->pdata = pdata;
	
}
#else
static void anx6345_parse_dt(struct edp_anx6345 * anx6345)
{
	
}
#endif
static int anx6345_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	struct edp_anx6345 *anx6345;
	int chip_id;


	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		return -ENODEV;
	}
	anx6345 = devm_kzalloc(&client->dev, sizeof(struct edp_anx6345),
				GFP_KERNEL);
	if (unlikely(!anx6345)) {
		dev_err(&client->dev, "alloc for struct anx6345 fail\n");
		return -ENOMEM;
	}

	anx6345->client = client;
	anx6345->pdata = dev_get_platdata(&client->dev);
	if (!anx6345->pdata) {
		anx6345_parse_dt(anx6345);
	}
	i2c_set_clientdata(client,anx6345);
	rk_fb_get_prmry_screen(&anx6345->screen);
	if (anx6345->screen.type != SCREEN_EDP){
		dev_err(&client->dev, "screen is not edp!\n");
		return -EINVAL;
	}
	if(anx6345->pdata->power_ctl)
		anx6345->pdata->power_ctl(anx6345->pdata);

#if defined(CONFIG_DEBUG_FS)
	anx6345->debugfs_dir = debugfs_create_dir("edp", NULL);
	if (IS_ERR(anx6345->debugfs_dir)) {
		dev_err(&client->dev, "failed to create debugfs dir for edp!\n");
	}
	else
		debugfs_create_file("edp-reg", S_IRUSR,anx6345->debugfs_dir,anx6345,&edp_reg_fops);
#endif

#ifdef CONFIG_HAS_EARLYSUSPEND
	anx6345->early_suspend.suspend = anx6345_early_suspend;
	anx6345->early_suspend.resume = anx6345_late_resume;
    	anx6345->early_suspend.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING;
	register_early_suspend(&anx6345->early_suspend);
#endif
	chip_id = get_dp_chip_id(client);
	if(chip_id == 0x9805)
		anx6345->edp_anx_init = anx980x_init;
	else
		anx6345->edp_anx_init = anx6345_init;
	edp = anx6345;

	rk_fb_trsm_ops_register(&trsm_edp_ops, SCREEN_EDP);

	dev_info(&client->dev, "edp anx%x probe ok \n", get_dp_chip_id(client));
	
	return 0;
}

static int  anx6345_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id id_table[] = {
	{"anx6345", 0 },
	{ }
};

#if defined(CONFIG_OF)
static struct of_device_id anx6345_dt_ids[] = {
	{ .compatible = "analogix, anx6345" },
	{ }
};
#endif

static struct i2c_driver anx6345_i2c_driver  = {
	.driver = {
		.name  = "anx6345",
		.owner = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(anx6345_dt_ids),
#endif
	},
	.probe		= &anx6345_i2c_probe,
	.remove     	= &anx6345_i2c_remove,
	.id_table	= id_table,
};


static int __init anx6345_module_init(void)
{
	return i2c_add_driver(&anx6345_i2c_driver);
}

static void __exit anx6345_module_exit(void)
{
	i2c_del_driver(&anx6345_i2c_driver);
}

fs_initcall(anx6345_module_init);
module_exit(anx6345_module_exit);

