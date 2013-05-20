#ifndef _STP_DEBUG_H_
#define _STP_DEBUG_H_

#include <linux/time.h>
#include "osal.h"

#define CONFIG_LOG_STP_INTERNAL

#if 1//#ifndef CONFIG_LOG_STP_INTERNAL
#define STP_PKT_SZ  16
#define STP_DMP_SZ 2048
#define STP_PKT_NO 2048

#define STP_DBG_LOG_ENTRY_NUM 2048
#define STP_DBG_LOG_ENTRY_SZ  2048

#else

#define STP_PKT_SZ  16
#define STP_DMP_SZ 16
#define STP_PKT_NO 16

#define STP_DBG_LOG_ENTRY_NUM 28
#define STP_DBG_LOG_ENTRY_SZ 64


#endif


typedef enum {
    STP_DBG_EN         = 0,
    STP_DBG_PKT        = 1,
    STP_DBG_DR         = 2,
    STP_DBG_FW_ASSERT  = 3,
    STP_DBG_FW_LOG = 4,
    STP_DBG_FW_DMP = 5, 
    STP_DBG_MAX
}STP_DBG_OP_T;

typedef enum {
    STP_DBG_PKT_FIL_ALL = 0,
    STP_DBG_PKT_FIL_BT  = 1,
    STP_DBG_PKT_FIL_GPS = 2,
    STP_DBG_PKT_FIL_FM  = 3,
    STP_DBG_PKT_FIL_WMT = 4,
    STP_DBG_PKT_FIL_MAX
} STP_DBG_PKT_FIL_T;

static  char * const gStpDbgType[]={
    "< BT>",
    "< FM>",
    "<GPS>",
    "<WiFi>",
    "<WMT>",
    "<STP>",
    "<DBG>",
    "<UNKOWN>"
};


typedef enum {
    STP_DBG_DR_MAX = 0,
} STP_DBG_DR_FIL_T;

typedef enum {
    STP_DBG_FW_MAX = 0,
} STP_DBG_FW_FIL_T;

typedef enum {
    PKT_DIR_RX = 0,
    PKT_DIR_TX
} STP_DBG_PKT_DIR_T;

/*simple log system ++*/

typedef struct {
    int  id; /*type: 0. pkt trace 1. fw info 2. assert info 3. trace32 dump . -1. linked to the the previous*/
    int  len;
    char buffer[STP_DBG_LOG_ENTRY_SZ];
} MTKSTP_LOG_ENTRY_T;

typedef struct log_sys {
    MTKSTP_LOG_ENTRY_T  queue[STP_DBG_LOG_ENTRY_NUM];
    unsigned int size;
    unsigned int in;
    unsigned int out;
    spinlock_t   lock;
} MTKSTP_LOG_SYS_T;
/*--*/

typedef struct stp_dbg_pkt_hdr{
    //packet information
    unsigned int   sec;
    unsigned int   usec;
    unsigned int   dbg_type;
    unsigned int   dmy;
    unsigned int   no;
    unsigned int   dir;

    //packet content
    unsigned int  type;
    unsigned int  len;
    unsigned int  ack;
    unsigned int  seq;
    unsigned int  chs;
    unsigned int  crc;
}STP_DBG_HDR_T;

typedef struct stp_dbg_pkt{
    struct stp_dbg_pkt_hdr hdr;
    unsigned char raw[STP_DMP_SZ];
}STP_PACKET_T;

typedef struct mtkstp_dbg_t{
    /*log_sys*/
    int pkt_trace_no;
    void *btm;
    int is_enable;
    MTKSTP_LOG_SYS_T *logsys;
}MTKSTP_DBG_T;

extern void aed_combo_exception(const int *, int, const int *, int, const char *);

#define STP_CORE_DUMP_TIMEOUT 5*60*1000 // default 5minutes
#define STP_OJB_NAME_SZ 20
#define STP_CORE_DUMP_INFO_SZ 500
typedef enum wcn_compress_algorithm_t {
    GZIP = 0,
    BZIP2 = 1,
    RAR = 2,
    LMA = 3,
    MAX
}WCN_COMPRESS_ALG_T;

