#ifndef __FM_RDS_H__
#define __FM_RDS_H__
#include "fm_typedef.h"

//FM_RDS_DATA_CRC_FFOST
#define FM_RDS_GDBK_IND_A	 (0x08)
#define FM_RDS_GDBK_IND_B	 (0x04)
#define FM_RDS_GDBK_IND_C	 (0x02)
#define FM_RDS_GDBK_IND_D	 (0x01)
#define FM_RDS_DCO_FIFO_OFST (0x01E0)
#define	FM_RDS_READ_DELAY	 (0x80)

#define RDS_RX_BLOCK_PER_GROUP (4)
#define RDS_RX_GROUP_SIZE (2*RDS_RX_BLOCK_PER_GROUP)
#define MAX_RDS_RX_GROUP_CNT (12)
#define RDS_RT_MULTI_REV_TH 100

typedef struct rds_packet_t {
    fm_u16 blkA;
    fm_u16 blkB;
    fm_u16 blkC;
    fm_u16 blkD;
    fm_u16 cbc; //correct bit cnt
    fm_u16 crc; //crc checksum
} rds_packet_t;

typedef struct rds_rx_t {
    fm_u16 sin;
    fm_u16 cos;
    rds_packet_t data[MAX_RDS_RX_GROUP_CNT];
} rds_rx_t;

typedef enum rds_ps_state_machine_t {
    RDS_PS_START = 0,
    RDS_PS_DECISION,
    RDS_PS_GETLEN,
    RDS_PS_DISPLAY,
    RDS_PS_FINISH,
    RDS_PS_MAX
} rds_ps_state_machine_t;

typedef enum rds_rt_state_machine_t {
    RDS_RT_START = 0,
    RDS_RT_DECISION,
    RDS_RT_GETLEN,
    RDS_RT_DISPLAY,
    RDS_RT_FINISH,
    RDS_RT_MAX
} rds_rt_state_machine_t;


enum {
    RDS_GRP_VER_A = 0,  //group version A
    RDS_GRP_VER_B
};

typedef enum rds_blk_t {
    RDS_BLK_A = 0,
    RDS_BLK_B,
    RDS_BLK_C,
    RDS_BLK_D,
    RDS_BLK_MAX
} rds_blk_t;

//For RDS feature, these strcutures also be defined in "fm.h"
typedef struct rds_flag_t {
    fm_u8 TP;
    fm_u8 TA;
    fm_u8 Music;
    fm_u8 Stereo;
    fm_u8 Artificial_Head;
    fm_u8 Compressed;
    fm_u8 Dynamic_PTY;
    fm_u8 Text_AB;
    fm_u32 flag_status;
} rds_flag_t;

typedef struct rds_ct_t {
    fm_u16 Month;
    fm_u16 Day;
    fm_u16 Year;
    fm_u16 Hour;
    fm_u16 Minute;
    fm_u8 Local_Time_offset_signbit;
    fm_u8 Local_Time_offset_half_hour;
} rds_ct_t;

typedef struct rds_af_t {
    fm_s16 AF_Num;
    fm_s16 AF[2][25];  //100KHz
    fm_u8 Addr_Cnt;
    fm_u8 isMethod_A;
    fm_u8 isAFNum_Get;
} rds_af_t;

typedef struct rds_ps_t {
    fm_u8 PS[4][8];
    fm_u8 Addr_Cnt;
} rds_ps_t;

typedef struct rds_rt_t {
    fm_u8 TextData[4][64];
    fm_u8 GetLength;
    fm_u8 isRTDisplay;
    fm_u8 TextLength;
    fm_u8 isTypeA;
    fm_u8 BufCnt;
    fm_u16 Addr_Cnt;
} rds_rt_t;

typedef struct rds_raw_t {
    fm_s32 dirty; //indicate if the data changed or not
    fm_s32 len; //the data len form chip
    fm_u8 data[146];
} rds_raw_t;

typedef struct rds_group_cnt_t {
    unsigned long total;
    unsigned long groupA[16]; //RDS groupA counter
    unsigned long groupB[16]; //RDS groupB counter
} rds_group_cnt_t;

typedef enum rds_group_cnt_op_t {
    RDS_GROUP_CNT_READ = 0,
    RDS_GROUP_CNT_WRITE,
    RDS_GROUP_CNT_RESET,
    RDS_GROUP_CNT_MAX
} rds_group_cnt_op_t;

typedef struct rds_group_cnt_req_t {
    fm_s32 err;
    enum rds_group_cnt_op_t op;
    struct rds_group_cnt_t gc;
} rds_group_cnt_req_t;

