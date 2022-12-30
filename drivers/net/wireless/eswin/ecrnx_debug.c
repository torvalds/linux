/**
 ****************************************************************************************
 *
 * @file ecrnx_debug.c
 *
 * @brief ecrnx driver debug functions;
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ****************************************************************************************
 */
#include <stdarg.h>
#include <linux/init.h>
#include "ecrnx_defs.h"
#include "eswin_utils.h"

#ifdef CONFIG_ECRNX_DBG_LEVEL
int ecrnx_dbg_level = CONFIG_ECRNX_DBG_LEVEL; //defined in the 6600u_feature file
#else
int ecrnx_dbg_level = DRV_DBG_TYPE_NONE;
#endif

LOG_CTL_ST log_ctl={
    .level = 2,
    .dir = 0,
};

#ifndef CONFIG_ECRNX_DEBUGFS_CUSTOM
int ecrnx_fw_log_level_set(u32 level, u32 dir)
{
    uint32_t dbg_info[3] = {0};

    dbg_info[0] = 0x01; //SLAVE_LOG_LEVEL
    dbg_info[1] = level;
    dbg_info[2] = dir;

    ECRNX_PRINT("%s: fstype:%d, level:%d, dir:%d \n", __func__, dbg_info[0], dbg_info[1], dbg_info[2]);
    ECRNX_PRINT("info_len:%d \n", sizeof(dbg_info));
    return host_send(dbg_info, sizeof(dbg_info), TX_FLAG_MSG_DEBUGFS_IE);
}

#endif

// #endif


