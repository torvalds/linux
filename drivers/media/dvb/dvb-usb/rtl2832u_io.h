


#ifndef __USB_SYS_RTL2832_H__
#define __USB_SYS_RTL2832_H__

#include "dvb-usb.h"

extern int dvb_usb_rtl2832u_debug;
#define deb_info(args...) dprintk(dvb_usb_rtl2832u_debug,0x01,args)
#define deb_xfer(args...) dprintk(dvb_usb_rtl2832u_debug,0x02,args)
#define deb_rc(args...)   dprintk(dvb_usb_rtl2832u_debug,0x03,args)
#define LEN_1_BYTE					1
#define LEN_2_BYTE					2
#define LEN_4_BYTE					4


#define	RTL2832_CTRL_ENDPOINT	0x00
#define DIBUSB_I2C_TIMEOUT				5000

#define SKEL_VENDOR_IN  (USB_DIR_IN|USB_TYPE_VENDOR)
#define SKEL_VENDOR_OUT (USB_DIR_OUT|USB_TYPE_VENDOR)

#define SYS_BASE_ADDRESS	0x30 //0x3000
#define USB_BASE_ADDRESS	0x20 //0x2000 


#define RTL2832_DEMOD_ADDR	0x20
#define RTL2836_DEMOD_ADDR	0x3e
#define RTL2840_DEMOD_ADDR	0x44



typedef enum { RTD2832U_USB =1, 
	       RTD2832U_SYS =2,
	       RTD2832U_RC  =3	 
	     } RegType;

enum {
	PAGE_0 = 0,
	PAGE_1 = 1,
	PAGE_2 = 2,
	PAGE_3 = 3,
	PAGE_4 = 4,	
	PAGE_5 = 5,	
	PAGE_6 = 6,	
	PAGE_7 = 7,	
	PAGE_8 = 8,	
	PAGE_9 = 9,	
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//		remote control 
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int
read_rc_char_bytes(
	struct dvb_usb_device*	dib,
	RegType	type,
	unsigned short	byte_addr,
	unsigned char*	buf,
	unsigned short	byte_num);

int
write_rc_char_bytes(
	struct dvb_usb_device*	dib,
	RegType	type,
	unsigned short	byte_addr,
	unsigned char*	buf,
	unsigned short	byte_num);
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//3////////////////////////
//3for return PUCHAR value
//3///////////////////////

int
read_usb_sys_char_bytes(
	struct dvb_usb_device*	dib,
	RegType	type,
	unsigned short	byte_addr,
	unsigned char*	buf,
	unsigned short	byte_num);



int
write_usb_sys_char_bytes(
	struct dvb_usb_device*	dib,
	RegType	type,
	unsigned short	byte_addr,
	unsigned char*	buf,
	unsigned short	byte_num);



//3//////////////////
//3for return INT value
//3//////////////////

int
read_usb_sys_int_bytes(
	struct dvb_usb_device*	dib,
	RegType	type,
	unsigned short	byte_addr,
	unsigned short	n_bytes,
	int*	p_val);


int
write_usb_sys_int_bytes(
	struct dvb_usb_device*	dib,
	RegType	type,
	unsigned short	byte_addr,
	unsigned short	n_bytes,
	int	val);


/////////////////////////////////////////////////////////////////////////////////////////
//	Remote Control
////////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////

void
platform_wait(
	unsigned long nMinDelayTime);



#if 0
//3//////////////////
//3for std i2c r/w
//3//////////////////

int
read_rtl2832_stdi2c(
	struct dvb_usb_device*	dib,
	unsigned short			dev_i2c_addr,
	unsigned char*			data,
	unsigned short			bytelength);

int 
write_rtl2832_stdi2c(
	struct dvb_usb_device*	dib,
	unsigned short			dev_i2c_addr,
	unsigned char*			data,
	unsigned short			bytelength);

#endif



int 
read_demod_register(
	struct dvb_usb_device*dib,
	unsigned char			demod_device_addr,	
	unsigned char 		page,
	unsigned char 		offset,
	unsigned char*		data,
	unsigned short		bytelength);




int
write_demod_register(
	struct dvb_usb_device*dib,
	unsigned char			demod_device_addr,		
	unsigned char			page,
	unsigned char			offset,
	unsigned char			*data,
	unsigned short		bytelength);



int 
read_rtl2832_tuner_register(
	struct dvb_usb_device	*dib,
	unsigned char			device_address,
	unsigned char			offset,
	unsigned char			*data,
	unsigned short		bytelength);




int write_rtl2832_tuner_register(
	struct dvb_usb_device *dib,
	unsigned char			device_address,
	unsigned char			offset,
	unsigned char			*data,
	unsigned short		bytelength);





int 
	write_rtl2832_stdi2c(
	struct dvb_usb_device*	dib,
	unsigned short			dev_i2c_addr,
	unsigned char*			data,
	unsigned short			bytelength);



int
	read_rtl2832_stdi2c(
	struct dvb_usb_device*	dib,
	unsigned short			dev_i2c_addr,
	unsigned char*			data,
	unsigned short			bytelength);


int
write_rtl2836_demod_register(
	struct dvb_usb_device*dib,
	unsigned char			demod_device_addr,		
	unsigned char			page,
	unsigned char			offset,
	unsigned char			*data,
	unsigned short		bytelength);


int 
read_rtl2836_demod_register(
	struct dvb_usb_device*dib,
	unsigned char			demod_device_addr,	
	unsigned char 		page,
	unsigned char 		offset,
	unsigned char*		data,
	unsigned short		bytelength);



////////////////////////////////////

#define BIT0		0x00000001
#define BIT1		0x00000002
#define BIT2		0x00000004
#define BIT3		0x00000008
#define BIT4		0x00000010
#define BIT5		0x00000020
#define BIT6		0x00000040
#define BIT7		0x00000080
#define BIT8		0x00000100
#define BIT9		0x00000200
#define BIT10	0x00000400
#define BIT11	0x00000800
#define BIT12	0x00001000
#define BIT13	0x00002000
#define BIT14	0x00004000
#define BIT15	0x00008000
#define BIT16	0x00010000
#define BIT17	0x00020000
#define BIT18	0x00040000
#define BIT19	0x00080000
#define BIT20	0x00100000
#define BIT21	0x00200000
#define BIT22	0x00400000
#define BIT23	0x00800000
#define BIT24	0x01000000
#define BIT25	0x02000000
#define BIT26	0x04000000
#define BIT27	0x08000000
#define BIT28	0x10000000
#define BIT29	0x20000000
#define BIT30	0x40000000
#define BIT31	0x80000000


#endif