typedef INT32 (*COMPRESS_HANDLER)(void *worker, UINT8 *in_buf, INT32 in_sz, UINT8 *out_buf, INT32 *out_sz, INT32 finish);
typedef struct wcn_compressor_t {
    // current object name
    UINT8 name[STP_OJB_NAME_SZ + 1];
    
    // buffer for raw data, named L1
    PUINT8 L1_buf;
    INT32 L1_buf_sz;
    INT32 L1_pos;
    
    // target buffer, named L2
    PUINT8 L2_buf;
    INT32 L2_buf_sz;
    INT32 L2_pos;

    // compress state
    UINT8 f_done;
    UINT16 reserved;
    UINT32 uncomp_size;
    UINT32 crc32;
    
    // compress algorithm
    UINT8 f_compress_en;
    WCN_COMPRESS_ALG_T compress_type;
    void *worker;
    COMPRESS_HANDLER handler;    
}WCN_COMPRESSOR_T, *P_WCN_COMPRESSOR_T;

P_WCN_COMPRESSOR_T wcn_compressor_init(PUINT8 name, INT32 L1_buf_sz, INT32 L2_buf_sz);
INT32 wcn_compressor_deinit(P_WCN_COMPRESSOR_T compressor);
INT32 wcn_compressor_in(P_WCN_COMPRESSOR_T compressor, PUINT8 buf, INT32 len, INT32 finish);
INT32 wcn_compressor_out(P_WCN_COMPRESSOR_T compressor, PUINT8 *pbuf, PINT32 len);
INT32 wcn_compressor_reset(P_WCN_COMPRESSOR_T compressor, UINT8 enable, WCN_COMPRESS_ALG_T type);

typedef enum core_dump_state_t {
    CORE_DUMP_INIT = 0,
    CORE_DUMP_DOING,
    CORE_DUMP_TIMEOUT,
    CORE_DUMP_DONE,
    CORE_DUMP_MAX
}CORE_DUMP_STA;

typedef struct core_dump_t {
    // compress dump data and buffered
    P_WCN_COMPRESSOR_T compressor;

    // timer for monitor timeout
    OSAL_TIMER dmp_timer;
    UINT32 timeout;

    OSAL_SLEEPABLE_LOCK dmp_lock;
    
    // state machine for core dump flow
    CORE_DUMP_STA sm;

    // dump info
    CHAR info[STP_CORE_DUMP_INFO_SZ + 1];
} WCN_CORE_DUMP_T, *P_WCN_CORE_DUMP_T;

P_WCN_CORE_DUMP_T wcn_core_dump_init(UINT32 timeout);
INT32 wcn_core_dump_deinit(P_WCN_CORE_DUMP_T dmp);
INT32 wcn_core_dump_in(P_WCN_CORE_DUMP_T dmp, PUINT8 buf, INT32 len);
INT32 wcn_core_dump_out(P_WCN_CORE_DUMP_T dmp, PUINT8 *pbuf, PINT32 len);
INT32 wcn_core_dump_reset(P_WCN_CORE_DUMP_T dmp, UINT32 timeout);
extern INT32 wcn_core_dump_flush(INT32 rst);

extern int stp_dbg_enable(MTKSTP_DBG_T *stp_dbg);
extern int stp_dbg_disable(MTKSTP_DBG_T *stp_dbg);
extern MTKSTP_DBG_T *stp_dbg_init(void *);
extern int stp_dbg_deinit(MTKSTP_DBG_T *stp_dbg);
extern int stp_dbg_dmp_out_ex (char *buf, int *len);
extern int stp_dbg_dmp_out(MTKSTP_DBG_T *stp_dbg, char *buf, int *len);
extern int stp_dbg_dmp_printk(MTKSTP_DBG_T *stp_dbg);
extern char
stp_dbg_nl_send(
    char *  aucMsg,
    unsigned char      cmd
    );

extern INT32 stp_dbg_aee_send(unsigned char *aucMsg, INT32 len, INT32 cmd);

extern int
stp_dbg_log_pkt (
    MTKSTP_DBG_T *stp_dbg,
    int dbg_type,
    int type,
    int ack_no,
    int seq_no,
    int crc,
    int dir,
    int len,
    const unsigned char *body);
extern int stp_dbg_log_ctrl (unsigned int on);
#endif /* end of _STP_DEBUG_H_ */

