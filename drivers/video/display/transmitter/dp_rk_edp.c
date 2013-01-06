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
#include <linux/rk_edp.h>
#include <linux/debugfs.h>



int rk_edp_i2c_read_p0_reg(struct i2c_client *client, char reg, char *val)
{
	int ret;
	client->addr = DP_TX_PORT0_ADDR >> 1;
	ret = i2c_master_reg8_recv(client, reg, val, 1, RK_EDP_SCL_RATE) > 0? 0: -EINVAL;
	if(ret < 0)
	{
		printk(KERN_ERR "%s>>err\n",__func__);
	}

	return ret;
}
int rk_edp_i2c_write_p0_reg(struct i2c_client *client, char reg, char *val)
{
	int ret;
	client->addr = DP_TX_PORT0_ADDR >> 1;
	ret = i2c_master_reg8_send(client, reg, val, 1, RK_EDP_SCL_RATE) > 0? 0: -EINVAL;
	if(ret < 0)
	{
		printk(KERN_ERR "%s>>err\n",__func__);
	}

	return ret;
}
int rk_edp_i2c_read_p1_reg(struct i2c_client *client, char reg, char *val)
{
	int ret;
	client->addr = HDMI_TX_PORT0_ADDR >> 1;
	ret = i2c_master_reg8_recv(client, reg, val, 1, RK_EDP_SCL_RATE) > 0? 0: -EINVAL;
	if(ret < 0)
	{
		printk(KERN_ERR "%s>>err\n",__func__);
	}

	return ret;
}

int rk_edp_i2c_write_p1_reg(struct i2c_client *client, char reg, char *val)
{
	int ret;
	client->addr = HDMI_TX_PORT0_ADDR >> 1;
	ret = i2c_master_reg8_send(client, reg, val, 1, RK_EDP_SCL_RATE) > 0? 0: -EINVAL;
	if(ret < 0)
	{
		printk(KERN_ERR "%s>>err\n",__func__);
	}

	return ret;
}

static int  DP_TX_Chip_Located(struct i2c_client *client)
{
	char m,n,vid_l,vid_h;	
	
	rk_edp_i2c_read_p1_reg(client, DP_TX_VND_IDL_REG , &vid_l);
    	rk_edp_i2c_read_p1_reg(client, DP_TX_VND_IDH_REG , &vid_h);
    	rk_edp_i2c_read_p1_reg(client, DP_TX_DEV_IDL_REG , &m);
    	rk_edp_i2c_read_p1_reg(client, DP_TX_DEV_IDH_REG , &n);
    	
	printk("vid_l:0x%x>>vid_h:0x%x>>m:0x%x>>n:0x%x\n",vid_l,vid_h,m,n);

	return 0;
}

static void DP_TX_Video_Disable(struct i2c_client *client)
{
 	char val;
	rk_edp_i2c_read_p1_reg(client, DP_TX_VID_CTRL1_REG, &val);
	val &= ~DP_TX_VID_CTRL1_VID_EN;
 	rk_edp_i2c_read_p1_reg(client, DP_TX_VID_CTRL1_REG, &val);
}

void DP_TX_Enable_Video_Input(struct i2c_client *client)
{
	char val,i;
	//if(mode_dp_or_hdmi)
	//EnhacedMode_Clear();

	rk_edp_i2c_read_p1_reg(client,  DP_TX_VID_CTRL1_REG, &val);

	/*if(BIST_EN)  //mask by yxj
	{		
		if((dp_tx_lane_count == 0x01)||(DP_TX_Video_Input.bColordepth == COLOR_12)|| !mode_dp)
			c &= 0xf7;
		else
			c |= 0x08;
	}
	else
	{
		c &= 0xf7;
		//printk("not one lane\n");
	}*/

	val |= DP_TX_VID_CTRL1_VID_EN;
	rk_edp_i2c_write_p1_reg(client,  DP_TX_VID_CTRL1_REG, &val);

	//switch(video_bpc)
	switch(COLOR_6)
	{
		case COLOR_6:

			#if 1

			val = 0;
			rk_edp_i2c_write_p1_reg(client,  0x40, &val);
			rk_edp_i2c_write_p1_reg(client,  0x41, &val);
			rk_edp_i2c_write_p1_reg(client,  0x48, &val);
			rk_edp_i2c_write_p1_reg(client,  0x49, &val);
			rk_edp_i2c_write_p1_reg(client,  0x50, &val);
			rk_edp_i2c_write_p1_reg(client,  0x51, &val);
			for(i=0; i<6; i++)
			{    
				val = i;
				rk_edp_i2c_write_p1_reg(client,  0x42+i, &val);
			}
			
			for(i=0; i<6; i++)
			{    
				val = 6+i;
				rk_edp_i2c_write_p1_reg(client,  0x4a+i, &val);
			}

			for(i=0; i<6; i++)
			{    
				val = 0x0c+i;
				rk_edp_i2c_write_p1_reg(client,  0x52+i, &val);
			}
			#else
			
				for(i=0; i<18; i++)
				{    
					val = i;
					rk_edp_i2c_write_p1_reg(client,  0x40+i, &val);
				}
			}
			#endif
			break;
			
		case COLOR_8:
			for(i=0; i<8; i++)
			{    
				val = 0x04+i;
				rk_edp_i2c_write_p1_reg(client,  0x40+i, &val);
			}

			val = 0x10;
			rk_edp_i2c_write_p1_reg(client,  0x48, &val);	
			val = 0x11;
			rk_edp_i2c_write_p1_reg(client,  0x49, &val);
			for(i=0; i<6; i++)
			{    
				val = 0x18+i;
				rk_edp_i2c_write_p1_reg(client,  0x4a+i, &val);
			}

			for(i=0; i<8; i++)
			{    
				val = 0x22 + i;
				rk_edp_i2c_write_p1_reg(client,  0x50+i, &val);
			}
			break;
			
		case COLOR_10:
			for(i=0; i<10; i++)
			{    
				val = 0x02 + i;
				rk_edp_i2c_write_p1_reg(client,  0x40+i, &val);
			}

			for(i=0; i<4; i++)
			{    
				val = 0x0e + i;
				rk_edp_i2c_write_p1_reg(client,0x4a+i, &val);
			}
			for(i=0; i<6; i++)
			{    
				val = 0x18+i;
				rk_edp_i2c_write_p1_reg(client,i+0x4e,&val);
			}

			for(i=0; i<10; i++)
			{    
				val = 0x20 + i;
				rk_edp_i2c_write_p1_reg(client, 0x54+i, &val);
			}
			break;
			
		case COLOR_12:
			for(i=0; i<18; i++)
			{    
				val = i;
				rk_edp_i2c_write_p1_reg(client,  0x40+i, &val);
			}
			for(i=0; i<18; i++)
			{    
				val = 0x18 + i;
				rk_edp_i2c_write_p1_reg(client,  0x52+i, &val);
			}
			break;
			
		default:
			break;
	}
	msleep(10);
	//val = 0x00;
	//rk_edp_i2c_write_p1_reg(client,  DP_TX_VID_CTRL3_REG, &val);//72:0a GBR mode 04/08/09 9804
	
	printk("Video Enabled!\n");

	/*if(mode_dp)//DP MODE
	{
		DP_TX_Clean_HDCP();

		DP_TX_Config_Packets(AVI_PACKETS);

		
		//if ( !SWITCH1 ) 
		//DP_TX_Config_Audio();  
		
	}*/ //mask by yxj
}




