
#ifndef _RTL2832U_H_
#define _RTL2832U_H_



#include "dvb-usb.h"

#ifndef USB_VID_REALTEK
	#define	USB_VID_REALTEK						0x0BDA
#endif
#define	USB_PID_RTL2832_WARM					0x2832 
#define	USB_PID_RTL2838_WARM					0x2838 
#define	USB_PID_RTL2836_WARM					0x2836
#define	USB_PID_RTL2839_WARM					0x2839
#define	USB_PID_RTL2840_WARM					0x2840
#define	USB_PID_RTL2841_WARM					0x2841
#define	USB_PID_RTL2834_WARM					0x2834 
#define	USB_PID_RTL2837_WARM          			0x2837
#define	USB_PID_RTL2820_WARM					0x2820 
#define	USB_PID_RTL2821_WARM          			0x2821
#define	USB_PID_RTL2822_WARM					0x2822 
#define	USB_PID_RTL2823_WARM          			0x2823
#define	USB_PID_RTL2810_WARM					0x2810 
#define	USB_PID_RTL2811_WARM          			0x2811
#define	USB_PID_RTL2824_WARM					0x2824 
#define	USB_PID_RTL2825_WARM          			0x2825

#ifndef USB_VID_DEXATEK
	#define	USB_VID_DEXATEK						0x1D19
#endif
#define	USB_PID_DEXATEK_1101					0x1101
#define	USB_PID_DEXATEK_1102					0x1102
#define	USB_PID_DEXATEK_1103					0x1103
#define	USB_PID_DEXATEK_1104					0x1104
#define	USB_PID_DEXATEK_1105					0x1105
#define	USB_PID_DEXATEK_1106					0x1106
#define	USB_PID_DEXATEK_1107					0x1107
#define	USB_PID_DEXATEK_1108					0x1108
#define	USB_PID_DEXATEK_2101					0x2101
#define	USB_PID_DEXATEK_8202					0x8202
#define	USB_PID_DEXATEK_9201					0x9201
#define	USB_PID_DEXATEK_3103					0x3103
#define	USB_PID_DEXATEK_9202					0x9202

#ifndef USB_VID_TERRATEC
	#define USB_VID_TERRATEC					0x0CCD
#endif
#define	USB_PID_TERRATEC_00A9					0x00A9
#define	USB_PID_TERRATEC_00B3					0x00B3
#define	USB_PID_TERRATEC_00D3					0x00D3
#define	USB_PID_TERRATEC_00D4					0x00D4
#define	USB_PID_TERRATEC_00E0					0x00E0

#ifndef USB_VID_AZUREWAVE_2
	#define	USB_VID_AZUREWAVE_2					0x13D3
#endif
	#define	USB_PID_AZUREWAVE_3234				0x3234
	#define	USB_PID_AZUREWAVE_3274				0x3274
	#define	USB_PID_AZUREWAVE_3282				0x3282


#ifndef USB_VID_THP
	#define	USB_VID_THP							0x1554
#endif
#define	USB_PID_THP_5013						0x5013
#define	USB_PID_THP_5020						0x5020	
#define	USB_PID_THP_5026						0x5026	

#ifndef USB_VID_KWORLD_1ST
	#define	USB_VID_KWORLD_1ST					0x1B80
#endif
#define	USB_PID_KWORLD_D393						0xD393
#define	USB_PID_KWORLD_D394						0xD394
#define	USB_PID_KWORLD_D395						0xD395
#define	USB_PID_KWORLD_D396						0xD396
#define	USB_PID_KWORLD_D397						0xD397
#define	USB_PID_KWORLD_D398						0xD398
#define	USB_PID_KWORLD_D39A						0xD39A
#define	USB_PID_KWORLD_D39B						0xD39B
#define	USB_PID_KWORLD_D39C						0xD39C
#define	USB_PID_KWORLD_D39E						0xD39E
#define	USB_PID_KWORLD_E77B						0xE77B
#define	USB_PID_KWORLD_D3A1						0xD3A1
#define	USB_PID_KWORLD_D3A4						0xD3A4
#define	USB_PID_KWORLD_E41D						0xE41D

#ifndef USB_VID_GOLDENBRIDGE
	#define	USB_VID_GOLDENBRIDGE				0x1680
#endif
#define	USB_PID_GOLDENBRIDGE_WARM				0xA332

#ifndef USB_VID_YUAN
	#define	USB_VID_YUAN						0x1164
#endif
#define	USB_PID_YUAN_WARM						0x6601
#define USB_PID_YUAN_WARM80						0x3280
#define USB_PID_YUAN_WARM84	                    0x3284

#ifndef USB_PID_GTEK_WARM_0837
	#define	USB_PID_GTEK_WARM_0837				0x0837	
#endif
#define	USB_PID_GTEK_WARM_A803					0xA803
#define	USB_PID_GTEK_WARM_B803					0xB803
#define	USB_PID_GTEK_WARM_C803					0xC803
#define	USB_PID_GTEK_WARM_D803					0xD803
#define	USB_PID_GTEK_WARM_C280					0xC280
#define	USB_PID_GTEK_WARM_D286					0xD286
#define	USB_PID_GTEK_WARM_0139					0x0139
#define	USB_PID_GTEK_WARM_A683					0xA683

#ifndef USB_VID_LEADTEK
	#define	USB_VID_LEADTEK						0x0413
#endif
#define	USB_PID_LEADTEK_WARM_1					0x6680
#define	USB_PID_LEADTEK_WARM_2					0x6F11
#define	USB_PID_WINFAST_DTV_DONGLE_MINI				0x6a03

#ifndef  USB_VID_COMPRO
	#define USB_VID_COMPRO						0x185B
