// SPDX-License-Identifier: GPL-2.0
/********************************************************************************
 *
 *  Copyright (C) 2017 	NEXTCHIP Inc. All rights reserved.
 *  Module		: Jaguar1 Device Driver
 *  Description	: coax_protocol.c
 *  Author		:
 *  Date         :
 *  Version		: Version 1.0
 *
 ********************************************************************************
 *  History      :
 *
 *
 ********************************************************************************/
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/unistd.h>
#include "jaguar1_common.h"
#include "jaguar1_coax_table.h"
#include "jaguar1_coax_protocol.h"
#include "jaguar1_video.h"

extern unsigned int acp_mode_enable;

/*=======================================================================================================
 ********************************************************************************************************
 **************************** Coaxial protocol up stream function ***************************************
 ********************************************************************************************************
 * Coaxial protocol up stream Flow
 * 1. Up stream initialize       -  coax_tx_init
 * 2. Fill upstream data & Send  -  coax_tx_cmd_send
 *
 * Coaxial protocol up stream register(example: channel 0)
 * (3x00) tx_baud               : 1 bit duty
 * (3x02) tx_pel_baud           : 1 bit duty of pelco(SD)
 * (3x03) tx_line_pos0          : up stream line position(low)
 * (3x04) tx_line_pos1          : up stream line position(high)
 * (3x05) tx_line_count         : up stream output line number in 1 frame
 * (3x07) tx_pel_line_pos0      : up stream line position of pelco(low)
 * (3x08) tx_pel_line_pos1      : up stream line position of pelco(high)
 * (3x0A) tx_line_count_max     : up stream output total line
 * (3x0B) tx_mode               : up stream Mode set (ACP, CCP, TCP)
 * (3x0D) tx_sync_pos0          : up stream sync start position(low)
 * (3x0E) tx_sync_pos1          : up stream sync start position(high)
 * (3x2F) tx_even               : up stream SD..Interlace
 * (3x0C) tx_zero_length        : Only CVI 4M
 ========================================================================================================*/
static NC_VIVO_CH_FORMATDEF g_coax_format;


/**************************************************************************************
 * @desc
 * 	JAGUAR1's This function initializes the register associated with the UP Stream..
 *
 * @param_in		(NC_VD_COAX_Tx_Init_STR *)coax_tx_mode			UP Stream Initialize structure
 *
 * @return   	void  		       								None
 *
 * ioctl : IOC_VDEC_COAX_TX_INIT
 ***************************************************************************************/
static NC_VD_ACP_CMDDEF_STR *__NC_VD_ACP_Get_CommandFormat_Get( NC_COAX_CMD_DEF def )
{
	NC_VD_ACP_CMDDEF_STR *pRet = &coax_cmd_lists[def];
	if( pRet == NULL )
	{
		printk("Not Supported format Yet!!!(%d)\n",def);
	}
	return  pRet;
}

static NC_VD_COAX_Init_STR *__NC_VD_COAX_InitFormat_Get( NC_VIVO_CH_FORMATDEF def )
{
	NC_VD_COAX_Init_STR *pRet = &coax_init_lists[def];
	if( pRet == NULL )
	{
		printk("Not Supported format Yet!!!(%d)\n",def);
	}
	return  pRet;
}

static NC_VD_COAX_Init_STR *__NC_VD_COAX_16bit_InitFormat_Get( NC_VIVO_CH_FORMATDEF def )
{
	NC_VD_COAX_Init_STR *pRet = &coax_acp_16bit_init_lists[def];
	if( pRet == NULL )
	{
		printk("Not Supported format Yet!!!(%d)\n",def);
	}
	return  pRet;
}

static int __NC_VD_COAX_Command_Each_Copy( unsigned char *Dst, int *Src )
{
	int items = 0;

	while( Src[items] != EOD )
	{
		Dst[items] = Src[items];
		items++;
	}

	return items;
}

static int __NC_VD_COAX_Command_Copy( NC_FORMAT_STANDARD format, NC_VIVO_CH_FORMATDEF vivofmt, unsigned char *Dst, NC_VD_ACP_CMDDEF_STR *pCMD )
{
	int cmd_cnt = 0;

	if( format == FMT_SD )
	{
		cmd_cnt = __NC_VD_COAX_Command_Each_Copy( Dst, pCMD->sd );
	}
	else if( (format == FMT_AHD20) || (format == FMT_AHD30) )
	{
		if( vivofmt == AHD30_5M_20P || vivofmt == AHD30_5M_12_5P || vivofmt == AHD30_5_3M_20P   )
			cmd_cnt = __NC_VD_COAX_Command_Each_Copy( Dst, pCMD->ahd_4_5m );
		else if( vivofmt == AHD30_4M_30P || vivofmt == AHD30_4M_25P || vivofmt == AHD30_4M_15P )
			cmd_cnt = __NC_VD_COAX_Command_Each_Copy( Dst, pCMD->ahd_4_5m );
		else
			cmd_cnt = __NC_VD_COAX_Command_Each_Copy( Dst, pCMD->ahd_8bit );
	}
	else if( format == FMT_CVI )
	{
		cmd_cnt= __NC_VD_COAX_Command_Each_Copy( Dst, pCMD->cvi_cmd );
	}
	else if( format == FMT_TVI )
	{
		if( (vivofmt == TVI_4M_30P) || (vivofmt == TVI_4M_25P) || (vivofmt == TVI_4M_15P) )
			cmd_cnt = __NC_VD_COAX_Command_Each_Copy( Dst, pCMD->tvi_v2_0 );
		else
			cmd_cnt = __NC_VD_COAX_Command_Each_Copy( Dst, pCMD->tvi_v1_0 );
	}
	else
		printk("NC_VD_COAX_Tx_Command_Send::Command Copy Error!!\n");


	return cmd_cnt;
}

static int __NC_VD_COAX_16bit_Command_Copy( NC_FORMAT_STANDARD format, NC_VIVO_CH_FORMATDEF vivofmt, unsigned char *Dst, NC_VD_ACP_CMDDEF_STR *pCMD )
{
	int cmd_cnt = 0;

	if( (vivofmt == AHD20_720P_25P) || (vivofmt == AHD20_720P_30P) ||\
			(vivofmt == AHD20_720P_25P_EX) || (vivofmt == AHD20_720P_30P_EX) ||\
			(vivofmt == AHD20_720P_25P_EX_Btype) || (vivofmt == AHD20_720P_30P_EX_Btype) )
	{
		cmd_cnt = __NC_VD_COAX_Command_Each_Copy( Dst, pCMD->ahd_16bit );
	}
	else if( (vivofmt == CVI_4M_25P) || (vivofmt == CVI_4M_30P) ||\
			(vivofmt == CVI_8M_15P) || (vivofmt == CVI_8M_12_5P) )
	{
		cmd_cnt = __NC_VD_COAX_Command_Each_Copy( Dst, pCMD->cvi_new_cmd );
	}
	else
	{
		printk("[drv_coax] Can not send commands!! Unsupported format!!\n" );
		return 0;
	}

	return cmd_cnt;
}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's This function initializes the register associated with the UP Stream..
 *
 * @param_in		(NC_VD_COAX_Tx_Init_STR *)coax_tx_mode			UP Stream Initialize structure
 *
 * @return   	void  		       								None
 *
 * ioctl : IOC_VDEC_COAX_TX_INIT
 ***************************************************************************************/
void coax_tx_init( void *p_param )
{
	NC_VD_COAX_STR *coax_tx = (NC_VD_COAX_STR*)p_param;
	NC_VD_COAX_Init_STR *CoaxVal = __NC_VD_COAX_InitFormat_Get( coax_tx->vivo_fmt );

	int ch = coax_tx->ch;
	unsigned char distance = 0;

	g_coax_format =  coax_tx->vivo_fmt;
	printk("[drv_coax]ch::%d >>> fmt::%s\n", ch, CoaxVal->name );

#if 0
	// Cable distance check
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->devnum], 0xFF, 0x13);          // BANK 13
	distance = gpio_i2c_read(jaguar1_i2c_addr[coax_tx->devnum], (0xA0+ch)); // 0:short, 1:100m, 2:200m, 3:300m, 4:400m, 5:500m, 0x0F:Unknown

	distance = distance&0x0F;
	if(distance == 0x0F)
	{
		printk("DRV::Cable distance Unknown!!\n");
		distance = 0;
	}
	else
		printk("DRV::Cable distance(%x)\n", distance);
