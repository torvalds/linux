/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP1_VERSION_H
#define _RKISP1_VERSION_H
#include <linux/version.h>

/*
 *RKISP1 DRIVER VERSION NOTE
 *
 *v0.1.0:
 *1. First version;
 *
 *v0.1.1:
 *1. request isp irqs independently for rk3326/rk1808;
 *2. fix demosaic is not bypass for grey sensor;
 *3. rk3326 use csi2host instead of old mipi host;
 *4. isp v12/v13 add raw stream;
 *5. selfpath support interlace input;
 *6. speed up stream off;
 *7. support for RK3368;
 *8. fix dvp data width config;
 *9. fix sp rgb output format;
 *10. del nonsupport yuv format;
 *11. fix high fps preview blurred bug;
 *12. support otp information;
 *13. add module and lens name to match iq file;
 *14. change vm149c driver and add vm149c dts node;
 *15. support iesharp/demosaiclp/wdr for isp v12/v13;
 *16. check first iq param is set or not;
 *17. add macro to switch between old mipi and new mipi;
 *18. stop isp when too many errors are reported;
 *19. use tasklet to get 3A states;
 *20. stop mipi with shutdown lan;
 *21. check for capture S_FMT;
 *22. raw patch with default sensor fmt&size;
 *
 *v0.1.2:
 *1. fix reset on too high isp_clk rate will result in bus dead;
 *2. add RKMODULE_LSC_CFG ioctl;
 *
 *v0.1.3:
 *1. fix wrong RG10 format
 *2. clear unready subdevice when kernel boot complete
 *3. fix diff isp ver to get frame num
 *4. enable af awb irq
 *
 *v0.1.4:
 *1. add dmarx patch;
 *2. fix get zero data when start stream again;
 *3. add pipeline power management;
 *
 */

#define RKISP1_DRIVER_VERSION KERNEL_VERSION(0, 1, 0x4)

#endif
