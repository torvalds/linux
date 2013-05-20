#ifndef __MT6626_FM_CMD_H__
#define __MT6626_FM_CMD_H__

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

fm_s32 mt6626_pwrup_clock_on(fm_u8 *tx_buf, fm_s32 tx_buf_size);
fm_s32 mt6626_pwrup_digital_init_1(fm_u8 *tx_buf, fm_s32 tx_buf_size);
fm_s32 mt6626_pwrup_digital_init_2(fm_u8 *tx_buf, fm_s32 tx_buf_size);
fm_s32 mt6626_pwrdown(fm_u8 *tx_buf, fm_s32 tx_buf_size);
fm_s32 mt6626_rampdown(fm_u8 *tx_buf, fm_s32 tx_buf_size);
fm_s32 mt6626_tune(fm_u8 *tx_buf, fm_s32 tx_buf_size, fm_u16 freq);
fm_s32 mt6626_seek(fm_u8 *tx_buf, fm_s32 tx_buf_size, fm_u16 seekdir, fm_u16 space, fm_u16 max_freq, fm_u16 min_freq);
fm_s32 mt6626_scan(fm_u8 *tx_buf, fm_s32 tx_buf_size, fm_u16 scandir, fm_u16 space, fm_u16 max_freq, fm_u16 min_freq);
fm_s32 mt6626_get_reg(fm_u8 *tx_buf, fm_s32 tx_buf_size, fm_u8 addr);
fm_s32 mt6626_set_reg(fm_u8 *tx_buf, fm_s32 tx_buf_size, fm_u8 addr, fm_u16 value);
fm_s32 mt6626_patch_download(fm_u8 *tx_buf, fm_s32 tx_buf_size, fm_u8 seg_num, fm_u8 seg_id, const fm_u8 *src, fm_s32 seg_len);
fm_s32 mt6626_coeff_download(fm_u8 *tx_buf, fm_s32 tx_buf_size, fm_u8 seg_num, fm_u8 seg_id, const fm_u8 *src, fm_s32 seg_len);
fm_s32 mt6626_hwcoeff_download(fm_u8 *tx_buf, fm_s32 tx_buf_size, fm_u8 seg_num, fm_u8 seg_id, const fm_u8 *src, fm_s32 seg_len);
fm_s32 mt6626_rom_download(fm_u8 *tx_buf, fm_s32 tx_buf_size, fm_u8 seg_num, fm_u8 seg_id, const fm_u8 *src, fm_s32 seg_len);

#endif