void DP_TX_Power_Down(struct i2c_client *client)
{
	char val;

	DP_TX_Video_Disable(client);
	
    	rk_edp_i2c_read_p1_reg(client, DP_POWERD_CTRL_REG , &val);
	val |= DP_POWERD_TOTAL_REG;
    	rk_edp_i2c_write_p1_reg(client, DP_POWERD_CTRL_REG, &val);	
}




void DP_TX_Power_On(struct i2c_client *client)
{
    char val;
    
    rk_edp_i2c_read_p1_reg(client, DP_POWERD_CTRL_REG , &val);
    val &= ~DP_POWERD_TOTAL_REG;
    rk_edp_i2c_write_p1_reg(client, DP_POWERD_CTRL_REG, &val);
}

void DP_TX_RST_AUX(struct i2c_client *client)
{
	char val;
	rk_edp_i2c_read_p1_reg(client, DP_TX_RST_CTRL2_REG, &val);
	val |= DP_TX_AUX_RST;
    	rk_edp_i2c_write_p1_reg(client, DP_TX_RST_CTRL2_REG, &val);
	val &= ~DP_TX_AUX_RST;
    	rk_edp_i2c_write_p1_reg(client, DP_TX_RST_CTRL2_REG, &val);
}


static void DP_TX_Initialization(struct i2c_client *client)
{
	char val = 0x00;

	 //power on all block and select DisplayPort mode
	val |= DP_POWERD_AUDIO_REG;
	rk_edp_i2c_write_p1_reg(client, DP_POWERD_CTRL_REG, &val );

	DP_TX_Video_Disable(client);

	//software reset    
	rk_edp_i2c_read_p1_reg(client, DP_TX_RST_CTRL_REG, &val);
	val |= DP_TX_RST_SW_RST;
	rk_edp_i2c_write_p1_reg(client, DP_TX_RST_CTRL_REG,&val);
	val &= ~DP_TX_RST_SW_RST;
	rk_edp_i2c_write_p1_reg(client, DP_TX_RST_CTRL_REG, &val);

	
	val = 0x07;
	rk_edp_i2c_write_p0_reg(client, DP_TX_PLL_CTRL_REG, &val);
	val = 0x50;
	rk_edp_i2c_write_p0_reg(client, DP_TX_EXTRA_ADDR_REG, &val);
	
	//24bit SDR,negedge latch, and wait video stable
	val = 0x01;
	rk_edp_i2c_write_p1_reg(client, DP_TX_VID_CTRL1_REG, &val);//72:08 for 9804 SDR, neg edge 05/04/09 extra pxl
	val = 0x19;
	rk_edp_i2c_write_p1_reg(client, DP_TX_PLL_FILTER_CTRL3, &val); 
	val = 0xd9;
	rk_edp_i2c_write_p1_reg(client, DP_TX_PLL_CTRL3, &val);
	// DP_TX_Write_Reg(HDMI_TX_PORT1_ADDR, HDMI_PLL_MISC_CTRL1, 0x10);
	//DP_TX_Write_Reg(HDMI_TX_PORT1_ADDR, HDMI_PLL_MISC_CTRL2, 0x20);

	//disable DDC level shift 08.11.11
	// DP_TX_Write_Reg(HDMI_TX_PORT1_ADDR, 0x65, 0x00);

	//serdes ac mode.
	rk_edp_i2c_read_p1_reg(client, DP_TX_RST_CTRL2_REG, &val);
	val |= DP_TX_AC_MODE;
	rk_edp_i2c_write_p1_reg(client, DP_TX_RST_CTRL2_REG, &val);

	//set channel output amplitude for DP PHY CTS
	//DP_TX_Write_Reg(HDMI_TX_PORT1_ADDR, HDMI_TMDS_CH0_REG, 0x10);
	// DP_TX_Write_Reg(HDMI_TX_PORT1_ADDR, HDMI_TMDS_CH1_REG, 0x10);
	// DP_TX_Write_Reg(HDMI_TX_PORT1_ADDR, HDMI_TMDS_CH2_REG, 0x10);
	// DP_TX_Write_Reg(HDMI_TX_PORT1_ADDR, HDMI_TMDS_CH3_REG, 0x10);
	//set termination
	val = 0xf0;
	rk_edp_i2c_write_p1_reg(client, ANALOG_DEBUG_REG1, &val);
	//set duty cycle
	val = 0x99;
	rk_edp_i2c_write_p1_reg(client, ANALOG_DEBUG_REG3, &val);

	rk_edp_i2c_read_p1_reg(client, DP_TX_PLL_FILTER_CTRL1, &val);
	val |= 0x2a; 
	rk_edp_i2c_write_p1_reg(client, DP_TX_PLL_FILTER_CTRL1, &val);

	//rk_edp_i2c_write_p0_reg(client, DP_TX_HDCP_CTRL, 0x01);
	val = 0x30;
	rk_edp_i2c_write_p0_reg(client, DP_TX_LINK_DEBUG_REG,&val);

	//for DP link CTS 
	rk_edp_i2c_read_p0_reg(client, DP_TX_GNS_CTRL_REG, &val);
	val |= 0x40;
	rk_edp_i2c_write_p0_reg(client, DP_TX_GNS_CTRL_REG, &val);

	//power down  PLL filter
	val = 0x06;
	rk_edp_i2c_write_p1_reg(client, DP_TX_PLL_FILTER_CTRL,&val);


	//rk_edp_i2c_write_p1_reg(client, 0xd7, 0x1b);


	//set system-state to "wait hot plug"
	// DP_TX_Set_System_State(DP_TX_WAIT_HOTPLUG);
}

void DP_TX_Wait_AUX_Finished(struct i2c_client *client)
{
	char val,cnt;
	cnt = 0;
	
	rk_edp_i2c_read_p0_reg(client,DP_TX_AUX_CTRL_REG2, &val);
	while(val&0x01)
	{
		//delay_ms(20);
		cnt ++;
		if(cnt == 10)
		{
		   printk("aux break");
		    DP_TX_RST_AUX(client);
		    //cnt = 0;
		    break;
		}
		rk_edp_i2c_read_p0_reg(client, DP_TX_AUX_CTRL_REG2, &val);
	}
}


