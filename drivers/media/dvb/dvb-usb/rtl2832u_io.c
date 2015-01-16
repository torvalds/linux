

#include "rtl2832u_io.h"
#include <linux/time.h>

#define ERROR_TRY_MAX_NUM		4


#define	DUMMY_PAGE		0x0a
#define	DUMMY_ADDR		0x01



void
platform_wait(
	unsigned long nMinDelayTime)
{
	// The unit of Sleep() waiting time is millisecond.
	unsigned long usec;
	do {
		usec = (nMinDelayTime > 8000) ? 8000 : nMinDelayTime;
		msleep(usec);
		nMinDelayTime -= usec;
		} while (nMinDelayTime > 0);

	return;
	
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//		remote control 
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int 
read_rc_register(
	struct dvb_usb_device*	dib,
	unsigned short	offset,
	unsigned char*	data,
	unsigned short	bytelength)
{
	int ret = -ENOMEM;
 
        ret = usb_control_msg(dib->udev,								/* pointer to device */
                usb_rcvctrlpipe( dib->udev,RTL2832_CTRL_ENDPOINT),					/* pipe to control endpoint */
                0,											/* USB message request value */
                SKEL_VENDOR_IN,										/* USB message request type value */
                offset,											/* USB message value */
                0x0201,											/* USB message index value */
                data,											/* pointer to the receive buffer */
                bytelength,										/* length of the buffer */
                DIBUSB_I2C_TIMEOUT);									/* time to wait for the message to complete before timing out */
 	
        if (ret != bytelength)
	{
		deb_info(" error try rc read register %s: offset=0x%x, error code=0x%x !\n", __FUNCTION__, offset, ret);
		return 1;
       	}

	return 0; 
}



static int 
write_rc_register(
	struct dvb_usb_device*	dib,
	unsigned short	offset,
	unsigned char*	data,
	unsigned short	bytelength)
{
	int ret = -ENOMEM;
	unsigned char try_num;

	try_num = 0;	
error_write_again:
	try_num++;	
 
        ret = usb_control_msg(dib->udev,								/* pointer to device */
                usb_sndctrlpipe( dib->udev,RTL2832_CTRL_ENDPOINT),					/* pipe to control endpoint */
                0,											/* USB message request value */
                SKEL_VENDOR_OUT,									/* USB message request type value */
                offset,											/* USB message value */
                0x0211,											/* USB message index value */
                data,											/* pointer to the receive buffer */
                bytelength,										/* length of the buffer */
                DIBUSB_I2C_TIMEOUT);									/* time to wait for the message to complete before timing out */

        if (ret != bytelength)
	{
		deb_info("error try rc write register  = %d, %s: offset=0x%x, error code=0x%x !\n",try_num ,__FUNCTION__, offset, ret);

		if( try_num > ERROR_TRY_MAX_NUM )	goto error;
		else				goto error_write_again;
       }

	return 0;
error:
	return 1;
 }


int
read_rc_char_bytes(
	struct dvb_usb_device*	dib,
	RegType	type,
	unsigned short	byte_addr,
	unsigned char*	buf,
	unsigned short	byte_num)
{
	int ret = 1;

	if( byte_num != 1 && byte_num !=2 && byte_num !=4 && byte_num != 0x80)
	{
		deb_info("error!! %s: length = %d \n", __FUNCTION__, byte_num);
		return 1;
	}


	if( mutex_lock_interruptible(&dib->usb_mutex) )	return 1;
	
	if (type == RTD2832U_RC )	
		ret = read_rc_register( dib , byte_addr , buf , byte_num);
	else
	{
		deb_info("error!! %s: erroe register type \n", __FUNCTION__);
		return 1;
	}				
	mutex_unlock(&dib->usb_mutex);

	return ret;
	
}



int
write_rc_char_bytes(
	struct dvb_usb_device*	dib,
	RegType	type,
	unsigned short	byte_addr,
	unsigned char*	buf,
	unsigned short	byte_num)
{
	int ret = 1;

	if( byte_num != 1 && byte_num !=2 && byte_num !=4 && byte_num !=0x80)
	{
		deb_info("error!! %s: length = %d \n", __FUNCTION__, byte_num);
		return 1;
	}

	if( mutex_lock_interruptible(&dib->usb_mutex) )	return 1;
	
	if (type == RTD2832U_RC )	
		ret = write_rc_register( dib , byte_addr , buf , byte_num);
	else
	{
		deb_info("error!! %s: erroe register type \n", __FUNCTION__);
		ret=1;
	}	
	mutex_unlock(&dib->usb_mutex);	

	return ret;

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int 
read_usb_register(
	struct dvb_usb_device*	dib,
	unsigned short	offset,
	unsigned char*	data,
	unsigned short	bytelength)
{
	int ret = -ENOMEM;
 
        ret = usb_control_msg(dib->udev,								/* pointer to device */
                usb_rcvctrlpipe( dib->udev,RTL2832_CTRL_ENDPOINT),			/* pipe to control endpoint */
                0,														/* USB message request value */
                SKEL_VENDOR_IN,										/* USB message request type value */
                (USB_BASE_ADDRESS<<8) + offset,						/* USB message value */
                0x0100,													/* USB message index value */
                data,													/* pointer to the receive buffer */
                bytelength,												/* length of the buffer */
                DIBUSB_I2C_TIMEOUT);									/* time to wait for the message to complete before timing out */
 	
        if (ret != bytelength)
	{
		deb_info(" %s: offset=0x%x, error code=0x%x !\n", __FUNCTION__, offset, ret);
		return 1;
       }

	return 0; 
}



static int 
write_usb_register(
	struct dvb_usb_device*	dib,
	unsigned short	offset,
	unsigned char*	data,
	unsigned short	bytelength)
{
	int ret = -ENOMEM;
	unsigned char try_num;

	try_num = 0;	
error_write_again:
	try_num++;	
 
        ret = usb_control_msg(dib->udev,								/* pointer to device */
                usb_sndctrlpipe( dib->udev,RTL2832_CTRL_ENDPOINT),		/* pipe to control endpoint */
                0,														/* USB message request value */
                SKEL_VENDOR_OUT,										/* USB message request type value */
                (USB_BASE_ADDRESS<<8) + offset,						/* USB message value */
                0x0110,													/* USB message index value */
                data,													/* pointer to the receive buffer */
                bytelength,												/* length of the buffer */
                DIBUSB_I2C_TIMEOUT);									/* time to wait for the message to complete before timing out */

        if (ret != bytelength)
	{
		deb_info("error try = %d, %s: offset=0x%x, error code=0x%x !\n",try_num ,__FUNCTION__, offset, ret);

		if( try_num > ERROR_TRY_MAX_NUM )	goto error;
		else				goto error_write_again;
       }

	return 0;
error:
	return 1;
 }




static int 
read_sys_register(
	struct dvb_usb_device*	dib,
	unsigned short	offset,
	unsigned char*	data,
	unsigned short	bytelength)
{
	int ret = -ENOMEM;
 
        ret = usb_control_msg(dib->udev,								/* pointer to device */
                usb_rcvctrlpipe( dib->udev,RTL2832_CTRL_ENDPOINT),		/* pipe to control endpoint */
                0,														/* USB message request value */
                SKEL_VENDOR_IN,										/* USB message request type value */
                (SYS_BASE_ADDRESS<<8) + offset,						/* USB message value */
                0x0200,													/* USB message index value */
                data,													/* pointer to the receive buffer */
                bytelength,												/* length of the buffer */
                DIBUSB_I2C_TIMEOUT);									/* time to wait for the message to complete before timing out */
			
        if (ret != bytelength)
	{
		deb_info(" %s: offset=0x%x, error code=0x%x !\n", __FUNCTION__, offset, ret);
		return 1;
       }

	return 0; 

  }


static int 
write_sys_register(
	struct dvb_usb_device*	dib,
	unsigned short	offset,
	unsigned char*	data,
	unsigned short	bytelength)
{ 
	int ret = -ENOMEM;
	unsigned char try_num;

	try_num = 0;	
error_write_again:	
	try_num++;	

        ret = usb_control_msg(dib->udev,								/* pointer to device */
                usb_sndctrlpipe( dib->udev,RTL2832_CTRL_ENDPOINT),		/* pipe to control endpoint */
                0,														/* USB message request value */
                SKEL_VENDOR_OUT,										/* USB message request type value */
                (SYS_BASE_ADDRESS<<8) + offset,						/* USB message value */
                0x0210,													/* USB message index value */
                data,													/* pointer to the receive buffer */
                bytelength,												/* length of the buffer */
                DIBUSB_I2C_TIMEOUT);									/* time to wait for the message to complete before timing out */
		
        if (ret != bytelength)
	{
		deb_info(" error try= %d, %s: offset=0x%x, error code=0x%x !\n",try_num, __FUNCTION__, offset, ret);
		if( try_num > ERROR_TRY_MAX_NUM )	goto error;
		else				goto error_write_again;	
        }

	return 0;
error:
	return 1;
 }




int 
read_demod_register(
	struct dvb_usb_device*dib,
	unsigned char			demod_device_addr,	
	unsigned char 		page,
	unsigned char 		offset,
	unsigned char*		data,
	unsigned short		bytelength)
{
	int ret = -ENOMEM;
	int i;
	unsigned char	tmp;

	if( mutex_lock_interruptible(&dib->usb_mutex) )	goto error;

        ret = usb_control_msg(dib->udev,								/* pointer to device */
                usb_rcvctrlpipe( dib->udev,RTL2832_CTRL_ENDPOINT),			/* pipe to control endpoint */
                0,														/* USB message request value */
                SKEL_VENDOR_IN,										/* USB message request type value */
                demod_device_addr + (offset<<8),						/* USB message value */
                (0x0000 + page),										/* USB message index value */
                data,													/* pointer to the receive buffer */
                bytelength,												/* length of the buffer */
                DIBUSB_I2C_TIMEOUT);									/* time to wait for the message to complete before timing out */


	usb_control_msg(dib->udev,								/* pointer to device */
                usb_rcvctrlpipe( dib->udev,RTL2832_CTRL_ENDPOINT),			/* pipe to control endpoint */
                0,														/* USB message request value */
                SKEL_VENDOR_IN,										/* USB message request type value */
                RTL2832_DEMOD_ADDR + (DUMMY_ADDR<<8),						/* USB message value */
                (0x0000 + DUMMY_PAGE),										/* USB message index value */
                &tmp,													/* pointer to the receive buffer */
                LEN_1_BYTE,												/* length of the buffer */
                DIBUSB_I2C_TIMEOUT);									/* time to wait for the message to complete before timing out */

	mutex_unlock(&dib->usb_mutex);

	
	//	deb_info("%s[1345]: ret=%d, DA=0x%x, len=%d, page=%d, offset=0x%x, data=(", __FUNCTION__, ret, demod_device_addr, bytelength,page, offset);
//		for(i = 0; i < bytelength; i++)
//			deb_info("0x%x,", data[i]);
//		deb_info(")\n");
			
        if (ret != bytelength)
	{
		deb_info("error!! %s: ret=%d, DA=0x%x, len=%d, page=%d, offset=0x%x, data=(", __FUNCTION__, ret, demod_device_addr, bytelength,page, offset);
		for(i = 0; i < bytelength; i++)
			deb_info("0x%x,", data[i]);
		deb_info(")\n");
		
		goto error;
       }

	return 0;  

error:
	return 1;
}




int
write_demod_register(
	struct dvb_usb_device*dib,
	unsigned char			demod_device_addr,		
	unsigned char			page,
	unsigned char			offset,
	unsigned char			*data,
	unsigned short		bytelength)
{
	int ret = -ENOMEM;
	unsigned char  i, try_num;
	unsigned char	tmp;	

	try_num = 0;	
error_write_again:	
	try_num++;

	if( mutex_lock_interruptible(&dib->usb_mutex) )	goto error;
	
        ret = usb_control_msg(dib->udev,								/* pointer to device */
                usb_sndctrlpipe( dib->udev,RTL2832_CTRL_ENDPOINT),		/* pipe to control endpoint */
                0,														/* USB message request value */
                SKEL_VENDOR_OUT,										/* USB message request type value */
                demod_device_addr + (offset<<8),						/* USB message value */
                (0x0010 + page),										/* USB message index value */
                data,													/* pointer to the receive buffer */
                bytelength,												/* length of the buffer */
                DIBUSB_I2C_TIMEOUT);									/* time to wait for the message to complete before timing out */


	usb_control_msg(dib->udev,								/* pointer to device */
                usb_rcvctrlpipe( dib->udev,RTL2832_CTRL_ENDPOINT),			/* pipe to control endpoint */
                0,														/* USB message request value */
                SKEL_VENDOR_IN,										/* USB message request type value */
                RTL2832_DEMOD_ADDR + (DUMMY_ADDR<<8),						/* USB message value */
                (0x0000 + DUMMY_PAGE),										/* USB message index value */
                &tmp,													/* pointer to the receive buffer */
                LEN_1_BYTE,												/* length of the buffer */
                DIBUSB_I2C_TIMEOUT);									/* time to wait for the message to complete before timing out */


	mutex_unlock(&dib->usb_mutex);

//		deb_info("%s: ret=%d, DA=0x%x, len=%d, page=%d, offset=0x%x, data=(", __FUNCTION__, ret, demod_device_addr, bytelength, page,offset);
//		for(i = 0; i < bytelength; i++)
//			deb_info("0x%x,", data[i]);
//		deb_info(")\n");


        if (ret != bytelength)
	{
		deb_info("error try = %d!! %s: ret=%d, DA=0x%x, len=%d, page=%d, offset=0x%x, data=(",try_num , __FUNCTION__, ret, demod_device_addr, bytelength,page,offset);
		for(i = 0; i < bytelength; i++)
			deb_info("0x%x,", data[i]);
		deb_info(")\n");
	
		if( try_num > ERROR_TRY_MAX_NUM )	goto error;
		else				goto error_write_again;
        }

	return 0;

error:
	return 1;
 }






int 
read_rtl2832_tuner_register(
	struct dvb_usb_device	*dib,
	unsigned char			device_address,
	unsigned char			offset,
	unsigned char			*data,
	unsigned short		bytelength)
{
	int ret = -ENOMEM;
 	int i;
	unsigned char data_tmp[128];	


	if( mutex_lock_interruptible(&dib->usb_mutex) )	goto error;
	
        ret = usb_control_msg(dib->udev,								/* pointer to device */
                usb_rcvctrlpipe( dib->udev,RTL2832_CTRL_ENDPOINT),			/* pipe to control endpoint */
                0,														/* USB message request value */
                SKEL_VENDOR_IN,										/* USB message request type value */
                device_address+(offset<<8),							/* USB message value */
                0x0300,													/* USB message index value */
                data_tmp,												/* pointer to the receive buffer */
                bytelength,												/* length of the buffer */
                DIBUSB_I2C_TIMEOUT);									/* time to wait for the message to complete before timing out */

	mutex_unlock(&dib->usb_mutex);

//		deb_info("%s: ret=%d, DA=0x%x, len=%d, offset=0x%x, data=(", __FUNCTION__, ret, device_address, bytelength,offset);
//		for(i = 0; i < bytelength; i++)
//			deb_info("0x%x,", data_tmp[i]);
//		deb_info(")\n");
			
        if (ret != bytelength)
 	{
		deb_info("error!! %s: ret=%d, DA=0x%x, len=%d, offset=0x%x, data=(", __FUNCTION__, ret, device_address, bytelength,offset);
		for(i = 0; i < bytelength; i++)
			deb_info("0x%x,", data_tmp[i]);
		deb_info(")\n");
		
		goto error;
        }

	memcpy(data,data_tmp,bytelength);

	return 0;
	
error:
	return 1;   
	
 
}

int write_rtl2832_tuner_register(
	struct dvb_usb_device *dib,
	unsigned char			device_address,
	unsigned char			offset,
	unsigned char			*data,
	unsigned short		bytelength)
{
	int ret = -ENOMEM;
	unsigned char  i, try_num;

	try_num = 0;	
error_write_again:	
	try_num++;

	if( mutex_lock_interruptible(&dib->usb_mutex) )	goto error;
 
        ret = usb_control_msg(dib->udev,								/* pointer to device */
                usb_sndctrlpipe( dib->udev,RTL2832_CTRL_ENDPOINT),		/* pipe to control endpoint */
                0,														/* USB message request value */
                SKEL_VENDOR_OUT,										/* USB message request type value */
                device_address+(offset<<8),							/* USB message value */
                0x0310,													/* USB message index value */
                data,													/* pointer to the receive buffer */
                bytelength,												/* length of the buffer */
                DIBUSB_I2C_TIMEOUT);									/* time to wait for the message to complete before timing out */

	mutex_unlock(&dib->usb_mutex);

//		deb_info("%s: ret=%d, DA=0x%x, len=%d, offset=0x%x, data=(", __FUNCTION__, ret, device_address, bytelength, offset);
//		for(i = 0; i < bytelength; i++)
//			deb_info("0x%x,", data[i]);
//		deb_info(")\n");

			
        if (ret != bytelength)
	{
		deb_info("error try= %d!! %s: ret=%d, DA=0x%x, len=%d, offset=0x%x, data=(",try_num, __FUNCTION__, ret, device_address, bytelength, offset);
		for(i = 0; i < bytelength; i++)
			deb_info("0x%x,", data[i]);
		deb_info(")\n");
		
		if( try_num > ERROR_TRY_MAX_NUM )	goto error;
		else				goto error_write_again;
       }

	return 0;

error:
	return 1;
 }




int
read_rtl2832_stdi2c(
	struct dvb_usb_device*	dib,
	unsigned short			dev_i2c_addr,
	unsigned char*			data,
	unsigned short			bytelength)
{
	int i;
	int ret = -ENOMEM;
	unsigned char  try_num;
	unsigned char data_tmp[128];	

	try_num = 0;	
error_write_again:		
	try_num ++;	
        

	if(bytelength >= 128)
	{
		deb_info("%s error bytelength >=128  \n", __FUNCTION__);
		goto error;
	}

	if( mutex_lock_interruptible(&dib->usb_mutex) )	goto error;
	
	ret = usb_control_msg(dib->udev,								/* pointer to device */
		usb_rcvctrlpipe( dib->udev,RTL2832_CTRL_ENDPOINT),			/* pipe to control endpoint */
		0,														/* USB message request value */
		SKEL_VENDOR_IN,											/* USB message request type value */
		dev_i2c_addr,											/* USB message value */
		0x0600,													/* USB message index value */
		data_tmp,												/* pointer to the receive buffer */
		bytelength,												/* length of the buffer */
		DIBUSB_I2C_TIMEOUT);									/* time to wait for the message to complete before timing out */

	mutex_unlock(&dib->usb_mutex);
 
	if (ret != bytelength)
	{
		deb_info("error try= %d!! %s: ret=%d, DA=0x%x, len=%d, data=(",try_num, __FUNCTION__, ret, dev_i2c_addr, bytelength);
		for(i = 0; i < bytelength; i++)
			deb_info("0x%x,", data_tmp[i]);
		deb_info(")\n");
		
		if( try_num > ERROR_TRY_MAX_NUM )	goto error;
		else				goto error_write_again;
	}

 	memcpy(data,data_tmp,bytelength);

	return 0;  
error: 
	return 1;

}



int 
write_rtl2832_stdi2c(
	struct dvb_usb_device*	dib,
	unsigned short			dev_i2c_addr,
	unsigned char*			data,
	unsigned short			bytelength)
{
	int i;
	int ret = -ENOMEM;  
	unsigned char  try_num;

	try_num = 0;	
error_write_again:		
	try_num ++;	

	if( mutex_lock_interruptible(&dib->usb_mutex) )	goto error;

	ret = usb_control_msg(dib->udev,								/* pointer to device */
		usb_sndctrlpipe( dib->udev,RTL2832_CTRL_ENDPOINT),			/* pipe to control endpoint */
		0,														/* USB message request value */
		SKEL_VENDOR_OUT,										/* USB message request type value */
		dev_i2c_addr,											/* USB message value */
		0x0610,													/* USB message index value */
		data,													/* pointer to the receive buffer */
		bytelength,												/* length of the buffer */
		DIBUSB_I2C_TIMEOUT);									/* time to wait for the message to complete before timing out */

	mutex_unlock(&dib->usb_mutex);
 
	if (ret != bytelength)
	{
		deb_info("error try= %d!! %s: ret=%d, DA=0x%x, len=%d, data=(",try_num, __FUNCTION__, ret, dev_i2c_addr, bytelength);
		for(i = 0; i < bytelength; i++)
			deb_info("0x%x,", data[i]);
		deb_info(")\n");
		
		if( try_num > ERROR_TRY_MAX_NUM )	goto error;
		else				goto error_write_again;		

	}
 
	return 0;
	
error:
	return 1;  
	
}






//3for return PUCHAR value

int
read_usb_sys_char_bytes(
	struct dvb_usb_device*	dib,
	RegType	type,
	unsigned short	byte_addr,
	unsigned char*	buf,
	unsigned short	byte_num)
{
	int ret = 1;

	if( byte_num != 1 && byte_num !=2 && byte_num !=4)
	{
		deb_info("error!! %s: length = %d \n", __FUNCTION__, byte_num);
		return 1;
	}


	if( mutex_lock_interruptible(&dib->usb_mutex) )	return 1;
		
	if( type == RTD2832U_USB )
	{
		ret = read_usb_register( dib , byte_addr , buf , byte_num);
	}
	else if ( type == RTD2832U_SYS )
	{
		ret = read_sys_register( dib , byte_addr , buf , byte_num);
	}
	
	mutex_unlock(&dib->usb_mutex);

	return ret;
	
}



int
write_usb_sys_char_bytes(
	struct dvb_usb_device*	dib,
	RegType	type,
	unsigned short	byte_addr,
	unsigned char*	buf,
	unsigned short	byte_num)
{
	int ret = 1;

	if( byte_num != 1 && byte_num !=2 && byte_num !=4)
	{
		deb_info("error!! %s: length = %d \n", __FUNCTION__, byte_num);
		return 1;
	}

	if( mutex_lock_interruptible(&dib->usb_mutex) )	return 1;	
	
	if( type == RTD2832U_USB )
	{
		ret = write_usb_register( dib , byte_addr , buf , byte_num);
	}
	else if ( type == RTD2832U_SYS )
	{
		ret = write_sys_register( dib , byte_addr , buf , byte_num);
	}
	
	mutex_unlock(&dib->usb_mutex);	
	
	return ret;
	
}


//3for return INT value
int
read_usb_sys_int_bytes(
	struct dvb_usb_device*	dib,
	RegType	type,
	unsigned short	byte_addr,
	unsigned short	n_bytes,
	int*	p_val)
{
	int	i;
	u8	val[LEN_4_BYTE];
	int	nbit_shift; 

	*p_val= 0;

	if (read_usb_sys_char_bytes( dib, type, byte_addr, val , n_bytes)) goto error;

	for (i= 0; i< n_bytes; i++)
	{				
		nbit_shift = i<<3 ;
		*p_val = *p_val + (val[i]<<nbit_shift );
       }

	return 0;
error:
	return 1;
			
}



int
write_usb_sys_int_bytes(
	struct dvb_usb_device*	dib,
	RegType	type,
	unsigned short	byte_addr,
	unsigned short	n_bytes,
	int	val)
{
	int	i;
	int	nbit_shift;
	u8	u8_val[LEN_4_BYTE];		

	for (i= n_bytes- 1; i>= 0; i--)
	{
		nbit_shift= i << 3;		
		u8_val[i] = (val>> nbit_shift) & 0xff;
    	}

	if( write_usb_sys_char_bytes( dib , type , byte_addr, u8_val , n_bytes) ) goto error;			

	return 0;
error:
	return 1;
}



int
write_rtl2836_demod_register(
	struct dvb_usb_device* dib,
	unsigned char			demod_device_addr,		
	unsigned char			page,
	unsigned char			offset,
	unsigned char			*data,
	unsigned short		bytelength)
{
	int i;
	unsigned char           datatmp;
	int                            try_num;
	switch(page)
	{
		//3 R/W regitser Once R/W "ONE BYTE"
		case PAGE_0:
		case PAGE_1:
		case PAGE_2:
		case PAGE_3:
		case PAGE_4:
			for(i=0; i<bytelength; i++)
			{
				try_num = 0;

label_write:
				if(write_demod_register(dib, demod_device_addr, page,  offset+i,  data+i, LEN_1_BYTE))
					goto error;

label_read:
				if(try_num >= 4)
					goto error;

				if(read_demod_register(dib, demod_device_addr, page,  offset+i,  &datatmp, LEN_1_BYTE))
				{
					try_num++;
					deb_info("%s fail read\n", __FUNCTION__);
					goto label_read;
				}
				
				if(datatmp != data[i])
				{
				       try_num++;
					deb_info("%s read != write\n", __FUNCTION__);   
					goto label_write;
				}								
			}
			break;

		default:
			goto error;
			break;
	}

	return 0;
	
error:
	return 1;
}



int 
read_rtl2836_demod_register(
	struct dvb_usb_device*dib,
	unsigned char			demod_device_addr,	
	unsigned char 		page,
	unsigned char 		offset,
	unsigned char*		data,
	unsigned short		bytelength)
{

       int i;
	unsigned char  tmp;
		
	switch(page)
	{
		//3 R/W regitser Once R/W "ONE BYTE"
		case PAGE_0:
		case PAGE_1:
		case PAGE_2:
		case PAGE_3:
		case PAGE_4:
			for(i=0; i<bytelength; i++)
			{
				if(read_demod_register(dib, demod_device_addr, page,  offset+i,  data+i, LEN_1_BYTE))
					goto error;
			}
			break;
				
       	case PAGE_6:
		case PAGE_7:
		case PAGE_8:
		case PAGE_9:
			if(read_demod_register(dib, demod_device_addr, page,  offset,  data, bytelength)) 
				goto error;
			break;	

              case PAGE_5:
		       if(read_demod_register(dib, demod_device_addr, page,  offset,  data, bytelength)) 
				goto error;

			if(read_demod_register(dib, demod_device_addr, PAGE_0, 0x00, &tmp, LEN_1_BYTE))//read page 0
				goto error;
			
		       break;

		default:
			goto error;
			break;
	}

	return 0;
	
error:
	return 1;

}










