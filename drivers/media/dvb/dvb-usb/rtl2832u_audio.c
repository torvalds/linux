

#include "rtl2832u_audio.h"
//#include "rtl2832u_ioctl.h"

#ifndef NO_FE_IOCTL_OVERRIDE

#define RTK_Demod_Byte_Write(page, offset, length, data) \
	((write_demod_register(p_state->d, RTL2832_DEMOD_ADDR, page, offset, data, length)) ? 0 : 1 )


#define RTK_Demod_Byte_Read(page, offset, length, data) \
	((read_demod_register(p_state->d, RTL2832_DEMOD_ADDR, page, offset, data, length)) ? 0 : 1 )


int Set1(struct rtl2832_state*	p_state)
{	
	unsigned char data[1];
	unsigned short	length = 1;
	int hr = 1;
	
	data[0] = 202;
	hr &= RTK_Demod_Byte_Write(1, 0x1C, length, data);
		
	data[0] = 220;
	hr &= RTK_Demod_Byte_Write(1, 0x1D, length, data);
		
	data[0] = 215;
	hr &= RTK_Demod_Byte_Write(1, 0x1E, length, data);
		
	data[0] = 216;
	hr &= RTK_Demod_Byte_Write(1, 0x1F, length, data);
		
	data[0] = 224;
	hr &= RTK_Demod_Byte_Write(1, 0x20, length, data);
		
	data[0] = 242;
	hr &= RTK_Demod_Byte_Write(1, 0x21, length, data);
		
	data[0] = 14;
	hr &= RTK_Demod_Byte_Write(1, 0x22, length, data);
	
	data[0] = 53;
	hr &= RTK_Demod_Byte_Write(1, 0x23, length, data);
	
	data[0] = 6;
	hr &= RTK_Demod_Byte_Write(1, 0x24, length, data);
		
	data[0] = 80;
	hr &= RTK_Demod_Byte_Write(1, 0x25, length, data);
		
	data[0] = 156;
	hr &= RTK_Demod_Byte_Write(1, 0x26, length, data);
		
	data[0] = 13;
	hr &= RTK_Demod_Byte_Write(1, 0x27, length, data);
		
	data[0] = 113;
	hr &= RTK_Demod_Byte_Write(1, 0x28, length, data);
		
	data[0] = 17;
	hr &= RTK_Demod_Byte_Write(1, 0x29, length, data);
		
	data[0] = 20;
	hr &= RTK_Demod_Byte_Write(1, 0x2A, length, data);
		
	data[0] = 113;
	hr &= RTK_Demod_Byte_Write(1, 0x2B, length, data);
		
	data[0] = 116;
	hr &= RTK_Demod_Byte_Write(1, 0x2C, length, data);
		
	data[0] = 25;
	hr &= RTK_Demod_Byte_Write(1, 0x2D, length, data);
		
	data[0] = 65;
	hr &= RTK_Demod_Byte_Write(1, 0x2E, length, data);
		
	data[0] = 165;
	hr &= RTK_Demod_Byte_Write(1, 0x2F, length, data);

	return hr;	
}

int Set2(struct rtl2832_state*	p_state)
{
	UCHAR FM_coe2[6] = {-1, 1, 6, 13, 22, 27}; 	
	unsigned char data[1];
    	unsigned short addr = 0x1F;

	int i;
	BOOL rst = 1;
	for(i=0; i<6; i++)
	{
		data[0] = FM_coe2[i];
	
		rst &= RTK_Demod_Byte_Write(0, addr, 1, data);
		addr--;
	}

	return rst;
}   

 int RTL2832_SWReset(struct rtl2832_state*	p_state)
{
	unsigned char data[1];
	int hr = 1;

	hr &= RTK_Demod_Byte_Read(1, 0x01, 1, data);
	data[0] = data[0] | 0x04;
	hr &= RTK_Demod_Byte_Write(1, 0x01, 1, data);
	data[0] = data[0] & 0xFB;
	hr &= RTK_Demod_Byte_Write(1, 0x01, 1, data);

	return hr; 
}	

 int RTL2832_SWReset_2(struct rtl2832_state* p_state)
{
	unsigned char data[1];
	unsigned char tmp;
	int hr = 1;

	// disable
	hr &= RTK_Demod_Byte_Read(0, 0x19, 1, data);
	data[0] = data[0] & 0xFE;
	tmp = data[0];
	hr &= RTK_Demod_Byte_Write(0, 0x19, 1, data);


	// sw reset
	hr &= RTK_Demod_Byte_Read(1, 0x01, 1, data);
	data[0] = data[0] | 0x04;
	hr &= RTK_Demod_Byte_Write(1, 0x01, 1, data);
	data[0] = data[0] & 0xFB;
	hr &= RTK_Demod_Byte_Write(1, 0x01, 1, data);

	//enable
	tmp = tmp | 0x1;
	hr &= RTK_Demod_Byte_Write(0, 0x19, 1, &tmp);

	return hr; 
}