static int  DP_TX_AUX_DPCDRead_Bytes(struct i2c_client *client,unsigned long addr, char cCount,char* pBuf)
{
	char val,i;
	//BYTE c1;

	//clr buffer
	val = 0x80;
	rk_edp_i2c_write_p0_reg(client, DP_TX_BUF_DATA_COUNT_REG, &val);

	//set read cmd and count
	val = (((char)(cCount-1) <<4)&(0xf0))|0x09;
	rk_edp_i2c_write_p0_reg(client, DP_TX_AUX_CTRL_REG, &val);

	//set aux address15:0
	val = (char)addr&0xff;
	rk_edp_i2c_write_p0_reg(client, DP_TX_AUX_ADDR_7_0_REG, &val);
	val = (char)((addr>>8)&0xff);
	rk_edp_i2c_write_p0_reg(client, DP_TX_AUX_ADDR_15_8_REG, &val);

	//set address19:16 and enable aux
	rk_edp_i2c_read_p0_reg(client, DP_TX_AUX_ADDR_19_16_REG, &val);
	val &=(0xf0)|(char)((addr>>16)&0xff);
	rk_edp_i2c_write_p0_reg(client, DP_TX_AUX_ADDR_19_16_REG, &val);

	//Enable Aux
	rk_edp_i2c_read_p0_reg(client, DP_TX_AUX_CTRL_REG2, &val);
	val |= 0x01;
	rk_edp_i2c_write_p0_reg(client, DP_TX_AUX_CTRL_REG2, &val);

	//delay_ms(2);
	DP_TX_Wait_AUX_Finished(client);
/*
    rk_edp_i2c_read_p0_reg(client, DP_TX_AUX_STATUS, &c);
    if(c != 0x00)
    {
        DP_TX_RST_AUX();
        printk("aux rd fail");
        return 1;
    }*/

	for(i =0;i<cCount;i++)
	{
		rk_edp_i2c_read_p0_reg(client, DP_TX_BUF_DATA_0_REG+i, &val);

		//debug_printf("c = %.2x\n",(WORD)c);
		*(pBuf+i) = val;
		//c1 = *(pBuf +i);

		//debug_printf("(pBuf+i)  = %.2x\n",(WORD)c1);

		//pBuf++;

		if(i >= MAX_BUF_CNT)
			return 1;
			//break;
	}

	return 0;
	

}

void DP_TX_AUX_DPCDWrite_Bytes(struct i2c_client *client,unsigned long addr, char cCount, char* pBuf)
{
	char val,i;
	u8 cnt = 10;

	//clr buffer
	val = 0x80;
	rk_edp_i2c_write_p0_reg(client, DP_TX_BUF_DATA_COUNT_REG, &val);

	//set write cmd and count;
	val = (((char)(cCount-1) <<4) & 0xf0)|0x08;
	rk_edp_i2c_write_p0_reg(client, DP_TX_AUX_CTRL_REG,&val);

	//set aux address15:0
	val = (char)(addr & 0xff);
	rk_edp_i2c_write_p0_reg(client, DP_TX_AUX_ADDR_7_0_REG,&val );
	val = (char)((addr>>8) & 0xff);
	rk_edp_i2c_write_p0_reg(client, DP_TX_AUX_ADDR_15_8_REG,&val);

	//set address19:16
	rk_edp_i2c_read_p0_reg(client, DP_TX_AUX_ADDR_19_16_REG, &val);
	val &= (0xf0) | (char)((addr>>16) & 0xff);
	rk_edp_i2c_write_p0_reg(client, DP_TX_AUX_ADDR_19_16_REG,&val);


	//write data to buffer
	for(i =0;i<cCount;i++)
	{
		val = *pBuf;
		pBuf++;
		rk_edp_i2c_write_p0_reg(client, DP_TX_BUF_DATA_0_REG+i, &val);

		if(i >= MAX_BUF_CNT)
			break;
	}

	//Enable Aux
	rk_edp_i2c_read_p0_reg(client, DP_TX_AUX_CTRL_REG2, &val);
	val |= 0x01;
	rk_edp_i2c_write_p0_reg(client, DP_TX_AUX_CTRL_REG2,&val);

	//printk("L004w\n");

	DP_TX_Wait_AUX_Finished(client);

	//printk("L0005w\n");

	return ;

}

void EnhacedMode_Clear(struct i2c_client *client)
{
    char val;
    DP_TX_AUX_DPCDRead_Bytes(client,(unsigned long)0x00101,1,&val);
    val &= (~0x80);
    DP_TX_AUX_DPCDWrite_Bytes(client,(long)0x00101, 1, &val);

    rk_edp_i2c_read_p0_reg(client, DP_TX_SYS_CTRL4_REG, &val);
    val &= (~DP_TX_SYS_CTRL4_ENHANCED);
    rk_edp_i2c_write_p0_reg(client, DP_TX_SYS_CTRL4_REG, &val);
}

void DP_TX_EnhaceMode_Set(struct i2c_client *client)
{
	char val;    
	DP_TX_AUX_DPCDRead_Bytes(client,(unsigned long)0x00002,1,&val);
    //c = ;
	if(val & 0x80)
	{
		DP_TX_AUX_DPCDRead_Bytes(client,(unsigned long)0x00101,1,&val);
		val |= 0x80;
			DP_TX_AUX_DPCDWrite_Bytes(client,(unsigned long)0x00101, 1, &val);

		rk_edp_i2c_read_p0_reg(client, DP_TX_SYS_CTRL4_REG, &val);
		val |= DP_TX_SYS_CTRL4_ENHANCED;
		rk_edp_i2c_write_p0_reg(client, DP_TX_SYS_CTRL4_REG, &val);
		printk("Enhance mode");
	}
	else
	EnhacedMode_Clear(client);
}

