#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/anx6345.h>
#include <linux/debugfs.h>

//#define BIST_MODE 0

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


//get chip ID. Make sure I2C is OK
static int get_dp_chip_id(struct i2c_client *client)
{
	char c1,c2;
	int id;
	anx6345_i2c_read_p1_reg(client,SP_TX_DEV_IDL_REG,&c1);
    	anx6345_i2c_read_p1_reg(client,SP_TX_DEV_IDH_REG,&c2);
	id = c2;
	return (id<<8)|c1;
}


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

static int anx6345_bist_mode(struct i2c_client *client)
{
	char val = 0x00;
	//these register are for bist mode
	val = 0x2c;
	anx6345_i2c_write_p1_reg(client,SP_TX_TOTAL_LINEL_REG,&val);
	val = 0x06;
	anx6345_i2c_write_p1_reg(client,SP_TX_TOTAL_LINEH_REG,&val);
	val = 0x00;
	anx6345_i2c_write_p1_reg(client,SP_TX_ACT_LINEL_REG,&val);
	val = 0x06;
	anx6345_i2c_write_p1_reg(client,SP_TX_ACT_LINEH_REG,&val);
	val = 0x02;
	anx6345_i2c_write_p1_reg(client,SP_TX_VF_PORCH_REG,&val);
	val = 0x04;
	anx6345_i2c_write_p1_reg(client,SP_TX_VSYNC_CFG_REG,&val);
	val = 0x26;
	anx6345_i2c_write_p1_reg(client,SP_TX_VB_PORCH_REG,&val);
	val = 0x50;
	anx6345_i2c_write_p1_reg(client,SP_TX_TOTAL_PIXELL_REG,&val);
	val = 0x04;
	anx6345_i2c_write_p1_reg(client,SP_TX_TOTAL_PIXELH_REG,&val);
	val = 0x00;
	anx6345_i2c_write_p1_reg(client,SP_TX_ACT_PIXELL_REG,&val);
	val = 0x04;
	anx6345_i2c_write_p1_reg(client,SP_TX_ACT_PIXELH_REG,&val);
	val = 0x18;
	anx6345_i2c_write_p1_reg(client,SP_TX_HF_PORCHL_REG,&val);
	val = 0x00;
	anx6345_i2c_write_p1_reg(client,SP_TX_HF_PORCHH_REG,&val);
	val = 0x10;
	anx6345_i2c_write_p1_reg(client,SP_TX_HSYNC_CFGL_REG,&val);
	val = 0x00;
	anx6345_i2c_write_p1_reg(client,SP_TX_HSYNC_CFGH_REG,&val);
	val = 0x28;
	anx6345_i2c_write_p1_reg(client,SP_TX_HB_PORCHL_REG,&val);
	val = 0x13;
	anx6345_i2c_write_p1_reg(client,SP_TX_VID_CTRL10_REG,&val);


       //enable BIST. In normal mode, don't need to config this reg
	val = 0x08;
	anx6345_i2c_write_p1_reg(client, 0x0b, &val);
	printk("anx6345 enter bist mode\n");

	return 0;
}
static int anx6345_init(struct i2c_client *client)
{
	char val = 0x00;
	char i = 0;
	char lc,bw;
	char cnt = 50;

	val = 0x30;	
	anx6345_i2c_write_p1_reg(client,SP_POWERD_CTRL_REG,&val);

	//clock detect	
	for(i=0;i<50;i++)
	{
		
		anx6345_i2c_read_p0_reg(client, SP_TX_SYS_CTRL1_REG, &val);
		anx6345_i2c_write_p0_reg(client, SP_TX_SYS_CTRL1_REG, &val);
		anx6345_i2c_read_p0_reg(client, SP_TX_SYS_CTRL1_REG, &val);
		if((val&SP_TX_SYS_CTRL1_DET_STA)!=0)
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
		anx6345_i2c_read_p0_reg(client, SP_TX_SYS_CTRL2_REG, &val);
		anx6345_i2c_write_p0_reg(client,SP_TX_SYS_CTRL2_REG, &val);
		anx6345_i2c_read_p0_reg(client, SP_TX_SYS_CTRL2_REG, &val);
		if((val&SP_TX_SYS_CTRL2_CHA_STA)==0)
		{
			break;
		}
		mdelay(10);
	}
	if(i>49)
		printk("clk is not stable\n");
	
    	//VESA range, 6bits BPC, RGB 
	val = 0x00;
	anx6345_i2c_write_p1_reg(client, SP_TX_VID_CTRL2_REG, &val);
	
	//ANX6345 chip pll setting 
	val = 0x00;
	anx6345_i2c_write_p0_reg(client, SP_TX_PLL_CTRL_REG, &val);                  //UPDATE: FROM 0X07 TO 0X00
	
	
	//ANX chip analog setting
	val = 0x70;
	anx6345_i2c_write_p1_reg(client, ANALOG_DEBUG_REG1, &val);               //UPDATE: FROM 0XF0 TO 0X70
	val = 0x30;
	anx6345_i2c_write_p0_reg(client, SP_TX_LINK_DEBUG_REG, &val);

	//force HPD
	//anx6345_i2c_write_p0_reg(client, SP_TX_SYS_CTRL3_REG, &val);

	
	//reset AUX
	anx6345_i2c_read_p1_reg(client, SP_TX_RST_CTRL2_REG, &val);
	val |= SP_TX_AUX_RST;
	anx6345_i2c_write_p1_reg(client, SP_TX_RST_CTRL2_REG, &val);
	val &= ~SP_TX_AUX_RST;
	anx6345_i2c_write_p1_reg(client, SP_TX_RST_CTRL2_REG, &val);
	
	//Select 2.7G
	val = 0x0a;
	anx6345_i2c_write_p0_reg(client, SP_TX_LINK_BW_SET_REG, &val);  
	//Select 2 lanes
	val = 0x02;
	anx6345_i2c_write_p0_reg(client,SP_TX_LANE_COUNT_SET_REG,&val);
	
	val = SP_TX_LINK_TRAINING_CTRL_EN;  
	anx6345_i2c_write_p0_reg(client, SP_TX_LINK_TRAINING_CTRL_REG, &val);
	mdelay(5);
	anx6345_i2c_read_p0_reg(client, SP_TX_LINK_TRAINING_CTRL_REG, &val);
	while((val&0x80)&&(cnt))                                                                                //UPDATE: FROM 0X01 TO 0X80
	{
		printk("Waiting...\n");
		mdelay(5);
		anx6345_i2c_read_p0_reg(client,SP_TX_LINK_TRAINING_CTRL_REG,&val);
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
	anx6345_i2c_write_p1_reg(client,SP_TX_VID_CTRL1_REG,&val);

	anx_video_map_config(client);
	//force HPD and stream valid
	val = 0x33;
	anx6345_i2c_write_p0_reg(client,SP_TX_SYS_CTRL3_REG,&val);

	anx6345_i2c_read_p0_reg(client,SP_TX_LANE_COUNT_SET_REG, &lc);
	anx6345_i2c_read_p0_reg(client,SP_TX_LINK_BW_SET_REG, &bw);
	printk("%s..lc:%d--bw:%d\n",__func__,lc,bw);

	return 0;
}


#ifdef CONFIG_HAS_EARLYSUSPEND
static void anx6345_early_suspend(struct early_suspend *h)
{
	struct edp_anx6345 *anx6345 = container_of(h, struct edp_anx6345, early_suspend);
	gpio_set_value(anx6345->pdata->dvdd33_en_pin,!anx6345->pdata->dvdd33_en_val);
	gpio_set_value(anx6345->pdata->dvdd18_en_pin,!anx6345->pdata->dvdd18_en_val);
}

static void anx6345_late_resume(struct early_suspend *h)
{
	struct edp_anx6345 *anx6345 = container_of(h, struct edp_anx6345, early_suspend);
	gpio_set_value(anx6345->pdata->dvdd33_en_pin,anx6345->pdata->dvdd33_en_val);
	gpio_set_value(anx6345->pdata->dvdd18_en_pin,anx6345->pdata->dvdd18_en_val);
	gpio_set_value(anx6345->pdata->edp_rst_pin,0);
	msleep(50);
	gpio_set_value(anx6345->pdata->edp_rst_pin,1);
	anx6345->edp_anx_init(anx6345->client);
}
#endif				

static int anx6345_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	int ret;
	
	struct edp_anx6345 *anx6345 = NULL;
	int chip_id;


	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		ret = -ENODEV;
	}
	anx6345 = kzalloc(sizeof(struct edp_anx6345), GFP_KERNEL);
	if (anx6345 == NULL)
	{
		printk(KERN_ALERT "alloc for struct anx6345 fail\n");
		ret = -ENOMEM;
	}

	anx6345->client = client;
	anx6345->pdata = client->dev.platform_data;
	i2c_set_clientdata(client,anx6345);
	if(anx6345->pdata->power_ctl)
		anx6345->pdata->power_ctl();

	debugfs_create_file("edp-reg", S_IRUSR,NULL,anx6345,&edp_reg_fops);
	
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

	anx6345->edp_anx_init(client);

	printk("edp anx%x probe ok\n",get_dp_chip_id(client));

	return ret;
}

static int __devexit anx6345_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id id_table[] = {
	{"anx6345", 0 },
	{ }
};

static struct i2c_driver anx6345_i2c_driver  = {
	.driver = {
		.name  = "anx6345",
		.owner = THIS_MODULE,
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

fs_initcall_sync(anx6345_module_init);
module_exit(anx6345_module_exit);

