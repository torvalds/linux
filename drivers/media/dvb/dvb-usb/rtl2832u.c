
#include <linux/module.h>
#include <linux/version.h>

#include "rtl2832u.h"
#include "rtl2832u_io.h"
#include "rtl2832u_audio.h"
//#include "rtl2832u_ioctl.h"

int dvb_usb_rtl2832u_debug=0;
module_param_named(debug,dvb_usb_rtl2832u_debug, int, 0644);
MODULE_PARM_DESC(debug, "Set debugging level (1=info,xfer=2 (or-able),rc=3)." DVB_USB_DEBUG_STATUS);

int demod_default_type=0;
module_param_named(demod, demod_default_type, int, 0644);
MODULE_PARM_DESC(demod, "Set default demod type(0=dvb-t, 1=dtmb, 2=dvb-c)"DVB_USB_DEBUG_STATUS);

int dtmb_error_packet_discard;
module_param_named(dtmb_err_discard, dtmb_error_packet_discard, int, 0644);
MODULE_PARM_DESC(dtmb_err_discard, "Set error packet discard type(0=not discard, 1=discard)"DVB_USB_DEBUG_STATUS);

int dvb_use_rtl2832u_rc_mode=2;
module_param_named(rtl2832u_rc_mode, dvb_use_rtl2832u_rc_mode, int, 0644);
MODULE_PARM_DESC(rtl2832u_rc_mode, "Set default rtl2832u_rc_mode(0=rc6, 1=rc5, 2=nec, 3=disable rc, default=2)."DVB_USB_DEBUG_STATUS);

int dvb_use_rtl2832u_card_type=0;
module_param_named(rtl2832u_card_type, dvb_use_rtl2832u_card_type, int, 0644);
MODULE_PARM_DESC(rtl2832u_card_type, "Set default rtl2832u_card_type type(0=dongle, 1=mini card, default=0)."DVB_USB_DEBUG_STATUS);

int dvb_usb_rtl2832u_snrdb=0;
module_param_named(snrdb,dvb_usb_rtl2832u_snrdb, int, 0644);
MODULE_PARM_DESC(snrdb, "SNR type output (0=16bit, 1=dB decibel), default=0");




//#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);
//#endif

#define	USB_EPA_CTL	0x0148

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define RT_RC_POLLING_INTERVAL_TIME_MS			287
#define MAX_RC_PROTOCOL_NUM				3			

/* original realtek remote control key map */
/*
static struct dvb_usb_rc_key rtl2832u_rc_keys_map_table[] = {// realtek Key map   	
		{ 0x0400, KEY_0 },           // 0 
		{ 0x0401, KEY_1 },           // 1 
		{ 0x0402, KEY_2 },           // 2 
		{ 0x0403, KEY_3 },           // 3 
		{ 0x0404, KEY_4 },           // 4 
		{ 0x0405, KEY_5 },           // 5 
		{ 0x0406, KEY_6 },           // 6 
		{ 0x0407, KEY_7 },           // 7 
		{ 0x0408, KEY_8 },           // 8 
		{ 0x0409, KEY_9 },           // 9 
		{ 0x040c, KEY_POWER },       // POWER 
		{ 0x040e, KEY_MUTE },        // MUTE 
		{ 0x0410, KEY_VOLUMEUP },    // VOL UP 
		{ 0x0411, KEY_VOLUMEDOWN },  // VOL DOWN 
		{ 0x0412, KEY_CHANNELUP },   // CH UP 
		{ 0x0413, KEY_CHANNELDOWN }, // CH DOWN 
		{ 0x0416, KEY_PLAY },        // PLAY 
		{ 0x0417, KEY_RECORD },      // RECORD 
		{ 0x0418, KEY_PLAYPAUSE },   // PAUSE 
		{ 0x0419, KEY_STOP },        // STOP 
		{ 0x041e, KEY_UP},	     // UP
		{ 0x041f, KEY_DOWN},	     // DOWN
		{ 0x0420, KEY_LEFT },        // LEFT
		{ 0x0421, KEY_RIGHT },       // RIGHT
		{ 0x0422, KEY_ZOOM },        // FULL SCREEN  -->OK 
		{ 0x0447, KEY_AUDIO },       // MY AUDIO 
		{ 0x045b, KEY_MENU},         // RED 
		{ 0x045c, KEY_EPG },         // GREEN 
		{ 0x045d, KEY_FIRST },       // YELLOW
		{ 0x045e, KEY_LAST },        // BLUE
		{ 0x045a, KEY_TEXT },        // TEXT TV
	 	{ 0x0423, KEY_BACK },        // <- BACK
		{ 0x0414, KEY_FORWARD }    // >> 
	};
*/

/* Xgazza remote control Ubuntu 11.10 key map */
static struct rc_map_table rtl2832u_rc_keys_map_table[] = {
	{ 0x40bf, KEY_POWER2 },        // TV POWER
	{ 0x08f7, KEY_POWER },         // PC POWER
	{ 0x58a7, KEY_REWIND },        // REWIND
	{ 0xd827, KEY_PLAY },          // PLAY
	{ 0x22dd, KEY_FASTFORWARD },   // FAST FORWARD
	{ 0x02fd, KEY_STOP },          // STOP
	{ 0x5aa5, KEY_PREVIOUS },      // SKIP BACK
	{ 0x42bd, KEY_PLAYPAUSE },     // PAUSE
	{ 0xa25d, KEY_NEXT },          // SKIP FOWARD
	{ 0x12ed, KEY_RECORD },        // RECORD
	{ 0x28d7, KEY_BACK },          // BACK
	{ 0xa857, KEY_INFO },          // MORE
//	{ 0x28d7, BTN_LEFT },          // MOUSE LEFT BUTTON
//	{ 0xa857, BTN_RIGHT },         // MOUSE RIGHT BUTTON
	{ 0x6897, KEY_UP},             // UP
	{ 0x48b7, KEY_DOWN},           // DOWN
	{ 0xe817, KEY_LEFT },          // LEFT
	{ 0x30cf, KEY_RIGHT },         // RIGHT
	{ 0x18e7, KEY_OK },            // OK 
	{ 0xc23d, KEY_ZOOM },          // ASPECT
//	{ 0xea15, KEY_??? },           // MOUSE
	{ 0x708f, KEY_RED },           // RED 
	{ 0xc837, KEY_GREEN },         // GREEN 
	{ 0x8877, KEY_YELLOW },        // YELLOW
	{ 0x9867, KEY_BLUE },          // BLUE
	{ 0x807f, KEY_VOLUMEUP },      // VOL UP 
	{ 0x7887, KEY_VOLUMEDOWN },    // VOL DOWN 
	{ 0xb04f, KEY_HOME },          // HOME
	{ 0x00ff, KEY_MUTE },          // MUTE 
	{ 0xd22d, KEY_CHANNELUP },     // CH UP 
	{ 0xf20d, KEY_CHANNELDOWN },   // CH DOWN 
	{ 0x50af, KEY_0 },             // 0 
	{ 0xf807, KEY_1 },             // 1 
	{ 0xc03f, KEY_2 },             // 2 
	{ 0x20df, KEY_3 },             // 3 
	{ 0xa05f, KEY_4 },             // 4 
	{ 0x38c7, KEY_5 },             // 5 
	{ 0x609f, KEY_6 },             // 6 
	{ 0xe01f, KEY_7 },             // 7 
	{ 0x10ef, KEY_8 },             // 8 
	{ 0xb847, KEY_9 },             // 9
	{ 0x906f, KEY_NUMERIC_STAR },  // *
	{ 0xd02f, KEY_NUMERIC_POUND }, // #
	{ 0x52ad, KEY_EPG },           // GUIDE
	{ 0x926d, KEY_VIDEO },         // RTV
	{ 0x32cd, KEY_HELP },          // HELP
	{ 0xca35, KEY_CYCLEWINDOWS },  // PIP(?)
	{ 0xb24d, KEY_RADIO },         // RADIO
	{ 0x0af5, KEY_DVD },           // DVD
	{ 0x8a75, KEY_AUDIO },         // AUDIO
	{ 0x4ab5, KEY_TITLE }          // TITLE
};

