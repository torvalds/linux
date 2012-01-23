
#ifndef __LCD_TV_BSP_I_H__
#define __LCD_TV_BSP_I_H__


#include "ebios_lcdc_tve.h"

#define LCDC_BIT0          0x00000001
#define LCDC_BIT1		  0x00000002
#define LCDC_BIT2		  0x00000004
#define LCDC_BIT3		  0x00000008
#define LCDC_BIT4		  0x00000010
#define LCDC_BIT5		  0x00000020
#define LCDC_BIT6		  0x00000040
#define LCDC_BIT7		  0x00000080
#define LCDC_BIT8		  0x00000100
#define LCDC_BIT9		  0x00000200
#define LCDC_BIT10		  0x00000400
#define LCDC_BIT11		  0x00000800
#define LCDC_BIT12		  0x00001000
#define LCDC_BIT13		  0x00002000
#define LCDC_BIT14		  0x00004000
#define LCDC_BIT15		  0x00008000
#define LCDC_BIT16		  0x00010000
#define LCDC_BIT17		  0x00020000
#define LCDC_BIT18		  0x00040000
#define LCDC_BIT19		  0x00080000
#define LCDC_BIT20		  0x00100000
#define LCDC_BIT21		  0x00200000
#define LCDC_BIT22		  0x00400000
#define LCDC_BIT23		  0x00800000
#define LCDC_BIT24		  0x01000000
#define LCDC_BIT25		  0x02000000
#define LCDC_BIT26		  0x04000000
#define LCDC_BIT27		  0x08000000
#define LCDC_BIT28		  0x10000000
#define LCDC_BIT29		  0x20000000
#define LCDC_BIT30		  0x40000000
#define LCDC_BIT31		  0x80000000


#define LCDC_CTL_OFF   			0x00				/*LCD Controller control registers offset*/
#define LCDC_STS_OFF   			0x04				/*LCD Controller status registers offset*/
#define LCDC_DCLK_OFF			0x08				/*LCD Controller dot clock registers offset*/
#define LCDC_BASIC0_OFF  		0x0c				/*LCD Controller base0 registers offset*/
#define LCDC_BASIC1_OFF  		0x10				/*LCD Controller base1 registers offset*/
#define LCDC_MODE_OFF			0x14				/*LCD Controller mode set registers offset*/
#define LCDC_TTL1_OFF			0x18				/*LCD Controller TTL1 registers offset*/
#define LCDC_TTL2_OFF			0x1c				/*LCD Controller TTL2 registers offset*/
#define LCDC_TTL3_OFF			0x20				/*LCD Controller TTL3 registers offset*/
#define LCDC_TTL4_OFF			0x24				/*LCD Controller TTL4 registers offset*/
#define LCDC_HDTV0_OFF			0x30				/*LCD Controller HDTV0 registers offset*/
#define LCDC_HDTV1_OFF			0x34				/*LCD Controller HDTV1 registers offset*/
#define LCDC_HDTV2_OFF			0x38				/*LCD Controller HDTV2 registers offset*/
#define LCDC_HDTV3_OFF			0x3c				/*LCD Controller HDTV3 registers offset*/
#define LCDC_HDTV4_OFF			0x40				/*LCD Controller HDTV4 registers offset*/
#define LCDC_HDTV5_OFF			0x44				/*LCD Controller HDTV5 registers offset*/
#define LCDC_HDTV6_OFF			0x48				/*LCD Controller HDTV6 registers offset*/
#define LCDC_GAMMA_TBL_OFF	    0x80				/*LCD Controller gamma table registers offset*/
#define LCDC_CSC0_OFF			0xc0				/*LCD Controller csc0 registers offset*/
#define LCDC_CSC1_OFF			0xc4				/*LCD Controller csc1 registers offset*/
#define LCDC_CSC2_OFF			0xc8				/*LCD Controller csc2 registers offset*/
#define LCDC_CSC3_OFF			0xcc				/*LCD Controller csc3 registers offset*/
#define LCDC_SRGB_OFF			0xd0				/*LCD Controller RGB enhancement registers offset*/
#define LCDC_CPUWR_OFF		    0xe0				/*LCD Controller cpu wr registers offset*/
#define LCDC_CPURD_OFF		    0xe4				/*LCD Controller cpu rd registers offset*/
#define LCDC_CPURDNX_OFF        0xe8				/*LCD Controller cpu rdnx registers offset*/
#define LCDC_IOCTL1_OFF		    0xf0				/*LCD Controller io control1 registers offset*/
#define LCDC_IOCTL2_OFF			0xf4				/*LCD Controller io control2 registers offset*/
#define LCDC_DUBUG_OFF          0xfc                /*LCD Controller debug register*/
#define LCDC_GAMMA_TABLE_OFF	0x100

#define LCDC_GAMMA_TABLE_SIZE	0x400


extern __u32 lcdc_reg_base0;
extern __u32 lcdc_reg_base1;
#define LCDC_GET_REG_BASE(sel)    ((sel)==0?(lcdc_reg_base0):(lcdc_reg_base1))