int Initial_2832_fm(struct rtl2832_state*	p_state)
{
	unsigned char data[4];
	unsigned short length;
	int hr = 1;

 	length = 2;
	data[0] = 0x00;
	data[1] = 0x00;
	hr &= RTK_Demod_Byte_Write(1, 0x3E, length, data);
	
    	length = 1;
	data[0] = 0x00;
	hr &= RTK_Demod_Byte_Write(1, 0x15, length, data);

	length = 3;
	data[0] = 0x00;
	data[1] = 0x00;
	data[2] = 0x00;
	hr &= RTK_Demod_Byte_Write(1, 0x16, length, data);

	if(p_state->tuner_type == RTL2832_TUNER_TYPE_MXL5007T)
	{
		data[0] = 0x35;
		data[1] = 0xD8;
		data[2] = 0x2E;
		hr &= RTK_Demod_Byte_Write(1, 0x19, 3, data);
	}
	else if(p_state->tuner_type == RTL2832_TUNER_TYPE_E4000)
	{
        	data[0] = 0x00;
		data[1] = 0x00;
		data[2] = 0x00;	
		hr &= RTK_Demod_Byte_Write(1, 0x19, 3, data);
	}
	else if(p_state->tuner_type == RTL2832_TUNER_TYPE_FC0012)
	{
		data[0] = 0x00;
		data[1] = 0x00;
		data[2] = 0x00;	
		hr &= RTK_Demod_Byte_Write(1, 0x19, 3, data);
	}

   	length = 4;
	data[0] = 0x03;
	data[1] = 0x84;
	data[2] = 0x00;
	data[3] = 0x00;
    	hr &= RTK_Demod_Byte_Write(1, 0x9F, length, data);

    	hr &= Set1(p_state);

 	length = 1;
	data[0] = 0x11;
	hr &= RTK_Demod_Byte_Write(0, 0x17, length, data);
    
	length = 1;
	data[0] = 0x10;
	hr &= RTK_Demod_Byte_Write(0, 0x18, length, data);
    
	length = 1;
	data[0] = 0x21;
   	hr &= RTK_Demod_Byte_Write(0, 0x19, length, data);
    
 	hr &= Set2(p_state);

      length = 1;
	data[0] = 0x00;
	hr &= RTK_Demod_Byte_Write(1, 0x92, length, data);

	length = 1;
	data[0] = 0xF0;
	hr &= RTK_Demod_Byte_Write(1, 0x93, length, data);

   	length = 1;
	data[0] = 0x0F;
	hr &= RTK_Demod_Byte_Write(1, 0x94, length, data);
   
	length = 1;
	data[0] = 0x60;
	hr &= RTK_Demod_Byte_Write(0, 0x61, length, data);//output of PID filter

	length = 1;
	data[0] = 0x80;
	hr &= RTK_Demod_Byte_Write(0, 0x06, length, data);

	if(p_state->tuner_type == RTL2832_TUNER_TYPE_FC0012  || p_state->tuner_type == RTL2832_TUNER_TYPE_E4000)
	{
		data[0] = 0xCD;
    		hr &= RTK_Demod_Byte_Write(0, 0x08, 1, data);

		data[0] = 0x1;
		hr &= RTK_Demod_Byte_Write(1, 0xB1, 1, data);
	}

     	hr &= RTL2832_SWReset(p_state);
 	if(!hr)
	{
		deb_info("FM Func: Initial 2832 register failed\n");
	}

	if(hr != 1)
		return  -1;
	
	return 0;
}


