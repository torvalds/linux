#ifndef __FM_LINK_H__
#define __FM_LINK_H__

#include "fm_typedef.h"
#include "fm_rds.h"

typedef enum {
    FM_TASK_RX_PARSER_PKT_TYPE = 0,
    FM_TASK_RX_PARSER_OPCODE,
    FM_TASK_RX_PARSER_PKT_LEN_1,
    FM_TASK_RX_PARSER_PKT_LEN_2,
    FM_TASK_RX_PARSER_PKT_PAYLOAD,
    FM_TASK_RX_PARSER_BUFFER_CONGESTION
} fm_task_parser_state;

enum {
    FM_TASK_COMMAND_PKT_TYPE = 0x01,
    FM_TASK_EVENT_PKT_TYPE = 0x04
};

enum {
    FM_STP_TEST_OPCODE      = 0x00,
    FSPI_ENABLE_OPCODE      = 0x01,
    FSPI_MUX_SEL_OPCODE     = 0x02,
    FSPI_READ_OPCODE        = 0x03,
    FSPI_WRITE_OPCODE       = 0x04,
    FI2C_READ_OPCODE        = 0x05,
    FI2C_WRITE_OPCODE       = 0x06,
    FM_ENABLE_OPCODE        = 0x07,
    FM_RESET_OPCODE         = 0x08,
    FM_TUNE_OPCODE          = 0x09,
    FM_SEEK_OPCODE          = 0x0a,
    FM_SCAN_OPCODE          = 0x0b,
    RDS_RX_ENABLE_OPCODE    = 0x0c,
    RDS_RX_DATA_OPCODE      = 0x0d,
    FM_RAMPDOWN_OPCODE      = 0x0e,
    FM_MCUCLK_SEL_OPCODE    = 0x0f,
    FM_MODEMCLK_SEL_OPCODE  = 0x10,
    RDS_TX_OPCODE           = 0x11,
    FM_PATCH_DOWNLOAD_OPCODE = 0x12,
    FM_COEFF_DOWNLOAD_OPCODE = 0x13,
    FM_HWCOEFF_DOWNLOAD_OPCODE = 0x14,
    FM_ROM_DOWNLOAD_OPCODE = 0x15,
    FM_SOFT_MUTE_TUNE_OPCODE = 0x17,
};

enum {
    FLAG_TEST = (1 << FM_STP_TEST_OPCODE),
    FLAG_FSPI_EN = (1 << FSPI_ENABLE_OPCODE),
    FLAG_FSPI_MUXSEL = (1 << FSPI_MUX_SEL_OPCODE),
    FLAG_FSPI_RD = (1 << FSPI_READ_OPCODE),
    FLAG_FSPI_WR = (1 << FSPI_WRITE_OPCODE),
    FLAG_I2C_RD = (1 << FI2C_READ_OPCODE),
    FLAG_I2C_WR = (1 << FI2C_WRITE_OPCODE),
    FLAG_EN = (1 << FM_ENABLE_OPCODE),
    FLAG_RST = (1 << FM_RESET_OPCODE),
    FLAG_TUNE = (1 << FM_TUNE_OPCODE),
    FLAG_SEEK = (1 << FM_SEEK_OPCODE),
    FLAG_SCAN = (1 << FM_SCAN_OPCODE),
    FLAG_RDS_RX_EN = (1 << RDS_RX_ENABLE_OPCODE),
    FLAG_RDS_DATA = (1 << RDS_RX_DATA_OPCODE),
    FLAG_RAMPDOWN = (1 << FM_RAMPDOWN_OPCODE),
    FLAG_MCUCLK = (1 << FM_MCUCLK_SEL_OPCODE),
    FLAG_MODEMCLK = (1 << FM_MODEMCLK_SEL_OPCODE),
    FLAG_RDS_TX = (1 << RDS_TX_OPCODE),
    FLAG_PATCH = (1 << FM_PATCH_DOWNLOAD_OPCODE),
    FLAG_COEFF = (1 << FM_COEFF_DOWNLOAD_OPCODE),
    FLAG_HWCOEFF = (1 << FM_HWCOEFF_DOWNLOAD_OPCODE),
    FLAG_ROM = (1 << FM_ROM_DOWNLOAD_OPCODE),
    FLAG_SM_TUNE = (1 << FM_SOFT_MUTE_TUNE_OPCODE), // 23
    FLAG_CQI_DONE = (1 << 27),
    FLAG_TUNE_DONE = (1 << 28),
    FLAG_SEEK_DONE = (1 << 29),
    FLAG_SCAN_DONE = (1 << 30),
    FLAG_TERMINATE = (1 << 31)
};

#define FM_SCANTBL_SIZE  16
#define FM_CQI_BUF_SIZE  96
struct fm_res_ctx {
    fm_u16 fspi_rd;
    fm_u16 seek_result;
    fm_u16 scan_result[FM_SCANTBL_SIZE];
    fm_s8 cqi[FM_CQI_BUF_SIZE];
    struct rds_rx_t rds_rx_result;
};

#define FM_TRACE_ENABLE