void DP_TX_Link_Training (struct i2c_client* client)
{
	char val;
	char dp_tx_bw = 0x06; // 1.62Gbps
	char dp_tx_lane_count = 0x04; //4 //lane
	char dp_tx_final_lane_count;
	u8 cnt = 10;
	printk("LT..");

	//set bandwidth
	rk_edp_i2c_write_p0_reg(client, DP_TX_LINK_BW_SET_REG, &dp_tx_bw);
	//set lane conut
	rk_edp_i2c_write_p0_reg(client, DP_TX_LANE_COUNT_SET_REG,&dp_tx_lane_count);
	/*
	rk_edp_i2c_read_p0_reg(client, DP_TX_ANALOG_TEST_REG, &c);
	rk_edp_i2c_write_p0_reg(client, DP_TX_ANALOG_TEST_REG, c | 0x20);
	delay_ms(2);
	rk_edp_i2c_write_p0_reg(client, DP_TX_ANALOG_TEST_REG, (c & ~0x20));
	*/

	val = 0x01;
	DP_TX_AUX_DPCDWrite_Bytes(client,(long)0x00600,1,&val);//set sink to D0 mode.
	val = DP_TX_LINK_TRAINING_CTRL_EN;
	rk_edp_i2c_write_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG, &val);
	msleep(5);

	rk_edp_i2c_read_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG, &val);
	while((val & DP_TX_LINK_TRAINING_CTRL_EN)&&(cnt--))
		rk_edp_i2c_read_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG, &val);
	if(val & 0x70)
	{
		val = (val & 0x70) >> 4;
		printk("HW LT failed, ERR code = %.2x\n",val);
		//return;//keep return. added at 08.5.28
	}
	DP_TX_EnhaceMode_Set(client);//guo .add 08.11.14
	/*
	if(c & 0x70)
	{
	c = (c & 0x70) >> 4;
	debug_printf("Link training error! Return error code = %.2x\n",(WORD)c);
	//if(c == 0x01)
	{
	//printk("Much deff error!");
	if(dp_tx_bw == 0x0a)
	{
	printk("Force to RBR");
	DP_TX_RST_AUX();
	dp_tx_bw = 0x06;
	DP_TX_HW_LT(dp_tx_bw, dp_tx_lane_count);
	}
	}
	}
	*/

	rk_edp_i2c_read_p0_reg(client, DP_TX_LANE_COUNT_SET_REG, &dp_tx_final_lane_count);
	rk_edp_i2c_read_p0_reg(client, DP_TX_TRAINING_LANE0_SET_REG, &val);
	printk("LANE0_SET = %.2x\n",val);
	if(dp_tx_final_lane_count > 1)
	{
		rk_edp_i2c_read_p0_reg(client, DP_TX_TRAINING_LANE1_SET_REG, &val);
		printk("LANE1_SET = %.2x\n",val);
	}
	if(dp_tx_final_lane_count > 2)
	{
		rk_edp_i2c_read_p0_reg(client, DP_TX_TRAINING_LANE2_SET_REG, &val);
		printk("LANE2_SET = %.2x\n",val);
		rk_edp_i2c_read_p0_reg(client, DP_TX_TRAINING_LANE3_SET_REG, &val);
		printk("LANE3_SET = %.2x\n",val);
	}

	printk("HW LT done");

	//DP_TX_Set_System_State(DP_TX_CONFIG_VIDEO);
	//DP_TX_Set_System_State(DP_TX_HDCP_AUTHENTICATION);
	//DP_TX_Set_System_State(DP_TX_CONFIG_AUDIO);

	return; 
}