int Initial_2832_dab(struct rtl2832_state*	p_state)
{
	unsigned char data[4];
	unsigned short length;	
	BOOL hr = 1;
	
	if( p_state->tuner_type == RTL2832_TUNER_TYPE_E4000)
	{
		length = 1;
		data[0] = 0xCD;//enable ADC_I, ADC_Q for zero-IF
		//data[0] = 0x8D;//enable ADC_I 
		hr &= RTK_Demod_Byte_Write(0, 0x08, length, data);
		
		//bit 0, enable en_bbin
		//bit 2,1  DC offset
		//bit 4,3  IQ mismatch
		length = 1;
		data[0] = 0x1F;//ZeroIF //?? data[0] = 0x1E;
		hr &= RTK_Demod_Byte_Write(1, 0xB1, length, data);
	}
   
	//----------------------------------------------
	//set each time change 
	//en_sfreq_sync = 0   page 1, 0x3E, bit6
	//pset_sfreq_off default = 0  page 1, 0x{3E, 3F}
	length = 2;
	data[0] = 0x00;
	data[1] = 0x00;
	hr &= RTK_Demod_Byte_Write(1, 0x3E, length, data);
	//-----------------------------------------------

	//spec_inv = 0; en_aci = 0; en_cfreq_sync = 0
	length = 1;
	data[0] = 0x00;
	hr &= RTK_Demod_Byte_Write(1, 0x15, length, data);
	
	//pset_cfreq_off = 0    Pre-set value of carrier frequency offset. 
	length = 3;
	data[0] = 0x00;
	data[1] = 0x00;
	data[2] = 0x00;
	hr &= RTK_Demod_Byte_Write(1, 0x16, length, data);

	//---- DDC Setting ------
	//pset_iffreq   IF frequency //36.125M  0x2F; 0xB8; 0xE4;
	if(p_state->tuner_type == RTL2832_TUNER_TYPE_MXL5007T)
	{//IF 4.57M
		length = 3;
		data[0] = 0x35;
		data[1] = 0xD8;
		data[2] = 0x2E;
		hr &= RTK_Demod_Byte_Write(1, 0x19, length, data);
	}
	else if(p_state->tuner_type == RTL2832_TUNER_TYPE_E4000)
	{//ZERO IF
		length = 3;
		data[0] = 0x00;
		data[1] = 0x00;
		data[2] = 0x00;
		hr &= RTK_Demod_Byte_Write(1, 0x19, length, data);
	}
	else if(p_state->tuner_type ==  RTL2832_TUNER_TYPE_FC0012)
	{//FC0012_TUNER is zero IF, but can set tunner to other frequency to avoid DC cancellation, frequency noise...
		
		//length = 3;
		//data[0] = 0x00;
		//data[1] = 0x00;
		//data[2] = 0x00;
		//hr &= RTK_Demod_Byte_Write(1, 0x19, length, data);
	
		// Set pset_iffreg of 2832
		length = 3;
		data[0] = 0x00;
		data[1] = 0x00;
		data[2] = 0x00;
		hr &= RTK_Demod_Byte_Write(1, 0x19, length, data);
	}
	//20110413 add by alan
	else if(p_state->tuner_type ==  RTL2832_TUNER_TYPE_FC0013)
	{//FC0013_TUNER is zero IF, but can set tunner to other frequency to avoid DC cancellation, frequency noise...
		
		//length = 3;
		//data[0] = 0x00;
		//data[1] = 0x00;
		//data[2] = 0x00;
		//hr &= RTK_Demod_Byte_Write(1, 0x19, length, data);
	
		// Set pset_iffreg of 2832
		length = 3;
		data[0] = 0x00;
		data[1] = 0x00;
		data[2] = 0x00;
		hr &= RTK_Demod_Byte_Write(1, 0x19, length, data);
	}
	//end	
	//------ Resampler Setting -------
	//resample ratio 28.8M --> 8.192M
	length = 4;
	data[0] = 0x03;
	data[1] = 0x84;
	data[2] = 0x00;
	data[3] = 0x00;
	hr &= RTK_Demod_Byte_Write(1, 0x9F, length, data);

 	//------- DDC LPF coe -------------
	// used in OpenDevice? more possible setFreq  
	length = 1;
	data[0] = 202;
	hr &= RTK_Demod_Byte_Write(1, 0x1C, length, data);

	length = 1;
	data[0] = 220;
	hr &= RTK_Demod_Byte_Write(1, 0x1D, length, data);

	length = 1;
	data[0] = 215;
	hr &= RTK_Demod_Byte_Write(1, 0x1E, length, data);

	length = 1;
	data[0] = 216;
	hr &= RTK_Demod_Byte_Write(1, 0x1F, length, data);

	length = 1;
	data[0] = 224;
	hr &= RTK_Demod_Byte_Write(1, 0x20, length, data);

	length = 1;
	data[0] = 242;
	hr &= RTK_Demod_Byte_Write(1, 0x21, length, data);

	length = 1;
	data[0] = 14;
	hr &= RTK_Demod_Byte_Write(1, 0x22, length, data);

	length = 1;
	data[0] = 53;
	hr &= RTK_Demod_Byte_Write(1, 0x23, length, data);
  
	length = 1;
	data[0] = 6;
	hr &= RTK_Demod_Byte_Write(1, 0x24, length, data);

	length = 1;
	data[0] = 80;
	hr &= RTK_Demod_Byte_Write(1, 0x25, length, data);

	length = 1;
	data[0] = 156;
	hr &= RTK_Demod_Byte_Write(1, 0x26, length, data);

	length = 1;
	data[0] = 13;
	hr &= RTK_Demod_Byte_Write(1, 0x27, length, data);

	length = 1;
	data[0] = 113;
	hr &= RTK_Demod_Byte_Write(1, 0x28, length, data);

	length = 1;
	data[0] = 17;
	hr &= RTK_Demod_Byte_Write(1, 0x29, length, data);

	length = 1;
	data[0] = 20;
	hr &= RTK_Demod_Byte_Write(1, 0x2A, length, data);
 
	length = 1;
	data[0] = 113;
	hr &= RTK_Demod_Byte_Write(1, 0x2B, length, data);

	length = 1;
	data[0] = 116;
	hr &= RTK_Demod_Byte_Write(1, 0x2C, length, data);

	length = 1;
	data[0] = 25;
	hr &= RTK_Demod_Byte_Write(1, 0x2D, length, data);

	length = 1;
	data[0] = 65;
	hr &= RTK_Demod_Byte_Write(1, 0x2E, length, data);

	length = 1;
	data[0] = 165;
	hr &= RTK_Demod_Byte_Write(1, 0x2F, length, data);

	//-------- DAB Setting ---------
	//dab dagc_target;     (S,8,7f) when dagc on 
	length = 1;
	data[0] = 0x11;//default: 0x13
	hr &= RTK_Demod_Byte_Write(0, 0x17, length, data);
    
	//dagc_gain_set;  (S,8,1f) when dagc off
	length = 1;
	data[0] = 0x10;//default: 0x10
	hr &= RTK_Demod_Byte_Write(0, 0x18, length, data);
    
	//0x19 [0]   0x01    1 for dab_enable;     0 for soft reset or module disable;       
	//0x19 [2:1] 0x02    mode  10 for DAB , 00 for FM , 01/11 for pattern
 	//0x19 [4:3] 0x00    dagc_loop_gain; 0~3 for 2^-10 ~ 2^-7:                     
	//0x19 [5]   0x01    dagc_on;        0 for off , 1 for on                    
	length = 1;
	data[0] = 0x25;//0x25;//0x27;
	hr &= RTK_Demod_Byte_Write(0, 0x19, length, data);
	//-------------------------------

	//------- hold stage ------------
	// stage 11 is possible
	// hold stage 4 now !
	length = 1;
	data[0] = 0x00;//0x7F;//
	hr &= RTK_Demod_Byte_Write(1, 0x92, length, data);

	length = 1;
	data[0] = 0xF0;//0xF7;//
	hr &= RTK_Demod_Byte_Write(1, 0x93, length, data);

 	length = 1;
	data[0] = 0x0F;//0xFF;//
	hr &= RTK_Demod_Byte_Write(1, 0x94, length, data);
	//----------------------------------

	// DAB input from PIP ?
	length = 1;
	data[0] = 0x60;
	hr &= RTK_Demod_Byte_Write(0, 0x61, length, data);//output of PID filter
 
	//if(currentTunner == MT2266_TUNER || currentTunner == FC2580_TUNER || currentTunner == TUA9001_TUNER || currentTunner == FC0012_TUNER)
	{//???
		length = 1;
		data[0] = 0x40;//08.03.10
		hr &= RTK_Demod_Byte_Write(0, 0x20, length, data);
	}
	
	length = 1;
	//if(p_state->tuner_type == TUA9001_TUNER)// pay attention: for infineon tunner, exchange I and Q
	//{
	//	data[0] = 0x90;
	//}
	//else
	{	
		data[0] = 0x80;
	}
	hr &= RTK_Demod_Byte_Write(0, 0x06, length, data);

	//end
	//---- software reset -----
	//page 1, 0x01, bit 2, first 1 then zero
	length = 1;
	hr &= RTK_Demod_Byte_Read(1, 0x01, length, data);
	data[0] = data[0] | 0x04;
	hr &= RTK_Demod_Byte_Write(1, 0x01, length, data);
	data[0] = data[0] & 0xFB;
	hr &= RTK_Demod_Byte_Write(1, 0x01, length, data);

	if(!hr)
	{
		deb_info("DAB DLL: initial 2832 register fail\n");
	}

	if(hr != 1)
		return  -1;
	
	return 0;
}