////////////////////////////////////////////////////////////////////////////////////////////////////////
enum   rc_status_define{
	RC_FUNCTION_SUCCESS =0,
	RC_FUNCTION_UNSUCCESS
};

int rtl2832u_remote_control_state=0;
static int SampleNum2TNum[] = 
{
	0,0,0,0,0,				
	1,1,1,1,1,1,1,1,1,			
	2,2,2,2,2,2,2,2,2,			
	3,3,3,3,3,3,3,3,3,			
	4,4,4,4,4,4,4,4,4,			
	5,5,5,5,5,5,5,5,			
	6,6,6,6,6,6,6,6,6,			
	7,7,7,7,7,7,7,7,7,			
	8,8,8,8,8,8,8,8,8,			
	9,9,9,9,9,9,9,9,9,			
	10,10,10,10,10,10,10,10,10,	
	11,11,11,11,11,11,11,11,11,	
	12,12,12,12,12,12,12,12,12,	
	13,13,13,13,13,13,13,13,13,	
	14,14,14,14,14,14,14		
};
//IRRC register table 
static const RT_rc_set_reg_struct p_rtl2832u_rc_initial_table[]= 
{
		{RTD2832U_SYS,RC_USE_DEMOD_CTL1		,0x00,OP_AND,0xfb},
		{RTD2832U_SYS,RC_USE_DEMOD_CTL1		,0x00,OP_AND,0xf7}, 
		{RTD2832U_USB,USB_CTRL			,0x00,OP_OR ,0x20}, 
		{RTD2832U_SYS,SYS_GPD			,0x00,OP_AND,0xf7}, 
		{RTD2832U_SYS,SYS_GPOE			,0x00,OP_OR ,0x08}, 
		{RTD2832U_SYS,SYS_GPO			,0x00,OP_OR ,0x08}, 		
		{RTD2832U_RC,IR_RX_CTRL			,0x20,OP_NO ,0xff},
		{RTD2832U_RC,IR_RX_BUFFER_CTRL		,0x80,OP_NO ,0xff},
		{RTD2832U_RC,IR_RX_IF			,0xff,OP_NO ,0xff},
		{RTD2832U_RC,IR_RX_IE			,0xff,OP_NO ,0xff},
		{RTD2832U_RC,IR_MAX_DURATION0		,0xd0,OP_NO ,0xff},
		{RTD2832U_RC,IR_MAX_DURATION1		,0x07,OP_NO ,0xff},
		{RTD2832U_RC,IR_IDLE_LEN0		,0xc0,OP_NO ,0xff},
		{RTD2832U_RC,IR_IDLE_LEN1		,0x00,OP_NO ,0xff},
		{RTD2832U_RC,IR_GLITCH_LEN		,0x03,OP_NO ,0xff},
		{RTD2832U_RC,IR_RX_CLK			,0x09,OP_NO ,0xff},
		{RTD2832U_RC,IR_RX_CONFIG		,0x1c,OP_NO ,0xff},
		{RTD2832U_RC,IR_MAX_H_Tolerance_LEN	,0x1e,OP_NO ,0xff},
		{RTD2832U_RC,IR_MAX_L_Tolerance_LEN	,0x1e,OP_NO ,0xff},        
		{RTD2832U_RC,IR_RX_CTRL			,0x80,OP_NO ,0xff} 
		
};
	
int rtl2832u_remoto_control_initial_setting(struct dvb_usb_device *d)
{ 
	


	//begin setting
	int ret = RC_FUNCTION_SUCCESS;
	u8 data=0,i=0,NumberOfRcInitialTable=0;


	deb_rc("+rc_%s\n", __FUNCTION__);

	NumberOfRcInitialTable = sizeof(p_rtl2832u_rc_initial_table)/sizeof(RT_rc_set_reg_struct);
	

	for (i=0;i<NumberOfRcInitialTable;i++)
	{	
		switch(p_rtl2832u_rc_initial_table[i].type)
		{
			case RTD2832U_SYS:
			case RTD2832U_USB:
				data=p_rtl2832u_rc_initial_table[i].data;
				if (p_rtl2832u_rc_initial_table[i].op != OP_NO)
				{
					if ( read_usb_sys_char_bytes( d , 
								      p_rtl2832u_rc_initial_table[i].type , 
								      p_rtl2832u_rc_initial_table[i].address,
								      &data , 
								      LEN_1) ) 
					{
						deb_rc("+%s : rc- usb or sys register read error! \n", __FUNCTION__);
						ret=RC_FUNCTION_UNSUCCESS;
						goto error;
					}					
				
					if (p_rtl2832u_rc_initial_table[i].op == OP_AND){
					        data &=  p_rtl2832u_rc_initial_table[i].op_mask;	
					}
					else{//OP_OR
						data |=  p_rtl2832u_rc_initial_table[i].op_mask;
					}			
				}
				
				if ( write_usb_sys_char_bytes( d , 
							      p_rtl2832u_rc_initial_table[i].type , 
							      p_rtl2832u_rc_initial_table[i].address,
							      &data , 
							      LEN_1) ) 
				{
						deb_rc("+%s : rc- usb or sys register write error! \n", __FUNCTION__);
						ret= RC_FUNCTION_UNSUCCESS;
						goto error;
				}
		
			break;
			case RTD2832U_RC:
				data= p_rtl2832u_rc_initial_table[i].data;
				if (p_rtl2832u_rc_initial_table[i].op != OP_NO)
				{
					if ( read_rc_char_bytes( d , 
								 p_rtl2832u_rc_initial_table[i].type , 
								 p_rtl2832u_rc_initial_table[i].address,
								 &data , 
								 LEN_1) ) 
					{
						deb_rc("+%s : rc -ir register read error! \n", __FUNCTION__);
						ret=RC_FUNCTION_UNSUCCESS;
						goto error;
					}					
				
					if (p_rtl2832u_rc_initial_table[i].op == OP_AND){
					        data &=  p_rtl2832u_rc_initial_table[i].op_mask;	
					}
					else{//OP_OR
					    data |=  p_rtl2832u_rc_initial_table[i].op_mask;
					}			
				}
				if ( write_rc_char_bytes( d , 
							      p_rtl2832u_rc_initial_table[i].type , 
							      p_rtl2832u_rc_initial_table[i].address,
							      &data , 
							      LEN_1) ) 
				{
					deb_rc("+%s : rc -ir register write error! \n", __FUNCTION__);
					ret=RC_FUNCTION_UNSUCCESS;
					goto error;
				}

			break;
			default:
				deb_rc("+%s : rc table error! \n", __FUNCTION__);
				ret=RC_FUNCTION_UNSUCCESS;
				goto error;			     	
			break;	
		}	
	}
	rtl2832u_remote_control_state=RC_INSTALL_OK;
	ret=RC_FUNCTION_SUCCESS;
error: 
	deb_rc("-rc_%s ret = %d \n", __FUNCTION__, ret);
	return ret;

	
}