void DP_TX_HW_LT(struct i2c_client *client,char bw, char lc)
{
	char val;
	u8 cnt = 10;
	val = 0x00;
	rk_edp_i2c_write_p0_reg(client, DP_TX_TRAINING_LANE0_SET_REG, &val);
	rk_edp_i2c_write_p0_reg(client, DP_TX_TRAINING_LANE1_SET_REG, &val);
	rk_edp_i2c_write_p0_reg(client, DP_TX_TRAINING_LANE2_SET_REG, &val);
	rk_edp_i2c_write_p0_reg(client, DP_TX_TRAINING_LANE3_SET_REG, &val);

	rk_edp_i2c_write_p0_reg(client, DP_TX_LINK_BW_SET_REG, &bw);
	rk_edp_i2c_write_p0_reg(client, DP_TX_LANE_COUNT_SET_REG, &lc);
	
	val = DP_TX_LINK_TRAINING_CTRL_EN;
	rk_edp_i2c_write_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG,&val);
	msleep(2);
	rk_edp_i2c_read_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG, &val);
	while((val & DP_TX_LINK_TRAINING_CTRL_EN)&&(cnt--))
	{
		rk_edp_i2c_read_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG, &val);
		cnt--;
	}
	if(cnt < 0)
	{
		printk(KERN_INFO "HW LT fail\n");
	}
	else
		printk(KERN_INFO "HW LT Success!>>:times:%d\n",(11-cnt));
}
void RK_EDP_BIST_Format(struct i2c_client *client)
{
	char val,i;
	u8 cnt=0;

	//Power on total and select DP mode
	val = 00;
        rk_edp_i2c_write_p1_reg(client, DP_POWERD_CTRL_REG, &val);
	
	//HW reset
	val = DP_TX_RST_HW_RST;
	rk_edp_i2c_write_p1_reg(client, DP_TX_RST_CTRL_REG, &val);
	msleep(10);
	val = 0x00;
	rk_edp_i2c_write_p1_reg(client, DP_TX_RST_CTRL_REG, &val);


	rk_edp_i2c_read_p1_reg(client, DP_POWERD_CTRL_REG, &val);
	val = 0x00;
        rk_edp_i2c_write_p1_reg(client, DP_POWERD_CTRL_REG, &val);
	
	
	//get chip ID. Make sure I2C is OK
	rk_edp_i2c_read_p1_reg(client, DP_TX_DEV_IDH_REG , &val);
	if (val==0x98)
		printk("Chip found\n");	

	//for clocl detect
	for(i=0;i<100;i++)
	{
		rk_edp_i2c_read_p0_reg(client, DP_TX_SYS_CTRL1_REG, &val);
		rk_edp_i2c_write_p0_reg(client, DP_TX_SYS_CTRL1_REG, &val);
		rk_edp_i2c_read_p0_reg(client, DP_TX_SYS_CTRL1_REG, &val);
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
		rk_edp_i2c_read_p0_reg(client, DP_TX_SYS_CTRL2_REG, &val);
		rk_edp_i2c_write_p0_reg(client, DP_TX_SYS_CTRL2_REG, &val);
		rk_edp_i2c_read_p0_reg(client, DP_TX_SYS_CTRL2_REG, &val);
		if((val&DP_TX_SYS_CTRL2_CHA_STA)==0)
		{
			printk("clock is stable.\n");
			break;
		}
		msleep(10);
	}

	//VESA range, 8bits BPC, RGB 
	val = 0x10;
	rk_edp_i2c_write_p1_reg(client, DP_TX_VID_CTRL2_REG, &val);
	//RK_EDP chip analog setting
	val = 0x07;
	rk_edp_i2c_write_p0_reg(client, DP_TX_PLL_CTRL_REG, &val); 
	val = 0x19;
	rk_edp_i2c_write_p1_reg(client, DP_TX_PLL_FILTER_CTRL3, &val); 
	val = 0xd9;
	rk_edp_i2c_write_p1_reg(client, DP_TX_PLL_CTRL3, &val); 
	
	
	//DP_TX_Write_Reg(0x7a, 0x38, 0x10); 
	//DP_TX_Write_Reg(0x7a, 0x39, 0x20); 
	//DP_TX_Write_Reg(0x7a, 0x65, 0x00); 
	
	//Select AC mode
	val = 0x40;
	rk_edp_i2c_write_p1_reg(client, DP_TX_RST_CTRL2_REG, &val); 
	
	//DP_TX_Write_Reg(0x7a, 0x61, 0x10); 
	//DP_TX_Write_Reg(0x7a, 0x62, 0x10); 
	//DP_TX_Write_Reg(0x7a, 0x63, 0x10); 
	//DP_TX_Write_Reg(0x7a, 0x64, 0x10); 

	//RK_EDP chip analog setting
	val = 0xf0;
	rk_edp_i2c_write_p1_reg(client, ANALOG_DEBUG_REG1, &val);
	val = 0x99;
	rk_edp_i2c_write_p1_reg(client, ANALOG_DEBUG_REG3, &val);
	val = 0x7b;
	rk_edp_i2c_write_p1_reg(client, DP_TX_PLL_FILTER_CTRL1, &val);
	val = 0x30;
	rk_edp_i2c_write_p0_reg(client, DP_TX_LINK_DEBUG_REG,&val);
	val = 0x06;
	rk_edp_i2c_write_p1_reg(client, DP_TX_PLL_FILTER_CTRL, &val);
	
	//force HPD
	val = 0x30;
	rk_edp_i2c_write_p0_reg(client, DP_TX_SYS_CTRL3_REG, &val);
	//power on 4 lanes
	val = 0x00;
	rk_edp_i2c_write_p0_reg(client, 0xc8, &val);
	//lanes setting
	rk_edp_i2c_write_p0_reg(client, 0xa3, &val);
	rk_edp_i2c_write_p0_reg(client, 0xa4, &val);
	rk_edp_i2c_write_p0_reg(client, 0xa5,&val);
	rk_edp_i2c_write_p0_reg(client, 0xa6, &val);

#if 0
	//step 1: read DPCD 0x00001, the correct value should be 0x0a, or 0x06
	rk_edp_i2c_write_p0_reg(client,  0xE4,  0x80);

	//set read cmd and count, read 2 bytes data, get downstream max_bandwidth and max_lanes
	rk_edp_i2c_write_p0_reg(client, 0xE5,  0x19);

	//set aux address19:0
	rk_edp_i2c_write_p0_reg(client,  0xE6,  0x01);
	rk_edp_i2c_write_p0_reg(client,  0xE7,  0x00);
	rk_edp_i2c_write_p0_reg(client,  0xE8,  0x00);

	//Enable Aux
	rk_edp_i2c_write_p0_reg(client,  0xE9, 0x01);

	//wait aux finished
	for(i=0; i<50; i++)
	{
	  rk_edp_i2c_read_p0_reg(client,  0xE9,  &c);
	  if(c==0x00)
	  {
	    break;
	  }
	}

	//read data from buffer
	DP_TX_Write_Reg(  0x70,  0xF0,   &max_bandwidth);
	DP_TX_Write_Reg(  0x70,  0xF1,   &max_lanes);
	debug_printf("max_bandwidth = %.2x, max_lanes = %.2x\n", (WORD)max_bandwidth, (WORD)max_lanes);
#endif

	//reset AUX CH
	val = 0x44;
	rk_edp_i2c_write_p1_reg(client,  DP_TX_RST_CTRL2_REG, &val);
	val = 0x40;
	rk_edp_i2c_write_p1_reg(client,  DP_TX_RST_CTRL2_REG, &val);

	//Select 1.62G
	val = 0x06;
	rk_edp_i2c_write_p0_reg(client, DP_TX_LINK_BW_SET_REG, &val);
	//Select 4 lanes
	val = 0x04;
	rk_edp_i2c_write_p0_reg(client, DP_TX_LANE_COUNT_SET_REG, &val);
	
	//strart link traing
	//DP_TX_LINK_TRAINING_CTRL_EN is self clear. If link training is OK, it will self cleared.
	#if 1
	val = DP_TX_LINK_TRAINING_CTRL_EN;
	rk_edp_i2c_write_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG, &val);
	msleep(5);
	rk_edp_i2c_read_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG, &val);
	while((val&0x01)&&(cnt++ < 10))
	{
		printk("Waiting...\n");
		msleep(5);
		rk_edp_i2c_read_p0_reg(client, DP_TX_LINK_TRAINING_CTRL_REG, &val);
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
	rk_edp_i2c_write_p1_reg(client, DP_TX_TOTAL_LINEL_REG, &val);
	val = 0x06;
	rk_edp_i2c_write_p1_reg(client, DP_TX_TOTAL_LINEH_REG, &val);

	val = 0x00;
	rk_edp_i2c_write_p1_reg(client, DP_TX_ACT_LINEL_REG, &val);
	val = 0x06;
	rk_edp_i2c_write_p1_reg(client, DP_TX_ACT_LINEH_REG,&val);
	val = 0x02;
	rk_edp_i2c_write_p1_reg(client, DP_TX_VF_PORCH_REG, &val);
	val = 0x04;
	rk_edp_i2c_write_p1_reg(client, DP_TX_VSYNC_CFG_REG,&val);
	val = 0x26;
	rk_edp_i2c_write_p1_reg(client, DP_TX_VB_PORCH_REG, &val);
	val = 0x50;
	rk_edp_i2c_write_p1_reg(client, DP_TX_TOTAL_PIXELL_REG, &val);
	val = 0x04;
	rk_edp_i2c_write_p1_reg(client, DP_TX_TOTAL_PIXELH_REG, &val);
	val = 0x00;
	rk_edp_i2c_write_p1_reg(client, DP_TX_ACT_PIXELL_REG, &val);
	val = 0x04;
	rk_edp_i2c_write_p1_reg(client, DP_TX_ACT_PIXELH_REG, &val);

	val = 0x18;
	rk_edp_i2c_write_p1_reg(client, DP_TX_HF_PORCHL_REG, &val);
	val = 0x00;
	rk_edp_i2c_write_p1_reg(client, DP_TX_HF_PORCHH_REG, &val);

	val = 0x10;
	rk_edp_i2c_write_p1_reg(client, DP_TX_HSYNC_CFGL_REG,&val);
	val = 0x00;
	rk_edp_i2c_write_p1_reg(client, DP_TX_HSYNC_CFGH_REG,&val);
	val = 0x28;
	rk_edp_i2c_write_p1_reg(client, DP_TX_HB_PORCHL_REG, &val);
	val = 0x00;
	rk_edp_i2c_write_p1_reg(client, DP_TX_HB_PORCHH_REG, &val);
	val = 0x03;
	rk_edp_i2c_write_p1_reg(client, DP_TX_VID_CTRL10_REG, &val);

	//enable BIST
	val = DP_TX_VID_CTRL4_BIST;
	rk_edp_i2c_write_p1_reg(client, DP_TX_VID_CTRL4_REG, &val);
	//enable video input
	val = 0x8d;
	rk_edp_i2c_write_p1_reg(client, DP_TX_VID_CTRL1_REG, &val);
	//force HPD and stream valid
	val = 0x33;
	rk_edp_i2c_write_p0_reg(client, 0x82, &val);
}