int switch_mode(struct rtl2832_state* p_state, int audio_mdoe)
{
	if( mutex_lock_interruptible(&p_state->i2c_repeater_mutex) )	goto mutex_error;

	if(p_state->tuner_type != RTL2832_TUNER_TYPE_FC0012 && 
				p_state->tuner_type != RTL2832_TUNER_TYPE_MXL5007T &&
				p_state->tuner_type != RTL2832_TUNER_TYPE_E4000 &&
				p_state->tuner_type != RTL2832_TUNER_TYPE_FC0013)
	{
		deb_info("Illegal tuner type\n");
		goto error;
	}
	
	deb_info("+switch_mode\n");

	if(p_state->demod_support_type & SUPPORT_DVBT_MODE)
	{
		// if current state is 2832
		if(p_state->demod_type == RTL2832)
		{
	      		// Demod  H/W Reset
           		if(rtl2832_hw_reset( p_state))
					goto error;
					
			if(p_state->tuner_type == RTL2832_TUNER_TYPE_FC0012)
			{
				if(rtl2832_fc0012_Initialize_fm(p_state->pNim))
					goto error;
			}
			else if(p_state->tuner_type == RTL2832_TUNER_TYPE_MXL5007T)
			{
				if(rtl2832_mxl5007t_Initialize_fm(p_state->pNim))
					goto error;
			}
			else if(p_state->tuner_type == RTL2832_TUNER_TYPE_E4000)
			{	
				if(rtl2832_e4000_Initialize_fm(p_state->pNim))
					goto error;
			}
			else if(p_state->tuner_type == RTL2832_TUNER_TYPE_FC0013)
			{	
				if(rtl2832_fc0013_Initialize_fm(p_state->pNim))
					goto error;
			}


			switch(audio_mdoe)
			{
				case FM_MODE:
					if (Initial_2832_fm(p_state)) 
					{
						deb_info("%s: fail to initial fm\n",__FUNCTION__);
						goto error;
					}
					deb_info("%s: switch to fm.....\n",__FUNCTION__);	
				break;

				case DAB_MODE:
					if (Initial_2832_dab(p_state)) 
					{
						deb_info("%s: fail to initial dab\n",__FUNCTION__);
						goto error;
					}
					deb_info("%s: switch to dab.....\n",__FUNCTION__);	
				break;	
			}
		}
	}

	deb_info("-switch_mode\n");

       mutex_unlock(&p_state->i2c_repeater_mutex);
	return 0;

error:
	mutex_unlock(&p_state->i2c_repeater_mutex);
mutex_error:
	return -1;
}


