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
#ifndef _IT6681_DRV_H_
#define _IT6681_DRV_H_

#ifndef IT6681_EDID_MAX_BLOCKS
    #define IT6681_EDID_MAX_BLOCKS 4
#endif
#define IT6681_EDID_MAX_LENGTH (IT6681_EDID_MAX_BLOCKS*128)
extern unsigned char it6681_edid_buf[ IT6681_EDID_MAX_LENGTH ];

void hdmirx_irq( void );
int hdmitx_ini(void);
void hdmitx_pwrdn( void );
void hdmitx_pwron( void );
void hdmitx_irq( struct it6681_dev_data *it6811 );

void cbus_send_mscmsg( struct it6681_dev_data *it6681 );
void hdmitx_set_termination(int ena_term);

void it6681_disable_cbus_1k_detection(void);
void it6681_enable_cbus_1k_detection(void);
void it668x_set_trans_mode( char mode );

#if _SUPPORT_HDCP_
    int Hdmi_HDCP_handler(struct it6681_dev_data *it6811 );
    #if _SHOW_HDCP_INFO_
    static void hdcpsts( void );
    #endif
#endif

#if _SUPPORT_RCP_
    void mhl_parse_RCPkey(struct it6681_dev_data *it6681);
#endif

#if _SUPPORT_RAP_ 
    void mhl_parse_RAPkey(struct it6681_dev_data *it6681);
#endif

#if _SUPPORT_UCP_
    void mhl_parse_UCPkey(struct it6681_dev_data *it6681);
#endif

#if _SUPPORT_UCP_MOUSE_
    void mhl_parse_MOUSEkey(struct it6681_dev_data *it6681);
#endif
struct it6681_dev_data* get_it6681_dev_data(void);

void it6681_dump_register( void );

#endif
