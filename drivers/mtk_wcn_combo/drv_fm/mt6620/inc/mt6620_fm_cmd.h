#ifndef __MT6620_FM_CMD_H__
#define __MT6620_FM_CMD_H__

#include <linux/types.h>
#include "fm_typedef.h"

/* FM basic-operation's opcode */
#define FM_BOP_BASE (0x80)
enum {
    FM_WRITE_BASIC_OP       = (FM_BOP_BASE + 0x00),
    FM_UDELAY_BASIC_OP      = (FM_BOP_BASE + 0x01),
    FM_RD_UNTIL_BASIC_OP    = (FM_BOP_BASE + 0x02),
    FM_MODIFY_BASIC_OP      = (FM_BOP_BASE + 0x03),
    FM_MSLEEP_BASIC_OP      = (FM_BOP_BASE + 0x04),
    FM_MAX_BASIC_OP         = (FM_BOP_BASE + 0x05)
};

/* FM BOP's size */
#define FM_WRITE_BASIC_OP_SIZE      (3)
#define FM_UDELAY_BASIC_OP_SIZE     (4)
#define FM_RD_UNTIL_BASIC_OP_SIZE   (5)
#define FM_MODIFY_BASIC_OP_SIZE     (5)
#define FM_MSLEEP_BASIC_OP_SIZE     (4)

fm_s32 mt6620_off_2_longANA_1(fm_u8 *buf, fm_s32 buf_size);
fm_s32 mt6620_off_2_longANA_2(fm_u8 *buf, fm_s32 buf_size);

fm_s32 mt6620_pwrup_digital_init_1(fm_u8 *buf, fm_s32 buf_size);
fm_s32 mt6620_pwrup_digital_init_2(fm_u8 *buf, fm_s32 buf_size);
fm_s32 mt6620_pwrup_digital_init_3(fm_u8 *buf, fm_s32 buf_size);

fm_s32 mt6620_pwrdown(fm_u8 *buf, fm_s32 buf_size);
fm_s32 mt6620_rampdown(fm_u8 *buf, fm_s32 buf_size);

fm_s32 mt6620_tune_1(fm_u8 *buf, fm_s32 buf_size, fm_u16 freq);
fm_s32 mt6620_tune_2(fm_u8 *buf, fm_s32 buf_size, fm_u16 freq);
fm_s32 mt6620_tune_3(fm_u8 *buf, fm_s32 buf_size, fm_u16 freq);
fm_s32 mt6620_fast_tune(fm_u8 *tx_buf, fm_s32 tx_buf_size, fm_u16 freq);
fm_s32 mt6620_full_cqi_req(fm_u8 *buf, fm_s32 buf_size, fm_u16 freq, fm_s32 cnt, fm_s32 type);

fm_s32 mt6620_seek_1(fm_u8 *buf, fm_s32 buf_size, fm_u16 seekdir, fm_u16 space, fm_u16 max_freq, fm_u16 min_freq);
fm_s32 mt6620_seek_2(fm_u8 *buf, fm_s32 buf_size, fm_u16 seekdir, fm_u16 space, fm_u16 max_freq, fm_u16 min_freq);

fm_s32 mt6620_scan_1(fm_u8 *buf, fm_s32 buf_size, fm_u16 scandir, fm_u16 space, fm_u16 max_freq, fm_u16 min_freq);
fm_s32 mt6620_scan_2(fm_u8 *buf, fm_s32 buf_size, fm_u16 scandir, fm_u16 space, fm_u16 max_freq, fm_u16 min_freq);

fm_s32 mt6620_get_reg(fm_u8 *buf, fm_s32 buf_size, fm_u8 addr);
fm_s32 mt6620_set_reg(fm_u8 *buf, fm_s32 buf_size, fm_u8 addr, fm_u16 value);
fm_s32 mt6620_rampdown_tx(unsigned char *tx_buf, int tx_buf_size);
fm_s32 mt6620_tune_txscan(unsigned char *tx_buf, int tx_buf_size, uint16_t freq);
fm_s32 mt6620_tune_tx(unsigned char *tx_buf, int tx_buf_size, uint16_t freq);
fm_s32 mt6620_rds_rx_enable(unsigned char *tx_buf, int tx_buf_size);
fm_s32 mt6620_rds_rx_disable(unsigned char *tx_buf, int tx_buf_size);
fm_s32 mt6620_rds_tx(unsigned char *tx_buf, int tx_buf_size, uint16_t pi, uint16_t *ps, uint16_t *other_rds, uint8_t other_rds_cnt);
fm_s32 mt6620_off_2_tx_shortANA(fm_u8 *tx_buf, fm_s32 tx_buf_size);
fm_s32 mt6620_dig_init(fm_u8 *tx_buf, fm_s32 tx_buf_size);

extern fm_s32 fm_get_channel_space(int freq);

#endif