//void DP_TX_BIST_Format_Config(WORD dp_tx_bist_select_number)
void DP_TX_BIST_Format_Config(struct i2c_client *client)
{
	u16 dp_tx_bist_data;
	u8 c,c1;
	u16 wTemp,wTemp1,wTemp2;
	bool bInterlace;
	char val;

	struct rk_edp *rk_edp = i2c_get_clientdata(client);
	rk_screen * screen = &rk_edp->screen;
	printk("config vid timing\n");

	set_lcd_info(screen,NULL);
	//Interlace or Progressive mode
	rk_edp_i2c_read_p1_reg(client, DP_TX_VID_CTRL10_REG, &val);
	val &= ~ DP_TX_VID_CTRL10_I_SCAN;
	rk_edp_i2c_write_p1_reg(client, DP_TX_VID_CTRL10_REG, &val);
	

	//Vsync Polarity set
	//temp = (DP_TX_EDID_PREFERRED[17]&0x04)>>2;	
	rk_edp_i2c_read_p1_reg(client, DP_TX_VID_CTRL10_REG, &val);
	if(!screen->pin_vsync)
	{
		val |= DP_TX_VID_CTRL10_VSYNC_POL;
		rk_edp_i2c_write_p1_reg(client, DP_TX_VID_CTRL10_REG, &val);
	}
	else
	{
		val &= ~ DP_TX_VID_CTRL10_VSYNC_POL;
		rk_edp_i2c_write_p1_reg(client, DP_TX_VID_CTRL10_REG, &val);
	}

	//Hsync Polarity set
	//temp = (DP_TX_EDID_PREFERRED[17]&0x20)>>1;	
	rk_edp_i2c_read_p1_reg(client, DP_TX_VID_CTRL10_REG, &val);
	if(!screen->pin_hsync)
	{
		val |= DP_TX_VID_CTRL10_HSYNC_POL;
		rk_edp_i2c_write_p1_reg(client, DP_TX_VID_CTRL10_REG, &val);
	}
	else
	{
		val &= ~ DP_TX_VID_CTRL10_HSYNC_POL;
		rk_edp_i2c_write_p1_reg(client, DP_TX_VID_CTRL10_REG, &val);
	}

	//H active length set
	//wTemp = DP_TX_EDID_PREFERRED[4];
	//wTemp = (wTemp << 4) & 0x0f00;
	//dp_tx_bist_data = wTemp + DP_TX_EDID_PREFERRED[2];
	//if(((dp_tx_lane_count != 0x01)&&((DP_TX_Video_Input.bColordepth != COLOR_12))) && mode_dp)
	//	dp_tx_bist_data = dp_tx_bist_data / 2;

	val = screen->x_res & (0x00ff);
	rk_edp_i2c_write_p1_reg(client, DP_TX_ACT_PIXELL_REG, &val);
	val = screen->x_res >> 8;
	rk_edp_i2c_write_p1_reg(client, DP_TX_ACT_PIXELH_REG, &val);

	//H total length = hactive+hblank
	#if 0
	wTemp = DP_TX_EDID_PREFERRED[4];
	wTemp = (wTemp<< 8) & 0x0f00;
	wTemp= wTemp + DP_TX_EDID_PREFERRED[3];	
	dp_tx_bist_data = dp_tx_bist_data + wTemp;
	if(((dp_tx_lane_count != 0x01)&&((DP_TX_Video_Input.bColordepth != COLOR_12))) && mode_dp)
		dp_tx_bist_data = dp_tx_bist_data / 2;
	#else
		val = (screen->x_res + screen->left_margin + screen->right_margin + screen->hsync_len) & (0x00ff);
	#endif
	rk_edp_i2c_write_p1_reg(client, DP_TX_TOTAL_PIXELL_REG, &val);
	val = (screen->x_res + screen->left_margin + screen->right_margin + screen->hsync_len) >> 8;
	rk_edp_i2c_write_p1_reg(client, DP_TX_TOTAL_PIXELH_REG, &val);


	//H front porch width set
	#if 0
	wTemp = DP_TX_EDID_PREFERRED[11];
	wTemp = (wTemp << 2) & 0x0300;
	wTemp = wTemp + DP_TX_EDID_PREFERRED[8];
	if(((dp_tx_lane_count != 0x01)&&((DP_TX_Video_Input.bColordepth != COLOR_12))) && mode_dp)
		wTemp = wTemp / 2;
	#else
	val = screen->right_margin && 0x00ff;
	#endif
	rk_edp_i2c_write_p1_reg(client, DP_TX_HF_PORCHL_REG, &val);
	val = screen->right_margin >> 8;
	rk_edp_i2c_write_p1_reg(client, DP_TX_HF_PORCHH_REG, &val);

	//H sync width set
	#if 0
	wTemp = DP_TX_EDID_PREFERRED[11];
	wTemp = (wTemp << 4) & 0x0300;
	wTemp = wTemp + DP_TX_EDID_PREFERRED[9];
	if(((dp_tx_lane_count != 0x01)&&((DP_TX_Video_Input.bColordepth != COLOR_12))) && mode_dp)
		wTemp = wTemp / 2;
	#else
	val =  screen->hsync_len &(0x00ff);
	#endif
	rk_edp_i2c_write_p1_reg(client, DP_TX_HSYNC_CFGL_REG, &val);
	val = screen->hsync_len >> 8;
	rk_edp_i2c_write_p1_reg(client, DP_TX_HSYNC_CFGH_REG, &val);

	//H back porch = H blank - H Front porch - H sync width
	#if 0
	//Hblank
	wTemp = DP_TX_EDID_PREFERRED[4];
	wTemp = (wTemp<< 8) & 0x0f00;
	wTemp= wTemp + DP_TX_EDID_PREFERRED[3];

	//H Front porch
	wTemp1 = DP_TX_EDID_PREFERRED[11];
	wTemp1 = (wTemp1 << 2) & 0x0300;
	wTemp1 = wTemp1 + DP_TX_EDID_PREFERRED[8];

	//Hsync width
	dp_tx_bist_data = DP_TX_EDID_PREFERRED[11];
	dp_tx_bist_data = (dp_tx_bist_data << 4) & 0x0300;
	dp_tx_bist_data = dp_tx_bist_data + DP_TX_EDID_PREFERRED[9];

	//H Back porch
	wTemp2 = (wTemp - wTemp1) - dp_tx_bist_data;
	if(((dp_tx_lane_count != 0x01)&&((DP_TX_Video_Input.bColordepth != COLOR_12))) && mode_dp)
		wTemp2 = wTemp2 / 2;
	#else
	val = screen->left_margin & (0x00ff);
	#endif
	rk_edp_i2c_write_p1_reg(client, DP_TX_HB_PORCHL_REG, &val);
	val = screen->left_margin >> 8;
	rk_edp_i2c_write_p1_reg(client, DP_TX_HB_PORCHH_REG, &val);

	//V active length set
	#if 0
	wTemp = DP_TX_EDID_PREFERRED[7];
	wTemp = (wTemp << 4) & 0x0f00;
	dp_tx_bist_data = wTemp + DP_TX_EDID_PREFERRED[5];
	//for interlaced signal
	if(bInterlace)
		dp_tx_bist_data = dp_tx_bist_data*2;
	#else
	val = screen->y_res & (0x00ff);
	#endif
	rk_edp_i2c_write_p1_reg(client, DP_TX_ACT_LINEL_REG, &val);
	val = screen->y_res >> 8;
	rk_edp_i2c_write_p1_reg(client, DP_TX_ACT_LINEH_REG, &val);

	//V total length set
	#if 0
	wTemp = DP_TX_EDID_PREFERRED[7];
	wTemp = (wTemp << 8) & 0x0f00;
	wTemp = wTemp + DP_TX_EDID_PREFERRED[6];
	//vactive+vblank
	dp_tx_bist_data = dp_tx_bist_data + wTemp;
	//for interlaced signal
	if(bInterlace)
		dp_tx_bist_data = dp_tx_bist_data*2+1;
	#else
	val = (screen->y_res + screen->vsync_len + screen->left_margin + screen->upper_margin)&&(0x00ff);
	#endif
	rk_edp_i2c_write_p1_reg(client, DP_TX_TOTAL_LINEL_REG, &val);
	val = (screen->y_res + screen->vsync_len + screen->left_margin + screen->upper_margin) >> 8;
	rk_edp_i2c_write_p1_reg(client, DP_TX_TOTAL_LINEH_REG, &val);

	//V front porch width set
	#if 0
	wTemp = DP_TX_EDID_PREFERRED[11];
	wTemp = (wTemp << 2) & 0x0030;
	wTemp = wTemp + (DP_TX_EDID_PREFERRED[10] >> 4);
	#else
	val = screen->lower_margin;
	#endif
	rk_edp_i2c_write_p1_reg(client, DP_TX_VF_PORCH_REG, &val);

	//V sync width set
	#if 0
	wTemp = DP_TX_EDID_PREFERRED[11];
	wTemp = (wTemp << 4) & 0x0030;
	wTemp = wTemp + (DP_TX_EDID_PREFERRED[10] & 0x0f);
	rk_edp_i2c_write_p1_reg(client, DP_TX_VSYNC_CFG_REG, (BYTE)wTemp);


	//V back porch = V blank - V Front porch - V sync width
	//V blank
	wTemp = DP_TX_EDID_PREFERRED[7];
	wTemp = (wTemp << 8) & 0x0f00;
	wTemp = wTemp + DP_TX_EDID_PREFERRED[6];

	//V front porch
	wTemp1 = DP_TX_EDID_PREFERRED[11];
	wTemp1 = (wTemp1 << 2) & 0x0030;
	wTemp1 = wTemp1 + (DP_TX_EDID_PREFERRED[10] >> 4);

	//V sync width
	wTemp2 = DP_TX_EDID_PREFERRED[11];
	wTemp2 = (wTemp2 << 4) & 0x0030;
	wTemp2 = wTemp2 + (DP_TX_EDID_PREFERRED[10] & 0x0f);
	dp_tx_bist_data = (wTemp - wTemp1) - wTemp2;
	#else
	val = screen->upper_margin;
	#endif
	rk_edp_i2c_write_p1_reg(client, DP_TX_VB_PORCH_REG, &val);


	//BIST color bar width set--set to each bar is 32 pixel width
	rk_edp_i2c_read_p1_reg(client, DP_TX_VID_CTRL4_REG, &val);
	val &= ~DP_TX_VID_CTRL4_BIST_WIDTH;
	rk_edp_i2c_write_p1_reg(client, DP_TX_VID_CTRL4_REG, &val);

	//Enable video BIST
	rk_edp_i2c_read_p1_reg(client, DP_TX_VID_CTRL4_REG, &val);
	val &= DP_TX_VID_CTRL4_BIST;
	rk_edp_i2c_write_p1_reg(client, DP_TX_VID_CTRL4_REG, &val);
}