#define FM_TRACE_FIFO_SIZE 200
#define FM_TRACE_PKT_SIZE 60
struct fm_trace_t {
    fm_s32 type;
    fm_s32 opcode;
    fm_s32 len;
    fm_u8 pkt[FM_TRACE_PKT_SIZE]; // full packet
    unsigned long time;
    fm_s32 tid;
};

struct fm_trace_fifo_t {
    fm_s8 name[20+1];
    struct fm_trace_t trace[FM_TRACE_FIFO_SIZE];
    fm_u32 size;
    fm_u32 in;
    fm_u32 out;
    fm_u32 len;
    fm_s32 (*trace_in)(struct fm_trace_fifo_t *thiz, struct fm_trace_t *new_tra);
    fm_s32 (*trace_out)(struct fm_trace_fifo_t *thiz, struct fm_trace_t *dst_tra);
    fm_bool (*is_full)(struct fm_trace_fifo_t *thiz);
    fm_bool (*is_empty)(struct fm_trace_fifo_t *thiz);
};

#define FM_TRACE_IN(fifop, tracep)  \
({                                    \
    fm_s32 __ret = (fm_s32)0;              \
    if(fifop && (fifop)->trace_in){          \
        __ret = (fifop)->trace_in(fifop, tracep);   \
    }                               \
    __ret;                          \
})

#define FM_TRACE_OUT(fifop, tracep)  \
({                                    \
    fm_s32 __ret = (fm_s32)0;              \
    if(fifop && (fifop)->trace_out){          \
        __ret = (fifop)->trace_out(fifop, tracep);   \
    }                               \
    __ret;                          \
})

#define FM_TRACE_FULL(fifop)  \
({                                    \
    fm_bool __ret = (fm_bool)fm_false;      \
    if(fifop && (fifop)->is_full){          \
        __ret = (fifop)->is_full(fifop);   \
    }                               \
    __ret;                          \
})

#define FM_TRACE_EMPTY(fifop)  \
({                                    \
    fm_bool __ret = (fm_bool)fm_false;      \
    if(fifop && (fifop)->is_empty){          \
        __ret = (fifop)->is_empty(fifop);   \
    }                               \
    __ret;                          \
})

#if (defined(MT6620_FM)||defined(MT6628_FM))
#include "fm_utils.h"

#define RX_BUF_SIZE 128
#define TX_BUF_SIZE 1024

#define SW_RETRY_CNT            (1)
#define SW_RETRY_CNT_MAX        (5)
#define SW_WAIT_TIMEOUT_MAX     (100)
// FM operation timeout define for error handle
#define TEST_TIMEOUT            (3)
#define FSPI_EN_TIMEOUT         (3)
#define FSPI_MUXSEL_TIMEOUT     (3)
#define FSPI_RD_TIMEOUT         (3)
#define FSPI_WR_TIMEOUT         (3)
#define I2C_RD_TIMEOUT          (3)
#define I2C_WR_TIMEOUT          (3)
#define EN_TIMEOUT              (3)
#define RST_TIMEOUT             (3)
#define TUNE_TIMEOUT            (3)
#define SM_TUNE_TIMEOUT         (6)
#define SEEK_TIMEOUT            (15)
#define SCAN_TIMEOUT            (15) //usualy scan will cost 10 seconds 
#define RDS_RX_EN_TIMEOUT       (3)
#define RDS_DATA_TIMEOUT        (100)
#define RAMPDOWN_TIMEOUT        (3)
#define MCUCLK_TIMEOUT          (3)
#define MODEMCLK_TIMEOUT        (3)
#define RDS_TX_TIMEOUT          (3)
#define PATCH_TIMEOUT           (3)
#define COEFF_TIMEOUT           (3)
#define HWCOEFF_TIMEOUT         (3)
#define ROM_TIMEOUT             (3)

struct fm_link_event {
	struct fm_flag_event *ln_event;
	struct fm_res_ctx result; // seek/scan/read/RDS
};
#endif

/*
 * FM data and ctrl link APIs: platform related and bus related
 */
extern fm_s32 fm_link_setup(void* data);

extern fm_s32 fm_link_release(void);

extern fm_s32 fm_cmd_tx(fm_u8* buf, fm_u16 len, fm_s32 mask, fm_s32 cnt, fm_s32 timeout, fm_s32(*callback)(struct fm_res_ctx* result));

extern fm_s32 fm_event_parser(fm_s32(*rds_parser)(struct rds_rx_t*, fm_s32));

extern fm_s32 fm_ctrl_rx(fm_u8 addr, fm_u16 *val);

extern fm_s32 fm_ctrl_tx(fm_u8 addr, fm_u16 val);

extern fm_s32 fm_force_active_event(fm_u32 mask);

extern fm_bool fm_wait_stc_done(fm_u32 sec);

extern struct fm_trace_fifo_t* fm_trace_fifo_create(const fm_s8 *name);

extern fm_s32 fm_trace_fifo_release(struct fm_trace_fifo_t *fifo);

extern fm_s32 fm_print_cmd_fifo(void);

extern fm_s32 fm_print_evt_fifo(void);

#endif //__FM_LINK_H__