#endif

	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x01);  // BANK 1
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xA8, 0x00);  // MPP_TST_SEL1
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xA9, 0x00);  // MPP_TST_SEL2
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xAA, 0x00);  // MPP_TST_SEL3
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xAB, 0x00);  // MPP_TST_SEL4

	// Coaxial each mode set
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x02+((ch%4)/2));  // BANK 2, 3
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x7C+((ch%2)*0x80), CoaxVal->rx_src);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x7D+((ch%2)*0x80), CoaxVal->rx_slice_lev);

	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x02+((ch%4)/2));
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x00+((ch%2)*0x80), CoaxVal->tx_baud[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x02+((ch%2)*0x80), CoaxVal->tx_pel_baud[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x03+((ch%2)*0x80), CoaxVal->tx_line_pos0[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x04+((ch%2)*0x80), CoaxVal->tx_line_pos1[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x05+((ch%2)*0x80), CoaxVal->tx_line_count);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x07+((ch%2)*0x80), CoaxVal->tx_pel_line_pos0[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x08+((ch%2)*0x80), CoaxVal->tx_pel_line_pos1[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0A+((ch%2)*0x80), CoaxVal->tx_line_count_max);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0B+((ch%2)*0x80), CoaxVal->tx_mode);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0D+((ch%2)*0x80), CoaxVal->tx_sync_pos0[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0E + ((ch%2)*0x80), CoaxVal->tx_sync_pos1[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x2F+((ch%2)*0x80), CoaxVal->tx_even);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0C+((ch%2)*0x80), CoaxVal->tx_zero_length);

#if 0
	// MPP Coaxial mode select Ch1~4
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x01);  // BANK 1
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xA8, 0x08);  // MPP_TST_SEL1
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xA9, 0x09);  // MPP_TST_SEL2
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xAA, 0x0A);  // MPP_TST_SEL3
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xAB, 0x0B);  // MPP_TST_SEL4

	// Coaxial each mode set
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x05+ch%4);  // BANK 5
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x2F, 0x00);       // MPP_H_INV, MPP_V_INV, MPP_F_INV
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x30, 0xE0);       // MPP_H_S[7~4], MPP_H_E[3:0]
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x31, 0x43);       // MPP_H_S[7:0]
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x32, 0xA2);       // MPP_H_E[7:0]
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x7C, CoaxVal->rx_src);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x7D, CoaxVal->rx_slice_lev);

	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x02+((ch%4)/2));

	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x00+((ch%2)*0x80), CoaxVal->tx_baud[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x02+((ch%2)*0x80), CoaxVal->tx_pel_baud[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x03+((ch%2)*0x80), CoaxVal->tx_line_pos0[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x04+((ch%2)*0x80), CoaxVal->tx_line_pos1[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x05+((ch%2)*0x80), CoaxVal->tx_line_count);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x07+((ch%2)*0x80), CoaxVal->tx_pel_line_pos0[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x08+((ch%2)*0x80), CoaxVal->tx_pel_line_pos1[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0A+((ch%2)*0x80), CoaxVal->tx_line_count_max);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0B+((ch%2)*0x80), CoaxVal->tx_mode);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0D+((ch%2)*0x80), CoaxVal->tx_sync_pos0[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0E + ((ch%2)*0x80), CoaxVal->tx_sync_pos1[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x2F+((ch%2)*0x80), CoaxVal->tx_even);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0C+((ch%2)*0x80), CoaxVal->tx_zero_length);
#endif

#if DBG_TX_INIT_PRINT
	printk("[drv]tx_src:            5x7C>> 0x%02X\n", CoaxVal->rx_src );
	printk("[drv]tx_slice_lev:      5x7D>> 0x%02X\n", CoaxVal->rx_slice_lev );
	printk("[drv]tx_pel_baud:       3x02>> 0x%02X\n", CoaxVal->tx_baud[distance] );
	printk("[drv]tx_pel_line_pos0:  3x07>> 0x%02X\n", CoaxVal->tx_pel_line_pos0[distance] );
	printk("[drv]tx_pel_line_pos1:  3x08>> 0x%02X\n", CoaxVal->tx_pel_line_pos1[distance] );
	printk("[drv]tx_mode:           3x0B>> 0x%02X\n", CoaxVal->tx_mode );
	printk("[drv]tx_baud:           3x00>> 0x%02X\n", CoaxVal->tx_baud[distance]);
	printk("[drv]tx_line_pos0:      3x03>> 0x%02X\n", CoaxVal->tx_line_pos0[distance] );
	printk("[drv]tx_line_pos1:      3x04>> 0x%02X\n", CoaxVal->tx_line_pos1[distance] );
	printk("[drv]tx_line_count:     3x05>> 0x%02X\n", CoaxVal->tx_line_count );
	printk("[drv]tx_line_count_max: 3x0A>> 0x%02X\n", CoaxVal->tx_line_count_max );
	printk("[drv]tx_sync_pos0:      3x0D>> 0x%02X\n", CoaxVal->tx_sync_pos0[distance] );
	printk("[drv]tx_sync_pos1:      3x0E>> 0x%02X\n", CoaxVal->tx_sync_pos1[distance] );
	printk("[drv]tx_even:           3x2F>> 0x%02X\n", CoaxVal->tx_even );
	printk("[drv]tx_zero_length:    3x0C>> 0x%02X\n", CoaxVal->tx_zero_length);
#endif

}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's This function initializes the register associated with the UP Stream..
 *
 * @param_in		(NC_VD_COAX_Tx_Init_STR *)coax_tx_mode			UP Stream Initialize structure
 *
 * @return   	void  		       								None
 *
 * ioctl : IOC_VDEC_COAX_TX_INIT
 ***************************************************************************************/
void coax_tx_16bit_init( void *p_param )
{
	NC_VD_COAX_STR *coax_tx = (NC_VD_COAX_STR*)p_param;
	NC_VD_COAX_Init_STR *CoaxVal;

	int ch = coax_tx->ch;
	int fmt = coax_tx->vivo_fmt;
	unsigned char distance = 0;

	if( (fmt == AHD20_720P_25P) || (fmt == AHD20_720P_30P) ||\
			(fmt == AHD20_720P_25P_EX) || (fmt == AHD20_720P_30P_EX) ||\
			(fmt == AHD20_720P_25P_EX_Btype) || (fmt == AHD20_720P_30P_EX_Btype)\
	  )
	{
		printk("[drv_coax]Ch: %d ACP 16bit initialize!!!\n", ch );
	}
	else if( (fmt == CVI_4M_25P) || (fmt == CVI_4M_30P) ||\
			(fmt == CVI_8M_15P) || (fmt == CVI_8M_12_5P) )
	{
		printk("[drv_coax]Ch: %d CVI New Protocol initialize!!!\n", ch );
	}
	else
	{
		printk("[drv_coax]Ch: %d Can not initialize!! Unsupported format!!\n", ch );
		return;
	}

	CoaxVal = __NC_VD_COAX_16bit_InitFormat_Get( coax_tx->vivo_fmt );
	printk("[drv_coax]Ch: %d Format >>>>> %s\n", ch, CoaxVal->name );

#if 0
	// Cable distance check
	gpio_i2c_write(jaguar1_i2c_addr[chip_num], 0xFF, 0x13);          // BANK 13
	distance = gpio_i2c_read(jaguar1_i2c_addr[chip_num], (0xA0+ch)); // 0:short, 1:100m, 2:200m, 3:300m, 4:400m, 5:500m, 0x0F:Unknown

	distance = distance&0x0F;
	if(distance == 0x0F)
	{
		printk("DRV::Cable distance Unknown!!\n");
		distance = 0;
	}
	else
		printk("DRV::Cable distance(%x)\n", distance);
#endif

	// MPP Coaxial mode select Ch1~4
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x01);  // BANK 1
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xA8, 0x00);  // MPP_TST_SEL1
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xA9, 0x00);  // MPP_TST_SEL2
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xAA, 0x00);  // MPP_TST_SEL3
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xAB, 0x00);  // MPP_TST_SEL4

	// Coaxial each mode set
	//gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x05+ch%4);  // BANK 5
	//gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x2F, 0x00);       // MPP_H_INV, MPP_V_INV, MPP_F_INV
	//gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x30, 0xE0);       // MPP_H_S[7~4], MPP_H_E[3:0]
	//gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x31, 0x43);       // MPP_H_S[7:0]
	//gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x32, 0xA2);       // MPP_H_E[7:0]

    gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x02+((ch%4)/2));  // BANK 2, 3
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x7C, CoaxVal->rx_src);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x7D, CoaxVal->rx_slice_lev);

	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x02+((ch%4)/2));

	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x00+((ch%2)*0x80), CoaxVal->tx_baud[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x02+((ch%2)*0x80), CoaxVal->tx_pel_baud[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x03+((ch%2)*0x80), CoaxVal->tx_line_pos0[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x04+((ch%2)*0x80), CoaxVal->tx_line_pos1[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x05+((ch%2)*0x80), CoaxVal->tx_line_count);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x07+((ch%2)*0x80), CoaxVal->tx_pel_line_pos0[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x08+((ch%2)*0x80), CoaxVal->tx_pel_line_pos1[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0A+((ch%2)*0x80), CoaxVal->tx_line_count_max);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0B+((ch%2)*0x80), CoaxVal->tx_mode);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0D+((ch%2)*0x80), CoaxVal->tx_sync_pos0[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0E + ((ch%2)*0x80), CoaxVal->tx_sync_pos1[distance]);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x2F+((ch%2)*0x80), CoaxVal->tx_even);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0C+((ch%2)*0x80), CoaxVal->tx_zero_length);

#if DBG_TX_INIT_PRINT
	printk("[drv]tx_src:            5x7C>> 0x%02X\n", CoaxVal->rx_src );
	printk("[drv]tx_slice_lev:      5x7D>> 0x%02X\n", CoaxVal->rx_slice_lev );
	printk("[drv]tx_pel_baud:       3x02>> 0x%02X\n", CoaxVal->tx_baud[distance] );
	printk("[drv]tx_pel_line_pos0:  3x07>> 0x%02X\n", CoaxVal->tx_pel_line_pos0[distance] );
	printk("[drv]tx_pel_line_pos1:  3x08>> 0x%02X\n", CoaxVal->tx_pel_line_pos1[distance] );
	printk("[drv]tx_mode:           3x0B>> 0x%02X\n", CoaxVal->tx_mode );
	printk("[drv]tx_baud:           3x00>> 0x%02X\n", CoaxVal->tx_baud[distance]);
	printk("[drv]tx_line_pos0:      3x03>> 0x%02X\n", CoaxVal->tx_line_pos0[distance] );
	printk("[drv]tx_line_pos1:      3x04>> 0x%02X\n", CoaxVal->tx_line_pos1[distance] );
	printk("[drv]tx_line_count:     3x05>> 0x%02X\n", CoaxVal->tx_line_count );
	printk("[drv]tx_line_count_max: 3x0A>> 0x%02X\n", CoaxVal->tx_line_count_max );
	printk("[drv]tx_sync_pos0:      3x0D>> 0x%02X\n", CoaxVal->tx_sync_pos0[distance] );
	printk("[drv]tx_sync_pos1:      3x0E>> 0x%02X\n", CoaxVal->tx_sync_pos1[distance] );
	printk("[drv]tx_even:           3x2F>> 0x%02X\n", CoaxVal->tx_even );
	printk("[drv]tx_zero_length:    3x0C>> 0x%02X\n", CoaxVal->tx_zero_length);
#endif

}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's Send UP Stream command.
 *
 * @param_in		(NC_VD_COAX_SET_STR *)coax_tx_mode			    UP Stream Command structure
 *
 * @return   	void  		       								None
 *
 * ioctl : IOC_VDEC_COAX_TX_CMD_SEND
 ***************************************************************************************/