typedef struct rds_t {
    struct rds_ct_t CT;
    struct rds_flag_t RDSFlag;
    fm_u16 PI;
    fm_u8 Switch_TP;
    fm_u8 PTY;
    struct rds_af_t AF_Data;
    struct rds_af_t AFON_Data;
    fm_u8 Radio_Page_Code;
    fm_u16 Program_Item_Number_Code;
    fm_u8 Extend_Country_Code;
    fm_u16 Language_Code;
    struct rds_ps_t PS_Data;
    fm_u8 PS_ON[8];
    struct rds_rt_t RT_Data;
    fm_u16 event_status; //will use RDSFlag_Struct RDSFlag->flag_status to check which event, is that ok?
    struct rds_group_cnt_t gc;
} rds_t;


//Need care the following definition.
//valid Rds Flag for notify
typedef enum rds_flag_status_t {
    RDS_FLAG_IS_TP              = 0x0001, // Program is a traffic program
    RDS_FLAG_IS_TA              = 0x0002, // Program currently broadcasts a traffic ann.
    RDS_FLAG_IS_MUSIC           = 0x0004, // Program currently broadcasts music
    RDS_FLAG_IS_STEREO          = 0x0008, // Program is transmitted in stereo
    RDS_FLAG_IS_ARTIFICIAL_HEAD = 0x0010, // Program is an artificial head recording
    RDS_FLAG_IS_COMPRESSED      = 0x0020, // Program content is compressed
    RDS_FLAG_IS_DYNAMIC_PTY     = 0x0040, // Program type can change
    RDS_FLAG_TEXT_AB            = 0x0080  // If this flag changes state, a new radio text 					 string begins
} rds_flag_status_t;

typedef enum rds_event_status_t {
    RDS_EVENT_FLAGS          = 0x0001, // One of the RDS flags has changed state
    RDS_EVENT_PI_CODE        = 0x0002, // The program identification code has changed
    RDS_EVENT_PTY_CODE       = 0x0004, // The program type code has changed
    RDS_EVENT_PROGRAMNAME    = 0x0008, // The program name has changed
    RDS_EVENT_UTCDATETIME    = 0x0010, // A new UTC date/time is available
    RDS_EVENT_LOCDATETIME    = 0x0020, // A new local date/time is available
    RDS_EVENT_LAST_RADIOTEXT = 0x0040, // A radio text string was completed
    RDS_EVENT_AF             = 0x0080, // Current Channel RF signal strength too weak, need do AF switch
    RDS_EVENT_AF_LIST        = 0x0100, // An alternative frequency list is ready
    RDS_EVENT_AFON_LIST      = 0x0200, // An alternative frequency list is ready
    RDS_EVENT_TAON           = 0x0400,  // Other Network traffic announcement start
    RDS_EVENT_TAON_OFF       = 0x0800, // Other Network traffic announcement finished.
    RDS_EVENT_RDS            = 0x2000, // RDS Interrupt had arrived durint timer period
    RDS_EVENT_NO_RDS         = 0x4000, // RDS Interrupt not arrived durint timer period
    RDS_EVENT_RDS_TIMER      = 0x8000 // Timer for RDS Bler Check. ---- BLER  block error rate
} rds_event_status_t;


#define RDS_LOG_SIZE 2
struct rds_log_t {
    struct rds_rx_t rds_log[RDS_LOG_SIZE];
    fm_s32 log_len[RDS_LOG_SIZE];
    fm_u32 size;
    fm_u32 in;
    fm_u32 out;
    fm_u32 len;
    fm_s32(*log_in)(struct rds_log_t *thiz, struct rds_rx_t *new_log, fm_s32 new_len);
    fm_s32(*log_out)(struct rds_log_t *thiz, struct rds_rx_t *dst, fm_s32 *dst_len);
};

extern fm_s32 rds_parser(rds_t *rds_dst, struct rds_rx_t *rds_raw, fm_s32 rds_size, fm_u16(*getfreq)(void));
extern fm_s32 rds_grp_counter_get(struct rds_group_cnt_t *dst, struct rds_group_cnt_t *src);
extern fm_s32 rds_grp_counter_reset(struct rds_group_cnt_t *gc);
extern fm_s32 rds_log_in(struct rds_log_t *thiz, struct rds_rx_t *new_log, fm_s32 new_len);
extern fm_s32 rds_log_out(struct rds_log_t *thiz, struct rds_rx_t *dst, fm_s32 *dst_len);


#define DEFINE_RDSLOG(name) \
    struct rds_log_t name = { \
        .size = RDS_LOG_SIZE, \
        .in = 0, \
        .out = 0, \
        .len = 0, \
        .log_in = rds_log_in, \
        .log_out = rds_log_out, \
    }


#endif //__FM_RDS_H__

