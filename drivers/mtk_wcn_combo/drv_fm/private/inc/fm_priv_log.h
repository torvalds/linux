#ifndef __FM_PRIV_LOG_H__
#define __FM_PRIV_LOG_H__
#if 0
#include "fm_typedef.h"

/******************DBG level ************************/
#define D_BASE  4
#define D_IOCTL (1 << (D_BASE+0))
#define D_RX    (1 << (D_BASE+1))
#define D_TIMER (1 << (D_BASE+2))
#define D_BLKC	(1 << (D_BASE+3))
#define D_G0	(1 << (D_BASE+4))
#define D_G1	(1 << (D_BASE+5))
#define D_G2	(1 << (D_BASE+6))
#define D_G3	(1 << (D_BASE+7))
#define D_G4	(1 << (D_BASE+8))
#define D_G14	(1 << (D_BASE+9))
#define D_RAW	(1 << (D_BASE+10))
#define D_RDS	(1 << (D_BASE+11))
#define D_INIT	(1 << (D_BASE+12))
#define D_MAIN	(1 << (D_BASE+13))
#define D_CMD	(1 << (D_BASE+14))
#define D_ALL	0xfffffff0

#define L0 0x00000000 // EMERG, system will crush 
#define L1 0x00000001 // ALERT, need action in time
#define L2 0x00000002 // CRIT, important HW or SW operation failed 
#define L3 0x00000003 // ERR, normal HW or SW ERR 
#define L4 0x00000004 // WARNING, importan path or somewhere may occurs err
#define L5 0x00000005 // NOTICE, normal case 
#define L6 0x00000006 // INFO, print info if need 
#define L7 0x00000007 // DEBUG, for debug info

#define FM_EMERG    L0
#define FM_ALERT    L1
#define FM_CRIT     L2
#define FM_ERR      L3
#define FM_WARNING  L4
#define FM_NOTICE   L5
#define FM_INFO     L6
#define FM_DEBUG    L7

extern fm_u32 g_dbg_level;

#define FM_LOG_DBG(flag, fmt, args...) \
            do{ \
                if(pub_cb->log && (FM_DEBUG <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                    pub_cb->log("[" #flag "]" fmt, ## args); \
                } \
            } while(0)
        
#define FM_LOG_INF(flag, fmt, args...) \
            do{ \
                if(pub_cb->log && (FM_INFO <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                     pub_cb->log("[" #flag "]" fmt, ## args); \
                } \
            } while(0)
        
#define FM_LOG_NTC(flag, fmt, args...) \
            do{ \
                if(pub_cb->log && (FM_NOTICE <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                     pub_cb->log("[" #flag "]" fmt, ## args); \
                } \
            } while(0)
        
#define FM_LOG_WAR(flag, fmt, args...) \
            do{ \
                if(pub_cb->log && (FM_WARNING <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                     pub_cb->log("[" #flag "]" fmt, ## args); \
                } \
            } while(0)
        
#define FM_LOG_ERR(flag, fmt, args...) \
            do{ \
                if(pub_cb->log && (FM_ERR <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                     pub_cb->log("[" #flag "]" fmt, ## args); \
                } \
            } while(0)
        
#define FM_LOG_CRT(flag, fmt, args...) \
            do{ \
                if(pub_cb->log && (FM_CRIT <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                     pub_cb->log("[" #flag "]" fmt, ## args); \
                } \
            } while(0)
        
#define FM_LOG_ALT(flag, fmt, args...) \
            do{ \
                if(pub_cb->log && (FM_ALERT <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                     pub_cb->log("[" #flag "]" fmt, ## args); \
                } \
            } while(0)
        
#define FM_LOG_EMG(flag, fmt, args...) \
            do{ \
                if(pub_cb->log && (FM_EMERG <= (g_dbg_level&0x0000000f)) && ((flag)&0xfffffff0)& g_dbg_level) { \
                     pub_cb->log("[" #flag "]" fmt, ## args); \
                } \
            } while(0)
#endif
#endif //__FM_PRIV_LOG_H__