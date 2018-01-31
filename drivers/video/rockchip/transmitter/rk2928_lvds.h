/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RK_LVDS_H_
#define RK_LVDS_H

#define LVDS_CON0_OFFSET 	0x150
#define LVDS_CON0_REG 		(RK2928_GRF_BASE + LVDS_CON0_OFFSET) 

#define LVDSRdReg()					__raw_readl(LVDS_CON0_REG)
#define LVDSWrReg(val)       	__raw_writel( val ,LVDS_CON0_REG)

#define m_value(x,offset,mask)      \
			((mask<<(offset+16)) | (x&mask)<<offset)

#define OEN 				(1<<9)
#define m_OEN(x)   			m_value(x,9,1)
#define PD_PLL 				(1<<8)
#define m_PD_PLL(x) 		m_value(x,8,1)
#define PDN_CBG 			(1<<7)
#define m_PDN_CBG(x)		m_value(x,7,1)
#define PDN 				(1<<6)
#define m_PDN(x) 			m_value(x,6,1)
#define DS 					(3<<4)
#define m_DS(x) 			m_value(x,4,3)
#define MSBSEL				(1<<3)
#define m_MSBSEL(x)			m_value(x,3,1)
#define OUT_FORMAT			(3<<1)
#define m_OUT_FORMAT(x) 	m_value(x,1,3)
#define LCDC_SEL			(1<<0)
#define m_LCDC_SEL(x)		m_value(x,0,1)

enum{
	OUT_DISABLE=0,
	OUT_ENABLE,
};

//DS
#define DS_3PF 			0
#define DS_7PF 			0
#define DS_5PF 			0
#define DS_10PF			0

//LVDS lane input format
#define DATA_D0_MSB    	0
#define DATA_D7_MSB    	1
//LVDS input source
#define FROM_LCDC0     	0
#define FROM_LCDC1 		1

extern int rk_lvds_register(rk_screen *screen);
#endif