//3 Related to urb behavior
struct usb_data_stream stream_cp;
struct  fm_stream_ctrl_struct
{
	u8  fm_stream_buf[24064];
	int  fm_stream_index;
};
struct fm_stream_ctrl_struct fm_struct;
struct fm_stream_ctrl_struct* fm_stream;

static void dvb_dmx_fm_filter(struct dvb_demux *demux, const u8 *buf, size_t count)
{

       struct dvb_demux_feed *feed;
	int p,i,j;

	spin_lock(&demux->lock);

       list_for_each_entry(feed, &demux->feed_list, list_head) {
		if(feed->pid != 0x2000)
			continue;

	p = 0;
	if (fm_stream->fm_stream_index) {
		i = fm_stream->fm_stream_index;
		j = 24064 - i;

		if (count < j) {
			memcpy(&fm_stream->fm_stream_buf[i], buf, count);
			fm_stream->fm_stream_index += count; 
			goto bailout;
		}
		
		memcpy(&fm_stream->fm_stream_buf[i], buf, j);
		feed->cb.ts(fm_stream->fm_stream_buf, 24064, NULL, 0, &feed->feed.ts, DMX_OK);
		fm_stream->fm_stream_index = 0;
		p += j;
	}

	while (p < count) {
		if (count - p >= 24064) {
				feed->cb.ts(&buf[p], 24064, NULL, 0, &feed->feed.ts, DMX_OK);
				p += 24064;
		} else {
				i = count - p;
				memcpy(fm_stream->fm_stream_buf, &buf[p], i);
				fm_stream->fm_stream_index = i;
				goto bailout;
		}
	}

	}

bailout:
	spin_unlock(&demux->lock);
}