void coax_tx_cmd_send( void *p_param )
{
	NC_VD_COAX_STR *coax_tx = (NC_VD_COAX_STR*)p_param;
	int i;
	int cmd_cnt = 0;
	unsigned char ch              = coax_tx->ch;
	NC_COAX_CMD_DEF cmd           = coax_tx->cmd;
	NC_FORMAT_STANDARD format     = coax_tx->format_standard;
	NC_VIVO_CH_FORMATDEF vivofmt  = coax_tx->vivo_fmt;

	unsigned char tx_bank          = 0x00;
	unsigned char tx_cmd_addr      = 0x00;
	unsigned char tx_shot_addr     = 0x00;
	unsigned char command[32]      = {0,};
	unsigned char TCP_CMD_Stop_v10[10] = { 0xb5, 0x00, 0x14, 0x00, 0x80, 0x00, 0x00, 0x00, 0xc9, 0x80 };
	unsigned char TCP_CMD_Stop_v20[10] = { 0xb5, 0x01, 0x14, 0x00, 0x80, 0x00, 0x00, 0x00, 0xc5, 0x80 };

	// UP Stream get from coax table
	NC_VD_COAX_Init_STR *CoaxVal = __NC_VD_COAX_InitFormat_Get(vivofmt);    // Get from Coax_Tx_Init Table
	NC_VD_ACP_CMDDEF_STR *pCMD   = __NC_VD_ACP_Get_CommandFormat_Get(cmd);  // Get From Coax_Tx_Command Table
	printk("[drv_coax]Ch: %d Command >>>>> %s\n", ch, pCMD->name );

	tx_bank      = CoaxVal->tx_bank;
	tx_cmd_addr  = CoaxVal->tx_cmd_addr;
	tx_shot_addr = CoaxVal->tx_shot_addr;

	// UP Stream command copy in coax command table
	cmd_cnt = __NC_VD_COAX_Command_Copy( format, vivofmt, command, pCMD );
	//	printk("cmd_cnt: %d\n", cmd_cnt);

	// fill command + shot
	if( format == FMT_SD )
	{
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, tx_bank+((ch%4)/2) );
		for(i=0;i<cmd_cnt;i++)
		{
			gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], (tx_cmd_addr+((ch%2)*0x80))+i, 0);
		}
		// Shot
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x02+((ch%4)/2) );
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_shot_addr+((ch%2)*0x80), 0x01);
		msleep(CoaxVal->shot_delay);
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_shot_addr+((ch%2)*0x80), 0x00);

		msleep(CoaxVal->reset_delay);

		for(i=0;i<cmd_cnt;i++)
		{
			gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], (tx_cmd_addr+((ch%2)*0x80))+i, command[i]);
		}
		// Shot
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x02+((ch%4)/2) );
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_shot_addr+((ch%2)*0x80), 0x01);
		msleep(CoaxVal->shot_delay);
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_shot_addr+((ch%2)*0x80), 0x00);
	}
	else if(format == FMT_CVI)
	{
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, tx_bank+(ch%4));
		for(i=0;i<cmd_cnt;i++)
		{
			gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_cmd_addr+i, command[i]);
			gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x10+i, 0xff);
		}

		// Shot
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x02+((ch%4)/2) );
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_shot_addr+((ch%2)*0x80), 0x01);
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_shot_addr+((ch%2)*0x80), 0x00);
	}
	else
	{
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, tx_bank+((ch%4)/2) );
		for(i=0;i<cmd_cnt;i++)
		{
			gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], (tx_cmd_addr+((ch%2)*0x80))+i, command[i]);
		}

		// Shot
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x02+((ch%4)/2) );
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_shot_addr+((ch%2)*0x80), 0x01);
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_shot_addr+((ch%2)*0x80), 0x00);
	}

	// Only TVI >>> OSD Stop Command Send
	if( format == FMT_TVI )
	{
		msleep(30);
		if( (vivofmt == TVI_4M_30P) || (vivofmt == TVI_4M_25P) || (vivofmt == TVI_4M_15P) )
		{
			gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, tx_bank+((ch%4)/2));
			for(i=0;i<10;i++)
			{
				gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_cmd_addr+((ch%2)*0x80)+i, TCP_CMD_Stop_v20[i]);
			}
		}
		else
		{
			gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, tx_bank+((ch%4)/2));
			for(i=0;i<10;i++)
			{
				gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_cmd_addr+((ch%2)*0x80)+i, TCP_CMD_Stop_v10[i]);
			}
		}

		// shot
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_shot_addr+((ch%2)*0x80), 0x01);
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_shot_addr+((ch%2)*0x80), 0x00);
	}
}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's Send UP Stream command.
 *
 * @param_in		(NC_VD_COAX_SET_STR *)coax_tx_mode			    UP Stream Command structure
 *
 * @return   	void  		       								None
 *
 * ioctl : IOC_VDEC_COAX_TX_CMD_SEND
 ***************************************************************************************/
void coax_tx_16bit_cmd_send( void *p_param )
{
	NC_VD_COAX_STR *coax_tx = (NC_VD_COAX_STR*)p_param;
	int i;
	int cmd_cnt = 0;
	unsigned char ch              = coax_tx->ch;
	NC_COAX_CMD_DEF cmd           = coax_tx->cmd;
	NC_FORMAT_STANDARD format     = coax_tx->format_standard;
	NC_VIVO_CH_FORMATDEF vivofmt  = coax_tx->vivo_fmt;

	unsigned char tx_bank          = 0x00;
	unsigned char tx_cmd_addr      = 0x00;
	unsigned char tx_shot_addr     = 0x00;
	unsigned char command[32]      ={0,};

	// UP Stream get from coax table
	NC_VD_COAX_Init_STR *CoaxVal = __NC_VD_COAX_InitFormat_Get(vivofmt);    // Get from Coax_Tx_Init Table
	NC_VD_ACP_CMDDEF_STR *pCMD   = __NC_VD_ACP_Get_CommandFormat_Get(cmd);  // Get From Coax_Tx_Command Table
	printk("[drv_coax]Ch: %d 16bit Command >>>>> %s\n", ch, pCMD->name );

	tx_bank      = CoaxVal->tx_bank;
	tx_cmd_addr  = CoaxVal->tx_cmd_addr;
	tx_shot_addr = CoaxVal->tx_shot_addr;

	// UP Stream command copy in coax command table
	cmd_cnt = __NC_VD_COAX_16bit_Command_Copy( format, vivofmt, command, pCMD );

	// Adjust Bank
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x02+((ch%4)/2) );

	// fill Reset
	for(i=0;i<cmd_cnt;i++)
	{
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x20+((ch%2)*0x80)+i, 0);
	}

	// Command Shot
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x02+((ch%4)/2) );
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0c+((ch%2)*0x80), 0x01);
	msleep(30);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0c+((ch%2)*0x80), 0x00);


	// fill command
	for(i=0;i<cmd_cnt;i++)
	{
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x20+((ch%2)*0x80)+i, command[i]);
	}

	// Command Shot
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x02+((ch%4)/2) );
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0c+((ch%2)*0x80), 0x01);
	msleep(30);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0x0c+((ch%2)*0x80), 0x00);

}

void coax_tx_cvi_new_cmd_send( void *p_param )
{
	NC_VD_COAX_STR *coax_tx = (NC_VD_COAX_STR*)p_param;
	int i;
	int cmd_cnt = 0;
	//	unsigned char vd_dev          = coax_tx->vd_dev;
	unsigned char ch              = coax_tx->ch;
	NC_COAX_CMD_DEF cmd           = coax_tx->cmd;
	NC_FORMAT_STANDARD format     = coax_tx->format_standard;
	NC_VIVO_CH_FORMATDEF vivofmt  = coax_tx->vivo_fmt;

	unsigned char tx_bank          = 0x00;
	unsigned char tx_cmd_addr      = 0x00;
	unsigned char tx_shot_addr     = 0x00;
	unsigned char command[32]      ={0,};

	// UP Stream get from coax table
	NC_VD_COAX_Init_STR *CoaxVal = __NC_VD_COAX_InitFormat_Get(vivofmt);    // Get from Coax_Tx_Init Table
	NC_VD_ACP_CMDDEF_STR *pCMD   = __NC_VD_ACP_Get_CommandFormat_Get(cmd);  // Get From Coax_Tx_Command Table
	printk("[drv_coax]Ch: %d Command >>>>> %s\n", ch, pCMD->name );

	tx_bank      = CoaxVal->tx_bank;
	tx_cmd_addr  = CoaxVal->tx_cmd_addr;
	tx_shot_addr = CoaxVal->tx_shot_addr;

	// UP Stream command copy in coax command table
	cmd_cnt = __NC_VD_COAX_16bit_Command_Copy( format, vivofmt, command, pCMD );

	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, tx_bank+(ch%4));
	for(i=0;i<cmd_cnt;i++)
	{
		gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_cmd_addr+i, command[i]);
	}

	// Shot
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], 0xFF, 0x02+((ch%4)/2) );
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_shot_addr+((ch%2)*0x80), 0x01);
	gpio_i2c_write(jaguar1_i2c_addr[coax_tx->vd_dev], tx_shot_addr+((ch%2)*0x80), 0x00);
}

