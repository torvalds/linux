
#ifndef __DE_LCDC_I_H__                                                                                                         
#define __DE_LCDC_I_H__                                                                                                         
                                                                                                                                
#define LCDC_BIT0         (0x00000001)                                                                                          
#define LCDC_BIT1		  (0x00000002)                                                                                          
#define LCDC_BIT2		  (0x00000004)  
#define LCDC_BIT3		  (0x00000008)  
#define LCDC_BIT4		  (0x00000010)  
#define LCDC_BIT5		  (0x00000020)  
#define LCDC_BIT6		  (0x00000040)  
#define LCDC_BIT7		  (0x00000080)  
#define LCDC_BIT8		  (0x00000100)  
#define LCDC_BIT9		  (0x00000200)  
#define LCDC_BIT10		  (0x00000400)  
#define LCDC_BIT11		  (0x00000800)  
#define LCDC_BIT12		  (0x00001000)  
#define LCDC_BIT13		  (0x00002000)  
#define LCDC_BIT14		  (0x00004000)  
#define LCDC_BIT15		  (0x00008000)  
#define LCDC_BIT16		  (0x00010000)  
#define LCDC_BIT17		  (0x00020000)  
#define LCDC_BIT18		  (0x00040000)  
#define LCDC_BIT19		  (0x00080000)  
#define LCDC_BIT20		  (0x00100000)  
#define LCDC_BIT21		  (0x00200000)  
#define LCDC_BIT22		  (0x00400000)  
#define LCDC_BIT23		  (0x00800000)  
#define LCDC_BIT24		  (0x01000000)  
#define LCDC_BIT25		  (0x02000000)  
#define LCDC_BIT26		  (0x04000000)  
#define LCDC_BIT27		  (0x08000000)  
#define LCDC_BIT28		  (0x10000000)  
#define LCDC_BIT29		  (0x20000000)  
#define LCDC_BIT30		  (0x40000000)  
#define LCDC_BIT31		  (0x80000000) 


#define LCDC_GCTL_OFF   		(0x000)				/*LCD Controller global control registers offset*/
#define LCDC_GINT0_OFF   		(0x004)				/*LCD Controller interrupt registers offset*/
#define LCDC_GINT1_OFF   		(0x008)				/*LCD Controller interrupt registers offset*/
#define LCDC_FRM0_OFF   		(0x010)				/*LCD Controller frm registers offset*/
#define LCDC_FRM1_OFF   		(0x014)				/*LCD Controller frm registers offset*/
#define LCDC_FRM2_OFF   		(0x02c)				/*LCD Controller frm registers offset*/
#define LCDC_CTL_OFF   			(0x040)				/*LCD Controller control registers offset*/
#define LCDC_DCLK_OFF			(0x044)				/*LCD Controller dot clock registers offset*/
#define LCDC_BASIC0_OFF  		(0x048)				/*LCD Controller base0 registers offset*/
#define LCDC_BASIC1_OFF  		(0x04c)				/*LCD Controller base1 registers offset*/
#define LCDC_BASIC2_OFF  		(0x050)				/*LCD Controller base2 registers offset*/
#define LCDC_BASIC3_OFF  		(0x054)				/*LCD Controller base3 registers offset*/
#define LCDC_HVIF_OFF  			(0x058)				/*LCD Controller hv interface registers offset*/
#define LCDC_CPUIF_OFF  		(0x060)				/*LCD Controller cpu interface registers offset*/
#define LCDC_CPUWR_OFF		    (0x064)				/*LCD Controller cpu wr registers offset*/
#define LCDC_CPURD_OFF		    (0x068)				/*LCD Controller cpu rd registers offset*/
#define LCDC_CPURDNX_OFF        (0x06c)				/*LCD Controller cpu rdnx registers offset*/
#define LCDC_TTL0_OFF			(0x070)				/*LCD Controller TTL0 registers offset*/
#define LCDC_TTL1_OFF			(0x074)				/*LCD Controller TTL1 registers offset*/
#define LCDC_TTL2_OFF			(0x078)				/*LCD Controller TTL2 registers offset*/
#define LCDC_TTL3_OFF			(0x07c)				/*LCD Controller TTL3 registers offset*/
#define LCDC_TTL4_OFF			(0x080)				/*LCD Controller TTL4 registers offset*/
#define LCDC_LVDS_OFF			(0x084)				/*LCD Controller LVDS registers offset*/
#define LCDC_IOCTL0_OFF		    (0x088)				/*LCD Controller io control0 registers offset*/
#define LCDC_IOCTL1_OFF			(0x08c)				/*LCD Controller io control1 registers offset*/

#define LCDC_HDTVIF_OFF			(0x090)				/*LCD Controller tv interface  registers offset*/
#define LCDC_HDTV0_OFF			(0x094)				/*LCD Controller HDTV0 registers offset*/
#define LCDC_HDTV1_OFF			(0x098)				/*LCD Controller HDTV1 registers offset*/
#define LCDC_HDTV2_OFF			(0x09c)				/*LCD Controller HDTV2 registers offset*/
#define LCDC_HDTV3_OFF			(0x0a0)				/*LCD Controller HDTV3 registers offset*/
#define LCDC_HDTV4_OFF			(0x0a4)				/*LCD Controller HDTV4 registers offset*/
#define LCDC_HDTV5_OFF			(0x0a8)				/*LCD Controller HDTV5 registers offset*/
#define LCDC_IOCTL2_OFF		    (0x0f0)				/*LCD Controller io control2 registers offset*/
#define LCDC_IOCTL3_OFF			(0x0f4)				/*LCD Controller io control3 registers offset*/
#define LCDC_DUBUG_OFF          (0x0fc)             /*LCD Controller debug register*/

#define LCDC_CEU_OFF          	(0x100)
#define	LCDC_MUX_CTRL			(0x200)
#define	LCDC_LVDS_ANA0			(0x220)
#define	LCDC_LVDS_ANA1			(0x224)

#define	LCDC_3DF_CTL			(0x300)
#define	LCDC_3DF_A1B			(0x304)
#define	LCDC_3DF_A1E			(0x308)
#define	LCDC_3DF_D1				(0x30C)
#define	LCDC_3DF_A2B			(0x310)
#define	LCDC_3DF_A2E			(0x314)
#define	LCDC_3DF_D2				(0x318)
#define	LCDC_3DF_A3B			(0x31C)
#define	LCDC_3DF_A3E			(0x320)
#define	LCDC_3DF_D3				(0x318)

#define LCDC_GAMMA_TABLE_OFF    (0x400)

#define LCDC_GET_REG_BASE(sel)    ((sel)==0?(lcdc_reg_base0):(lcdc_reg_base1))

#define LCDC_WUINT32(sel,offset,value)          (*((volatile __u32 *)( LCDC_GET_REG_BASE(sel) + (offset) ))=(value))
#define LCDC_RUINT32(sel,offset)                (*((volatile __u32 *)( LCDC_GET_REG_BASE(sel) + (offset) )))

#define LCDC_SET_BIT(sel,offset,bit)            (*((volatile __u32 *)( LCDC_GET_REG_BASE(sel) + (offset) )) |=(bit))
#define LCDC_CLR_BIT(sel,offset,bit)            (*((volatile __u32 *)( LCDC_GET_REG_BASE(sel) + (offset) )) &=(~(bit)))
#define LCDC_INIT_BIT(sel,offset,c,s)			(*((volatile __u32 *)( LCDC_GET_REG_BASE(sel) + (offset) )) = \
												(((*(volatile __u32 *)( LCDC_GET_REG_BASE(sel) + (offset) )) & (~(c))) | (s)))

#endif
