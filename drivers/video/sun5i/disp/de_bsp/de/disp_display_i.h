#ifndef __DISP_DISPLAY_I_H__
#define __DISP_DISPLAY_I_H__

#include "ebios/ebios_de.h"
#include "ebios/ebios_lcdc_tve.h"



#ifdef __LINUX_OSAL__
#define DE_INF __inf
#define DE_MSG __msg
#define DE_WRN __wrn
#define OSAL_IRQ_RETURN IRQ_HANDLED
#else
#define DE_INF(msg...)
#define DE_MSG __msg
#define DE_WRN __wrn
#define OSAL_IRQ_RETURN DIS_SUCCESS
#endif

#define HANDTOID(handle)  ((handle) - 100)
#define IDTOHAND(ID)  ((ID) + 100)

#define INTC_IRQNO_SCALER0  47
#define INTC_IRQNO_SCALER1  48
#define INTC_IRQNO_LCDC0    44
#define INTC_IRQNO_LCDC1    45

#define MAX_SPRITE_BLOCKS	32



/*basic data information definition*/
enum
{
    FALSE=0,
    TRUE
};

#define DIS_NULL 0

enum
{
   DIS_SUCCESS=0,
   DIS_FAIL=-1,
   DIS_PARA_FAILED=-2,
   DIS_PRIO_ERROR=-3,
   DIS_OBJ_NOT_INITED=-4,
   DIS_NOT_SUPPORT=-5,
   DIS_NO_RES=-6,
   DIS_OBJ_COLLISION=-7,
   DIS_DEV_NOT_INITED=-8,
   DIS_DEV_SRAM_COLLISION=-9,
   DIS_TASK_ERROR = -10,
   DIS_PRIO_COLLSION = -11
};

#define BIT0      0x00000001  
#define BIT1		  0x00000002  
#define BIT2		  0x00000004  
#define BIT3		  0x00000008  
#define BIT4		  0x00000010  
#define BIT5		  0x00000020  
#define BIT6		  0x00000040  
#define BIT7		  0x00000080  
#define BIT8		  0x00000100  
#define BIT9		  0x00000200  
#define BIT10		  0x00000400  
#define BIT11		  0x00000800  
#define BIT12		  0x00001000  
#define BIT13		  0x00002000  
#define BIT14		  0x00004000  
#define BIT15		  0x00008000  
#define BIT16		  0x00010000  
#define BIT17		  0x00020000  
#define BIT18		  0x00040000  
#define BIT19		  0x00080000  
#define BIT20		  0x00100000  
#define BIT21		  0x00200000  
#define BIT22		  0x00400000  
#define BIT23		  0x00800000  
#define BIT24		  0x01000000  
#define BIT25		  0x02000000  
#define BIT26		  0x04000000  
#define BIT27		  0x08000000  
#define BIT28		  0x10000000  
#define BIT29		  0x20000000  
#define BIT30		  0x40000000  
#define BIT31		  0x80000000 

#define sys_get_value(n)    (*((volatile __u8 *)(n)))          /* byte input */
#define sys_put_value(n,c)  (*((volatile __u8 *)(n))  = (c))   /* byte output */
#define sys_get_hvalue(n)   (*((volatile __u16 *)(n)))         /* half word input */
#define sys_put_hvalue(n,c) (*((volatile __u16 *)(n)) = (c))   /* half word output */
#define sys_get_wvalue(n)   (*((volatile __u32 *)(n)))          /* word input */
#define sys_put_wvalue(n,c) (*((volatile __u32 *)(n))  = (c))   /* word output */
#define sys_set_bit(n,c)    (*((volatile __u8 *)(n)) |= (c))   /* byte bit set */
#define sys_clr_bit(n,c)    (*((volatile __u8 *)(n)) &=~(c))   /* byte bit clear */
#define sys_set_hbit(n,c)   (*((volatile __u16 *)(n))|= (c))   /* half word bit set */
#define sys_clr_hbit(n,c)   (*((volatile __u16 *)(n))&=~(c))   /* half word bit clear */
#define sys_set_wbit(n,c)   (*((volatile __u32 *)(n))|= (c))    /* word bit set */
#define sys_cmp_wvalue(n,c) (c == (*((volatile __u32 *) (n))))
#define sys_clr_wbit(n,c)   (*((volatile __u32 *)(n))&=~(c))  

#endif