/*=======================================================================================================
 ********************************************************************************************************
 **************************** Coaxial protocol down stream function *************************************
 ********************************************************************************************************
 *
 * Coaxial protocol down stream Flow
 * 1. Down stream initialize   -  coax_rx_init
 * 2. Down stream data read    -  coax_rx_data_get
 *
 * Coaxial protocol down stream register(example: channel 0)
 * (3x63) rx_comm_on         : Coaxial Down Stream Mode ON/OFF ( 0: OFF / 1: ON )
 * (3x62) rx_area            : Down Stream Read Line Number
 * (3x66) rx_signal_enhance  : Signal Enhance ON/OFF ( 0: OFF / 1: ON )
 * (3x69) rx_manual_duty     : 1 Bit Duty Setting ( HD@25, 30P 0x32  /  HD@50, 60P, FHD@25, 30P 0x64 )
 * (3x60) rx_head_matching   : Same Header Read (EX. 0x48)
 * (3x61) rx_data_rz         : The lower 2 bits set Coax Mode.. ( 0 : A-CP ), ( 1 : C-CP ), ( 2 : T-CP )
 * (3x68) rx_sz              : Down stream size setting
 * (3x3A)                    : Down stream buffer clear
 ========================================================================================================*/
/**************************************************************************************
 * @desc
 * 	JAGUAR1's   This function initializes the register associated with the Down Stream.
 *
 * @param_in		(NC_VD_COAX_SET_STR *)coax_tx_mode			    Down Stream Initialize structure
 *
 * @return   	void  		       								None
 *
 * ioctl : IOC_VDEC_COAX_RX_INIT
 ***************************************************************************************/
void coax_rx_init(void *p_param)
{
	NC_VD_COAX_STR *coax_rx = (NC_VD_COAX_STR*)p_param;
	unsigned char ch             = coax_rx->ch;
	NC_VIVO_CH_FORMATDEF vivofmt = coax_rx->vivo_fmt;

	NC_VD_COAX_Init_STR *coax_rx_val = __NC_VD_COAX_InitFormat_Get(vivofmt);

	gpio_i2c_write(jaguar1_i2c_addr[coax_rx->vd_dev], 0xFF,  0x02+((ch%4)/2));

	gpio_i2c_write(jaguar1_i2c_addr[coax_rx->vd_dev], 0x63+((ch%2)*0x80), coax_rx_val->rx_comm_on);
	gpio_i2c_write(jaguar1_i2c_addr[coax_rx->vd_dev], 0x62+((ch%2)*0x80), coax_rx_val->rx_area);
	gpio_i2c_write(jaguar1_i2c_addr[coax_rx->vd_dev], 0x66+((ch%2)*0x80), coax_rx_val->rx_signal_enhance);
	gpio_i2c_write(jaguar1_i2c_addr[coax_rx->vd_dev], 0x69+((ch%2)*0x80), coax_rx_val->rx_manual_duty);
	gpio_i2c_write(jaguar1_i2c_addr[coax_rx->vd_dev], 0x60+((ch%2)*0x80), coax_rx_val->rx_head_matching);
	gpio_i2c_write(jaguar1_i2c_addr[coax_rx->vd_dev], 0x61+((ch%2)*0x80), coax_rx_val->rx_data_rz);
	gpio_i2c_write(jaguar1_i2c_addr[coax_rx->vd_dev], 0x68+((ch%2)*0x80), coax_rx_val->rx_sz);
#if	DBG_RX_INIT_PRINT
	printk("[drv]Channel %d Format >>>>> %s\n", ch, coax_rx_val->name );
	printk("[drv]rx_head_matching:  0x60 >> 0x%02X\n", coax_rx_val->rx_head_matching);
	printk("[drv]rx_data_rz:        0x61 >> 0x%02X\n", coax_rx_val->rx_data_rz);
	printk("[drv]rx_area:           0x62 >> 0x%02X\n", coax_rx_val->rx_area);
	printk("[drv]rx_comm_on:        0x63 >> 0x%02X\n", coax_rx_val->rx_comm_on );
	printk("[drv]rx_signal_enhance: 0x66 >> 0x%02X\n", coax_rx_val->rx_signal_enhance);
	printk("[drv]rx_sz:             0x68 >> 0x%02X\n", coax_rx_val->rx_sz);
	printk("[drv]rx_manual_duty:    0x69 >> 0x%02X\n", coax_rx_val->rx_manual_duty);
#endif

}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's   Read down stream data.
 *
 * @param_in		(NC_VD_COAX_SET_STR *)coax_tx_mode			    Down Stream read structure
 *
 * @return   	void  		       								None
 *
 * ioctl : IOC_VDEC_COAX_RX_DATA_READ
 ***************************************************************************************/
void coax_rx_data_get(void *p_param)
{
	NC_VD_COAX_STR *coax_rx = (NC_VD_COAX_STR*)p_param;

	int ii = 0;
	int ch = coax_rx->ch;
	int format = coax_rx->format_standard;

	gpio_i2c_write(jaguar1_i2c_addr[coax_rx->vd_dev], 0xFF, 0x02+((ch%4)/2));

	if( (format == FMT_CVI) || (format == FMT_TVI) )
	{
		for(ii=0;ii<5;ii++)
		{
			coax_rx->rx_data1[ii] = gpio_i2c_read(jaguar1_i2c_addr[coax_rx->vd_dev], (0x40+((ch%2)*0x80))+ii);   // ChX_Rx_Line_1 : 0x40 ~ 0x44 5byte
			coax_rx->rx_data2[ii] = gpio_i2c_read(jaguar1_i2c_addr[coax_rx->vd_dev], (0x45+((ch%2)*0x80))+ii);   // ChX_Rx_Line_2 : 0x45 ~ 0x49 5byte
			coax_rx->rx_data3[ii] = gpio_i2c_read(jaguar1_i2c_addr[coax_rx->vd_dev], (0x4A+((ch%2)*0x80))+ii);   // ChX_Rx_Line_3 : 0x4A ~ 0x4E 5byte
			coax_rx->rx_data4[ii] = gpio_i2c_read(jaguar1_i2c_addr[coax_rx->vd_dev], (0x6C+((ch%2)*0x80))+ii);   // ChX_Rx_Line_4 : 0x6C ~ 0x70 5byte
			coax_rx->rx_data5[ii] = gpio_i2c_read(jaguar1_i2c_addr[coax_rx->vd_dev], (0x71+((ch%2)*0x80))+ii);   // ChX_Rx_Line_5 : 0x71 ~ 0x75 5byte
			coax_rx->rx_data6[ii] = gpio_i2c_read(jaguar1_i2c_addr[coax_rx->vd_dev], (0x76+((ch%2)*0x80))+ii);   // ChX_Rx_Line_6 : 0x76 ~ 0x7A 5byte
		}
	}
	else  // AHD
	{
		for(ii=0;ii<8;ii++)
		{
			coax_rx->rx_pelco_data[ii] = gpio_i2c_read(jaguar1_i2c_addr[coax_rx->vd_dev], (0x50+((ch%2)*0x80))+ii);   // ChX_PELCO_Rx_Line_1 ~ 8 : 0x50 ~ 0x57 8byte
		}
	}
}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's   Down stream buffer clear.
 *
 * @param_in		(NC_VD_COAX_SET_STR *)coax_tx_mode			    UP Stream Command structure
 *
 * @return   	void  		       								None
 *
 * ioctl : IOC_VDEC_COAX_RX_BUF_CLEAR
 ***************************************************************************************/
void coax_rx_buffer_clear(void *p_param)
{
	NC_VD_COAX_STR *coax_val = (NC_VD_COAX_STR*)p_param;

	unsigned char ch = coax_val->ch;

	gpio_i2c_write(jaguar1_i2c_addr[coax_val->vd_dev], 0xFF, 0x02+((ch%4)/2));

	gpio_i2c_write(jaguar1_i2c_addr[coax_val->vd_dev], 0x3A+((ch%2)*0x80), 0x01);
	gpio_i2c_write(jaguar1_i2c_addr[coax_val->vd_dev], 0x3A+((ch%2)*0x80), 0x00);
}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's   Down stream mode off.
 *
 * @param_in		(NC_VD_COAX_SET_STR *)coax_tx_mode			    UP Stream Command structure
 *
 * @return   	void  		       								None
 *
 * ioctl : IOC_VDEC_COAX_RX_DEINIT
 ***************************************************************************************/
void coax_rx_deinit(void *p_param)
{
	NC_VD_COAX_STR *coax_val = (NC_VD_COAX_STR*)p_param;

	unsigned char ch = coax_val->ch;

	gpio_i2c_write(jaguar1_i2c_addr[coax_val->vd_dev], 0xFF, 0x02+((ch%4)/2));
	gpio_i2c_write(jaguar1_i2c_addr[coax_val->vd_dev], 0x63+((ch%2)*0x80), 0);
}

/*=======================================================================================================
 ********************************************************************************************************
 ************************** Coaxial protocol firmware upgrade function **********************************
 ********************************************************************************************************
 *
 * Coaxial protocol firmware upgrade Flow
 * 1. ACP Check - Down Stream Header 0x55  - coax_fw_ready_header_check_from_isp_recv
 * 2.1 FW ready send                       - coax_fw_ready_cmd_to_isp_send
 * 2.2 FW ready ACK receive                - coax_fw_ready_cmd_ack_from_isp_recv
 * 3.1 FW start send                       - coax_fw_start_cmd_to_isp_send
 * 3.2 FW start ACK receive                - coax_fw_start_cmd_ack_from_isp_recv
 * 4.1 FW data send - 139byte         	   - coax_fw_one_packet_data_to_isp_send
 * 4.2 FW data ACK receive - offset        - coax_fw_one_packet_data_ack_from_isp_recv
 * 5.1 FW end send                         - coax_fw_end_cmd_to_isp_send
 * 5.2 FW end ACK receive                  - coax_fw_end_cmd_ack_from_isp_recv
 ========================================================================================================*/

