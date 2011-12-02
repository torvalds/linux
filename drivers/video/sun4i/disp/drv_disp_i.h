#ifndef __DRV_DISP_I_H__
#define __DRV_DISP_I_H__

#include "de_bsp/bsp_display.h"

typedef enum
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
}__disp_return_value;

#define HANDTOID(handle)  ((handle) - 100)
#define IDTOHAND(ID)  ((ID) + 100)


#define DISP_IO_NUM     8 
#define DISP_IO_SCALER0 0
#define DISP_IO_SCALER1 1
#define DISP_IO_IMAGE0  2
#define DISP_IO_IMAGE1  3
#define DISP_IO_LCDC0   4
#define DISP_IO_LCDC1   5
#define DISP_IO_TVEC0    6
#define DISP_IO_TVEC1    7

#define sys_get_hvalue(n)   (*((volatile __u16 *)(n)))         /* half word input */
#define sys_put_hvalue(n,c) (*((volatile __u16 *)(n)) = (c))   /* half word output */
#define sys_get_wvalue(n)   (*((volatile __u32 *)(n)))          /* word input */
#define sys_put_wvalue(n,c) (*((volatile __u32 *)(n))  = (c))   /* word output */

#endif
