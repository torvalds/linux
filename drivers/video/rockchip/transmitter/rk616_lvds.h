#ifndef __RK616_VIF_H__
#define __RK616_VIF_H__
#include<linux/mfd/rk616.h>
#include<linux/earlysuspend.h>
#include<linux/rk_screen.h>


struct rk616_lvds {
	struct mfd_rk616 *rk616;
	rk_screen *screen;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif 
};

#endif