#define LCDC_WUINT8(sel,offset,value)           (*((volatile __u8  *)(LCDC_GET_REG_BASE(sel)+offset))=(value))
#define LCDC_RUINT8(sel,offset)                 (*((volatile __u8  *)(LCDC_GET_REG_BASE(sel)+offset)))
#define LCDC_WUINT16(sel,offset,value)          (*((volatile __u16 *)(LCDC_GET_REG_BASE(sel)+offset))=(value))
#define LCDC_RUINT16(sel,offset)                (*((volatile __u16 *)(LCDC_GET_REG_BASE(sel)+offset)))
#define LCDC_WUINT32(sel,offset,value)          (*((volatile __u32 *)(LCDC_GET_REG_BASE(sel)+offset))=(value))
#define LCDC_RUINT32(sel,offset)                (*((volatile __u32 *)(LCDC_GET_REG_BASE(sel)+offset)))

#define LCDC_WUINT8IDX(sel,offset,index,value)  (*((volatile __u8  *)(LCDC_GET_REG_BASE(sel)+offset+index))=(value))
#define LCDC_RUINT8IDX(sel,offset,index)        (*((volatile __u8  *)(LCDC_GET_REG_BASE(sel)+offset+index)))
#define LCDC_WUINT16IDX(sel,offset,index,value) (*((volatile __u16 *)(LCDC_GET_REG_BASE(sel)+offset+2*index))=(value))
#define LCDC_RUINT16IDX(sel,offset,index)       (*((volatile __u16 *)(LCDC_GET_REG_BASE(sel)+offset+2*index)))
#define LCDC_WUINT32IDX(sel,offset,index,value) (*((volatile __u32 *)(LCDC_GET_REG_BASE(sel)+offset+4*index))=(value))
#define LCDC_RUINT32IDX(sel,offset,index)       (*((volatile __u32 *)(LCDC_GET_REG_BASE(sel)+offset+4*index)))

#define LCDC_SET_BIT(sel,offset,bit)            (*((volatile __u32 *)(LCDC_GET_REG_BASE(sel)+offset)) |=(bit))
#define LCDC_CLR_BIT(sel,offset,bit)            (*((volatile __u32 *)(LCDC_GET_REG_BASE(sel)+offset)) &=(~bit))




/*tv encoder registers offset*/
#define TVE_000    0x00
#define TVE_004    0X04
#define TVE_008    0X08
#define TVE_00C    0x0c
#define TVE_010    0x10
#define TVE_014    0x14
#define TVE_018    0x18
#define TVE_01C    0x1c
#define TVE_020    0x20
#define TVE_024    0x24
#define TVE_030    0X30
#define TVE_034    0x34
#define TVE_038    0x38
#define TVE_03C    0x3c
#define TVE_100    0x100
#define TVE_104    0x104
#define TVE_10C    0x10c
#define TVE_110    0x110
#define TVE_114    0x114
#define TVE_118    0x118
#define TVE_11C    0x11c
#define TVE_124    0x124
#define TVE_128    0x128
#define TVE_12C    0x12c
#define TVE_130    0x130
#define TVE_138    0x138
#define TVE_13C    0x13C



extern __u32 tve_reg_base;
#define TVE_GET_REG_BASE()    (tve_reg_base)

#define TVE_WUINT8(offset,value)           (*((volatile __u8  *)(TVE_GET_REG_BASE()+offset))=(value))
#define TVE_RUINT8(offset)                 (*((volatile __u8  *)(TVE_GET_REG_BASE()+offset)))
#define TVE_WUINT16(offset,value)          (*((volatile __u16 *)(TVE_GET_REG_BASE()+offset))=(value))
#define TVE_RUINT16(offset)                (*((volatile __u16 *)(TVE_GET_REG_BASE()+offset)))
#define TVE_WUINT32(offset,value)          (*((volatile __u32 *)(TVE_GET_REG_BASE()+offset))=(value))
#define TVE_RUINT32(offset)                (*((volatile __u32 *)(TVE_GET_REG_BASE()+offset)))

#define TVE_WUINT8IDX(offset,index,value)  (*((volatile __u8  *)(TVE_GET_REG_BASE()+offset+index))=(value))
#define TVE_RUINT8IDX(offset,index)        (*((volatile __u8  *)(TVE_GET_REG_BASE()+offset+index)))
#define TVE_WUINT16IDX(offset,index,value) (*((volatile __u16 *)(TVE_GET_REG_BASE()+offset+2*index))=(value))
#define TVE_RUINT16IDX(offset,index)       (*((volatile __u16 *)(TVE_GET_REG_BASE()+offset+2*index)))
#define TVE_WUINT32IDX(offset,index,value) (*((volatile __u32 *)(TVE_GET_REG_BASE()+offset+4*index))=(value))
#define TVE_RUINT32IDX(offset,index)       (*((volatile __u32 *)(TVE_GET_REG_BASE()+offset+4*index)))

#define TVE_SET_BIT(offset,bit)            (*((volatile __u32 *)(TVE_GET_REG_BASE()+offset)) |= (bit))
#define TVE_CLR_BIT(offset,bit)            (*((volatile __u32 *)(TVE_GET_REG_BASE()+offset)) &= (~(bit)))

#endif
