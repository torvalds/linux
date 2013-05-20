#include <linux/kernel.h>

#include "fm_typedef.h"
#include "fm_dbg.h"
#include "fm_err.h"
#include "fm_rds.h"
#include "fm_cust_cfg.h"

#include "mt6626_fm_reg.h"
#include "mt6626_fm_link.h"
#include "mt6626_fm.h"
#include "mt6626_fm_cmd.h"

static fm_s32 fm_bop_write(fm_u8 addr, fm_u16 value, fm_u8 *buf, fm_s32 size)
{
    if (size < (FM_WRITE_BASIC_OP_SIZE + 2)) {
        return (-1);
    }

    if (buf == NULL) {
        return (-2);
    }

    buf[0] = FM_WRITE_BASIC_OP;
    buf[1] = FM_WRITE_BASIC_OP_SIZE;
    buf[2] = addr;
    buf[3] = (fm_u8)((value) & 0x00FF);
    buf[4] = (fm_u8)((value >> 8) & 0x00FF);

    WCN_DBG(FM_DBG | CHIP, "%02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4]);

    return (FM_WRITE_BASIC_OP_SIZE + 2);
}

static fm_s32 fm_bop_udelay(fm_u32 value, fm_u8 *buf, fm_s32 size)
{
    if (size < (FM_UDELAY_BASIC_OP_SIZE + 2)) {
        return (-1);
    }

    if (buf == NULL) {
        return (-2);
    }

    buf[0] = FM_UDELAY_BASIC_OP;
    buf[1] = FM_UDELAY_BASIC_OP_SIZE;
    buf[2] = (fm_u8)((value) & 0x000000FF);
    buf[3] = (fm_u8)((value >> 8) & 0x000000FF);
    buf[4] = (fm_u8)((value >> 16) & 0x000000FF);
    buf[5] = (fm_u8)((value >> 24) & 0x000000FF);

    WCN_DBG(FM_DBG | CHIP, "%02x %02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

    return (FM_UDELAY_BASIC_OP_SIZE + 2);
}

static fm_s32 fm_bop_rd_until(fm_u8 addr, fm_u16 mask, fm_u16 value, fm_u8 *buf, fm_s32 size)
{
    if (size < (FM_RD_UNTIL_BASIC_OP_SIZE + 2)) {
        return (-1);
    }

    if (buf == NULL) {
        return (-2);
    }

    buf[0] = FM_RD_UNTIL_BASIC_OP;
    buf[1] = FM_RD_UNTIL_BASIC_OP_SIZE;
    buf[2] = addr;
    buf[3] = (fm_u8)((mask) & 0x00FF);
    buf[4] = (fm_u8)((mask >> 8) & 0x00FF);
    buf[5] = (fm_u8)((value) & 0x00FF);
    buf[6] = (fm_u8)((value >> 8) & 0x00FF);

    WCN_DBG(FM_DBG | CHIP, "%02x %02x %02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

    return (FM_RD_UNTIL_BASIC_OP_SIZE + 2);
}

static fm_s32 fm_bop_modify(fm_u8 addr, fm_u16 mask_and, fm_u16 mask_or, fm_u8 *buf, fm_s32 size)
{
    if (size < (FM_MODIFY_BASIC_OP_SIZE + 2)) {
        return (-1);
    }

    if (buf == NULL) {
        return (-2);
    }

    buf[0] = FM_MODIFY_BASIC_OP;
    buf[1] = FM_MODIFY_BASIC_OP_SIZE;
    buf[2] = addr;
    buf[3] = (fm_u8)((mask_and) & 0x00FF);
    buf[4] = (fm_u8)((mask_and >> 8) & 0x00FF);
    buf[5] = (fm_u8)((mask_or) & 0x00FF);
    buf[6] = (fm_u8)((mask_or >> 8) & 0x00FF);

    WCN_DBG(FM_DBG | CHIP, "%02x %02x %02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

    return (FM_MODIFY_BASIC_OP_SIZE + 2);
}

#if 0
static fm_s32 fm_bop_msleep(fm_u32 value, fm_u8 *buf, fm_s32 size)
{
    if (size < (FM_MSLEEP_BASIC_OP_SIZE + 2)) {
        return (-1);
    }

    if (buf == NULL) {
        return (-2);
    }

    buf[0] = FM_MSLEEP_BASIC_OP;
    buf[1] = FM_MSLEEP_BASIC_OP_SIZE;
    buf[2] = (fm_u8)((value) & 0x000000FF);
    buf[3] = (fm_u8)((value >> 8) & 0x000000FF);
    buf[4] = (fm_u8)((value >> 16) & 0x000000FF);
    buf[5] = (fm_u8)((value >> 24) & 0x000000FF);

    WCN_DBG(FM_DBG | CHIP, "%02x %02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

    return (FM_MSLEEP_BASIC_OP_SIZE + 2);
}
#endif

/*
 * mt6626_pwrup_clock_on - Wholechip FM Power Up: step 1, FM Digital Clock enable
 * @buf - target buf
 * @buf_size - buffer size
 * return package size
 */
fm_s32 mt6626_pwrup_clock_on(fm_u8 *buf, fm_s32 buf_size)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

    //FM Digital Clock enable
    pkt_size += fm_bop_write(0x60, 0x3000, &buf[pkt_size], buf_size - pkt_size);//wr 60 3000
    pkt_size += fm_bop_write(0x60, 0x3001, &buf[pkt_size], buf_size - pkt_size);//wr 60 3001
    pkt_size += fm_bop_udelay(3000, &buf[pkt_size], buf_size - pkt_size);//delay 3ms
    pkt_size += fm_bop_write(0x60, 0x3003, &buf[pkt_size], buf_size - pkt_size);//wr 60 3003
    pkt_size += fm_bop_write(0x60, 0x3007, &buf[pkt_size], buf_size - pkt_size);//wr 60 3007
    //no low power mode, analog line in, long antenna
    pkt_size += fm_bop_modify(0x61, 0xFF63, 0x0000, &buf[pkt_size], buf_size - pkt_size);

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

/*
 * mt6626_patch_download - Wholechip FM Power Up: step 3, download patch to f/w,
 * @buf - target buf
 * @buf_size - buffer size
 * @seg_num - total segments that this patch divided into
 * @seg_id - No. of Segments: segment that will now be sent
 * @src - patch source buffer
 * @seg_len - segment size: segment that will now be sent
 * return package size
 */
fm_s32 mt6626_patch_download(fm_u8 *buf, fm_s32 buf_size, fm_u8 seg_num, fm_u8 seg_id, const fm_u8 *src, fm_s32 seg_len)
{
    fm_s32 pkt_size = 0;
    fm_u8 *dst = NULL;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_PATCH_DOWNLOAD_OPCODE;
    pkt_size = 4;

    buf[pkt_size++] = seg_num;
    buf[pkt_size++] = seg_id;

    if (seg_len > (buf_size - pkt_size)) {
        return -1;
    }

    dst = &buf[pkt_size];
    pkt_size += seg_len;

    //copy patch to tx buffer
    while (seg_len--) {
        *dst = *src;
        //printk(KERN_ALERT "%02x ", *dst);
        src++;
        dst++;
    }

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);
    WCN_DBG(FM_DBG | CHIP, "%02x %02x %02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

    return pkt_size;
}

/*
 * mt6626_coeff_download - Wholechip FM Power Up: step 3,download coeff to f/w,
 * @buf - target buf
 * @buf_size - buffer size
 * @seg_num - total segments that this patch divided into
 * @seg_id - No. of Segments: segment that will now be sent
 * @src - patch source buffer
 * @seg_len - segment size: segment that will now be sent
 * return package size
 */
fm_s32 mt6626_coeff_download(fm_u8 *buf, fm_s32 buf_size, fm_u8 seg_num, fm_u8 seg_id, const fm_u8 *src, fm_s32 seg_len)
{
    fm_s32 pkt_size = 0;
    fm_u8 *dst = NULL;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_COEFF_DOWNLOAD_OPCODE;
    pkt_size = 4;

    buf[pkt_size++] = seg_num;
    buf[pkt_size++] = seg_id;

    if (seg_len > (buf_size - pkt_size)) {
        return -1;
    }

    dst = &buf[pkt_size];
    pkt_size += seg_len;

    //copy patch to tx buffer
    while (seg_len--) {
        *dst = *src;
        //printk(KERN_ALERT "%02x ", *dst);
        src++;
        dst++;
    }

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);
    WCN_DBG(FM_DBG | CHIP, "%02x %02x %02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

    return pkt_size;
}

/*
 * mt6626_hwcoeff_download - Wholechip FM Power Up: step 3,download hwcoeff to f/w,
 * @buf - target buf
 * @buf_size - buffer size
 * @seg_num - total segments that this patch divided into
 * @seg_id - No. of Segments: segment that will now be sent
 * @src - patch source buffer
 * @seg_len - segment size: segment that will now be sent
 * return package size
 */
fm_s32 mt6626_hwcoeff_download(fm_u8 *buf, fm_s32 buf_size, fm_u8 seg_num, fm_u8 seg_id, const fm_u8 *src, fm_s32 seg_len)
{
    fm_s32 pkt_size = 0;
    fm_u8 *dst = NULL;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_HWCOEFF_DOWNLOAD_OPCODE;
    pkt_size = 4;

    buf[pkt_size++] = seg_num;
    buf[pkt_size++] = seg_id;

    if (seg_len > (buf_size - pkt_size)) {
        return -1;
    }

    dst = &buf[pkt_size];
    pkt_size += seg_len;

    //copy patch to tx buffer
    while (seg_len--) {
        *dst = *src;
        //printk(KERN_ALERT "%02x ", *dst);
        src++;
        dst++;
    }

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);
    WCN_DBG(FM_DBG | CHIP, "%02x %02x %02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

    return pkt_size;
}

/*
 * mt6626_rom_download - Wholechip FM Power Up: step 3,download rom to f/w,
 * @buf - target buf
 * @buf_size - buffer size
 * @seg_num - total segments that this patch divided into
 * @seg_id - No. of Segments: segment that will now be sent
 * @src - patch source buffer
 * @seg_len - segment size: segment that will now be sent
 * return package size
 */
fm_s32 mt6626_rom_download(fm_u8 *buf, fm_s32 buf_size, fm_u8 seg_num, fm_u8 seg_id, const fm_u8 *src, fm_s32 seg_len)
{
    fm_s32 pkt_size = 0;
    fm_u8 *dst = NULL;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_ROM_DOWNLOAD_OPCODE;
    pkt_size = 4;

    buf[pkt_size++] = seg_num;
    buf[pkt_size++] = seg_id;

    if (seg_len > (buf_size - pkt_size)) {
        return -1;
    }

    dst = &buf[pkt_size];
    pkt_size += seg_len;

    //copy patch to tx buffer
    while (seg_len--) {
        *dst = *src;
        //printk(KERN_ALERT "%02x ", *dst);
        src++;
        dst++;
    }

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);
    WCN_DBG(FM_DBG | CHIP, "%02x %02x %02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

    return pkt_size;
}

/*
 * mt6626_pwrup_digital_init - Wholechip FM Power Up: step 4, FM Digital Init: fm_rgf_maincon
 * @buf - target buf
 * @buf_size - buffer size
 * return package size
 */
fm_s32 mt6626_pwrup_digital_init_1(fm_u8 *buf, fm_s32 buf_size)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

    //Wholechip FM Power Up: FM Digital Init: fm_rgf_maincon
    pkt_size += fm_bop_write(0x6A, 0x0020, &buf[pkt_size], buf_size - pkt_size);//wr 6A 0020
    pkt_size += fm_bop_write(0x6B, 0x0020, &buf[pkt_size], buf_size - pkt_size);//wr 6B 0020
    pkt_size += fm_bop_write(0xA0, 0xD0B1, &buf[pkt_size], buf_size - pkt_size);//wr a0 D0B1
    pkt_size += fm_bop_write(0x60, 0x300F, &buf[pkt_size], buf_size - pkt_size);//wr 60 300F
    pkt_size += fm_bop_write(0x61, 0x0003, &buf[pkt_size], buf_size - pkt_size);//wr 61 3
    pkt_size += fm_bop_write(0x61, 0x0002, &buf[pkt_size], buf_size - pkt_size);//wr 61 2
    //pkt_size += fm_bop_modify(0x61, 0xFFFF, 0x0002, &buf[pkt_size], buf_size - pkt_size);
    //pkt_size += fm_bop_modify(0x61, 0xFFFE, 0x0000, &buf[pkt_size], buf_size - pkt_size);

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

/*
 * mt6626_pwrup_digital_init - Wholechip FM Power Up: step 4, FM Digital Init: fm_rgf_maincon
 * @buf - target buf
 * @buf_size - buffer size
 * return package size
 */
fm_s32 mt6626_pwrup_digital_init_2(fm_u8 *buf, fm_s32 buf_size)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_rd_until(0x64, 0x001F, 0x0002, &buf[pkt_size], buf_size - pkt_size);//Poll 64[0~4] = 2

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

/*
 * mt6626_pwrdown - Wholechip FM Power down: Digital Modem Power Down
 * @buf - target buf
 * @buf_size - buffer size
 * return package size
 */
fm_s32 mt6626_pwrdown(fm_u8 *buf, fm_s32 buf_size)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

    //Disable HW clock control
    pkt_size += fm_bop_write(0x60, 0x330F, &buf[pkt_size], buf_size - pkt_size);//wr 60 330F
    //Reset ASIP
    pkt_size += fm_bop_write(0x61, 0x0001, &buf[pkt_size], buf_size - pkt_size);//wr 61 0001
    //digital core + digital rgf reset
    pkt_size += fm_bop_modify(0x6E, 0xFFF8, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 6E[0~2] 0
    pkt_size += fm_bop_modify(0x6E, 0xFFF8, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 6E[0~2] 0
    pkt_size += fm_bop_modify(0x6E, 0xFFF8, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 6E[0~2] 0
    //Disable all clock
    pkt_size += fm_bop_write(0x60, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 60 0000
    //Reset rgfrf
    pkt_size += fm_bop_write(0x60, 0x4000, &buf[pkt_size], buf_size - pkt_size);//wr 60 4000
    pkt_size += fm_bop_write(0x60, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 60 0000

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

/*
 * mt6626_rampdown - f/w will wait for STC_DONE interrupt
 * @buf - target buf
 * @buf_size - buffer size
 * return package size
 */
fm_s32 mt6626_rampdown(fm_u8 *buf, fm_s32 buf_size)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_RAMPDOWN_OPCODE;
    pkt_size = 4;

    //Clear DSP state
    pkt_size += fm_bop_modify(FM_MAIN_CTRL, 0xFFF0, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 63[3:0] = 0
    //Set DSP ramp down state
    pkt_size += fm_bop_modify(FM_MAIN_CTRL, 0xFFFF, RAMP_DOWN, &buf[pkt_size], buf_size - pkt_size);//wr 63[8] = 1
    //@Wait for STC_DONE interrupt@
    pkt_size += fm_bop_rd_until(FM_MAIN_INTR, FM_INTR_STC_DONE, FM_INTR_STC_DONE, &buf[pkt_size], buf_size - pkt_size);//Poll 69[0] = b'1
    //Clear DSP ramp down state
    pkt_size += fm_bop_modify(FM_MAIN_CTRL, (~RAMP_DOWN), 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 63[8] = 0
    //Write 1 clear the STC_DONE interrupt status flag
    pkt_size += fm_bop_modify(FM_MAIN_INTR, 0xFFFF, FM_INTR_STC_DONE, &buf[pkt_size], buf_size - pkt_size);//wr 69[0] = 1

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

/*
 * mt6626_tune - execute tune action,
 * @buf - target buf
 * @buf_size - buffer size
 * @freq - 760 ~ 1080, 100KHz unit
 * return package size
 */
fm_s32 mt6626_tune(fm_u8 *buf, fm_s32 buf_size, fm_u16 freq)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    freq = (freq - 640) * 2;

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_TUNE_OPCODE;
    pkt_size = 4;

    //Set desired channel & channel parameter
    pkt_size += fm_bop_modify(FM_CHANNEL_SET, 0xFC00, freq, &buf[pkt_size], buf_size - pkt_size);// set 0x65[9:0] = 0x029e, => ((97.5 - 64) * 20)
    //Enable hardware controlled tuning sequence
    pkt_size += fm_bop_modify(FM_MAIN_CTRL, 0xFFFE, TUNE, &buf[pkt_size], buf_size - pkt_size);// set 0x63[0] = 1
    //Wait for STC_DONE interrupt
    pkt_size += fm_bop_rd_until(FM_MAIN_INTR, FM_INTR_STC_DONE, FM_INTR_STC_DONE, &buf[pkt_size], buf_size - pkt_size);//Poll 69[0] = b'1
    //Write 1 clear the STC_DONE interrupt status flag
    pkt_size += fm_bop_modify(FM_MAIN_INTR, 0xFFFF, FM_INTR_STC_DONE, &buf[pkt_size], buf_size - pkt_size);//wr 69[0] = 1

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

/*
 * mt6626_seek - execute seek action,
 * @buf - target buf
 * @buf_size - buffer size
 * @seekdir - 0=seek up, 1=seek down
 * @space - step, 50KHz:001, 100KHz:010, 200KHz:100
 * @max_freq - upper bound
 * @min_freq - lower bound
 * return package size
 */
fm_s32 mt6626_seek(fm_u8 *buf, fm_s32 buf_size, fm_u16 seekdir, fm_u16 space, fm_u16 max_freq, fm_u16 min_freq)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_SEEK_OPCODE;
    pkt_size = 4;

    //Program seek direction
    if (seekdir == 0) {
        pkt_size += fm_bop_modify(FM_MAIN_CFG1, 0xFBFF, 0x0000, &buf[pkt_size], buf_size - pkt_size);//0x66[10] = 0, seek up
    } else {
        pkt_size += fm_bop_modify(FM_MAIN_CFG1, 0xFFFF, 0x0400, &buf[pkt_size], buf_size - pkt_size);//0x66[10] = 1, seek down
    }

    //Program scan channel spacing
    if (space == 1) {
        pkt_size += fm_bop_modify(FM_MAIN_CFG1, 0x8FFF, 0x1000, &buf[pkt_size], buf_size - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=001
    } else if (space == 2) {
        pkt_size += fm_bop_modify(FM_MAIN_CFG1, 0x8FFF, 0x2000, &buf[pkt_size], buf_size - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=010
    } else if (space == 4) {
        pkt_size += fm_bop_modify(FM_MAIN_CFG1, 0x8FFF, 0x4000, &buf[pkt_size], buf_size - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=100
    }

    //enable wrap , if it is not auto scan function, 0x66[11] 0=no wrarp, 1=wrap
    pkt_size += fm_bop_modify(FM_MAIN_CFG1, 0xFFFF, 0x0800, &buf[pkt_size], buf_size - pkt_size);//0x66[11] = 1, wrap
    //0x66[9:0] freq upper bound
    max_freq = (max_freq - 640) * 2;
    pkt_size += fm_bop_modify(FM_MAIN_CFG1, 0xFC00, max_freq, &buf[pkt_size], buf_size - pkt_size);
    //0x67[9:0] freq lower bound
    min_freq = (min_freq - 640) * 2;
    pkt_size += fm_bop_modify(FM_MAIN_CFG2, 0xFC00, min_freq, &buf[pkt_size], buf_size - pkt_size);
    //Enable hardware controlled seeking sequence
    pkt_size += fm_bop_modify(FM_MAIN_CTRL, 0xFFFF, SEEK, &buf[pkt_size], buf_size - pkt_size);//0x63[1] = 1
    //Wait for STC_DONE interrupt
    pkt_size += fm_bop_rd_until(FM_MAIN_INTR, FM_INTR_STC_DONE, FM_INTR_STC_DONE, &buf[pkt_size], buf_size - pkt_size);//Poll 69[0] = b'1
    //Write 1 clear the STC_DONE interrupt status flag
    pkt_size += fm_bop_modify(FM_MAIN_INTR, 0xFFFF, FM_INTR_STC_DONE, &buf[pkt_size], buf_size - pkt_size);//wr 69[0] = 1

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

/*
 * mt6626_scan - execute scan action,
 * @buf - target buf
 * @buf_size - buffer size
 * @scandir - 0=seek up, 1=seek down
 * @space - step, 50KHz:001, 100KHz:010, 200KHz:100
 * @max_freq - upper bound
 * @min_freq - lower bound
 * return package size
 */
fm_s32 mt6626_scan(fm_u8 *buf, fm_s32 buf_size, fm_u16 scandir, fm_u16 space, fm_u16 max_freq, fm_u16 min_freq)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_SCAN_OPCODE;
    pkt_size = 4;

    //Program seek direction
    if (scandir == 0) {
        pkt_size += fm_bop_modify(FM_MAIN_CFG1, 0xFBFF, 0x0000, &buf[pkt_size], buf_size - pkt_size);//0x66[10] = 0, seek up
    } else {
        pkt_size += fm_bop_modify(FM_MAIN_CFG1, 0xFFFF, 0x0400, &buf[pkt_size], buf_size - pkt_size);//0x66[10] = 1, seek down
    }

    //Program scan channel spacing
    if (space == 1) {
        pkt_size += fm_bop_modify(FM_MAIN_CFG1, 0x8FFF, 0x1000, &buf[pkt_size], buf_size - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=001
    } else if (space == 2) {
        pkt_size += fm_bop_modify(FM_MAIN_CFG1, 0x8FFF, 0x2000, &buf[pkt_size], buf_size - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=010
    } else if (space == 4) {
        pkt_size += fm_bop_modify(FM_MAIN_CFG1, 0x8FFF, 0x4000, &buf[pkt_size], buf_size - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=100
    }

    //disable wrap , if it is auto scan function, 0x66[11] 0=no wrarp, 1=wrap
    pkt_size += fm_bop_modify(FM_MAIN_CFG1, 0xF7FF, 0x0000, &buf[pkt_size], buf_size - pkt_size);//0x66[11] = 0, no wrap
    //0x66[9:0] freq upper bound
    max_freq = (max_freq - 640) * 2;
    pkt_size += fm_bop_modify(FM_MAIN_CFG1, 0xFC00, max_freq, &buf[pkt_size], buf_size - pkt_size);
    //0x67[9:0] freq lower bound
    min_freq = (min_freq - 640) * 2;
    pkt_size += fm_bop_modify(FM_MAIN_CFG2, 0xFC00, min_freq, &buf[pkt_size], buf_size - pkt_size);
    //Enable hardware controlled scanning sequence
    pkt_size += fm_bop_modify(FM_MAIN_CTRL, 0xFFFF, SCAN, &buf[pkt_size], buf_size - pkt_size);//0x63[1] = 1
    //Wait for STC_DONE interrupt
    pkt_size += fm_bop_rd_until(FM_MAIN_INTR, FM_INTR_STC_DONE, FM_INTR_STC_DONE, &buf[pkt_size], buf_size - pkt_size);//Poll 69[0] = b'1
    //Write 1 clear the STC_DONE interrupt status flag
    pkt_size += fm_bop_modify(FM_MAIN_INTR, 0xFFFF, FM_INTR_STC_DONE, &buf[pkt_size], buf_size - pkt_size);//wr 69[0] = 1

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

fm_s32 mt6626_get_reg(fm_u8 *buf, fm_s32 buf_size, fm_u8 addr)
{
    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FSPI_READ_OPCODE;
    buf[2] = 0x01;
    buf[3] = 0x00;
    buf[4] = addr;

    WCN_DBG(FM_DBG | CHIP, "%02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4]);
    return 5;
}

fm_s32 mt6626_set_reg(fm_u8 *buf, fm_s32 buf_size, fm_u8 addr, fm_u16 value)
{
    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FSPI_WRITE_OPCODE;
    buf[2] = 0x03;
    buf[3] = 0x00;
    buf[4] = addr;
    buf[5] = (fm_u8)((value) & 0x00FF);
    buf[6] = (fm_u8)((value >> 8) & 0x00FF);

    WCN_DBG(FM_DBG | CHIP, "%02x %02x %02x %02x %02x %02x %02x \n", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);
    return 7;
}

