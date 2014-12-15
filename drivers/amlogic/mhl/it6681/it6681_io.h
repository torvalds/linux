///*****************************************
//  Copyright (C) 2009-2014
//  ITE Tech. Inc. All Rights Reserved
//  Proprietary and Confidential
///*****************************************
//   @file   <IT6811.h>
//   @author Hermes.Wu@ite.com.tw
//   @date   2013/05/07
//   @fileversion: ITE_IT6811_6607_SAMPLE_1.06
//******************************************/
#ifndef _IT6681_IO_H_
#define _IT6681_IO_H_

#ifdef __cplusplus
extern "C" {
#endif

void delay1ms(USHORT ms);
unsigned long it6681_get_tick_count(void);
unsigned char hdmirxrd( unsigned char offset );
void hdmirxwr( unsigned char offset, unsigned char value );
void hdmirxset( unsigned char offset, unsigned char mask, unsigned char wdata );

unsigned char hdmitxrd( unsigned char offset );
void hdmitxwr( unsigned char offset, unsigned char value );
void hdmitxset( unsigned char offset, unsigned char mask, unsigned char wdata );
void hdmitxbrd( unsigned char offset, void *buffer, unsigned char length );

unsigned char mhltxrd( unsigned char offset );
void mhltxwr( unsigned char offset, unsigned char value );
void mhltxset( unsigned char offset, unsigned char mask, unsigned char wdata );

#if _SUPPORT_RCP_
    void mhl_RCP_handler(struct it6681_dev_data *it6681);
#endif

#if _SUPPORT_UCP_
    void mhl_UCP_handler(struct it6681_dev_data *it6681);
#endif
#if _SUPPORT_UCP_MOUSE_
    void mhl_UCP_mouse_handler( unsigned char key, int x, int y);
#endif

void SetLED_MHL_Out( char Val );
void SetLED_PathEn( char Val );
void SetLED_MHL_CBusEn( char Val );
void SetLED_HDMI_InStable( char Val );

void set_operation_mode( unsigned char mode );
void set_vbus_output( unsigned char enable );

//void it6681_copy_edid(void);

#ifdef __cplusplus
}
#endif

#endif
