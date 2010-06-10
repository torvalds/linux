#ifndef _RK1000_TV_H
#define _RK1000_TV_H
#include <linux/ioctl.h>

/******************* TVOUT ioctl CMD ************************/
#define TVOUT_MAGIC 'T'

#define RK1000_TV_SET_OUTPUT     _IOW(TVOUT_MAGIC, 1, int)

/******************* TVOUT OUTPUT TYPE **********************/
#define RK28_LCD    0
#define Cvbs_NTSC   1
#define Cvbs_PAL    2
#define Ypbpr480    3
#define Ypbpr576    4
#define Ypbpr720    5

int rk1000_tv_get_output_status(void);

#endif