//3--> dvb_usb_data_complete
static void fm_usb_data_complete(struct usb_data_stream *stream, u8 *buffer, size_t length)
{
	struct dvb_usb_adapter *adap = stream->user_priv;
	
	//deb_info("%s: length %d\n ", __func__, length);//debug
	if (adap->feedcount > 0 && adap->state & DVB_USB_ADAP_STATE_DVB)
		dvb_dmx_fm_filter(&adap->demux, buffer, length);
}

#ifdef V4L2_REFACTORED_MFE_CODE
void fm_stream_ctrl(int f,  struct dvb_usb_adapter*  adapter)
{
	if(f)
	{//store  usb_data_stream part
		memcpy(&stream_cp, &adapter->fe_adap[0].stream, sizeof(struct usb_data_stream));
		adapter->fe_adap[0].stream.complete = fm_usb_data_complete;
	}
	else
	{//resume dvb-t usb_data_stream part
		memcpy(&adapter->fe_adap[0].stream, &stream_cp, sizeof(struct usb_data_stream));
	}
}
#else
void fm_stream_ctrl(int f,  struct dvb_usb_adapter*  adapter)
{
	if(f)
	{//store  usb_data_stream part
		memcpy(&stream_cp, &adapter->stream, sizeof(struct usb_data_stream));
		adapter->stream.complete = fm_usb_data_complete;
	}
	else
	{//resume dvb-t usb_data_stream part
		memcpy(&adapter->stream, &stream_cp, sizeof(struct usb_data_stream));
	}
}
#endif