void DP_TX_Config_Video (struct i2c_client *client)
{
	char val;

	char safe_mode = 0;
	char ByteBuf[2];
	char dp_tx_bw,dp_tx_lane_count;
	
		
	rk_edp_i2c_read_p0_reg(client,  DP_TX_SYS_CTRL1_REG, &val);
	rk_edp_i2c_write_p0_reg(client, DP_TX_SYS_CTRL1_REG, &val);
	rk_edp_i2c_read_p0_reg(client,  DP_TX_SYS_CTRL1_REG, &val);
	if(!(val & DP_TX_SYS_CTRL1_DET_STA))
	{
		printk("No pclk\n");
		//return;  //mask by yxj
	}

	rk_edp_i2c_read_p0_reg(client,  DP_TX_SYS_CTRL2_REG, &val);
	rk_edp_i2c_write_p0_reg(client,  DP_TX_SYS_CTRL2_REG, &val);
	rk_edp_i2c_read_p0_reg(client,  DP_TX_SYS_CTRL2_REG, &val);
	if(val & DP_TX_SYS_CTRL2_CHA_STA)
	{
		printk("pclk not stable!\n");
		//return; mask by yxj
	}

	DP_TX_AUX_DPCDRead_Bytes(client,(unsigned long)0x00001,2,ByteBuf);
	dp_tx_bw = ByteBuf[0];
	dp_tx_lane_count = ByteBuf[1] & 0x0f;
	printk("max_bw = %.2x\n",dp_tx_bw);
	printk("max_lc = %.2x\n",dp_tx_lane_count);

		
	//DP_TX_BIST_Format_Config(client);
	if(!safe_mode)
	{
		/*
		rk_edp_i2c_read_p1_reg(client,  DP_TX_VID_CTRL2_REG, &c);
		switch(DP_TX_Video_Input.bColordepth)
		{
		case COLOR_6:
		rk_edp_i2c_write_p1_reg(client,  DP_TX_VID_CTRL2_REG, c & 0x8f);
		break;
		case COLOR_8:
		rk_edp_i2c_write_p1_reg(client,  DP_TX_VID_CTRL2_REG, (c & 0x8f) | 0x10);
		break;
		case COLOR_10:
		rk_edp_i2c_write_p1_reg(client,  DP_TX_VID_CTRL2_REG, (c & 0x8f) | 0x20);
		break;
		case COLOR_12:
		rk_edp_i2c_write_p1_reg(client,  DP_TX_VID_CTRL2_REG, (c & 0x8f) | 0x30);
		break;	
		default:
		break;
		}*/


		//set Input BPC mode & color space
		rk_edp_i2c_read_p1_reg(client,  DP_TX_VID_CTRL2_REG, &val);
		val &= 0x8c;
		val = val |((char)(0) << 4);  //8bits  ,rgb
		rk_edp_i2c_write_p1_reg(client,  DP_TX_VID_CTRL2_REG, &val);
	}
	
	
	
	//enable video input
	DP_TX_Enable_Video_Input(client);
	
}