/**************************************************************************************
 * @desc
 * 	JAGUAR1's   Down stream check header value.(AHD : 0x55)
 *
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->channel       FW Update channel
 * @param_out	(FIRMWARE_UP_FILE_INFO *)p_param->result        Header check result
 *
 * @return   	void  		       								None
 *
 * ioctl : IOC_VDEC_COAX_FW_ACP_HEADER_GET
 ***************************************************************************************/
void coax_fw_ready_header_check_from_isp_recv(void *p_param)
{
	int ret = FW_FAILURE;
	int ch = 0;
	int devnum = 0;
	unsigned char readval = 0;

	FIRMWARE_UP_FILE_INFO *pstFileInfo = (FIRMWARE_UP_FILE_INFO*)p_param;
	ch = pstFileInfo->channel;
	devnum = pstFileInfo->channel/4;

	/* set register */
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2));
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x50+((ch%2)*0x80), 0x05 );  // PELCO Down Stream Read 1st Line
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x60+((ch%2)*0x80), 0x55 );  // Header Matching

	/* If the header is (0x50=>0x55) and chip information is (0x51=>0x3X, 0x4X, 0x5X ), it can update firmware */
	if( gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x50+((ch%2)*0x80) ) == 0x55 )
	{
		printk(">>>>> DRV[%s:%d] CH:%d, this camera can update, please, wait! = 0x%x\n", __func__, __LINE__, ch, gpio_i2c_read( jaguar1_i2c_addr[ch/4], 0x51+((ch%2)*0x80) ) );
		ret = FW_SUCCESS;
	}
	else
	{
		readval= gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x50+((ch%2)*0x80) );
		printk(">>>>> DRV[%s:%d] check ACP_STATUS_MODE::0x%x\n", __func__, __LINE__, readval );
		ret = FW_FAILURE;
	}

	pstFileInfo->result = ret;
}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's   FW Ready command send to camera ( Mode change to FHD@25P )
 *
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->channel       FW Update channel
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->cp_mode       Camera Format
 * @param_out	(FIRMWARE_UP_FILE_INFO *)p_param->result        Function execution result
 *
 * @return   	void  		       								None
 *
 * ioctl : IOC_VDEC_COAX_FW_READY_CMD_SET
 ***************************************************************************************/
void coax_fw_ready_cmd_to_isp_send(void *p_param) // FW Ready
{
	int ch = 0;
	int devnum = 0;
	int ret = FW_FAILURE;
	int cp_mode = 0;

	FIRMWARE_UP_FILE_INFO *pstFileInfo = (FIRMWARE_UP_FILE_INFO*)p_param;
	ch = pstFileInfo->channel;
	cp_mode = pstFileInfo->cp_mode;
	devnum = pstFileInfo->channel/4;

	/* Adjust Tx */
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2));
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x0A+((ch%2)*0x80), 0x04);  // Tx Line count max

	/* change video mode FHD@25P Command Send */
	if( (cp_mode == FMT_AHD20) || (cp_mode == FMT_AHD30) )
	{
		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x10+((ch%2)*0x80), 0x60);	// Register Write Control 				 - 17th line
		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x11+((ch%2)*0x80), 0xB0);	// table(Mode Change Command) 			 - 18th line
		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x12+((ch%2)*0x80), 0x02);	// Flash Update Mode(big data)			 - 19th line
		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x13+((ch%2)*0x80), 0x02);	// Init Value(FW Information Check Mode) - 20th line

		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x09+((ch%2)*0x80), 0x08);	// trigger on
		msleep(200);
		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x09+((ch%2)*0x80), 0x10);	// reset
		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x09+((ch%2)*0x80), 0x00);	// trigger Off
		printk(">>>>> DRV[%s:%d] CH:%d, coax_fw_ready_cmd_to_isp_send!!- AHD\n", __func__, __LINE__, ch );
		ret = FW_SUCCESS;
	}
	else if( (cp_mode == FMT_CVI) || (cp_mode == FMT_TVI) )
	{
		/* change video mode FHD@25P Command Send */
		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2) );
		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x10+((ch%2)*0x80), 0x55);	// 0x55(header)          				 - 16th line
		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x11+((ch%2)*0x80), 0x60);	// Register Write Control 				 - 17th line
		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x12+((ch%2)*0x80), 0xB0);	// table(Mode Change Command) 			 - 18th line
		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x13+((ch%2)*0x80), 0x02);	// Flash Update Mode         			 - 19th line
		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x14+((ch%2)*0x80), 0x00);	// Init Value(FW Information Check Mode) - 20th line

		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x09+((ch%2)*0x80), 0x08);	// trigger on
		msleep(1000);
		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x09+((ch%2)*0x80), 0x10);	// reset
		gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x09+((ch%2)*0x80), 0x00);	// trigger Off
		printk(">>>>> DRV[%s:%d] CH:%d, coax_fw_ready_cmd_to_isp_send!!- AHD\n", __func__, __LINE__, ch );
		ret = FW_SUCCESS;
	}
	else
	{
		printk(">>>> DRV[%s:%d] CH:%d, FMT:%d > Unknown Format!!! \n", __func__, __LINE__, ch, cp_mode );
		ret = FW_FAILURE;
	}

	pstFileInfo->result = ret;
}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's   FW Ready ACK receive from camera
 *
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->channel       FW Update channel

 * @param_out	(FIRMWARE_UP_FILE_INFO *)p_param->result        Function execution result
 *
 * @return   	void  		       								None
 *
 * ioctl : IOC_VDEC_COAX_FW_READY_ACK_GET
 ***************************************************************************************/
void coax_fw_ready_cmd_ack_from_isp_recv(void *p_param)
{
	int ret = FW_FAILURE;
	int ch = 0;
	int devnum = 0;
	unsigned char retval = 0x00;
	unsigned char retval2 = 0x00;
	static int toggle = 0;

	FIRMWARE_UP_FILE_INFO *pstFileInfo = (FIRMWARE_UP_FILE_INFO*)p_param;
	ch = pstFileInfo->channel;
	devnum = pstFileInfo->channel/4;


	/* AHD FHD 25P Video Setting  */
	if(!toggle)
	{
		video_input_init vd_set;
		vd_set.ch = ch;
		vd_set.format = AHD20_1080P_25P;
		vd_set.input = SINGLE_ENDED;
		vd_jaguar1_init_set(&vd_set);
		toggle =1;
	}

	/* Adjust Rx FHD@25P */
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2));
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x63+((ch%2)*0x80), 0x01 );    // Ch_X Rx ON
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x62+((ch%2)*0x80), 0x05 );    // Ch_X Rx Area
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x66+((ch%2)*0x80), 0x81 );    // Ch_X Rx Signal enhance
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x69+((ch%2)*0x80), 0x2D );    // Ch_X Rx Manual duty
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x60+((ch%2)*0x80), 0x55 );    // Ch_X Rx Header matching
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x61+((ch%2)*0x80), 0x00 );    // Ch_X Rx data_rz
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x68+((ch%2)*0x80), 0x80 );    // Ch_X Rx SZ

	if( gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x57+((ch%2)*0x80) ) == 0x02 )
	{
		/* get status, If the status is 0x00(Camera information), 0x01(Firmware version */
		if( gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x56+((ch%2)*0x80) ) == 0x00 )
		{
			printk(">>>>> DRV[%s:%d]CH:%d Receive ISP status : [READY]\n", __func__, __LINE__, ch );
			ret = FW_SUCCESS;
		}
	}
	else
	{
		retval  = gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x56+((ch%2)*0x80) );
		retval2 = gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x57+((ch%2)*0x80) );
		printk(">>>>> DRV[%s:%d]CH:%d retry : Receive ISP status[READY], [0x56-true[0x00]:0x%x], [0x57-true[0x02]:0x%x]\n",	__func__, __LINE__, ch, retval, retval2 );
		ret = FW_FAILURE;
	}

	/* Rx Buffer clear */
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2));
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x3A+((ch%2)*0x80), 0x01);
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x3A+((ch%2)*0x80), 0x00);

	pstFileInfo->result = ret;
}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's   FW start command send to camera ( change to black pattern )
 *
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->channel       FW Update channel
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->cp_mode       Camera Format
 * @param_out	(FIRMWARE_UP_FILE_INFO *)p_param->result        Function execution result
 *
 * @return   	void  		       								None
 *
 * ioctl : IOC_VDEC_COAX_FW_START_CMD_SET
 ***************************************************************************************/
void coax_fw_start_cmd_to_isp_send(void *p_param)
{
	int ch = 0;
	int devnum = 0;

	FIRMWARE_UP_FILE_INFO *pstFileInfo = (FIRMWARE_UP_FILE_INFO*)p_param;

	ch = pstFileInfo->channel;
	devnum = pstFileInfo->channel/4;

	/* Adjust Tx */
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2) );
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x00+((ch%2)*0x80), 0x2D);   // Duty
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x03+((ch%2)*0x80), 0x0D);   // line
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x05+((ch%2)*0x80), 0x03);   // tx_line_count
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x0A+((ch%2)*0x80), 0x04);   // tx_line_count_max

	// Tx Command set
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x10+((ch%2)*0x80), 0x60);	 // Register Write Control 				 - 17th line
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x11+((ch%2)*0x80), 0xB0);	 // table(Mode Change Command) 			 - 18th line
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x12+((ch%2)*0x80), 0x02);	 // Flash Update Mode(big data)			 - 19th line
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x13+((ch%2)*0x80), 0x40);	 // Start firmware update                - 20th line

	// Tx Command Shot
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x09+((ch%2)*0x80), 0x08);	 // trigger on
	msleep(200);
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x09+((ch%2)*0x80), 0x10);	 // reset
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x09+((ch%2)*0x80), 0x00);	 // trigger Off

	printk(">>>>> DRV[%s:%d]CH:%d >> Send command[START]\n", __func__, __LINE__, ch );

}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's    FW Start ACK receive from camera
 *
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->channel       FW Update channel
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->cp_mode       Camera Format
 * @param_out	(FIRMWARE_UP_FILE_INFO *)p_param->result        Function execution result
 *
 * @return   	void  		       								None
 *
 * ioctl : IOC_VDEC_COAX_FW_START_ACK_GET
 ***************************************************************************************/