int fe_fm_cmd_ctrl(struct dvb_frontend *fe, void *parg)
{
	struct rtl2832_state*	p_state = fe->demodulator_priv;
	struct dvb_usb_adapter*  adapter = p_state->d->adapter;//ptr to adapter[0]
	struct fm_cmd_struct*  fm_ctrl = (struct fm_cmd_struct*)parg;
	int fm_cmd = fm_ctrl->cmd_num;
	unsigned int tmp2;
	unsigned char data[4];
	int hr;
	struct fm_cmd_struct  tmp_str;
	unsigned int orgValue;
	unsigned int psetValue;

	deb_info("+fe_fm_cmd_ctrl\n");

	switch(fm_cmd)
	{
		case FE_ENABLE_FM:

			deb_info("FE_OPEN_FM\n");

			if(p_state->rtl2832_audio_video_mode == RTK_AUDIO)
			{
				deb_info("It has been FM mode\n");
				return 1;
			}

			//check whether it is legal, no dvb-t, no fm
					
			//switch to fm mode
			if(switch_mode(p_state, FM_MODE))
				return -1;
			
			//change usb_data_stream part
			//fm_stream = vmalloc(sizeof(struct fm_stream_ctrl_struct));
			fm_stream = &fm_struct;
			fm_stream_ctrl(1, adapter);
		
			p_state->rtl2832_audio_video_mode = RTK_AUDIO;//FM or DAB

			tmp_str.tuner= p_state->tuner_type;//tuner type
			memcpy(parg, &tmp_str, sizeof(struct fm_cmd_struct));//will be copy to user
	
			return 1;//break;

		case FE_ENABLE_DAB:

			deb_info("FE_OPEN_DAB\n");

			if(p_state->rtl2832_audio_video_mode == RTK_AUDIO)
			{
				deb_info("It has been DAB mode\n");
				return 1;
			}

			//check whether it is legal, no dvb-t, no fm
					
			//switch to fm mode
			if(switch_mode(p_state, DAB_MODE))
				return -1;
			
			//change usb_data_stream part
			//fm_stream = vmalloc(sizeof(struct fm_stream_ctrl_struct));
			fm_stream = &fm_struct;
			fm_stream_ctrl(1, adapter);
		
			p_state->rtl2832_audio_video_mode = RTK_AUDIO;//FM or DAB

			tmp_str.tuner= p_state->tuner_type;//tuner type
			memcpy(parg, &tmp_str, sizeof(struct fm_cmd_struct));//will be copy to user
	
			return 1;//break;

			
		case FE_DISABLE_FM:
		case FE_DISABLE_DAB:
			
			deb_info("FE_CLOSE_FM or DAB\n");

			if(p_state->rtl2832_audio_video_mode != RTK_AUDIO)
			{
				deb_info("It is not start from FM or DAB mode\n");
				return 1;
			}
			
			fm_stream_ctrl(0, adapter);
			p_state->rtl2832_audio_video_mode = RTK_VIDEO;
			return 1;//break;

		case CR_D_:

			deb_info("CR_d\n");

			tmp2 = (fm_ctrl->cr)  & 0x3FFFFF;
	    		data[0] = (tmp2>>16) & 0x3F;
	    		data[1] = (tmp2>>8) & 0xFF;
			data[2] = tmp2 & 0xFF;
	    		hr = RTK_Demod_Byte_Write(1, 0x16, 3, data);
			if(!hr)
				return -1;

			hr = RTL2832_SWReset_2(p_state);
			if(!hr)
				return -1;

			deb_info("CR_d done\n");
			return 1;

		case CR_A_:

			deb_info("CR_a\n");

			hr = RTK_Demod_Byte_Read(1, 0x16, 3, data);
			if(!hr)
			{
				return -1;
			}

			tmp2 = (fm_ctrl->cr)  & 0x3FFFFF;

			orgValue =  (((unsigned int)data[0]&0x3F)<<16) | (((unsigned int)data[1])<<8) | (unsigned short)data[2];
			psetValue = tmp2 + orgValue; 
			
			data[0] = (psetValue>>16) & 0x3F;
			data[1] = (psetValue>>8) & 0xFF;
			data[2] = psetValue & 0xFF;
			hr = RTK_Demod_Byte_Write(1, 0x16, 3, data);
			if(!hr)
				return -1;

			hr = RTL2832_SWReset_2(p_state);
			if(!hr)
				return -1;	

			deb_info("CR_a done\n");
			return 1;

		case TR_D:   

			deb_info("TR_d \n");

			tmp2 = (fm_ctrl->cr);
			data[0] = (tmp2>>8) & 0x3F;
			data[1] = tmp2 & 0xFF;
			hr = RTK_Demod_Byte_Write(1, 0x3E, 2, data);//0x3E 0x3F
			if(!hr)
				return -1;

			//hr = RTL2832_SWReset(p_state);
			//if(!hr)
			//	return -1;

			deb_info("TR_d done\n");
			return 1;

		case TR_A:    //increasing value

			deb_info("TR_a\n");

			hr = RTK_Demod_Byte_Read(1, 0x3E, 2, data);//0x3E, 0x3F
			if(!hr)
				return -1;

			tmp2 = (fm_ctrl->cr);
			orgValue =  (((unsigned int)data[0] & 0x3F) << 8) | (unsigned int)data[1];
			psetValue = (tmp2 & 0x3FFF) + orgValue; 
	
			data[0] = (psetValue>>8) & 0x3F;
			data[1] = psetValue & 0xFF;

			hr = RTK_Demod_Byte_Write(1, 0x3E, 2, data);//0x3E, 0x3F
			if(!hr)
				return -1;			
			
			//hr = RTL2832_SWReset(p_state);
			//if(!hr)
			//	return -1;

			deb_info("TR_a done\n");
			return 1;

		case SW_RESET:

			hr = RTL2832_SWReset(p_state);
			if(!hr)
				return -1;

			deb_info("RTL2832_SWReset\n");
			return 1;

		default:
			return -1;			
	}

	return 0;		
}

int rtl2832_fe_ioctl_override(struct dvb_frontend *fe,unsigned int cmd, void *parg, unsigned int stage)
{
	
	//deb_info("rtl2832_fe_ioctl_override : cmd =%d\n", cmd);
	
	int ret = 0;

	if(stage == DVB_FE_IOCTL_PRE)
	{
		switch(cmd)
		{
			case FE_FM_CMD:

				ret = fe_fm_cmd_ctrl(fe, parg);
			break;
	
		}
	}
	//else
	//{
	//deb_info("rtl2832_fe_ioctl_override : xxxxx\n");
	//}	

	return ret;
}

#endif