static int frt0(u8* rt_uccode,u8 byte_num,u8 *p_uccode)
{
	u8 *pCode = rt_uccode;
	int TNum =0;
	u8   ucBits[frt0_bits_num];
	u8  i=0,state=WAITING_6T;
	int  LastTNum = 0,CurrentBit = 0;
	int ret=RC_FUNCTION_SUCCESS;
	u8 highestBit = 0,lowBits=0;
	u32 scancode=0;
	
	if(byte_num < frt0_para1){
		deb_rc("Bad rt uc code received, byte_num is error\n");
		ret= RC_FUNCTION_UNSUCCESS;
		goto error;
	}
	while(byte_num > 0)
	{

		highestBit = (*pCode)&0x80;
		lowBits = (*pCode) & 0x7f;
		TNum=SampleNum2TNum[lowBits];
		
		if(highestBit != 0)	TNum = -TNum;

		pCode++;
		byte_num--;

		if(TNum <= -6)	 state = WAITING_6T;

		if(WAITING_6T == state)
		{
			if(TNum <= -6)	state = WAITING_2T_AFTER_6T;
		}
		else if(WAITING_2T_AFTER_6T == state)
		{
			if(2 == TNum)	
			{
				state = WAITING_NORMAL_BITS;
				LastTNum   = 0;
				CurrentBit = 0;
			}
			else 	state = WAITING_6T;
		} 
		else if(WAITING_NORMAL_BITS == state)
		{
			if(0 == LastTNum)	LastTNum = TNum;
			else	{
				if(LastTNum < 0)	ucBits[CurrentBit]=1;
				else			ucBits[CurrentBit]=0;

				CurrentBit++;

				if(CurrentBit >= frt0_bits_num)	{
 					deb_rc("Bad frame received, bits num is error\n");
					CurrentBit = frt0_bits_num -1 ;

				}
				if(TNum > 3)	{
						for(i=0;i<frt0_para2;i++){
							if (ucBits[i+frt0_para4])	scancode  |= (0x01 << (frt0_para2-i-1));
						}	
				}
				else{
					LastTNum += TNum;	
				}							
			}			
		}	

	}
	p_uccode[0]=(u8)((scancode>>24)  &  frt0_BITS_mask0);
	p_uccode[1]=(u8)((scancode>>16)  &  frt0_BITS_mask1);
	p_uccode[2]=(u8)((scancode>>8)  & frt0_BITS_mask2);
	p_uccode[3]=(u8)((scancode>>0)  & frt0_BITS_mask3);
	
	deb_rc("-rc_%s 3::rc6:%x %x %x %x \n", __FUNCTION__,p_uccode[0],p_uccode[1],p_uccode[2],p_uccode[3]);
	ret= RC_FUNCTION_SUCCESS;
error:

	return ret;
}


static int frt1(u8* rt_uccode,u8 byte_num,u8 *p_uccode)
{
	u8 *pCode = rt_uccode;
	u8  ucBits[frt1_bits_num];
	u8 i=0,CurrentBit=0,index=0;
	u32 scancode=0;
	int ret= RC_FUNCTION_SUCCESS;

	deb_rc("+rc_%s \n", __FUNCTION__);
	if(byte_num < frt1_para1)	{
		deb_rc("Bad rt uc code received, byte_num = %d is error\n",byte_num);
		ret = RC_FUNCTION_UNSUCCESS;
		goto error;
	}
	
	memset(ucBits,0,frt1_bits_num);		

	for(i = 0; i < byte_num; i++)	{
		if ((pCode[i] & frt1_para2)< frt1_para3)    index=frt1_para5 ;   
		else 					    index=frt1_para6 ;  

		ucBits[i]= (pCode[i] & 0x80) + index;
	}
	if(ucBits[0] !=frt1_para_uc_1 && ucBits[0] !=frt1_para_uc_2 )   {ret= RC_FUNCTION_UNSUCCESS; goto error;}

	if(ucBits[1] !=frt1_para5  && ucBits[1] !=frt1_para6)   	{ret= RC_FUNCTION_UNSUCCESS;goto error;}

	if(ucBits[2] >= frt1_para_uc_1)  				ucBits[2] -= 0x01;
	else			 					{ret= RC_FUNCTION_UNSUCCESS;goto error;}

	
   	i = 0x02;
	CurrentBit = 0x00;

	while(i < byte_num-1)
	{
		if(CurrentBit >= 32)	{
			break;
		}	

		if((ucBits[i] & 0x0f) == 0x0)	{
			i++;
			continue;
		}
		if(ucBits[i++] == 0x81)	{
						if(ucBits[i] >=0x01)	{
							scancode |= 0x00 << (31 - CurrentBit++);
						}	 							
		}
		else	{
				if(ucBits[i] >=0x81)	{
					scancode |= 0x01 << (31 - CurrentBit++); 
				}
		}
				
		ucBits[i] -= 0x01;
		continue;
	}
	p_uccode[3]=(u8)((scancode>>16)  &  frt1_bits_mask3);
	p_uccode[2]=(u8)((scancode>>24)  &  frt1_bits_mask2);
	p_uccode[1]=(u8)((scancode>>8)   &  frt1_bits_mask1);
	p_uccode[0]=(u8)((scancode>>0)   &  frt1_bits_mask0);

	
	deb_rc("-rc_%s rc5:%x %x %x %x -->scancode =%x\n", __FUNCTION__,p_uccode[0],p_uccode[1],p_uccode[2],p_uccode[3],scancode);
	ret= RC_FUNCTION_SUCCESS;
error:
	return ret;
}

static int frt2(u8* rt_uccode,u8 byte_num,u8 *p_uccode)
{
	u8 *pCode = rt_uccode;
	u8  i=0;
	u32 scancode=0;
	u8  out_io=0;
			
	int ret= RC_FUNCTION_SUCCESS;

	deb_rc("+rc_%s \n", __FUNCTION__);

	if(byte_num < frt2_para1)  				goto error;
    	if(pCode[0] != frt2_para2) 				goto error;
	if((pCode[1] <frt2_para3 )||(pCode[1] >frt2_para4))	goto error;	


	if( (pCode[2] <frt2_para5 ) && (pCode[2] >frt2_para6) )   
	{ 

		if( (pCode[3] <frt2_para7 ) && (pCode[3] >frt2_para8 ) &&(pCode[4]==frt2_para9 ))  scancode=0xffff;
		else goto error;

	}
	else if( (pCode[2] <frt2_para10  ) && (pCode[2] >frt2_para11 ) ) 
	{

	 	for (i = 3; i <68; i++)
		{  
                        if ((i% 2)==1)
			{
				if( (pCode[i]>frt2_para7 ) || (pCode[i] <frt2_para8 ) )  
				{ 
					deb_rc("Bad rt uc code received[4]\n");
					ret= RC_FUNCTION_UNSUCCESS;
					goto error;
				}			
			}
			else
			{
				if(pCode[i]<frt2_para12  )  out_io=0;
				else			    out_io=1;
				scancode |= (out_io << (31 -(i-4)/2) );
			}
		} 



	}
	else  	goto error;
	deb_rc("-rc_%s nec:%x\n", __FUNCTION__,scancode);
	p_uccode[0]=(u8)((scancode>>24)  &  frt2_bits_mask0);
	p_uccode[1]=(u8)((scancode>>16)  &  frt2_bits_mask1);
	p_uccode[2]=(u8)((scancode>>8)   &  frt2_bits_mask2);
	p_uccode[3]=(u8)((scancode>>0)   &  frt2_bits_mask3);
	ret= RC_FUNCTION_SUCCESS;
error:	

	return ret;
}
#define receiveMaskFlag1  0x80
#define receiveMaskFlag2  0x03
#define flush_step_Number 0x05
#define rt_code_len       0x80  