static ssize_t rk_edp0_debug_show(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
	int i = 0;
	char val;
	struct rk_edp *rk_edp = dev_get_drvdata(dev);
	
	for(i=0;i< MAX_REG;i++)
	{
		rk_edp_i2c_read_p0_reg(rk_edp->client, i , &val);
		printk("0x%02x>>0x%02x\n",i,val);
	}
	
	return 0;
        
}


static ssize_t rk_edp0_debug_store(struct device *dev, struct device_attribute *attr,
                        const char *buf,size_t count)
{

        return count;
}

static ssize_t rk_edp1_debug_show(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
	int i = 0;
	char val;
	struct rk_edp *rk_edp = dev_get_drvdata(dev);
	
	for(i=0;i< MAX_REG;i++)
	{
		rk_edp_i2c_read_p1_reg(rk_edp->client, i , &val);
		printk("0x%02x>>0x%02x\n",i,val);
	}

	return 0;
        
}


static ssize_t rk_edp1_debug_store(struct device *dev, struct device_attribute *attr,
                       const char *buf,size_t count)
{

        return count;
}

static struct device_attribute rk_edp_attrs[] = {
        __ATTR(rk_edp-0x70, S_IRUGO | S_IWUSR, rk_edp0_debug_show,rk_edp0_debug_store),
        __ATTR(rk_edp-0x72,S_IRUGO | S_IWUSR, rk_edp1_debug_show,rk_edp1_debug_store),
};


static int rk_edp_create_sysfs(struct device *dev)
{
        int r;
        int t;
        for (t = 0; t < ARRAY_SIZE(rk_edp_attrs); t++)
        {
                r = device_create_file(dev,&rk_edp_attrs[t]);
                if (r)
                {
                        dev_err(dev, "failed to create sysfs "
                                        "file\n");
                        return r;
                }
        }


        return 0;
}

static int edp_reg_show(struct seq_file *s, void *v)
{
	int i = 0;
	char val;
	struct rk_edp *rk_edp = s->private;
	if(!rk_edp)
	{
		printk(KERN_ERR "no edp device!\n");
		return 0;
	}

	seq_printf(s,"0x70:\n");
	for(i=0;i< MAX_REG;i++)
	{
		rk_edp_i2c_read_p0_reg(rk_edp->client, i , &val);
		seq_printf(s,"0x%02x>>0x%02x\n",i,val);
	}

	
	seq_printf(s,"\n0x72:\n");
	for(i=0;i< MAX_REG;i++)
	{
		rk_edp_i2c_read_p1_reg(rk_edp->client, i , &val);
		seq_printf(s,"0x%02x>>0x%02x\n",i,val);
	}
	return 0;
}

static int edp_reg_open(struct inode *inode, struct file *file)
{
	struct rk_edp *rk_edp = inode->i_private;
	return single_open(file, edp_reg_show, rk_edp);
}

static const struct file_operations edp_reg_fops = {
	.owner		= THIS_MODULE,
	.open		= edp_reg_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};


#ifdef CONFIG_HAS_EARLYSUSPEND
static void rk_edp_early_suspend(struct early_suspend *h)
{
	struct rk_edp *rk_edp = container_of(h, struct rk_edp, early_suspend);
	gpio_set_value(rk_edp->pdata->dvdd33_en_pin,!rk_edp->pdata->dvdd33_en_val);
	gpio_set_value(rk_edp->pdata->dvdd18_en_pin,!rk_edp->pdata->dvdd18_en_val);
}

static void rk_edp_late_resume(struct early_suspend *h)
{
	struct rk_edp *rk_edp = container_of(h, struct rk_edp, early_suspend);
	gpio_set_value(rk_edp->pdata->dvdd33_en_pin,rk_edp->pdata->dvdd33_en_val);
	gpio_set_value(rk_edp->pdata->dvdd18_en_pin,rk_edp->pdata->dvdd18_en_val);
	gpio_set_value(rk_edp->pdata->edp_rst_pin,0);
	msleep(50);
	gpio_set_value(rk_edp->pdata->edp_rst_pin,1);
//	msleep(10);
#if 1
	DP_TX_Initialization(rk_edp->client);
	DP_TX_HW_LT(rk_edp->client,0x06,0x04); // 1.62Gpbs 4lane
	DP_TX_Config_Video(rk_edp->client);
#else
	RK_EDP_BIST_Format(rk_edp->client);
#endif
}
#endif				

static int rk_edp_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
	int ret;
	
	struct rk_edp *rk_edp = NULL;


	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
	{
		dev_err(&client->dev, "Must have I2C_FUNC_I2C.\n");
		ret = -ENODEV;
	}
	rk_edp = kzalloc(sizeof(struct rk_edp), GFP_KERNEL);
	if (rk_edp == NULL)
	{
		printk(KERN_ALERT "alloc for struct rk_edp fail\n");
		ret = -ENOMEM;
	}

	rk_edp->client = client;
	rk_edp->pdata = client->dev.platform_data;
	i2c_set_clientdata(client,rk_edp);
	if(rk_edp->pdata->power_ctl)
		rk_edp->pdata->power_ctl();

	debugfs_create_file("edp-reg", S_IRUSR,NULL, rk_edp,
				    &edp_reg_fops);
	ret = DP_TX_Chip_Located(client);
	if(ret < 0)
	{
		printk(KERN_ERR "rk_edp not found\n");
		return ret;
	}
	else
	{
		printk(KERN_INFO "rk_edp found\n");
	}
#ifdef CONFIG_HAS_EARLYSUSPEND
	rk_edp->early_suspend.suspend = rk_edp_early_suspend;
	rk_edp->early_suspend.resume = rk_edp_late_resume;
    	rk_edp->early_suspend.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING;
	register_early_suspend(&rk_edp->early_suspend);
#endif

#if 1
	DP_TX_Initialization(client);
	DP_TX_HW_LT(client,0x06,0x04); // 1.62 Gpbs 4lane
	DP_TX_Config_Video(client);
#else
	RK_EDP_BIST_Format(client);
#endif

	printk("edp probe ok\n");

	return ret;
}

static int __devexit rk_edp_i2c_remove(struct i2c_client *client)
{
	
	
	return 0;
}
static const struct i2c_device_id rk_edp_id[] = {
	{ "rk_edp", 0 },
	{ }
};

static struct i2c_driver rk_edp_i2c_driver  = {
    .driver = {
        .name  = "rk_edp",
        .owner = THIS_MODULE,
    },
    .probe =    &rk_edp_i2c_probe,
    .remove     = &rk_edp_i2c_remove,
    .id_table	= rk_edp_id,
};


static int __init rk_edp_module_init(void)
{
    return i2c_add_driver(&rk_edp_i2c_driver);
}

static void __exit rk_edp_module_exit(void)
{
    i2c_del_driver(&rk_edp_i2c_driver);
}

//subsys_initcall_sync(rk_edp_module_init);
late_initcall_sync(rk_edp_module_init);
module_exit(rk_edp_module_exit);

