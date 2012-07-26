///////////////////////////////////////////////////////////////////////////////////
//
// Filename: lnxdrv.h
// Author:	sjchen
// Copyright: 
// Date: 2012/07/09
// Description:
//			the macor of GPS baseband
//
// Revision:
//		0.0.1
//
///////////////////////////////////////////////////////////////////////////////////

#ifndef __LNXDRV_H__
#define __LNXDRV_H__

///////////////////////////////////////////////////////////////////////////////////
// 
// macro declaration
//
///////////////////////////////////////////////////////////////////////////////////

#define MEM_CHECK_BOUNDARY  (16)

//base band control registers offset	    
#define BB_CTRL_OFFSET       			0x0400
#define BB_START_ADR_OFFSET  			0x0404
#define BB_DS_PAR_OFFSET     			0x0408
#define BB_INT_ENA_OFFSET    			0x040c
#define BB_INT_STATUS_OFFSET 			0x0410
#define BB_CHN_STATUS_OFFSET 			0x0414
#define BB_CHN_VALID_OFFSET  			0x0418
#define BB_TIMER_VAL_OFFSET  			0x041c
#define BB_RF_WT_ADDR_OFFSET 			0x0420
				
				

//the following is bb register bit define
#define BB_RESET				0x4	 
#define BB_NXT_BLK				0x2	 
#define BB_TRICKLE				0x1	 
#define BB_CTRL_CLR				0x0	 

#define TIMEOUT_INT				0x1	 
#define ACC_BLK_DONE_INT			0x2	 
#define OBUF_RDY_INT				0x4	 

#endif /* __LNXDRV_H__ */