static int rtl2832u_rc_query(struct dvb_usb_device *d, u32 *event, int *state)
{

	static const RT_rc_set_reg_struct p_flush_table1[]={
		{RTD2832U_RC,IR_RX_CTRL			,0x20,OP_NO ,0xff},
		{RTD2832U_RC,IR_RX_BUFFER_CTRL		,0x80,OP_NO ,0xff},
		{RTD2832U_RC,IR_RX_IF			,0xff,OP_NO ,0xff},
		{RTD2832U_RC,IR_RX_IE			,0xff,OP_NO ,0xff},        
		{RTD2832U_RC,IR_RX_CTRL			,0x80,OP_NO ,0xff} 

	};
	static const RT_rc_set_reg_struct p_flush_table2[]={
		{RTD2832U_RC,IR_RX_IF			,0x03,OP_NO ,0xff},
		{RTD2832U_RC,IR_RX_BUFFER_CTRL		,0x80,OP_NO ,0xff},	
		{RTD2832U_RC,IR_RX_CTRL			,0x80,OP_NO ,0xff} 

	};


	u8  data=0,i=0,byte_count=0;
	int ret=0;
	u8  rt_u8_code[rt_code_len];
	u8  ucode[4];
	u16 scancode=0;

	deb_rc("+%s \n", __FUNCTION__);
	if (dvb_use_rtl2832u_rc_mode >= MAX_RC_PROTOCOL_NUM) 	
	{
		
		deb_rc("%s : dvb_use_rtl2832u_rc_mode=%d \n", __FUNCTION__,dvb_use_rtl2832u_rc_mode);		
		return 0;
	}

	if(rtl2832u_remote_control_state == RC_NO_SETTING)
	{
                deb_rc("%s : IrDA Initial Setting rtl2832u_remote_control_state=%d\n", __FUNCTION__,rtl2832u_remote_control_state);
		ret=rtl2832u_remoto_control_initial_setting(d);	

	}
	if ( read_rc_char_bytes( d ,RTD2832U_RC, IR_RX_IF,&data ,LEN_1) ) 
	{
		ret=-1;
		deb_rc("%s : Read IrDA IF is failed\n", __FUNCTION__);	
		goto error;
	}
	/* debug */
	if (data != 0)
	{
		deb_rc("%s : IR_RX_IF= 0x%x\n", __FUNCTION__,data);	
	}


	if (!(data & receiveMaskFlag1))
	{
		ret =0 ;
		goto error;
	}	
	
	if (data & receiveMaskFlag2)
	{		
			/* delay */
			msleep(287);

			if ( read_rc_char_bytes( d ,RTD2832U_RC,IR_RX_BC,&byte_count ,LEN_1) ) 
			{
				deb_rc("%s : rc -ir register read error! \n", __FUNCTION__);
				ret=-1;
				goto error;
			}		
			if (byte_count == 0 )  
			{	
				//ret=0;
				goto error;
			}
				
			if ((byte_count%LEN_2) == 1)   byte_count+=LEN_1;	
			if (byte_count > rt_code_len)  byte_count=rt_code_len;	
					
			memset(rt_u8_code,0,rt_code_len);
			deb_rc("%s : byte_count= %d type = %d \n", __FUNCTION__,byte_count,dvb_use_rtl2832u_rc_mode);
			if ( read_rc_char_bytes( d ,RTD2832U_RC,IR_RX_BUF,rt_u8_code ,0x80) ) 
			{
				deb_rc("%s : rc -ir register read error! \n", __FUNCTION__);
				ret=-1;
				goto error;
			}
				
			memset(ucode,0,4);
		
			
			ret=0;
			if (dvb_use_rtl2832u_rc_mode == 0)		ret =frt0(rt_u8_code,byte_count,ucode);
			else if (dvb_use_rtl2832u_rc_mode == 1)		ret =frt1(rt_u8_code,byte_count,ucode);
			else if (dvb_use_rtl2832u_rc_mode== 2)		ret =frt2(rt_u8_code,byte_count,ucode);	
			else  
			{
					//deb_rc("%s : rc - unknow rc protocol set ! \n", __FUNCTION__);
					ret=-1;
					goto error;	
			}
			
			if((ret != RC_FUNCTION_SUCCESS) || (ucode[0] ==0 && ucode[1] ==0 && ucode[2] ==0 && ucode[3] ==0))   
 			{
					//deb_rc("%s : rc-rc is error scan code ! %x %x %x %x \n", __FUNCTION__,ucode[0],ucode[1],ucode[2],ucode[3]);
					ret=-1;
					goto error;	
			}
			scancode=(ucode[2]<<8) | ucode[3] ;
			deb_info("-%s scan code %x %x %x %x,(0x%x) -- len=%d\n", __FUNCTION__,ucode[0],ucode[1],ucode[2],ucode[3],scancode,byte_count);
			////////// map/////////////////////////////////////////////////////////////////////////////////////////////////////
			for (i = 0; i < ARRAY_SIZE(rtl2832u_rc_keys_map_table); i++) {
				if(rtl2832u_rc_keys_map_table[i].scancode == scancode ){
#ifdef V4L2_REFACTORED_RC_CODE
					*event = rtl2832u_rc_keys_map_table[i].keycode;
#else
					*event = rtl2832u_rc_keys_map_table[i].event;
#endif
					*state = REMOTE_KEY_PRESSED;
					deb_rc("%s : map number = %d \n", __FUNCTION__,i);	
					break;
				}		
				
			}

			memset(rt_u8_code,0,rt_code_len);
			byte_count=0;
			for (i=0;i<3;i++){
				data= p_flush_table2[i].data;
				if ( write_rc_char_bytes( d ,RTD2832U_RC, p_flush_table2[i].address,&data,LEN_1) ) {
					deb_rc("+%s : rc -ir register write error! \n", __FUNCTION__);
					ret=-1;
					goto error;
				}		

			}

			ret =0;	
			return ret;
	}
error:
			memset(rt_u8_code,0,rt_code_len);
			byte_count=0;
			for (i=0;i<flush_step_Number;i++){
				data= p_flush_table1[i].data;
				if ( write_rc_char_bytes( d ,RTD2832U_RC, p_flush_table1[i].address,&data,LEN_1) ) {
					deb_rc("+%s : rc -ir register write error! \n", __FUNCTION__);
					ret=-1;
					break;
				}		

			}			   			
			ret =0;    //must return 0   
			return ret;

}

static int rtl2832u_streaming_ctrl(struct dvb_usb_adapter *adap , int onoff)
{
	u8 data[2];	
	//3 to avoid  scanning  channels loss
	if(onoff)
	{
		data[0] = data[1] = 0;		
		if ( write_usb_sys_char_bytes( adap->dev , RTD2832U_USB , USB_EPA_CTL , data , 2) ) goto error;				
	}
	else
	{
		data[0] = 0x10;	//3stall epa, set bit 4 to 1
		data[1] = 0x02;	//3reset epa, set bit 9 to 1
		if ( write_usb_sys_char_bytes( adap->dev , RTD2832U_USB , USB_EPA_CTL , data , 2) ) goto error;		
	}

	return 0;
error: 
	return -1;
}


static int rtl2832u_frontend_attach(struct dvb_usb_adapter *adap)
{
#ifdef V4L2_REFACTORED_MFE_CODE
	adap->fe_adap[0].fe = rtl2832u_fe_attach(adap->dev);
#else
	adap->fe = rtl2832u_fe_attach(adap->dev);
#endif
	return 0;
}


static void rtl2832u_usb_disconnect(struct usb_interface *intf)
{
	try_module_get(THIS_MODULE);
	dvb_usb_device_exit(intf);	
}


static struct dvb_usb_device_properties rtl2832u_1st_properties;
static struct dvb_usb_device_properties rtl2832u_2nd_properties;
static struct dvb_usb_device_properties rtl2832u_3th_properties;
static struct dvb_usb_device_properties rtl2832u_4th_properties;
static struct dvb_usb_device_properties rtl2832u_5th_properties;
static struct dvb_usb_device_properties rtl2832u_6th_properties;
static struct dvb_usb_device_properties rtl2832u_7th_properties;
static struct dvb_usb_device_properties rtl2832u_8th_properties;
static struct dvb_usb_device_properties rtl2832u_9th_properties;


