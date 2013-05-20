
#ifndef _HIF_SDIO_CHRDEV_H_

#define _HIF_SDIO_CHRDEV_H_


#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <linux/kthread.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>


#include "wmt_exp.h"
#include "wmt_plat.h"
#ifdef CFG_WMT_PS_SUPPORT
#undef CFG_WMT_PS_SUPPORT
#endif


extern INT32 hif_sdio_create_dev_node(void);
extern INT32 hif_sdio_remove_dev_node(void);
extern INT32 hifsdiod_start(void);
extern INT32 hifsdiod_stop(void);
INT32 hif_sdio_match_chipid_by_dev_id (const struct sdio_device_id *id);
INT32 hif_sdio_is_chipid_valid (INT32 chipId);



#endif /*_HIF_SDIO_CHRDEV_H_*/