void coax_fw_start_cmd_ack_from_isp_recv( void *p_param )
{
	int ch = 0;
	int devnum = 0;
	int ret = FW_FAILURE;

	FIRMWARE_UP_FILE_INFO *pstFileInfo = (FIRMWARE_UP_FILE_INFO*)p_param;
	ch = pstFileInfo->channel;
	devnum = pstFileInfo->channel/4;

	/* Adjust Rx FHD@25P */
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2));
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x63+((ch%2)*0x80), 0x01 ); // Ch_X Rx ON
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x62+((ch%2)*0x80), 0x05 ); // Ch_X Rx Area
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x66+((ch%2)*0x80), 0x81 ); // Ch_X Rx Signal enhance
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x69+((ch%2)*0x80), 0x2D ); // Ch_X Rx Manual duty
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x60+((ch%2)*0x80), 0x55 ); // Ch_X Rx Header matching
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x61+((ch%2)*0x80), 0x00 ); // Ch_X Rx data_rz
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x68+((ch%2)*0x80), 0x80 ); // Ch_X Rx SZ

	if( gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x57+((ch%2)*0x80) ) == 0x02 )
	{
		if( gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x56+((ch%2)*0x80) ) == 0x02 )
		{
			printk(">>>>> DRV[%s:%d]CH:%d Receive ISP status : [START]\n", __func__, __LINE__, ch );
			ret = FW_SUCCESS;
		}
		else
		{
			unsigned char retval1;
			unsigned char retval2;
			gpio_i2c_write( jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2));
			retval1 = gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x56+((ch%2)*0x80) );
			retval2 = gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x57+((ch%2)*0x80) );
			ret = FW_FAILURE;
			printk(">>>>> DRV[%s:%d]CH:%d retry : Receive ISP status[START], [0x56-true[0x02]:0x%x], [0x57-true[0x02]:0x%x]\n",	__func__, __LINE__, ch, retval1, retval2 );
		}
	}

	/* Rx Buffer clear */
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2));
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x3A+((ch%2)*0x80), 0x01);
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x3A+((ch%2)*0x80), 0x00);

	pstFileInfo->result = ret;
}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's    FW Data send to camera(One packet data size 139byte)
 *
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->channel                  FW Update channel
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->readsize                 One packet data size
 * @param_out	(FIRMWARE_UP_FILE_INFO *)p_param->currentFileOffset        File offset
 * @param_out	(FIRMWARE_UP_FILE_INFO *)p_param->result                   Function execution result
 *
 * @return   	void  		       								           None
 *
 * ioctl : IOC_VDEC_COAX_FW_SEND_DATA_SET
 ***************************************************************************************/
void coax_fw_one_packet_data_to_isp_send( void *p_param )
{
	int ch = 0;
	int devnum = 0;
	int ii = 0;
	unsigned int low = 0x00;
	unsigned int mid = 0x00;
	unsigned int high = 0x00;
	unsigned int readsize = 0;
	int byteNumOfPacket = 0;
	FIRMWARE_UP_FILE_INFO *pstFileInfo = (FIRMWARE_UP_FILE_INFO*)p_param;

	/* file information */
	ch        = pstFileInfo->channel;
	readsize  = pstFileInfo->readsize;
	devnum 	  = pstFileInfo->channel/4;

	/* fill packet(139bytes), end packet is filled with 0xff */
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0xff, 0x0c+(ch%4) );
	for( ii = 0; ii < 139; ii++ )
	{
		if( byteNumOfPacket < readsize)
		{
			gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x00+ii, pstFileInfo->onepacketbuf[ii] );
			byteNumOfPacket++;
		}
		else if( byteNumOfPacket >= readsize ) // end packet : fill 0xff
		{
			gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x00+ii, 0xff );
			byteNumOfPacket++;
		}

		if( ii == 0 )
			low = pstFileInfo->onepacketbuf[ii];
		else if( ii == 1 )
			mid = pstFileInfo->onepacketbuf[ii];
		else if( ii == 2 )
			high = pstFileInfo->onepacketbuf[ii];
	}

	/* offset */
	pstFileInfo->currentFileOffset = (unsigned int)((high << 16 )&(0xFF0000))| (unsigned int)((mid << 8 )&(0xFF00)) | (unsigned char)(low);

	/* Tx Change mode to use Big data */
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2) );
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x0B+((ch%2)*0x80), 0x30);
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x05+((ch%2)*0x80), 0x8A);

	/* Tx Shot */
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2) );
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x09+((ch%2)*0x80), 0x08);	// trigger on
}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's    FW Data ACK receive from camera
 *
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->channel                  FW Update channel
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->currentFileOffset        File offset

 * @param_out	(FIRMWARE_UP_FILE_INFO *)p_param->result                   Function execution result
 *
 * @return   	void  		       								           None
 *
 * ioctl : IOC_VDEC_COAX_FW_SEND_ACK_GET
 ***************************************************************************************/
void coax_fw_one_packet_data_ack_from_isp_recv( void *p_param )
{
	int ret = FW_FAILURE;
	int ch = 0;
	int devnum = 0;
	unsigned int onepacketaddr = 0;
	unsigned int receive_addr = 0;

	FIRMWARE_UP_FILE_INFO *pstFileInfo = (FIRMWARE_UP_FILE_INFO*)p_param;
	ch = pstFileInfo->channel;
	onepacketaddr = pstFileInfo->currentFileOffset;
	devnum = pstFileInfo->channel/4;

	/* Adjust Rx FHD@25P */
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2));
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x63+((ch%2)*0x80), 0x01 ); // Ch_X Rx ON
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x62+((ch%2)*0x80), 0x05 ); // Ch_X Rx Area
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x66+((ch%2)*0x80), 0x81 ); // Ch_X Rx Signal enhance
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x69+((ch%2)*0x80), 0x2D ); // Ch_X Rx Manual duty
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x60+((ch%2)*0x80), 0x55 ); // Ch_X Rx Header matching
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x61+((ch%2)*0x80), 0x00 ); // Ch_X Rx data_rz
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x68+((ch%2)*0x80), 0x70 ); // Ch_X Rx SZ

	if( gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x57+((ch%2)*0x80) ) == 0x02 )
	{
		/* check ISP status - only check first packet */
		if( pstFileInfo->currentpacketnum == 0 )
		{
			if( gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x56+((ch%2)*0x80) ) == 0x03 )
			{
				pstFileInfo->result = FW_FAILURE;
				printk(">>>>> DRV[%s:%d] CH:%d, Failed, error status, code=3..................\n", __func__, __LINE__, ch );
				return;
			}
		}

		/* check offset */
		receive_addr = (( gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x53+((ch%2)*0x80))<<16) + \
				(gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x54+((ch%2)*0x80))<<8) +
				gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x55+((ch%2)*0x80)));
		if( onepacketaddr == receive_addr )
		{
			gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x09+((ch%2)*0x80), 0x10);	// Reset
			gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x09+((ch%2)*0x80), 0x00);	// trigger off
			ret = FW_SUCCESS;
			pstFileInfo->receive_addr = receive_addr;
			pstFileInfo->result = ret;
		}
	}

	pstFileInfo->result = ret;
}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's    FW End command send to camera
 *
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->channel                  FW Update channel
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->result                   FW Data send result
 *
 * @return   	void  		       								           None
 *
 * ioctl : IOC_VDEC_COAX_FW_END_CMD_SET
 ***************************************************************************************/
void coax_fw_end_cmd_to_isp_send(void *p_param)
{
	int ch = 0;
	int devnum = 0;
	int send_success = 0;

	FIRMWARE_UP_FILE_INFO *pstFileInfo = (FIRMWARE_UP_FILE_INFO*)p_param;

	ch = pstFileInfo->channel;
	send_success = pstFileInfo->result;
	devnum = pstFileInfo->channel/4;

	/* adjust Tx line */
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2) );
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x0B+((ch%2)*0x80), 0x10);  // Tx_Mode
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x05+((ch%2)*0x80), 0x03);	// Tx_Line Count       3 line number
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x0A+((ch%2)*0x80), 0x03);	// Tx Total Line Count 3 line number

	/* Fill end command */
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x10+((ch%2)*0x80), 0x60);
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x11+((ch%2)*0x80), 0xb0);
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x12+((ch%2)*0x80), 0x02);
	if( send_success == FW_FAILURE )
	{
		gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x13+((ch%2)*0x80), 0xE0/*0xC0*/);
		printk(">>>>> DRV[%s:%d] CH:%d, Camera UPDATE error signal. send Abnormal ending!\n", __func__, __LINE__, ch );
	}
	else
	{
		gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x13+((ch%2)*0x80), 0x80/*0x60*/);
		printk(">>>>> DVR[%s:%d] CH:%d, Camera UPDATE ending signal. wait please!\n", __func__, __LINE__, ch );
	}

	/* Shot */
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x09+((ch%2)*0x80), 0x08);
	msleep(200);
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0x09+((ch%2)*0x80), 0x00);

}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's    FW End command ACK receive from camera
 *
 * @param_in		(FIRMWARE_UP_FILE_INFO *)p_param->channel                  FW Update channel
 *
 * @param_out	(FIRMWARE_UP_FILE_INFO *)p_param->result                   Function execution result
 *
 * @return   	void  		       								           None
 *
 * ioctl : IOC_VDEC_COAX_FW_END_ACK_GET
 ***************************************************************************************/