//#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26)
static int rtl2832u_usb_probe(struct usb_interface *intf,
		const struct usb_device_id *id)
{
	/* Thats avoids bugs with 2 instances of driver that operate same hardware */
	/* https://gitorious.org/rtl2832/rtl2832/commit/5495b3fda9e2c3bf4feef5d5751f6f2343380ea9 */
	if (!intf->altsetting->desc.bNumEndpoints)
		return -ENODEV;

	if ( ( 0== dvb_usb_device_init(intf,&rtl2832u_1st_properties,THIS_MODULE,NULL,adapter_nr) )||
		( 0== dvb_usb_device_init(intf,&rtl2832u_2nd_properties,THIS_MODULE,NULL,adapter_nr) ) ||
		( 0== dvb_usb_device_init(intf,&rtl2832u_3th_properties,THIS_MODULE,NULL,adapter_nr) ) ||
		( 0== dvb_usb_device_init(intf,&rtl2832u_4th_properties,THIS_MODULE,NULL,adapter_nr) ) ||
		( 0== dvb_usb_device_init(intf,&rtl2832u_5th_properties,THIS_MODULE,NULL,adapter_nr) ) ||
		( 0== dvb_usb_device_init(intf,&rtl2832u_6th_properties,THIS_MODULE,NULL,adapter_nr) ) ||
		( 0== dvb_usb_device_init(intf,&rtl2832u_7th_properties,THIS_MODULE,NULL,adapter_nr) ) ||
		( 0== dvb_usb_device_init(intf,&rtl2832u_8th_properties,THIS_MODULE,NULL,adapter_nr) ) ||
		( 0== dvb_usb_device_init(intf,&rtl2832u_9th_properties,THIS_MODULE,NULL,adapter_nr) ) )
		return 0;

	return -ENODEV;
}

static struct usb_device_id rtl2832u_usb_table [] = {								
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2832_WARM) },		// 0			
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2838_WARM) },		// 1
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2836_WARM) },		// 2
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2839_WARM) },		// 3
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2840_WARM) },		// 4
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2841_WARM) },		// 5
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2834_WARM) },		// 6
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2837_WARM) },		// 7
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2820_WARM) },		// 8
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2821_WARM) },		// 9
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2822_WARM) },		// 10
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2823_WARM) },		// 11
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2810_WARM) },		// 12
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2811_WARM) },		// 13
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2824_WARM) },		// 14
	{ USB_DEVICE(USB_VID_REALTEK, USB_PID_RTL2825_WARM) },		// 15

	{ USB_DEVICE(USB_VID_DEXATEK, USB_PID_DEXATEK_1101) },		// 16	
	{ USB_DEVICE(USB_VID_DEXATEK, USB_PID_DEXATEK_1102) },		// 17
	{ USB_DEVICE(USB_VID_DEXATEK, USB_PID_DEXATEK_1103) },		// 18	
	{ USB_DEVICE(USB_VID_DEXATEK, USB_PID_DEXATEK_1104) },		// 19
	{ USB_DEVICE(USB_VID_DEXATEK, USB_PID_DEXATEK_1105) },		// 20	
	{ USB_DEVICE(USB_VID_DEXATEK, USB_PID_DEXATEK_1106) },		// 21
	{ USB_DEVICE(USB_VID_DEXATEK, USB_PID_DEXATEK_1107) },		// 22	
	{ USB_DEVICE(USB_VID_DEXATEK, USB_PID_DEXATEK_1108) },		// 23
	{ USB_DEVICE(USB_VID_DEXATEK, USB_PID_DEXATEK_2101) },		// 24	
	{ USB_DEVICE(USB_VID_DEXATEK, USB_PID_DEXATEK_8202) },		// 25
	{ USB_DEVICE(USB_VID_DEXATEK, USB_PID_DEXATEK_9201) },		// 26	
	{ USB_DEVICE(USB_VID_DEXATEK, USB_PID_DEXATEK_3103) },		// 27
	{ USB_DEVICE(USB_VID_DEXATEK, USB_PID_DEXATEK_9202) },		// 28

	{ USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_00A9)},	// 29
	{ USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_00B3)},	// 30

	{ USB_DEVICE(USB_VID_AZUREWAVE_2, USB_PID_AZUREWAVE_3234) },	// 31
	{ USB_DEVICE(USB_VID_AZUREWAVE_2, USB_PID_AZUREWAVE_3274) },	// 32
	{ USB_DEVICE(USB_VID_AZUREWAVE_2, USB_PID_AZUREWAVE_3282) },	// 33

	{ USB_DEVICE(USB_VID_THP, USB_PID_THP_5013)},				// 34
	{ USB_DEVICE(USB_VID_THP, USB_PID_THP_5020)},				// 35
	{ USB_DEVICE(USB_VID_THP, USB_PID_THP_5026)},				// 36

	{ USB_DEVICE(USB_VID_KWORLD_1ST, USB_PID_KWORLD_D393) },	// 37
	{ USB_DEVICE(USB_VID_KWORLD_1ST, USB_PID_KWORLD_D394) },	// 38
	{ USB_DEVICE(USB_VID_KWORLD_1ST, USB_PID_KWORLD_D395) },	// 39
	{ USB_DEVICE(USB_VID_KWORLD_1ST, USB_PID_KWORLD_D396) },	// 40
	{ USB_DEVICE(USB_VID_KWORLD_1ST, USB_PID_KWORLD_D397) },	// 41
	{ USB_DEVICE(USB_VID_KWORLD_1ST, USB_PID_KWORLD_D398) },	// 42
	{ USB_DEVICE(USB_VID_KWORLD_1ST, USB_PID_KWORLD_D39A) },	// 43
	{ USB_DEVICE(USB_VID_KWORLD_1ST, USB_PID_KWORLD_D39B) },	// 44
	{ USB_DEVICE(USB_VID_KWORLD_1ST, USB_PID_KWORLD_D39C) },	// 45
	{ USB_DEVICE(USB_VID_KWORLD_1ST, USB_PID_KWORLD_D39E) },	// 46
	{ USB_DEVICE(USB_VID_KWORLD_1ST, USB_PID_KWORLD_E77B) },	// 47
	{ USB_DEVICE(USB_VID_KWORLD_1ST, USB_PID_KWORLD_D3A1) },	// 48
	{ USB_DEVICE(USB_VID_KWORLD_1ST, USB_PID_KWORLD_D3A4) },	// 49
	{ USB_DEVICE(USB_VID_KWORLD_1ST, USB_PID_KWORLD_E41D) },	// 50
	
	{ USB_DEVICE(USB_VID_GTEK, USB_PID_GTEK_WARM_0837)},		// 51
	{ USB_DEVICE(USB_VID_GTEK, USB_PID_GTEK_WARM_A803)},		// 52
	{ USB_DEVICE(USB_VID_GTEK, USB_PID_GTEK_WARM_B803)},		// 53
	{ USB_DEVICE(USB_VID_GTEK, USB_PID_GTEK_WARM_C803)},		// 54
	{ USB_DEVICE(USB_VID_GTEK, USB_PID_GTEK_WARM_D803)},		// 55
	{ USB_DEVICE(USB_VID_GTEK, USB_PID_GTEK_WARM_C280)},		// 56
	{ USB_DEVICE(USB_VID_GTEK, USB_PID_GTEK_WARM_D286)},		// 57
	{ USB_DEVICE(USB_VID_GTEK, USB_PID_GTEK_WARM_0139)},		// 58
	{ USB_DEVICE(USB_VID_GTEK, USB_PID_GTEK_WARM_A683)},		// 59

	{ USB_DEVICE(USB_VID_LEADTEK, USB_PID_LEADTEK_WARM_1)},		// 60			
	{ USB_DEVICE(USB_VID_LEADTEK, USB_PID_LEADTEK_WARM_2)},		// 61

	{ USB_DEVICE(USB_VID_YUAN, USB_PID_YUAN_WARM)},			//62
        { USB_DEVICE(USB_VID_YUAN, USB_PID_YUAN_WARM80)},		//63
	{ USB_DEVICE(USB_VID_YUAN, USB_PID_YUAN_WARM84)},	 	//64

	{ USB_DEVICE(USB_VID_COMPRO, USB_PID_COMPRO_WARM_0620)},	// 65			
	{ USB_DEVICE(USB_VID_COMPRO, USB_PID_COMPRO_WARM_0630)},	// 66			
	{ USB_DEVICE(USB_VID_COMPRO, USB_PID_COMPRO_WARM_0640)},	// 67			
	{ USB_DEVICE(USB_VID_COMPRO, USB_PID_COMPRO_WARM_0650)},	// 68			
	{ USB_DEVICE(USB_VID_COMPRO, USB_PID_COMPRO_WARM_0680)},	// 69			
	{ USB_DEVICE(USB_VID_COMPRO, USB_PID_COMPRO_WARM_9580)},	// 70			
	{ USB_DEVICE(USB_VID_COMPRO, USB_PID_COMPRO_WARM_9550)},	// 71			
	{ USB_DEVICE(USB_VID_COMPRO, USB_PID_COMPRO_WARM_9540)},	// 72			
	{ USB_DEVICE(USB_VID_COMPRO, USB_PID_COMPRO_WARM_9530)},	// 73  71																						//------rtl2832u_6th_properties(6)
	{ USB_DEVICE(USB_VID_COMPRO,  USB_PID_COMPRO_WARM_9520)},	// 74
		
	{ USB_DEVICE(USB_VID_GOLDENBRIDGE, USB_PID_GOLDENBRIDGE_WARM)},	//75

	{ USB_DEVICE(USB_VID_LEADTEK, USB_PID_WINFAST_DTV_DONGLE_MINI)},	// 76

	{ USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_00D3)},		// 77
	{ USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_00D4)},		// 78
	{ USB_DEVICE(USB_VID_TERRATEC, USB_PID_TERRATEC_00E0)},		// 79

	{ 0 },
};


