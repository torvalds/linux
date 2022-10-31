/**
 ****************************************************************************************
 *
 * @file ecrnx_debug.h
 *
 * @brief ecrnx driver debug structure declarations
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ****************************************************************************************
 */

#ifndef ECRNX_DEBUG_H_
#define ECRNX_DEBUG_H_

#ifdef CONFIG_ECRNX_SOFTMAC
#define FW_STR  "lmac"
#elif defined CONFIG_ECRNX_FULLMAC
#define FW_STR  "fmac"
#endif
#if 0
#ifdef CONFIG_ECRNX_DBG
/*  #define ECRNX_DBG(format, arg...) pr_warn(format, ## arg) */
#define ECRNX_DBG printk
#else
#define ECRNX_DBG(a...) do {} while (0)
#endif
#endif

#ifdef CONFIG_ECRNX_DBG

#define ECRNX_FN_ENTRY_STR "%s() enter, line:%d\n", __func__, __LINE__
#define DBG_PREFIX "[ecrnx] "
#define DBG_PREFIX_IW_CFM "[ecrnx] iwpriv cfm:"
#define DBG_PREFIX_PAT "[ecrnx] pattern error:"
#define DBG_PREFIX_CRC_CHECK "[ecrnx] crc check:"
#define DBG_PREFIX_SDIO_RX "[ecrnx] sdio rx:"
#define DBG_PREFIX_SDIO_TX "[ecrnx] sdio tx:"

/* driver log level*/
enum ECRNX_DRV_DBG_TYEP{
    DRV_DBG_TYPE_NONE,
    DRV_DBG_TYPE_ALWAYS,
    DRV_DBG_TYPE_ERR,
    DRV_DBG_TYPE_WARNING,
    DRV_DBG_TYPE_INFO,
    DRV_DBG_TYPE_DEBUG,
    DRV_DBG_TYPE_MAX,
};

#define ECRNX_PRINT(fmt, arg...)     \
    do {\
        if (DRV_DBG_TYPE_ALWAYS <= ecrnx_dbg_level) {\
            printk(DBG_PREFIX fmt, ##arg);\
        } \
    } while (0)

#define ECRNX_ERR(fmt, arg...)     \
    do {\
        if (DRV_DBG_TYPE_ERR <= ecrnx_dbg_level) {\
            printk(DBG_PREFIX " ERROR " fmt, ##arg);\
        } \
    } while (0)

#define ECRNX_WARN(fmt, arg...)     \
    do {\
        if (DRV_DBG_TYPE_WARNING <= ecrnx_dbg_level) {\
            printk(DBG_PREFIX " WARN " fmt, ##arg);\
        } \
    } while (0)

#define ECRNX_INFO(fmt, arg...)     \
    do {\
        if (DRV_DBG_TYPE_INFO <= ecrnx_dbg_level) {\
            printk(DBG_PREFIX fmt, ##arg);\
        } \
    } while (0)

#define ECRNX_DBG(fmt, arg...)     \
    do {\
        if (DRV_DBG_TYPE_DEBUG <= ecrnx_dbg_level) {\
            printk(DBG_PREFIX fmt, ##arg);\
        } \
    } while (0)

#else
#define ECRNX_PRINT(...)
#define ECRNX_ERR(...)
#define ECRNX_WARN(...)
#define ECRNX_INFO(...)
#define ECRNX_DBG(...)
#endif

typedef struct {
    u32 level;
    u32 dir;
} LOG_CTL_ST;

extern int ecrnx_dbg_level;
extern LOG_CTL_ST log_ctl;

#ifndef CONFIG_ECRNX_DEBUGFS_CUSTOM
int ecrnx_fw_log_level_set(u32 level, u32 dir);
#endif

#endif /* ECRNX_DEBUG_H_ */