void coax_fw_end_cmd_ack_from_isp_recv(void *p_param)
{
	int ch = 0;
	int devnum = 0;

	unsigned char videofm = 0x00;
	unsigned char ack_return = 0x00;
	unsigned char isp_status = 0x00;
	FIRMWARE_UP_FILE_INFO *pstFileInfo = (FIRMWARE_UP_FILE_INFO*)p_param;

	ch = pstFileInfo->channel;
	devnum = pstFileInfo->channel/4;

	/* check video format(video loss), 0:videoloss, 1:video on */
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0xFF, 0x05+(ch%4));
	videofm = gpio_i2c_read( jaguar1_i2c_addr[devnum], 0xF0);

	if( videofm == 0xFF )
	{
		printk(">>>>> DRV[%s:%d] Final[CH:%d], No video[END]!\n", __func__, __LINE__, ch );
		pstFileInfo->result = FW_FAILURE;
		return;
	}

	/* Adjust Rx FHD@25P */
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2));
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x63+((ch%2)*0x80), 0x01 );   // Ch_X Rx ON
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x62+((ch%2)*0x80), 0x05 );   // Ch_X Rx Area
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x66+((ch%2)*0x80), 0x81 );   // Ch_X Rx Signal enhance
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x69+((ch%2)*0x80), 0x2D );   // Ch_X Rx Manual duty
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x60+((ch%2)*0x80), 0x55 );   // Ch_X Rx Header matching
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x61+((ch%2)*0x80), 0x00 );   // Ch_X Rx data_rz
	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0x68+((ch%2)*0x80), 0x80 );   // Ch_X Rx SZ

	/* get status, If the ack_return(0x56) is 0x05(completed writing f/w file to isp's flash) */
	gpio_i2c_write( jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2));
	ack_return = gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x56+((ch%2)*0x80) );
	isp_status = gpio_i2c_read( jaguar1_i2c_addr[devnum], 0x57+((ch%2)*0x80) );
	if( isp_status == 0x02 && ack_return == 0x05 )
	{
		printk(">>>>> DRV[%s:%d]CH:%d Receive ISP status : [END]\n", __func__, __LINE__, ch );
		pstFileInfo->result = FW_SUCCESS;
		return;
	}
	else
	{
		printk(">>>>> DRV[%s:%d]CH:%d retry : Receive ISP status[END], [0x56-true[0x05]:0x%x], [0x57-true[0x02]:0x%x]\n", __func__, __LINE__, ch, ack_return, isp_status );
		pstFileInfo->result = FW_FAILURE;
		return;
	}

}


void coax_fw_revert_to_previous_fmt_set(void *p_param)
{
	int ch = 0;
	int devnum = 0;
	video_input_init vd_set;

	FIRMWARE_UP_FILE_INFO *pstFileInfo = (FIRMWARE_UP_FILE_INFO*)p_param;

	ch = pstFileInfo->channel;
	devnum = pstFileInfo->channel/4;

	/* previous video format Setting  */
	vd_set.ch = ch;
	vd_set.format = g_coax_format;
	vd_set.input = SINGLE_ENDED;
	vd_jaguar1_init_set(&vd_set);

}

/*=======================================================================================================
 *  Coaxial protocol Support option function
 *
 ========================================================================================================*/
/**************************************************************************************
 * @desc
 * 	JAGUAR1's    RT/NRT Mode change
 *
 * @param_in		(NC_VD_COAX_Tx_Init_STR *)p_param->channel                 Coax read channel
 *
 * @return   	void  		       								           None
 *
 * ioctl : IOC_VDEC_COAX_TEST_TX_INIT_DATA_READ
 ***************************************************************************************/
void coax_option_rt_nrt_mode_change_set(void *p_param)
{
	NC_VD_COAX_STR *coax_val = (NC_VD_COAX_STR*)p_param;

	unsigned char ch    = coax_val->ch;
	unsigned char param = coax_val->param;
	unsigned char tx_line = 0;
	unsigned char tx_line_max = 0;
	//
	gpio_i2c_write(jaguar1_i2c_addr[coax_val->vd_dev], 0xFF, 0x02+((ch%4)/2));

	tx_line     = gpio_i2c_read( jaguar1_i2c_addr[coax_val->vd_dev], 0x05+((ch%2)*0x80) );
	tx_line_max = gpio_i2c_read( jaguar1_i2c_addr[coax_val->vd_dev], 0x0A+((ch%2)*0x80) );

	// Adjust Tx
	gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x05+((ch%2)*0x80), 0x03);       // Tx line set
	gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x0A+((ch%2)*0x80), 0x04);       // Tx max line set

	if( !param ) // RT Mode
	{
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x10+((ch%2)*0x80), 0x60);   // Register write
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x11+((ch%2)*0x80), 0xb1);   // Output command
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x12+((ch%2)*0x80), 0x00);   // RT Mode
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x13+((ch%2)*0x80), 0x00);
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x14+((ch%2)*0x80), 0x00);
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x15+((ch%2)*0x80), 0x00);
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x16+((ch%2)*0x80), 0x00);
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x17+((ch%2)*0x80), 0x00);
	}
	else // NRT Mode
	{
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x10+((ch%2)*0x80), 0x60);   // Register write
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x11+((ch%2)*0x80), 0xb1);   // Output command
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x12+((ch%2)*0x80), 0x01);   // RT Mode
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x13+((ch%2)*0x80), 0x00);
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x14+((ch%2)*0x80), 0x00);
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x15+((ch%2)*0x80), 0x00);
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x16+((ch%2)*0x80), 0x00);
		gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x17+((ch%2)*0x80), 0x00);
	}

	// Tx Command Shot
	gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x09+((ch%2)*0x80), 0x08);	 // trigger on
	msleep(100);
	gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x09+((ch%2)*0x80), 0x10);	 // reset
	gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x09+((ch%2)*0x80), 0x00);	 // trigger Off

	gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x05+((ch%2)*0x80), tx_line);           // Tx line set
	gpio_i2c_write( jaguar1_i2c_addr[coax_val->vd_dev], 0x0A+((ch%2)*0x80), tx_line_max);       // Tx max line set

}

/*=======================================================================================================
 *  Coaxial protocol test function
 *
 ========================================================================================================*/
/**************************************************************************************
 * @desc
 * 	JAGUAR1's    Test function. Read coax Tx initialize value
 *
 * @param_in		(NC_VD_COAX_Tx_Init_STR *)p_param->channel                 Coax read channel
 *
 * @return   	void  		       								           None
 *
 * ioctl : IOC_VDEC_COAX_TEST_TX_INIT_DATA_READ
 ***************************************************************************************/
void coax_test_tx_init_read(NC_VD_COAX_TEST_STR *coax_tx_mode)
{
	//int ch = coax_tx_mode->ch;
	//int devnum = coax_tx_mode->chip_num;

	int ch = 0;
	int devnum = 0;

	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0xFF, 0x05+ch%4);
	coax_tx_mode->rx_src = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x7C);
	coax_tx_mode->rx_slice_lev = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x7D);

	gpio_i2c_write(jaguar1_i2c_addr[devnum], 0xFF, 0x02+((ch%4)/2));
	coax_tx_mode->tx_baud           = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x00+((ch%2)*0x80));
	coax_tx_mode->tx_pel_baud       = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x02+((ch%2)*0x80));
	coax_tx_mode->tx_line_pos0      = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x03+((ch%2)*0x80));
	coax_tx_mode->tx_line_pos1      = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x04+((ch%2)*0x80));
	coax_tx_mode->tx_line_count     = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x05+((ch%2)*0x80));
	coax_tx_mode->tx_pel_line_pos0  = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x07+((ch%2)*0x80));
	coax_tx_mode->tx_pel_line_pos1  = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x08+((ch%2)*0x80));
	coax_tx_mode->tx_line_count_max = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x0A+((ch%2)*0x80));
	coax_tx_mode->tx_mode           = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x0B+((ch%2)*0x80));
	coax_tx_mode->tx_sync_pos0      = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x0D+((ch%2)*0x80));
	coax_tx_mode->tx_sync_pos1      = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x0E +((ch%2)*0x80));
	coax_tx_mode->tx_even           = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x2F+((ch%2)*0x80));
	coax_tx_mode->tx_zero_length    = gpio_i2c_read(jaguar1_i2c_addr[devnum], 0x0C+((ch%2)*0x80));
}

void coax_acp_rx_detect_get( void *p_param )
{
	NC_VD_COAX_STR *coax_val = (NC_VD_COAX_STR*)p_param;

	unsigned char ch = coax_val->ch;
	unsigned char val_1 = 0;
	unsigned char val_2 = 0;


	gpio_i2c_write(jaguar1_i2c_addr[coax_val->vd_dev], 0xFF, 0x02+ch%4);
	val_1 = gpio_i2c_read(jaguar1_i2c_addr[coax_val->vd_dev], 0x5c+((ch%2)*0x80));
	//	printk("[drv]%d_1. detection: 0x%02X\n", ch, val_1);

	gpio_i2c_write(jaguar1_i2c_addr[coax_val->vd_dev], 0xFF, 0x00);
	val_2 = gpio_i2c_read(jaguar1_i2c_addr[coax_val->vd_dev], 0xa8 + ch);
	//	printk("[drv]%d_2. detection: 0x%02X\n", ch, val_2);

	coax_val->param = val_1;
}
/**************************************************************************************
 * @desc
 * 	JAGUAR1's    Test function. bank, address, value setting.  get from application
 *
 * @param_in		(NC_VD_COAX_TEST_STR *)coax_data                 Coax Tx setting value
 *
 * @return   	void  		       								 None
 *
 * ioctl : IOC_VDEC_COAX_TEST_DATA_SET
 ***************************************************************************************/