MODULE_DEVICE_TABLE(usb, rtl2832u_usb_table);

static struct dvb_usb_device_properties rtl2832u_1st_properties = {

	.num_adapters = 1,
	.adapter = 
	{
		{
#ifndef NO_FE_IOCTL_OVERRIDE
			.fe_ioctl_override = rtl2832_fe_ioctl_override,
#endif
#ifdef V4L2_REFACTORED_MFE_CODE
			.num_frontends = 1,
			.fe = {{
#endif
			.streaming_ctrl = rtl2832u_streaming_ctrl,
			.frontend_attach = rtl2832u_frontend_attach,
			//parameter for the MPEG2-data transfer 
			.stream = 
			{
				.type = USB_BULK,
				.count = RTD2831_URB_NUMBER,
				.endpoint = 0x01,		//data pipe
				.u = 
				{
					.bulk = 
					{
						.buffersize = RTD2831_URB_SIZE,
					}
				}
			},
#ifdef V4L2_REFACTORED_MFE_CODE
			}},
#endif
		}
	},
	
	//remote control
	.rc.legacy = {
#ifdef V4L2_REFACTORED_RC_CODE
		.rc_map_table = rtl2832u_rc_keys_map_table,             //user define key map
		.rc_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table), //user define key map size	
#else
		.rc_key_map       = rtl2832u_rc_keys_map_table,			//user define key map
		.rc_key_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table),	//user define key map size	
#endif	
		.rc_query         = rtl2832u_rc_query,				//use define quary function
		.rc_interval      = RT_RC_POLLING_INTERVAL_TIME_MS,		
	},
	
	.num_device_descs = 9,
	.devices = {
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[0], NULL },
		},
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[1], NULL },
		},
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[2], NULL },
		},
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[3], NULL },
		},
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[4], NULL },
		},
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[5], NULL },
		},		
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[6], NULL },
		},		
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[7], NULL },
		},		
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[8], NULL },
		},
		{ NULL },
	}
};


static struct dvb_usb_device_properties rtl2832u_2nd_properties = {

	.num_adapters = 1,
	.adapter = 
	{
		{
#ifndef NO_FE_IOCTL_OVERRIDE
			.fe_ioctl_override = rtl2832_fe_ioctl_override,
#endif
#ifdef V4L2_REFACTORED_MFE_CODE
			.num_frontends = 1,
			.fe = {{
#endif
			.streaming_ctrl = rtl2832u_streaming_ctrl,
			.frontend_attach = rtl2832u_frontend_attach,
			//parameter for the MPEG2-data transfer 
			.stream = 
			{
				.type = USB_BULK,
				.count = RTD2831_URB_NUMBER,
				.endpoint = 0x01,		//data pipe
				.u = 
				{
					.bulk = 
					{
						.buffersize = RTD2831_URB_SIZE,
					}
				}
			},
#ifdef V4L2_REFACTORED_MFE_CODE
			}},
#endif
		}
	},
	//remote control
	.rc.legacy = {
#ifdef V4L2_REFACTORED_RC_CODE
		.rc_map_table = rtl2832u_rc_keys_map_table,             //user define key map
		.rc_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table), //user define key map size	
#else
		.rc_key_map       = rtl2832u_rc_keys_map_table,			//user define key map
		.rc_key_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table),	//user define key map size	
#endif	
		.rc_query         = rtl2832u_rc_query,				//use define query function
		.rc_interval      = RT_RC_POLLING_INTERVAL_TIME_MS,		
	},
	
	.num_device_descs = 9,
	.devices = {
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[9], NULL },
		},
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[10], NULL },
		},
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[11], NULL },
		},
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[12], NULL },
		},
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[13], NULL },
		},
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[14], NULL },
		},
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[15], NULL },
		},
		{ .name = "DK DONGLE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[16], NULL },
		},
		{ .name = "DK DONGLE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[17], NULL },
		},
		{ NULL },
	}
};



static struct dvb_usb_device_properties rtl2832u_3th_properties = {

	.num_adapters = 1,
	.adapter = 
	{
		{
#ifndef NO_FE_IOCTL_OVERRIDE
			.fe_ioctl_override = rtl2832_fe_ioctl_override,
#endif
#ifdef V4L2_REFACTORED_MFE_CODE
			.num_frontends = 1,
			.fe = {{
#endif
			.streaming_ctrl = rtl2832u_streaming_ctrl,
			.frontend_attach = rtl2832u_frontend_attach,
			//parameter for the MPEG2-data transfer 
			.stream = 
			{
				.type = USB_BULK,
				.count = RTD2831_URB_NUMBER,
				.endpoint = 0x01,		//data pipe
				.u = 
				{
					.bulk = 
					{
						.buffersize = RTD2831_URB_SIZE,
					}
				}
			},
#ifdef V4L2_REFACTORED_MFE_CODE
			}},
#endif
		}
	},

	//remote control
	.rc.legacy = {
#ifdef V4L2_REFACTORED_RC_CODE
		.rc_map_table = rtl2832u_rc_keys_map_table,             //user define key map
		.rc_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table), //user define key map size	
#else
		.rc_key_map       = rtl2832u_rc_keys_map_table,			//user define key map
		.rc_key_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table),	//user define key map size	
