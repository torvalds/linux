#ifndef __MT6628_FM_CMD_H__
#define __MT6628_FM_CMD_H__

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

fm_s32 mt6628_pwrup_fpga_on(fm_u8 *buf, fm_s32 buf_size);
fm_s32 mt6628_pwrup_clock_on(fm_u8 *buf, fm_s32 buf_size);
fm_s32 mt6628_pwrup_digital_init(fm_u8 *buf, fm_s32 buf_size);
fm_s32 mt6628_pwrdown(fm_u8 *buf, fm_s32 buf_size);
fm_s32 mt6628_rampdown(fm_u8 *buf, fm_s32 buf_size);
fm_s32 mt6628_tune(fm_u8 *buf, fm_s32 buf_size, fm_u16 freq, fm_u16 chan_para);
fm_s32 mt6628_seek(fm_u8 *buf, fm_s32 buf_size, fm_u16 seekdir, fm_u16 space, fm_u16 max_freq, fm_u16 min_freq);
fm_s32 mt6628_scan(fm_u8 *buf, fm_s32 buf_size, fm_u16 scandir, fm_u16 space, fm_u16 max_freq, fm_u16 min_freq);
fm_s32 mt6628_cqi_get(fm_u8 *buf, fm_s32 buf_size);
fm_s32 mt6628_get_reg(fm_u8 *buf, fm_s32 buf_size, fm_u8 addr);
fm_s32 mt6628_set_reg(fm_u8 *buf, fm_s32 buf_size, fm_u8 addr, fm_u16 value);
fm_s32 mt6628_patch_download(fm_u8 *buf, fm_s32 buf_size, fm_u8 seg_num, fm_u8 seg_id, const fm_u8 *src, fm_s32 seg_len);
fm_s32 mt6628_coeff_download(fm_u8 *buf, fm_s32 buf_size, fm_u8 seg_num, fm_u8 seg_id, const fm_u8 *src, fm_s32 seg_len);
fm_s32 mt6628_hwcoeff_download(fm_u8 *buf, fm_s32 buf_size, fm_u8 seg_num, fm_u8 seg_id, const fm_u8 *src, fm_s32 seg_len);
fm_s32 mt6628_rom_download(fm_u8 *buf, fm_s32 buf_size, fm_u8 seg_num, fm_u8 seg_id, const fm_u8 *src, fm_s32 seg_len);
fm_s32 mt6628_full_cqi_req(fm_u8 *buf, fm_s32 buf_size, fm_u16 *freq, fm_s32 cnt, fm_s32 type);

/*
 * fm_get_channel_space - get the spcace of gived channel
 * @freq - value in 760~1080 or 7600~10800
 *
 * Return 0, if 760~1080; return 1, if 7600 ~ 10800, else err code < 0
 */
extern fm_s32 fm_get_channel_space(int freq);

#endif
