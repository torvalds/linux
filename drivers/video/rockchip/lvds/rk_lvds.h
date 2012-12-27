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


#define LVDS_8BIT_1     0
#define LVDS_8BIT_2     1
#define LVDS_8BIT_3     2
#define LVDS_6BIT       3
/*      				LVDS config         
 *                  LVDS 外部连线接法                       
 *          LVDS_8BIT_1    LVDS_8BIT_2     LVDS_8BIT_3     LVDS_6BIT
----------------------------------------------------------------------
    TX0     R0              R2              R2              R0
    TX1     R1              R3              R3              R1
    TX2     R2              R4              R4              R2
Y   TX3     R3              R5              R5              R3
0   TX4     R4              R6              R6              R4
    TX6     R5              R7              R7              R5
    TX7     G0              G2              G2              G0
----------------------------------------------------------------------
    TX8     G1              G3              G3              G1
    TX9     G2              G4              G4              G2
Y   TX12    G3              G5              G5              G3
1   TX13    G4              G6              G6              G4
    TX14    G5              G7              G7              G5
    TX15    B0              B2              B2              B0
    TX18    B1              B3              B3              B1
----------------------------------------------------------------------
    TX19    B2              B4              B4              B2
    TX20    B3              B5              B5              B3
    TX21    B4              B6              B6              B4
Y   TX22    B5              B7              B7              B5
2   TX24    HSYNC           HSYNC           HSYNC           HSYNC
    TX25    VSYNC           VSYNC           VSYNC           VSYNC
    TX26    ENABLE          ENABLE          ENABLE          ENABLE
----------------------------------------------------------------------    
    TX27    R6              R0              GND             GND
    TX5     R7              R1              GND             GND
    TX10    G6              G0              GND             GND
Y   TX11    G7              G1              GND             GND
3   TX16    B6              B0              GND             GND
    TX17    B7              B1              GND             GND
    TX23    RSVD            RSVD            RSVD            RSVD
----------------------------------------------------------------------
*/


extern int rk_lvds_register(rk_screen *screen);
#endif