#endif 
#define USB_PID_COMPRO_WARM_0620				0x0620
#define USB_PID_COMPRO_WARM_0630				0x0630
#define USB_PID_COMPRO_WARM_0640				0x0640
#define USB_PID_COMPRO_WARM_0650				0x0650
#define USB_PID_COMPRO_WARM_0680				0x0680
#define USB_PID_COMPRO_WARM_9580				0x9580
#define USB_PID_COMPRO_WARM_9550				0x9550
#define USB_PID_COMPRO_WARM_9540				0x9540
#define USB_PID_COMPRO_WARM_9530				0x9530
#define USB_PID_COMPRO_WARM_9520				0x9520


#define RTD2831_URB_SIZE				4096// 39480
#define RTD2831_URB_NUMBER				10 //  4

///////////////////////////////////////////////////////////////////////////////////////////////////////
//			remote control 
///////////////////////////////////////////////////////////////////////////////////////////////////////


//define rtl283u rc register address
#define IR_RX_BUF			0xFC00
#define IR_RX_IE			0xFD00
#define IR_RX_IF			0xFD01
#define IR_RX_CTRL			0xFD02
#define IR_RX_CONFIG			0xFD03
#define IR_MAX_DURATION0		0xFD04
#define IR_MAX_DURATION1		0xFD05
#define IR_IDLE_LEN0			0xFD06
#define IR_IDLE_LEN1			0xFD07
#define IR_GLITCH_LEN			0xFD08
#define IR_RX_BUFFER_CTRL		0xFD09
#define IR_RX_BUFFER_DATA		0xFD0A
#define IR_RX_BC			0xFD0B
#define IR_RX_CLK			0xFD0C
#define IR_RX_C_COUNT_L			0xFD0D
#define IR_RX_C_COUNT_H			0xFD0E

#define IR_SUSPEND_CTRL			0xFD10
#define IR_Err_Tolerance_CTRL		0xFD11
#define IR_UNIT_LEN			0xFD12
#define IR_Err_Tolerance_LEN		0xFD13
#define IR_MAX_H_Tolerance_LEN		0xFD14
#define IR_MAX_L_Tolerance_LEN		0xFD15
#define IR_MASK_CTRL			0xFD16
#define IR_MASK_DATA			0xFD17
#define IR_RESUME_MASK_ADDR		0xFD18
#define IR_RESUME_MASK_T_LEN		0xFD19

#define USB_CTRL			0x0010
#define SYS_GPD				0x0004
#define SYS_GPOE			0x0003
#define SYS_GPO				0x0001
#define RC_USE_DEMOD_CTL1		0x000B

//define use len 
#define LEN_1				0x01
#define LEN_2				0x02
#define LEN_3				0x03
#define LEN_4				0x04



//function
int rtl2832u_remoto_control_initial_setting(struct dvb_usb_device *d);
#define	USB_EPA_CTL			0x0148

///////////////////////////////////////////////////////////////////////////////////////////////////////
//decode control para define
#define frt0_para1 			0x3c
#define frt0_para2 			0x20
#define frt0_para3			0x7f
#define frt0_para4      		0x05
#define frt0_bits_num 			0x80
#define frt0_BITS_mask 			0x01
#define frt0_BITS_mask0			0x00
#define frt0_BITS_mask1			0x00
#define frt0_BITS_mask2			0x0f
#define frt0_BITS_mask3			0xff

#define frt1_para1 			0x12
#define frt1_para3 			0x1a
#define frt1_para2 			0x7f
#define frt1_para4 			0x80
#define frt1_para5 			0x01
#define frt1_para6 			0x02
#define frt1_bits_num 			0x80
#define frt1_para_uc_1			0x81
#define frt1_para_uc_2 			0x82
#define frt1_bits_mask0			0x00
#define frt1_bits_mask1			0x00
#define frt1_bits_mask2			0x7f
#define frt1_bits_mask3			0xff

#define frt2_para1  			0x0a
#define frt2_para2  			0xFF
#define frt2_para3 			0xb0
#define frt2_para4 			0xc6
#define frt2_para5  			0x30
#define frt2_para6  			0x1b
#define frt2_para7  			0x8f
#define frt2_para8  			0x89
#define frt2_para9  			0x7f
#define frt2_para10 			0x60
#define frt2_para11 			0x38
#define frt2_para12  			0x15
#define frt2_bits_num			0x80
#define frt2_bits_mask0			0x00
#define frt2_bits_mask1			0x00
#define frt2_bits_mask2			0xff
#define frt2_bits_mask3			0xff
///////////////////////////////////////////////////////////////////////////////////////////////////////
//			remote control 
///////////////////////////////////////////////////////////////////////////////////////////////////////
enum protocol_type_for_RT{
				RT_RC6=0,
				RT_RC5,
				RT_NEC,
				RT_TEST
};
// op define	
enum OP{
	OP_NO	=0,
	OP_AND	  ,
	OP_OR
};

typedef enum RT_UC_CODE_STATE{
	WAITING_6T,
	WAITING_2T_AFTER_6T,
	WAITING_NORMAL_BITS
}RT_UC_CODE_STATE;

//struct define
typedef struct RT_rc_set_reg_struct{	
		u8	type;	
		u16 	address;
		u8      data;
		u8	op;
		u8	op_mask;	
}RT_rc_set_reg_struct;

enum RTL2832U_RC_STATE{
	RC_NO_SETTING=0,
	RC_INSTALL_OK,
	RC__POLLING_OK,
	RC_STOP_OK
};
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

extern struct dvb_frontend * rtl2832u_fe_attach(struct dvb_usb_device *d);

#endif



