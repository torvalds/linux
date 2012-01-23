
#ifndef __LCD_PANNEL_CFG_H__
#define __LCD_PANNEL_CFG_H__

//#include "string.h"
#include "../../include/eBSP_common_inc.h"
#include "../../bsp_display.h"

static void LCD_power_on(__u32 sel);
static void LCD_power_off(__u32 sel);
static void LCD_bl_open(__u32 sel);//打开LCD背光
static void LCD_bl_close(__u32 sel);//关闭LCD背光

extern void LCD_OPEN_FUNC(__u32 sel, LCD_FUNC func, __u32 delay/*ms*/);
extern void LCD_CLOSE_FUNC(__u32 sel, LCD_FUNC func, __u32 delay/*ms*/);
extern void TCON_open(__u32 sel);//打开LCD控制器
extern void TCON_close(__u32 sel);//关闭LCD控制器
extern void LCD_delay(__u32 count);
extern void LCD_get_init_para(__lcd_panel_init_para_t *para);


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