void coax_test_data_set(NC_VD_COAX_TEST_STR *coax_data)
{
	//	printk("[DRV_Set]bank(0x%02X)/addr(0x%02X)/param(0x%02X)\n", coax_data->bank, coax_data->data_addr, coax_data->param );
	gpio_i2c_write(jaguar1_i2c_addr[coax_data->chip_num], 0xFF, coax_data->bank);
	gpio_i2c_write(jaguar1_i2c_addr[coax_data->chip_num], coax_data->data_addr, coax_data->param );
}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's    Test function. Read value bank, address, value. To application
 *
 * @param_in		(NC_VD_COAX_TEST_STR *)coax_data                 Coax read channel
 *
 * @return   	void  		       								 None
 *
 * ioctl : IOC_VDEC_COAX_TEST_DATA_READ
 ***************************************************************************************/
void coax_test_data_get(NC_VD_COAX_TEST_STR *coax_data)
{
	gpio_i2c_write(jaguar1_i2c_addr[coax_data->chip_num], 0xFF, coax_data->bank);
	coax_data->param = gpio_i2c_read(jaguar1_i2c_addr[coax_data->chip_num], coax_data->data_addr);
	//	printk("[DRV_Get]bank(0x%02X), addr(0x%02X), param(0x%02X)\n", coax_data->bank, coax_data->data_addr, coax_data->param );
}

/**************************************************************************************
 * @desc
 * 	JAGUAR1's    Test function. Bank Dump To application
 *
 * @param_in		(NC_VD_COAX_BANK_DUMP_STR *)coax_data            Coax read channel
 *
 * @return   	void  		       								 None
 *
 * ioctl : IOC_VDEC_COAX_TEST_DATA_READ
 ***************************************************************************************/
void coax_test_Bank_dump_get(NC_VD_COAX_BANK_DUMP_STR *coax_data)
{
	int ii = 0;
	memset( coax_data->rx_pelco_data, 0, sizeof(coax_data->rx_pelco_data));

	printk("[drv]dev: %x, Bank: 0x%02X\n", coax_data->vd_dev, coax_data->bank);

	gpio_i2c_write(jaguar1_i2c_addr[coax_data->vd_dev], 0xFF, coax_data->bank);

	for(ii=0; ii<256; ii++)
	{
		coax_data->rx_pelco_data[ii] = gpio_i2c_read(jaguar1_i2c_addr[coax_data->vd_dev], 0x00+ii);
	}

}


/*******************************************************************************
*	Description		: write data to ISP
*	Argurments		: ch(channel ID),reg_addr(high[1byte]:bank, low[1byte]:register)
*					  reg_data(data)
*	Return value	: void
*	Modify			:
*	warning			:
*******************************************************************************/
void acp_isp_write(unsigned char ch, unsigned int reg_addr, unsigned char reg_data)
{
	unsigned char bankaddr = 0x00;
	unsigned char device_id = 0x00;
	/* int i; */
	unsigned char lcnt_bak, lcntm_bak, crc_bak;

	/* set coax RX device ID */
	bankaddr = (reg_addr>>8)&0xFF;
	if( bankaddr >= 0xB0 && bankaddr <= 0xB4 )
	{
		device_id = 0x55;
	}
	else
	{
		device_id = ACP_REG_WR;
	}

	gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0xFF, 0x02+((ch%4)/2));
	lcnt_bak = gpio_i2c_read(jaguar1_i2c_addr[ch/4], 0x05+((ch%2)*0x80));
	lcntm_bak = gpio_i2c_read(jaguar1_i2c_addr[ch/4], 0x0A+((ch%2)*0x80));
	crc_bak = gpio_i2c_read(jaguar1_i2c_addr[ch/4], 0x60+((ch%2)*0x80));
	//gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x05+((ch%2)*0x80), 0x03);
	gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x0A+((ch%2)*0x80), 0x03);
	gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x60+((ch%2)*0x80), device_id);

	/* write data to isp */
	gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0xFF, 0x02+((ch%4)/2));
	if(acp_mode_enable == 1)
	{
		gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0x10+((ch%2)*0x80), ACP_REG_WR);			// data1(#define ACP_AHD2_FHD_D0	0x10)
		gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0x11+((ch%2)*0x80), (reg_addr>>8)&0xFF);	// data2(bank)
		gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0x12+((ch%2)*0x80), reg_addr&0xFF);			// data3(address)
		gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0x13+((ch%2)*0x80), reg_data);				// data4(Don't care)
		gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0x09+((ch%2)*0x80), 0x08);					//   - pulse on(trigger)
		msleep(200);																		// sleep to recognize NRT(15fps) signal for ISP  (M)
		gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0xFF, 0x02+((ch%4)/2));
		gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0x09+((ch%2)*0x80), 0x10);					// reset - pulse off
		gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0x09+((ch%2)*0x80), 0x00);					//   - pulse off		
		printk("ahd acp 8bit mode test\n");
	}
	else
	{
		/*
		// fill Reset
		for(i=0;i<4;i++)
		{
			gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x20+((ch%2)*0x80)+i, 0);
		}
		// Command Shot
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0xFF, 0x02+((ch%4)/2) );
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x0c+((ch%2)*0x80), 0x01);
		msleep(30);
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x0c+((ch%2)*0x80), 0x00);
		*/

		//fill command 
		gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0x20+((ch%2)*0x80), ACP_REG_WR);			// data1(#define ACP_AHD2_FHD_D0	0x10)
		gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0x21+((ch%2)*0x80), (reg_addr>>8)&0xFF);	// data2(bank)
		gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0x22+((ch%2)*0x80), reg_addr&0xFF);			// data3(address)
		gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0x23+((ch%2)*0x80), reg_data);				// data4(Don't care)
		// Command Shot
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0xFF, 0x02+((ch%4)/2) );
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x0c+((ch%2)*0x80), 0x01);
		msleep(200);
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x0c+((ch%2)*0x80), 0x00);
		printk("ahd pelcod 16bit mode test\n");
	}	

	//gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x05+((ch%2)*0x80), lcnt_bak);
	gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x0A+((ch%2)*0x80), lcntm_bak);
	gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x60+((ch%2)*0x80), crc_bak);

	#if 0 // debuggin
	printk(">>>>> CH:%d NORMAL, RX->TX : ", ch );
	printk("%02x ", ACP_REG_WR );
	printk("%02x ", (reg_addr>>8)&0xFF );
	printk("%02x ", reg_addr&0xFF );
	printk("%02x ", reg_data );
	printk("\n");
        #endif
}

/*******************************************************************************
*	Description		: read acp data of ISP
*	Argurments		: ch(channel ID), reg_addr(high[1byte]:bank, low[1byte]:register)
*	Return value	: void
*	Modify			:
*	warning			:
*******************************************************************************/
unsigned char acp_isp_read(unsigned char ch, unsigned int reg_addr)
{
	unsigned int data_3x50[8];
	unsigned char lcnt_bak, lcntm_bak, crc_bak;
	unsigned char bank;
	unsigned char addr;
	int i;

	bank = (reg_addr>>8)&0xFF;
	addr = reg_addr&0xFF;

	gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0xFF, 0x02+((ch%4)/2));
	lcnt_bak = gpio_i2c_read(jaguar1_i2c_addr[ch/4], 0x05+((ch%2)*0x80));
	lcntm_bak = gpio_i2c_read(jaguar1_i2c_addr[ch/4], 0x0A+((ch%2)*0x80));
	crc_bak = gpio_i2c_read(jaguar1_i2c_addr[ch/4], 0x60+((ch%2)*0x80));
	//gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x05+((ch%2)*0x80), 0x03);
	gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x0A+((ch%2)*0x80), 0x03);
	gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x60+((ch%2)*0x80), ACP_REG_RD);

	if(acp_mode_enable == 1)
	{
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0xFF, 0x02+((ch%4)/2));
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x10+(ch%2)*0x80, 0x61);
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x10+1+(ch%2)*0x80, bank);
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x10+2+(ch%2)*0x80, addr);
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x10+3+(ch%2)*0x80, 0x00);
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x09+(ch%2)*0x80, 0x08);
	}
	else
	{
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0xFF, 0x02+((ch%4)/2));
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x20+(ch%2)*0x80, 0x61);
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x20+1+(ch%2)*0x80, bank);
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x20+2+(ch%2)*0x80, addr);
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x20+3+(ch%2)*0x80, 0x00);
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x0c+(ch%2)*0x80, 0x01);
	}
	msleep(300);
	for(i=0;i<8;i++)
	{
		data_3x50[i] = gpio_i2c_read(jaguar1_i2c_addr[ch/4],0x50+i+((ch%2)*0x80));
		printk("acp_isp_read ch = %d, reg_addr = %x, reg_data = %x\n", ch,reg_addr, data_3x50[i]);
	}
	//gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x05+((ch%2)*0x80), lcnt_bak);
	gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x0A+((ch%2)*0x80), lcntm_bak);
	gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x60+((ch%2)*0x80), crc_bak);
	
	if(acp_mode_enable == 1)
	{
		gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0x09+((ch%2)*0x80), 0x10);
		gpio_i2c_write( jaguar1_i2c_addr[ch/4], 0x09+((ch%2)*0x80), 0x00);
	}
	else
	{
		gpio_i2c_write(jaguar1_i2c_addr[ch/4], 0x0c+(ch%2)*0x80, 0x00);
	}
	return data_3x50[3];
}