#endif	
		.rc_query         = rtl2832u_rc_query,				//use define query function
		.rc_interval      = RT_RC_POLLING_INTERVAL_TIME_MS,		
	},

	.num_device_descs = 9,
	.devices = {
		{ .name = "DK DONGLE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[18], NULL },
		},
		{ .name = "DK DONGLE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[19], NULL },
		},
		{ .name = "DK DONGLE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[20], NULL },
		},
		{
		  .name = "DK DONGLE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[21], NULL },
		},
		{
		  .name = "DK DONGLE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[22], NULL },
		},
		{
		  .name = "DK DONGLE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[23], NULL },
		},
		{
		  .name = "DK DONGLE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[24], NULL },
		},
		{
		  .name = "DK DONGLE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[25], NULL },
		},
		{
		  .name = "DK DONGLE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[26], NULL },
		}
	}
};


static struct dvb_usb_device_properties rtl2832u_4th_properties = {

	.num_adapters = 1,
	.adapter = 
	{
		{
#ifndef NO_FE_IOCTL_OVERRIDE
			.fe_ioctl_override = rtl2832_fe_ioctl_override,
#endif
#ifdef V4L2_REFACTORED_MFE_CODE
			.num_frontends = 1,
			.fe = {{
#endif
			.streaming_ctrl = rtl2832u_streaming_ctrl,
			.frontend_attach = rtl2832u_frontend_attach,
			//parameter for the MPEG2-data transfer 
			.stream = 
			{
				.type = USB_BULK,
				.count = RTD2831_URB_NUMBER,
				.endpoint = 0x01,		//data pipe
				.u = 
				{
					.bulk = 
					{
						.buffersize = RTD2831_URB_SIZE,
					}
				}
			},
#ifdef V4L2_REFACTORED_MFE_CODE
			}},
#endif
		}
	},

	//remote control
	.rc.legacy = {
#ifdef V4L2_REFACTORED_RC_CODE
		.rc_map_table = rtl2832u_rc_keys_map_table,             //user define key map
		.rc_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table), //user define key map size	
#else
		.rc_key_map       = rtl2832u_rc_keys_map_table,			//user define key map
		.rc_key_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table),	//user define key map size	
#endif	
		.rc_query         = rtl2832u_rc_query,				//use define query function
		.rc_interval      = RT_RC_POLLING_INTERVAL_TIME_MS,		
	},

	.num_device_descs = 12,
	.devices = {
		{ .name = "DK DONGLE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[27], NULL },
		},
		{ .name = "DK DONGLE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[28], NULL },
		},
		{ .name = "Terratec Cinergy T Stick Black",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[29], NULL },
		},
		{
		  .name = "Terratec Cinergy T Stick Black",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[30], NULL },
		},
		{
		  .name = "USB DVB-T Device",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[31], NULL },
		},
		{
		  .name = "USB DVB-T Device",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[32], NULL },
		},
		
		{
		  .name = "USB DVB-T Device",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[33], NULL },
		},
				
		{
		  .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[34], NULL },
		},
		
		{
		  .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[35], NULL },
		},				
		
		{
		  .name = "Terratec Cinergy T Stick RC (Rev.3)",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[77], NULL },
		},

		{
		  .name = "Terratec Cinergy T Stick BLACK (Rev.2)",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[78], NULL },
		},

		{
		  .name = "Terratec Noxon DAB Stick (Rev.2)",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[79], NULL },
		},
	}
};

static struct dvb_usb_device_properties rtl2832u_5th_properties = {

	.num_adapters = 1,
	.adapter = 
	{
		{
#ifndef NO_FE_IOCTL_OVERRIDE
			.fe_ioctl_override = rtl2832_fe_ioctl_override,
#endif
#ifdef V4L2_REFACTORED_MFE_CODE
			.num_frontends = 1,
			.fe = {{
#endif
			.streaming_ctrl = rtl2832u_streaming_ctrl,
			.frontend_attach = rtl2832u_frontend_attach,
			//parameter for the MPEG2-data transfer 
			.stream = 
			{
				.type = USB_BULK,
				.count = RTD2831_URB_NUMBER,
				.endpoint = 0x01,		//data pipe
				.u = 
				{
					.bulk = 
					{
						.buffersize = RTD2831_URB_SIZE,
					}
				}
			},
#ifdef V4L2_REFACTORED_MFE_CODE
			}},
#endif
		}
	},

	//remote control
	.rc.legacy = {
#ifdef V4L2_REFACTORED_RC_CODE
		.rc_map_table = rtl2832u_rc_keys_map_table,             //user define key map
		.rc_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table), //user define key map size	
#else
		.rc_key_map       = rtl2832u_rc_keys_map_table,			//user define key map
		.rc_key_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table),	//user define key map size	
#endif	
		.rc_query         = rtl2832u_rc_query,				//use define query function
		.rc_interval      = RT_RC_POLLING_INTERVAL_TIME_MS,		
	},

	.num_device_descs = 9,
	.devices = {
		{ .name = "RTL2832U DVB-T USB DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[36], NULL },
		},
		{ .name = "USB DVB-T DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[37], NULL },
		},
		{ .name = "USB DVB-T DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[38], NULL },
		},
		{
		  .name = "USB DVB-T DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[39], NULL },
		},
		{
		  .name = "USB DVB-T DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[40], NULL },
		},
		{
		  .name = "USB DVB-T DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[41], NULL },
		},
		
		{
		  .name = "USB DVB-T DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[42], NULL },
		},
				
		{
		  .name = "USB DVB-T DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[43], NULL },
		},
		
		{
		  .name = "USB DVB-T DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[44], NULL },
		},				
		
		
	}
};

static struct dvb_usb_device_properties rtl2832u_6th_properties = {

	.num_adapters = 1,
	.adapter = 
	{
		{
#ifndef NO_FE_IOCTL_OVERRIDE
			.fe_ioctl_override = rtl2832_fe_ioctl_override,
#endif
#ifdef V4L2_REFACTORED_MFE_CODE
			.num_frontends = 1,
			.fe = {{
#endif
			.streaming_ctrl = rtl2832u_streaming_ctrl,
			.frontend_attach = rtl2832u_frontend_attach,
			//parameter for the MPEG2-data transfer 
			.stream = 
			{
				.type = USB_BULK,
				.count = RTD2831_URB_NUMBER,
				.endpoint = 0x01,		//data pipe
				.u = 
				{
					.bulk = 
					{
						.buffersize = RTD2831_URB_SIZE,
					}
				}
			},
#ifdef V4L2_REFACTORED_MFE_CODE
			}},
#endif
		}
	},

	 /*remote control*/
	.rc.legacy = {
#ifdef V4L2_REFACTORED_RC_CODE
		.rc_map_table = rtl2832u_rc_keys_map_table,             //user define key map
		.rc_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table), //user define key map size	
#else
		.rc_key_map       = rtl2832u_rc_keys_map_table,			//user define key map
		.rc_key_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table),	//user define key map size	
#endif	
		.rc_query         = rtl2832u_rc_query,				//use define query function
		.rc_interval      = RT_RC_POLLING_INTERVAL_TIME_MS,		
	},

	.num_device_descs = 9,
	.devices = {
		{ .name = "USB DVB-T DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[45], NULL },
		},
		{ .name = "USB DVB-T DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[46], NULL },
		},
		{ .name = "USB DVB-T DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[47], NULL },
		},
		{
		  .name ="USB DVB-T DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[48], NULL },
		},
		{
		  .name = "USB DVB-T DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[49], NULL },
		},
		{
		  .name = "USB DVB-T DEVICE",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[50], NULL },
		},
		{
		  .name ="DVB-T TV Stick",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[51], NULL },
		},
		{
		  .name = "DVB-T TV Stick",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[52], NULL },
		},
		{
		  .name = "DVB-T TV Stick",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[53], NULL },
		},
		
		{ NULL },				

		
	}
};

static struct dvb_usb_device_properties rtl2832u_7th_properties = {

	.num_adapters = 1,
	.adapter = 
	{
		{
#ifndef NO_FE_IOCTL_OVERRIDE
			.fe_ioctl_override = rtl2832_fe_ioctl_override,
#endif
#ifdef V4L2_REFACTORED_MFE_CODE
			.num_frontends = 1,
			.fe = {{
#endif
			.streaming_ctrl = rtl2832u_streaming_ctrl,
			.frontend_attach = rtl2832u_frontend_attach,
			//parameter for the MPEG2-data transfer 
			.stream = 
			{
				.type = USB_BULK,
				.count = RTD2831_URB_NUMBER,
				.endpoint = 0x01,		//data pipe
				.u = 
				{
					.bulk = 
					{
						.buffersize = RTD2831_URB_SIZE,
					}
				}
			},
#ifdef V4L2_REFACTORED_MFE_CODE
			}},
#endif
		}
	},

	//remote control
	.rc.legacy = {
#ifdef V4L2_REFACTORED_RC_CODE
		.rc_map_table = rtl2832u_rc_keys_map_table,             //user define key map
		.rc_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table), //user define key map size	
#else
		.rc_key_map       = rtl2832u_rc_keys_map_table,			//user define key map
		.rc_key_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table),	//user define key map size	
#endif	
		.rc_query         = rtl2832u_rc_query,				//use define query function
		.rc_interval      = RT_RC_POLLING_INTERVAL_TIME_MS,		
	},

	.num_device_descs = 10,
	.devices = {
		{ .name = "DVB-T TV Stick",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[54], NULL },
		},
		{ .name = "DVB-T TV Stick",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[55], NULL },
		},
		{ .name = "DVB-T TV Stick",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[56], NULL },
		},
		{
		  .name ="DVB-T TV Stick",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[57], NULL },
		},
		{ .name = "DVB-T TV Stick",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[58], NULL },
		},
		{ .name = "DVB-T TV Stick",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[59], NULL },
		},
		{ .name = "USB DVB-T Device",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[60], NULL },
		},
		{
		  .name = "USB DVB-T Device",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[61], NULL },
		},
		{
		  .name = "USB DVB-T Device",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[62], NULL },
		},
		{ .name = "Leadtek WinFast DTV Dongle Mini",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[76], NULL },
		},
		{ NULL },				
	}
};

static struct dvb_usb_device_properties rtl2832u_8th_properties = {

	.num_adapters = 1,
	.adapter =
	{
		{
#ifndef NO_FE_IOCTL_OVERRIDE
			.fe_ioctl_override = rtl2832_fe_ioctl_override,
#endif
#ifdef V4L2_REFACTORED_MFE_CODE
			.num_frontends = 1,
			.fe = {{
#endif
			.streaming_ctrl = rtl2832u_streaming_ctrl,
			.frontend_attach = rtl2832u_frontend_attach,
			//parameter for the MPEG2-data transfer 
			.stream = 
			{
				.type = USB_BULK,
				.count = RTD2831_URB_NUMBER,
				.endpoint = 0x01,		//data pipe
				.u = 
				{
					.bulk = 
					{
						.buffersize = RTD2831_URB_SIZE,
					}
				}
			},
#ifdef V4L2_REFACTORED_MFE_CODE
			}},
#endif
		}
	},

	//remote control
	.rc.legacy = {
#ifdef V4L2_REFACTORED_RC_CODE
		.rc_map_table = rtl2832u_rc_keys_map_table,             //user define key map
		.rc_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table), //user define key map size	
#else
		.rc_key_map       = rtl2832u_rc_keys_map_table,			//user define key map
		.rc_key_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table),	//user define key map size	
#endif	
		.rc_query         = rtl2832u_rc_query,				//use define query function
		.rc_interval      = RT_RC_POLLING_INTERVAL_TIME_MS,		
	},

	.num_device_descs = 9,
	.devices = {
		{ .name = "USB DVB-T Device",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[63], NULL },
		},
		{ .name = "USB DVB-T Device",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[64], NULL },
		},
		{ .name = "VideoMate DTV",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[65], NULL },
		},
		{
		  .name ="VideoMate DTV",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[66], NULL },
		},
		{ .name = "VideoMate DTV",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[67], NULL },
		},
		{ .name = "VideoMate DTV",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[68], NULL },
		},
		{ .name = "VideoMate DTV",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[69], NULL },
		},
		{
		  .name ="VideoMate DTV",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[70], NULL },
		},
		{
		  .name ="VideoMate DTV",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[71], NULL },
		},

		{ NULL },				
	}
};

static struct dvb_usb_device_properties rtl2832u_9th_properties = {

	.num_adapters = 1,
	.adapter = 
	{
		{
#ifndef NO_FE_IOCTL_OVERRIDE
			.fe_ioctl_override = rtl2832_fe_ioctl_override,
#endif
#ifdef V4L2_REFACTORED_MFE_CODE
			.num_frontends = 1,
			.fe = {{
#endif
			.streaming_ctrl = rtl2832u_streaming_ctrl,
			.frontend_attach = rtl2832u_frontend_attach,
			//parameter for the MPEG2-data transfer 
			.stream = 
			{
				.type = USB_BULK,
				.count = RTD2831_URB_NUMBER,
				.endpoint = 0x01,		//data pipe
				.u = 
				{
					.bulk = 
					{
						.buffersize = RTD2831_URB_SIZE,
					}
				}
			},
#ifdef V4L2_REFACTORED_MFE_CODE
			}},
#endif
		}
	},

	//remote control
	.rc.legacy = {
#ifdef V4L2_REFACTORED_RC_CODE
		.rc_map_table = rtl2832u_rc_keys_map_table,             //user define key map
		.rc_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table), //user define key map size	
#else
		.rc_key_map       = rtl2832u_rc_keys_map_table,			//user define key map
		.rc_key_map_size  = ARRAY_SIZE(rtl2832u_rc_keys_map_table),	//user define key map size	
#endif	
		.rc_query         = rtl2832u_rc_query,				//use define query function
		.rc_interval      = RT_RC_POLLING_INTERVAL_TIME_MS,		
	},

	.num_device_descs = 4,
	.devices = {
		{
		  .name ="VideoMate DTV",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[72], NULL },
		},
		{
		  .name ="VideoMate DTV",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[73], NULL },
		},
		{ .name = "VideoMate DTV",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[74], NULL },
		},
		{ .name = "DVB-T TV Stick",
		  .cold_ids = { NULL, NULL },
		  .warm_ids = { &rtl2832u_usb_table[75], NULL },
		},
		{ NULL },
	}
};




static struct usb_driver rtl2832u_usb_driver = {
	.name		= "dvb_usb_rtl2832u",
	.probe		= rtl2832u_usb_probe,
	.disconnect	= rtl2832u_usb_disconnect,
	.id_table		= rtl2832u_usb_table,
};


static int __init rtl2832u_usb_module_init(void)
{
	int result =0 ;

	deb_info("+info debug open_%s\n", __FUNCTION__);
	if ((result = usb_register(&rtl2832u_usb_driver))) {
		err("usb_register failed. (%d)",result);
		return result;
	}

	return 0;
}

static void __exit rtl2832u_usb_module_exit(void)
{
	usb_deregister(&rtl2832u_usb_driver);

	return ;	
}



module_init(rtl2832u_usb_module_init);
module_exit(rtl2832u_usb_module_exit);


MODULE_AUTHOR("Realtek");
MODULE_AUTHOR("Chialing Lu <chialing@realtek.com>");
MODULE_AUTHOR("Dean Chung<DeanChung@realtek.com>");
MODULE_DESCRIPTION("Driver for the RTL2832U DVB-T / RTL2836 DTMB USB2.0 device");
MODULE_VERSION("2.2.2");
MODULE_LICENSE("GPL");

