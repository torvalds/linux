#include <linux/kernel.h>
#include <linux/types.h>

#include "fm_typedef.h"
#include "fm_dbg.h"
#include "fm_err.h"
#include "fm_rds.h"
#include "fm_link.h"

#include "mt6620_fm_reg.h"
//#include "mt6620_fm_link.h"
#include "mt6620_fm.h"
#include "mt6620_fm_cmd.h"
#include "mt6620_fm_cust_cfg.h"

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


/*
 * mt6620_pwrup_clock_on - Wholechip FM Power Up: step 1, FM Digital Clock enable
 * @buf - target buf
 * @buf_size - buffer size
 * return package size
 */
fm_s32 mt6620_off_2_longANA_1(fm_u8 *buf, fm_s32 buf_size)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

    //A01 Turn on the bandgap and central biasing core
    pkt_size += fm_bop_write(0x01, 0x4A00, &buf[pkt_size], buf_size - pkt_size);//wr 1 4A00
    pkt_size += fm_bop_udelay(30, &buf[pkt_size], buf_size - pkt_size);//delay 30
    pkt_size += fm_bop_write(0x01, 0x6A00, &buf[pkt_size], buf_size - pkt_size);//wr 1 6A00
    pkt_size += fm_bop_udelay(50, &buf[pkt_size], buf_size - pkt_size);//delay 50
    //A02 Initialise the LDO's Output
    pkt_size += fm_bop_write(0x02, 0x299C, &buf[pkt_size], buf_size - pkt_size);//wr 2 299C
    //A03 Enable RX, ADC and ADPLL LDO
    pkt_size += fm_bop_write(0x01, 0x6B82, &buf[pkt_size], buf_size - pkt_size);//wr 1 6B82
    //A04 Update FMRF optimized register settings
    pkt_size += fm_bop_write(0x04, 0x0142, &buf[pkt_size], buf_size - pkt_size);//wr 4 0142
    pkt_size += fm_bop_write(0x05, 0x00E7, &buf[pkt_size], buf_size - pkt_size);//wr 5 00E7
    pkt_size += fm_bop_write(0x0A, 0x0060, &buf[pkt_size], buf_size - pkt_size);//wr a 0060
    pkt_size += fm_bop_write(0x0C, 0xA88C, &buf[pkt_size], buf_size - pkt_size);//wr c A88C
    pkt_size += fm_bop_write(0x0D, 0x0888, &buf[pkt_size], buf_size - pkt_size);//wr d 0888
    pkt_size += fm_bop_write(0x10, 0x1E8D, &buf[pkt_size], buf_size - pkt_size);//wr 10 1E8D
    pkt_size += fm_bop_write(0x27, 0x0005, &buf[pkt_size], buf_size - pkt_size);//wr 27 0005
    pkt_size += fm_bop_write(0x0E, 0x0040, &buf[pkt_size], buf_size - pkt_size);//wr e 0040
    pkt_size += fm_bop_write(0x03, 0x50F0, &buf[pkt_size], buf_size - pkt_size);//wr 3 50f0
    pkt_size += fm_bop_write(0x3F, 0xAD06, &buf[pkt_size], buf_size - pkt_size);//wr 3f AD06
    pkt_size += fm_bop_write(0x3E, 0x3280, &buf[pkt_size], buf_size - pkt_size);//wr 3e 3280
    pkt_size += fm_bop_write(0x06, 0x0124, &buf[pkt_size], buf_size - pkt_size);//wr 6 0124
    pkt_size += fm_bop_write(0x08, 0x15B8, &buf[pkt_size], buf_size - pkt_size);//wr 8 15B8
    //A05 Enable RX related blocks
    pkt_size += fm_bop_write(0x28, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 28 0000
    pkt_size += fm_bop_write(0x00, 0x0166, &buf[pkt_size], buf_size - pkt_size);//wr 0 0166
    pkt_size += fm_bop_write(0x3A, 0x0004, &buf[pkt_size], buf_size - pkt_size);//wr 3a 0004
    pkt_size += fm_bop_write(0x37, 0x2590, &buf[pkt_size], buf_size - pkt_size);//wr 37 2590
    // FM ADPLL Power Up
    // () for 16.384M mode, otherwise 15.36M
    pkt_size += fm_bop_write(0x25, 0x040F, &buf[pkt_size], buf_size - pkt_size);//wr 25 040f
    pkt_size += fm_bop_write(0x20, 0x2720, &buf[pkt_size], buf_size - pkt_size);//wr 20 2720
    //XHC, 2011/03/18, [wr 22 9980->6680]
    pkt_size += fm_bop_write(0x22, 0x6680, &buf[pkt_size], buf_size - pkt_size);//wr 22 9980
    pkt_size += fm_bop_write(0x25, 0x080F, &buf[pkt_size], buf_size - pkt_size);//wr 25 080f
    pkt_size += fm_bop_write(0x1E, 0x0863, &buf[pkt_size], buf_size - pkt_size);//wr 1e 0863(0A63)
    pkt_size += fm_bop_udelay(5000, &buf[pkt_size], buf_size - pkt_size);//delay 5000
    pkt_size += fm_bop_write(0x1E, 0x0865, &buf[pkt_size], buf_size - pkt_size);//wr 1e 0865 (0A65)
    pkt_size += fm_bop_udelay(5000, &buf[pkt_size], buf_size - pkt_size);//delay 5000
    pkt_size += fm_bop_write(0x1E, 0x0871, &buf[pkt_size], buf_size - pkt_size);//wr 1e 0871 (0A71)

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

fm_s32 mt6620_off_2_longANA_2(fm_u8 *buf, fm_s32 buf_size)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_udelay(100000, &buf[pkt_size], buf_size - pkt_size);//delay 100000
    pkt_size += fm_bop_write(0x2A, 0x1026, &buf[pkt_size], buf_size - pkt_size);//wr 2a 1026
    // FM RC Calibration
    pkt_size += fm_bop_write(0x00, 0x01E6, &buf[pkt_size], buf_size - pkt_size);//wr 0 01E6
    pkt_size += fm_bop_udelay(1, &buf[pkt_size], buf_size - pkt_size);//delay 1
    pkt_size += fm_bop_write(0x1B, 0x0094, &buf[pkt_size], buf_size - pkt_size);//wr 1b 0094
    pkt_size += fm_bop_write(0x1B, 0x0095, &buf[pkt_size], buf_size - pkt_size);//wr 1b 0095
    pkt_size += fm_bop_udelay(200, &buf[pkt_size], buf_size - pkt_size);//delay 200
    pkt_size += fm_bop_write(0x1B, 0x0094, &buf[pkt_size], buf_size - pkt_size);//wr 1b 0094
    pkt_size += fm_bop_write(0x00, 0x0166, &buf[pkt_size], buf_size - pkt_size);//wr 0 0166
    // FM VCO Enable
    pkt_size += fm_bop_write(0x01, 0x6B8A, &buf[pkt_size], buf_size - pkt_size);//wr 1 6B8A
    pkt_size += fm_bop_udelay(1, &buf[pkt_size], buf_size - pkt_size);//delay 1
    pkt_size += fm_bop_write(0x00, 0xC166, &buf[pkt_size], buf_size - pkt_size);//wr 0 C166
    pkt_size += fm_bop_udelay(3000, &buf[pkt_size], buf_size - pkt_size);//delay 3000
    pkt_size += fm_bop_write(0x00, 0xF166, &buf[pkt_size], buf_size - pkt_size);//wr 0 F166
    pkt_size += fm_bop_write(0x09, 0x2964, &buf[pkt_size], buf_size - pkt_size);//wr 9 2964
    // FM RFDIG settings
    pkt_size += fm_bop_write(0x2E, 0x0008, &buf[pkt_size], buf_size - pkt_size);//wr 2e 8
    pkt_size += fm_bop_write(0x2B, 0x0064, &buf[pkt_size], buf_size - pkt_size);//wr 2b 64
    pkt_size += fm_bop_write(0x2C, 0x0032, &buf[pkt_size], buf_size - pkt_size);//wr 2c 32
    pkt_size += fm_bop_write(0x11, 0x17d4, &buf[pkt_size], buf_size - pkt_size);//wr 11 17d4
    //Update dynamic subband switching setting, XHC 2011/05/17
    pkt_size += fm_bop_write(0x13, 0xFA00, &buf[pkt_size], buf_size - pkt_size);//wr 13 FA00
    pkt_size += fm_bop_write(0x14, 0x0580, &buf[pkt_size], buf_size - pkt_size);//wr 14 0580
    pkt_size += fm_bop_write(0x15, 0xFA80, &buf[pkt_size], buf_size - pkt_size);//wr 15 FA80
    pkt_size += fm_bop_write(0x16, 0x0580, &buf[pkt_size], buf_size - pkt_size);//wr 16 0580
    pkt_size += fm_bop_write(0x33, 0x0008, &buf[pkt_size], buf_size - pkt_size);//wr 33 0008
    // FM DCOC Calibration
    pkt_size += fm_bop_write(0x64, 0x0001, &buf[pkt_size], buf_size - pkt_size);//wr 64 1
    pkt_size += fm_bop_write(0x63, 0x0020, &buf[pkt_size], buf_size - pkt_size);//wr 63 20
    pkt_size += fm_bop_write(0x9C, 0x0044, &buf[pkt_size], buf_size - pkt_size);//wr 9C 0044
    //pkt_size += fm_bop_write(0x6B, 0x0100, &buf[pkt_size], buf_size - pkt_size);//"Disable other interrupts except for STC_DONE(dependent on interrupt output source selection)"
    pkt_size += fm_bop_write(0x0F, 0x1A08, &buf[pkt_size], buf_size - pkt_size);//wr F 1A08
    pkt_size += fm_bop_write(0x63, 0x0021, &buf[pkt_size], buf_size - pkt_size);//wr 63 21
    pkt_size += fm_bop_rd_until(0x69, 0x0001, 0x0001, &buf[pkt_size], buf_size - pkt_size);//Poll fm_intr_stc_done (69H D0) = 1
    pkt_size += fm_bop_write(0x69, 0x0001, &buf[pkt_size], buf_size - pkt_size);//wr 69 1
    pkt_size += fm_bop_write(0x63, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 63 0
    pkt_size += fm_bop_rd_until(0x6F, 0x0001, 0x0000, &buf[pkt_size], buf_size - pkt_size);//Poll stc_done (6FH D0)= 0
    // Others
    pkt_size += fm_bop_write(0x00, 0xF167, &buf[pkt_size], buf_size - pkt_size);//wr 0 F167

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

fm_s32 mt6620_rx_digital_init(fm_u8 *tx_buf, fm_s32 tx_buf_size)
{
    fm_s32 pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

    // fm_rgf_maincon
    //rd 62
    pkt_size += fm_bop_write(0x65, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 65 0
    pkt_size += fm_bop_write(0x64, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 64 1
    pkt_size += fm_bop_write(0x63, 0x0480, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 63 480
    pkt_size += fm_bop_write(0x6D, 0x1AB2, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6d 1ab2
    pkt_size += fm_bop_write(0x6B, 0x2100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6b 2100
    pkt_size += fm_bop_write(0x68, 0xE100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 68 E100
    pkt_size += fm_bop_udelay(10000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 10000
    pkt_size += fm_bop_write(0x68, 0xE000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 68 E000
    // fm_rgf_dac
    pkt_size += fm_bop_write(0x9C, 0xab48, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9c ab48
    pkt_size += fm_bop_write(0x9E, 0x000C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9e c
    pkt_size += fm_bop_write(0x71, 0x807f, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 71 807f
    pkt_size += fm_bop_write(0x72, 0x878f, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 72 878f
    //XHC, 2011/04/29 update 0x73 form 0x07C3 to 0x07C1 speed up I/Q calibration
    pkt_size += fm_bop_write(0x73, 0x07c1, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 73 7c3
    // fm_rgf_front
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 0
    pkt_size += fm_bop_write(0xCB, 0x00B0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr cb b0
    //XHC, 2011/05/06 FM RSSI config
//    pkt_size += fm_bop_write(0xE0, ((0xA301 & 0xFC00) | (FMR_RSSI_TH_LONG & 0x03FF)), &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e0 a301
//    pkt_size += fm_bop_write(0xE1, ((0x00E9 & 0xFF00) | (FMR_CQI_TH & 0x00FF)), &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e1 D0~D7, PAMD TH
	//Run,2013/01 smt scan, ignore this
    pkt_size += fm_bop_write(0xE0, ((0xA301 & 0xFC00) | (0x0301 & 0x03FF)), &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e0 a301
    pkt_size += fm_bop_write(0xE1, ((0x00E9 & 0xFF00) | (0x00E9 & 0x00FF)), &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e1 D0~D7, PAMD TH
    //XHC, 2011/04/15 update search MR threshold
//    pkt_size += fm_bop_write(0xE3, FMR_MR_TH, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e3 1B0
	//Run,2013/01 smt scan, ignore this
    pkt_size += fm_bop_write(0xE3, 0x01BD, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e3 1B0
    pkt_size += fm_bop_write(0xE4, 0x008F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e4 8f
    pkt_size += fm_bop_write(0xCC, 0x0488, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr cc 488
    pkt_size += fm_bop_write(0xD6, 0x036A, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d6 36a
    pkt_size += fm_bop_write(0xD7, 0x836a, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d7 836a
    pkt_size += fm_bop_write(0xDD, 0x0080, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr dd 80
    pkt_size += fm_bop_write(0xB0, 0xcd00, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b0 cd00
    //XHC, 2011/03/18 Update AFC gain[wr 96 41E2->4000][wr 97 049A->021F]
    //[wr 98 0B66->0D00][wr 99 0E1E->0E7F][wr D0 8233->8192][wr D1 20BC->2086]
    //[wr 90 03FF->0192][wr 91 01BE->0086][wr 92 03FF->0192][wr 93 0354->0086]
    //[wr 94 03FF->0192][wr 95 0354->0086][wr 52 17F3]
    pkt_size += fm_bop_write(0x96, 0x4000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 96     41E2 
    pkt_size += fm_bop_write(0x97, 0x021F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 97     049A 
    pkt_size += fm_bop_write(0x98, 0x0D00, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr  98    0B66 
    pkt_size += fm_bop_write(0x99, 0x0E7F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 99     0E1E 
    pkt_size += fm_bop_write(0xD0, 0x8192, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr D0    8233 
    pkt_size += fm_bop_write(0xD1, 0x2086, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr D1    20BC 
    pkt_size += fm_bop_write(0x90, 0x0192, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 90     03ff 
    pkt_size += fm_bop_write(0x91, 0x0086, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 91     01BE 
    pkt_size += fm_bop_write(0x92, 0x0192, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 92     03FF 
    pkt_size += fm_bop_write(0x93, 0x0086, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 93     0354 
    pkt_size += fm_bop_write(0x94, 0x0192, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 94     03FF 
    pkt_size += fm_bop_write(0x95, 0x0086, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 95     0354 
    pkt_size += fm_bop_write(0x52, 0x17F3, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 95     0354 
    // fm_rgf_fmx
    pkt_size += fm_bop_write(0x9F, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 1
    pkt_size += fm_bop_write(0xDE, 0x3388, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr de 3388
    pkt_size += fm_bop_write(0xC2, 0x0180, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c2 180
    pkt_size += fm_bop_write(0xC3, 0x0180, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c3 180
    pkt_size += fm_bop_write(0xDB, 0x0181, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr db 181
    pkt_size += fm_bop_write(0xDC, 0x0184, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr dc 184
    pkt_size += fm_bop_write(0xA2, 0x03C0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a2 3c0
    pkt_size += fm_bop_write(0xA3, 0x03C0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a3 3c0
    pkt_size += fm_bop_write(0xA4, 0x03C0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a4 3c0
    pkt_size += fm_bop_write(0xA5, 0x03C0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a5 3c0
    pkt_size += fm_bop_write(0xA6, 0x03C0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a6 3c0
    pkt_size += fm_bop_write(0xA7, 0x03C0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a7 3c0
    pkt_size += fm_bop_write(0xE3, 0x0280, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e3 280
    pkt_size += fm_bop_write(0xE4, 0x0280, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e4 280
    pkt_size += fm_bop_write(0xE5, 0x0280, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e5 280
    pkt_size += fm_bop_write(0xE6, 0x026C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e6 26c
    pkt_size += fm_bop_write(0xE7, 0x026C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr e7 26c
    pkt_size += fm_bop_write(0xEA, 0x026C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ea 26c
    pkt_size += fm_bop_udelay(1000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000
    pkt_size += fm_bop_write(0xB6, 0x03FC, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b6 3fc
    pkt_size += fm_bop_write(0xB7, 0x02C1, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b7 2c1
    pkt_size += fm_bop_write(0xA8, 0x0820, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 820
    pkt_size += fm_bop_write(0xAC, 0x065E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ac 65e
    pkt_size += fm_bop_write(0xAD, 0x09AD, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ad 9ad
    pkt_size += fm_bop_write(0xAE, 0x0CF8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ae cf8
    pkt_size += fm_bop_write(0xAF, 0x0302, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr af 302
    pkt_size += fm_bop_write(0xB0, 0x04A6, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b0 4a6
    pkt_size += fm_bop_write(0xB1, 0x062C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b1 62c
    pkt_size += fm_bop_write(0xEC, 0x014A, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ec 14a
    pkt_size += fm_bop_write(0xC8, 0x0232, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c8 232
    pkt_size += fm_bop_write(0xC7, 0x0184, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c7 0184
    pkt_size += fm_bop_write(0xD8, 0x00E8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d8 0e8
    pkt_size += fm_bop_write(0xFB, 0x051F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr fb 51f
    pkt_size += fm_bop_udelay(1000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000
    //XHC,2011/03/18 [wr C9 01F0][wr CA 0250][wr D4 2657]
    pkt_size += fm_bop_write(0xC9, 0x01F0, &tx_buf[pkt_size], tx_buf_size - pkt_size);
    pkt_size += fm_bop_write(0xCA, 0x0250, &tx_buf[pkt_size], tx_buf_size - pkt_size);
    pkt_size += fm_bop_write(0xD4, 0x2657, &tx_buf[pkt_size], tx_buf_size - pkt_size);
    pkt_size += fm_bop_write(0x9F, 0x0002, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 2
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0xFF80, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff80
    pkt_size += fm_bop_write(0xA8, 0x0061, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 61
    pkt_size += fm_bop_write(0xA8, 0xFF22, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff22
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 100
    pkt_size += fm_bop_write(0xA8, 0x009A, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 9a
    pkt_size += fm_bop_write(0xA8, 0xFF80, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff80
    pkt_size += fm_bop_write(0xA8, 0x0140, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 140    
    pkt_size += fm_bop_write(0xA8, 0xFF42, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff42   
    pkt_size += fm_bop_write(0xA8, 0xFFE0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ffe0    
    pkt_size += fm_bop_write(0xA8, 0x0114, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 114    
    pkt_size += fm_bop_write(0xA8, 0xFF4E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff4e   
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x003E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 3e
    pkt_size += fm_bop_write(0xA8, 0xFF6E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff6e
    pkt_size += fm_bop_write(0xA8, 0x0087, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 87
    pkt_size += fm_bop_write(0xA8, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0xFEDC, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 fedc
    pkt_size += fm_bop_write(0xA8, 0x0015, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 15
    pkt_size += fm_bop_write(0xA8, 0x00CF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 cf
    pkt_size += fm_bop_write(0xA8, 0xFF6B, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff6b
    pkt_size += fm_bop_write(0xA8, 0x0164, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 164
    pkt_size += fm_bop_write(0xA8, 0x002B, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 2b
    pkt_size += fm_bop_write(0xA8, 0xFF7E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 ff7e
    pkt_size += fm_bop_write(0xA8, 0x0065, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a8 65
    pkt_size += fm_bop_udelay(10000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 10000
    pkt_size += fm_bop_write(0x9F, 0x0002, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 2
    pkt_size += fm_bop_write(0xA9, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 0
    pkt_size += fm_bop_write(0xA9, 0xFFEE, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 ffee
    pkt_size += fm_bop_write(0xA9, 0xFF81, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 ff81
    //XHC,2011/03/18 [wr A9 FFC3]
    pkt_size += fm_bop_write(0xA9, 0xFFC3, &tx_buf[pkt_size], tx_buf_size - pkt_size);
    pkt_size += fm_bop_write(0xA9, 0x0022, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 22
    pkt_size += fm_bop_write(0xA9, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 0
    pkt_size += fm_bop_write(0xA9, 0xFFCC, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 ffcc
    pkt_size += fm_bop_write(0xA9, 0x0023, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 23
    pkt_size += fm_bop_write(0xA9, 0xFFDA, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 ffda
    pkt_size += fm_bop_write(0xA9, 0xFFF7, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a9 fff7
    pkt_size += fm_bop_udelay(10, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 10
    pkt_size += fm_bop_write(0x9F, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 1
    pkt_size += fm_bop_write(0xD3, 0x250b, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d3 250b
    pkt_size += fm_bop_write(0xBB, 0x2320, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr bb 2320
    pkt_size += fm_bop_write(0xD0, 0x02f8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d0 02f8
    pkt_size += fm_bop_write(0xEC, 0x019a, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ec 19a
    pkt_size += fm_bop_write(0xFE, 0x2140, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr fe 2140
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 0
    // fm_rgf_rds    
    pkt_size += fm_bop_write(0x9F, 0x0003, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 3
    pkt_size += fm_bop_write(0xBD, 0x37EB, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr bd 37eb
    pkt_size += fm_bop_write(0xBC, 0x0404, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr bc 404
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 0

    tx_buf[2] = (fm_u8)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (fm_u8)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

/*
 * mt6620_pwrup_digital_init - Wholechip FM Power Up: step 4, FM Digital Init: fm_rgf_maincon
 * @buf - target buf
 * @buf_size - buffer size
 * return package size
 */
fm_s32 mt6620_pwrup_digital_init_1(fm_u8 *buf, fm_s32 buf_size)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;


    // fm_rgf_maincon
    //rd 62
    pkt_size += fm_bop_write(0x65, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 65 0
    pkt_size += fm_bop_write(0x64, 0x0001, &buf[pkt_size], buf_size - pkt_size);//wr 64 1
    pkt_size += fm_bop_write(0x63, 0x0480, &buf[pkt_size], buf_size - pkt_size);//wr 63 480
    pkt_size += fm_bop_write(0x6D, 0x1AB2, &buf[pkt_size], buf_size - pkt_size);//wr 6d 1ab2
    pkt_size += fm_bop_write(0x6B, 0x2100, &buf[pkt_size], buf_size - pkt_size);//wr 6b 2100
    pkt_size += fm_bop_write(0x68, 0xE100, &buf[pkt_size], buf_size - pkt_size);//wr 68 E100

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

fm_s32 mt6620_pwrup_digital_init_2(fm_u8 *buf, fm_s32 buf_size)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_write(0x68, 0xE000, &buf[pkt_size], buf_size - pkt_size);//wr 68 E000
    // fm_rgf_dac
    pkt_size += fm_bop_write(0x9C, 0xab48, &buf[pkt_size], buf_size - pkt_size);//wr 9c ab48
    pkt_size += fm_bop_write(0x9E, 0x000C, &buf[pkt_size], buf_size - pkt_size);//wr 9e c
    pkt_size += fm_bop_write(0x71, 0x807f, &buf[pkt_size], buf_size - pkt_size);//wr 71 807f
    pkt_size += fm_bop_write(0x72, 0x878f, &buf[pkt_size], buf_size - pkt_size);//wr 72 878f
    //XHC, 2011/04/29 update 0x73 form 0x07C3 to 0x07C1 speed up I/Q calibration
    pkt_size += fm_bop_write(0x73, 0x07c1, &buf[pkt_size], buf_size - pkt_size);//wr 73 7c3
    // fm_rgf_front
    pkt_size += fm_bop_write(0x9F, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 9f 0
    pkt_size += fm_bop_write(0xCB, 0x00B0, &buf[pkt_size], buf_size - pkt_size);//wr cb b0
    //XHC, 2011/05/06 FM RSSI config
    pkt_size += fm_bop_write(0xE0, ((0xA301 & 0xFC00) | (FMR_RSSI_TH_LONG_MT6620 & 0x03FF)), &buf[pkt_size], buf_size - pkt_size);//wr e0 a301
    pkt_size += fm_bop_write(0xE1, ((0x00E9 & 0xFF00) | (FMR_CQI_TH_MT6620 & 0x00FF)), &buf[pkt_size], buf_size - pkt_size);//wr e1 D0~D7, PAMD TH
    //XHC, 2011/04/15 update search MR threshold
    pkt_size += fm_bop_write(0xE3, FMR_MR_TH_MT6620, &buf[pkt_size], buf_size - pkt_size);//wr e3 1B0
    pkt_size += fm_bop_write(0xE4, 0x008F, &buf[pkt_size], buf_size - pkt_size);//wr e4 8f
    pkt_size += fm_bop_write(0xCC, 0x0488, &buf[pkt_size], buf_size - pkt_size);//wr cc 488
    pkt_size += fm_bop_write(0xD6, 0x036A, &buf[pkt_size], buf_size - pkt_size);//wr d6 36a
    pkt_size += fm_bop_write(0xD7, 0x836a, &buf[pkt_size], buf_size - pkt_size);//wr d7 836a
    pkt_size += fm_bop_write(0xDD, 0x0080, &buf[pkt_size], buf_size - pkt_size);//wr dd 80
    pkt_size += fm_bop_write(0xB0, 0xcd00, &buf[pkt_size], buf_size - pkt_size);//wr b0 cd00
    //XHC, 2011/03/18 Update AFC gain[wr 96 41E2->4000][wr 97 049A->021F]
    //[wr 98 0B66->0D00][wr 99 0E1E->0E7F][wr D0 8233->8192][wr D1 20BC->2086]
    //[wr 90 03FF->0192][wr 91 01BE->0086][wr 92 03FF->0192][wr 93 0354->0086]
    //[wr 94 03FF->0192][wr 95 0354->0086][wr 52 17F3]
    pkt_size += fm_bop_write(0x96, 0x4000, &buf[pkt_size], buf_size - pkt_size);//wr 96     41E2
    pkt_size += fm_bop_write(0x97, 0x021F, &buf[pkt_size], buf_size - pkt_size);//wr 97     049A
    pkt_size += fm_bop_write(0x98, 0x0D00, &buf[pkt_size], buf_size - pkt_size);//wr  98    0B66
    pkt_size += fm_bop_write(0x99, 0x0E7F, &buf[pkt_size], buf_size - pkt_size);//wr 99     0E1E
    pkt_size += fm_bop_write(0xD0, 0x8192, &buf[pkt_size], buf_size - pkt_size);//wr D0    8233
    pkt_size += fm_bop_write(0xD1, 0x2086, &buf[pkt_size], buf_size - pkt_size);//wr D1    20BC
    pkt_size += fm_bop_write(0x90, 0x0192, &buf[pkt_size], buf_size - pkt_size);//wr 90     03ff
    pkt_size += fm_bop_write(0x91, 0x0086, &buf[pkt_size], buf_size - pkt_size);//wr 91     01BE
    pkt_size += fm_bop_write(0x92, 0x0192, &buf[pkt_size], buf_size - pkt_size);//wr 92     03FF
    pkt_size += fm_bop_write(0x93, 0x0086, &buf[pkt_size], buf_size - pkt_size);//wr 93     0354
    pkt_size += fm_bop_write(0x94, 0x0192, &buf[pkt_size], buf_size - pkt_size);//wr 94     03FF
    pkt_size += fm_bop_write(0x95, 0x0086, &buf[pkt_size], buf_size - pkt_size);//wr 95     0354
    pkt_size += fm_bop_write(0x52, 0x17F3, &buf[pkt_size], buf_size - pkt_size);//wr 95     0354
    // fm_rgf_fmx
    pkt_size += fm_bop_write(0x9F, 0x0001, &buf[pkt_size], buf_size - pkt_size);//wr 9f 1
    pkt_size += fm_bop_write(0xDE, 0x3388, &buf[pkt_size], buf_size - pkt_size);//wr de 3388
    pkt_size += fm_bop_write(0xC2, 0x0180, &buf[pkt_size], buf_size - pkt_size);//wr c2 180
    pkt_size += fm_bop_write(0xC3, 0x0180, &buf[pkt_size], buf_size - pkt_size);//wr c3 180
    pkt_size += fm_bop_write(0xDB, 0x0181, &buf[pkt_size], buf_size - pkt_size);//wr db 181
    pkt_size += fm_bop_write(0xDC, 0x0184, &buf[pkt_size], buf_size - pkt_size);//wr dc 184
    pkt_size += fm_bop_write(0xA2, 0x03C0, &buf[pkt_size], buf_size - pkt_size);//wr a2 3c0
    pkt_size += fm_bop_write(0xA3, 0x03C0, &buf[pkt_size], buf_size - pkt_size);//wr a3 3c0
    pkt_size += fm_bop_write(0xA4, 0x03C0, &buf[pkt_size], buf_size - pkt_size);//wr a4 3c0
    pkt_size += fm_bop_write(0xA5, 0x03C0, &buf[pkt_size], buf_size - pkt_size);//wr a5 3c0
    pkt_size += fm_bop_write(0xA6, 0x03C0, &buf[pkt_size], buf_size - pkt_size);//wr a6 3c0
    pkt_size += fm_bop_write(0xA7, 0x03C0, &buf[pkt_size], buf_size - pkt_size);//wr a7 3c0
    pkt_size += fm_bop_write(0xE3, 0x0280, &buf[pkt_size], buf_size - pkt_size);//wr e3 280
    pkt_size += fm_bop_write(0xE4, 0x0280, &buf[pkt_size], buf_size - pkt_size);//wr e4 280
    pkt_size += fm_bop_write(0xE5, 0x0280, &buf[pkt_size], buf_size - pkt_size);//wr e5 280
    pkt_size += fm_bop_write(0xE6, 0x026C, &buf[pkt_size], buf_size - pkt_size);//wr e6 26c
    pkt_size += fm_bop_write(0xE7, 0x026C, &buf[pkt_size], buf_size - pkt_size);//wr e7 26c
    pkt_size += fm_bop_write(0xEA, 0x026C, &buf[pkt_size], buf_size - pkt_size);//wr ea 26c
    pkt_size += fm_bop_udelay(1000, &buf[pkt_size], buf_size - pkt_size);//delay 1000
    pkt_size += fm_bop_write(0xB6, 0x03FC, &buf[pkt_size], buf_size - pkt_size);//wr b6 3fc
    pkt_size += fm_bop_write(0xB7, 0x02C1, &buf[pkt_size], buf_size - pkt_size);//wr b7 2c1
    pkt_size += fm_bop_write(0xA8, 0x0820, &buf[pkt_size], buf_size - pkt_size);//wr a8 820
    pkt_size += fm_bop_write(0xAC, 0x065E, &buf[pkt_size], buf_size - pkt_size);//wr ac 65e
    pkt_size += fm_bop_write(0xAD, 0x09AD, &buf[pkt_size], buf_size - pkt_size);//wr ad 9ad
    pkt_size += fm_bop_write(0xAE, 0x0CF8, &buf[pkt_size], buf_size - pkt_size);//wr ae cf8
    pkt_size += fm_bop_write(0xAF, 0x0302, &buf[pkt_size], buf_size - pkt_size);//wr af 302
    pkt_size += fm_bop_write(0xB0, 0x04A6, &buf[pkt_size], buf_size - pkt_size);//wr b0 4a6
    pkt_size += fm_bop_write(0xB1, 0x062C, &buf[pkt_size], buf_size - pkt_size);//wr b1 62c
    pkt_size += fm_bop_write(0xEC, 0x014A, &buf[pkt_size], buf_size - pkt_size);//wr ec 14a
    pkt_size += fm_bop_write(0xC8, 0x0232, &buf[pkt_size], buf_size - pkt_size);//wr c8 232
    pkt_size += fm_bop_write(0xC7, 0x0184, &buf[pkt_size], buf_size - pkt_size);//wr c7 0184
    pkt_size += fm_bop_write(0xD8, 0x00E8, &buf[pkt_size], buf_size - pkt_size);//wr d8 0e8
    pkt_size += fm_bop_write(0xFB, 0x051F, &buf[pkt_size], buf_size - pkt_size);//wr fb 51f
    pkt_size += fm_bop_udelay(1000, &buf[pkt_size], buf_size - pkt_size);//delay 1000
    //XHC,2011/03/18 [wr C9 01F0][wr CA 0250][wr D4 2657]
    pkt_size += fm_bop_write(0xC9, 0x01F0, &buf[pkt_size], buf_size - pkt_size);
    pkt_size += fm_bop_write(0xCA, 0x0250, &buf[pkt_size], buf_size - pkt_size);
    pkt_size += fm_bop_write(0xD4, 0x2657, &buf[pkt_size], buf_size - pkt_size);
    pkt_size += fm_bop_write(0x9F, 0x0002, &buf[pkt_size], buf_size - pkt_size);//wr 9f 2
    pkt_size += fm_bop_write(0xA8, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0xFF80, &buf[pkt_size], buf_size - pkt_size);//wr a8 ff80
    pkt_size += fm_bop_write(0xA8, 0x0061, &buf[pkt_size], buf_size - pkt_size);//wr a8 61
    pkt_size += fm_bop_write(0xA8, 0xFF22, &buf[pkt_size], buf_size - pkt_size);//wr a8 ff22
    pkt_size += fm_bop_write(0xA8, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0100, &buf[pkt_size], buf_size - pkt_size);//wr a8 100
    pkt_size += fm_bop_write(0xA8, 0x009A, &buf[pkt_size], buf_size - pkt_size);//wr a8 9a
    pkt_size += fm_bop_write(0xA8, 0xFF80, &buf[pkt_size], buf_size - pkt_size);//wr a8 ff80
    pkt_size += fm_bop_write(0xA8, 0x0140, &buf[pkt_size], buf_size - pkt_size);//wr a8 140
    pkt_size += fm_bop_write(0xA8, 0xFF42, &buf[pkt_size], buf_size - pkt_size);//wr a8 ff42
    pkt_size += fm_bop_write(0xA8, 0xFFE0, &buf[pkt_size], buf_size - pkt_size);//wr a8 ffe0
    pkt_size += fm_bop_write(0xA8, 0x0114, &buf[pkt_size], buf_size - pkt_size);//wr a8 114
    pkt_size += fm_bop_write(0xA8, 0xFF4E, &buf[pkt_size], buf_size - pkt_size);//wr a8 ff4e
    pkt_size += fm_bop_write(0xA8, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0x003E, &buf[pkt_size], buf_size - pkt_size);//wr a8 3e
    pkt_size += fm_bop_write(0xA8, 0xFF6E, &buf[pkt_size], buf_size - pkt_size);//wr a8 ff6e
    pkt_size += fm_bop_write(0xA8, 0x0087, &buf[pkt_size], buf_size - pkt_size);//wr a8 87
    pkt_size += fm_bop_write(0xA8, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr a8 0
    pkt_size += fm_bop_write(0xA8, 0xFEDC, &buf[pkt_size], buf_size - pkt_size);//wr a8 fedc
    pkt_size += fm_bop_write(0xA8, 0x0015, &buf[pkt_size], buf_size - pkt_size);//wr a8 15
    pkt_size += fm_bop_write(0xA8, 0x00CF, &buf[pkt_size], buf_size - pkt_size);//wr a8 cf
    pkt_size += fm_bop_write(0xA8, 0xFF6B, &buf[pkt_size], buf_size - pkt_size);//wr a8 ff6b
    pkt_size += fm_bop_write(0xA8, 0x0164, &buf[pkt_size], buf_size - pkt_size);//wr a8 164
    pkt_size += fm_bop_write(0xA8, 0x002B, &buf[pkt_size], buf_size - pkt_size);//wr a8 2b
    pkt_size += fm_bop_write(0xA8, 0xFF7E, &buf[pkt_size], buf_size - pkt_size);//wr a8 ff7e
    pkt_size += fm_bop_write(0xA8, 0x0065, &buf[pkt_size], buf_size - pkt_size);//wr a8 65

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

fm_s32 mt6620_pwrup_digital_init_3(fm_u8 *buf, fm_s32 buf_size)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_write(0x9F, 0x0002, &buf[pkt_size], buf_size - pkt_size);//wr 9f 2
    pkt_size += fm_bop_write(0xA9, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr a9 0
    pkt_size += fm_bop_write(0xA9, 0xFFEE, &buf[pkt_size], buf_size - pkt_size);//wr a9 ffee
    pkt_size += fm_bop_write(0xA9, 0xFF81, &buf[pkt_size], buf_size - pkt_size);//wr a9 ff81
    //XHC,2011/03/18 [wr A9 FFC3]
    pkt_size += fm_bop_write(0xA9, 0xFFC3, &buf[pkt_size], buf_size - pkt_size);
    pkt_size += fm_bop_write(0xA9, 0x0022, &buf[pkt_size], buf_size - pkt_size);//wr a9 22
    pkt_size += fm_bop_write(0xA9, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr a9 0
    pkt_size += fm_bop_write(0xA9, 0xFFCC, &buf[pkt_size], buf_size - pkt_size);//wr a9 ffcc
    pkt_size += fm_bop_write(0xA9, 0x0023, &buf[pkt_size], buf_size - pkt_size);//wr a9 23
    pkt_size += fm_bop_write(0xA9, 0xFFDA, &buf[pkt_size], buf_size - pkt_size);//wr a9 ffda
    pkt_size += fm_bop_write(0xA9, 0xFFF7, &buf[pkt_size], buf_size - pkt_size);//wr a9 fff7
    pkt_size += fm_bop_udelay(10, &buf[pkt_size], buf_size - pkt_size);//delay 10
    pkt_size += fm_bop_write(0x9F, 0x0001, &buf[pkt_size], buf_size - pkt_size);//wr 9f 1
    pkt_size += fm_bop_write(0xD3, 0x250b, &buf[pkt_size], buf_size - pkt_size);//wr d3 250b
    pkt_size += fm_bop_write(0xBB, 0x2320, &buf[pkt_size], buf_size - pkt_size);//wr bb 2320
    pkt_size += fm_bop_write(0xD0, 0x02f8, &buf[pkt_size], buf_size - pkt_size);//wr d0 02f8
    pkt_size += fm_bop_write(0xEC, 0x019a, &buf[pkt_size], buf_size - pkt_size);//wr ec 19a
    pkt_size += fm_bop_write(0xFE, 0x2140, &buf[pkt_size], buf_size - pkt_size);//wr fe 2140
    pkt_size += fm_bop_write(0x9F, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 9f 0
    // fm_rgf_rds
    pkt_size += fm_bop_write(0x9F, 0x0003, &buf[pkt_size], buf_size - pkt_size);//wr 9f 3
    pkt_size += fm_bop_write(0xBD, 0x37EB, &buf[pkt_size], buf_size - pkt_size);//wr bd 37eb
    pkt_size += fm_bop_write(0xBC, 0x0404, &buf[pkt_size], buf_size - pkt_size);//wr bc 808
    pkt_size += fm_bop_write(0x9F, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 9f 0

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

/*
 * mt6620_pwrdown - Wholechip FM Power down: Digital Modem Power Down
 * @buf - target buf
 * @buf_size - buffer size
 * return package size
 */
fm_s32 mt6620_pwrdown(fm_u8 *buf, fm_s32 buf_size)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

    //Digital Modem Power Down
    pkt_size += fm_bop_write(0x63, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 63 0
    pkt_size += fm_bop_modify(0x6E, 0xFFFC, 0x0000, &buf[pkt_size], buf_size - pkt_size);// clear 0x6e[1:0], round1
    pkt_size += fm_bop_modify(0x6E, 0xFFFC, 0x0000, &buf[pkt_size], buf_size - pkt_size);// clear 0x6e[1:0], round2
    pkt_size += fm_bop_modify(0x6E, 0xFFFC, 0x0000, &buf[pkt_size], buf_size - pkt_size);// clear 0x6e[1:0], round3
    pkt_size += fm_bop_modify(0x6E, 0xFFFC, 0x0000, &buf[pkt_size], buf_size - pkt_size);// clear 0x6e[1:0], round4
    //ADPLL Power Off Sequence
    pkt_size += fm_bop_write(0x2A, 0x0022, &buf[pkt_size], buf_size - pkt_size);//wr 2a 22
    pkt_size += fm_bop_write(0x1E, 0x0860, &buf[pkt_size], buf_size - pkt_size);//wr 1E 0860
    pkt_size += fm_bop_write(0x20, 0x0720, &buf[pkt_size], buf_size - pkt_size);//wr 20 0720
    pkt_size += fm_bop_write(0x20, 0x2720, &buf[pkt_size], buf_size - pkt_size);//wr 20 2720
    //ANALOG/RF Power Off Sequence
    pkt_size += fm_bop_write(0x00, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 0 0
    pkt_size += fm_bop_write(0x01, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 1 0
    pkt_size += fm_bop_write(0x04, 0x0141, &buf[pkt_size], buf_size - pkt_size);//wr 4 0141
    pkt_size += fm_bop_write(0x09, 0x0964, &buf[pkt_size], buf_size - pkt_size);//wr 9 0964
    pkt_size += fm_bop_write(0x0C, 0x288C, &buf[pkt_size], buf_size - pkt_size);//wr c 288c
    pkt_size += fm_bop_write(0x26, 0x0004, &buf[pkt_size], buf_size - pkt_size);//wr 26 0004
    pkt_size += fm_bop_write(0x3A, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 3A 0000
    pkt_size += fm_bop_write(0x3B, 0x00C3, &buf[pkt_size], buf_size - pkt_size);//wr 3B 00C3
    pkt_size += fm_bop_write(0x3E, 0x3280, &buf[pkt_size], buf_size - pkt_size);//wr 3E 3280
    pkt_size += fm_bop_write(0x3F, 0x4E16, &buf[pkt_size], buf_size - pkt_size);//wr 3F 4E16
    pkt_size += fm_bop_write(0x41, 0x0004, &buf[pkt_size], buf_size - pkt_size);//wr 41 0004
    //clear TX settings
    pkt_size += fm_bop_write(0x12, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 12 0000
    pkt_size += fm_bop_write(0x47, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 47 0000

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

/*
 * mt6620_rampdown - f/w will wait for STC_DONE interrupt
 * @buf - target buf
 * @buf_size - buffer size
 * return package size
 */
fm_s32 mt6620_rampdown(fm_u8 *buf, fm_s32 buf_size)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_RAMPDOWN_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_modify(0x9C, 0xFF87, 0x0068, &buf[pkt_size], buf_size - pkt_size);//wr 9c[3] = 1, ramp down
    pkt_size += fm_bop_write(0x9F, 0x0001, &buf[pkt_size], buf_size - pkt_size);//wr 9f 1
    pkt_size += fm_bop_write(0xC8, 0x0101, &buf[pkt_size], buf_size - pkt_size);//wr c8 101
    pkt_size += fm_bop_write(0xDD, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr dd 0
    pkt_size += fm_bop_write(0xD8, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr d8 0
    pkt_size += fm_bop_write(0x9F, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 9f 0
    pkt_size += fm_bop_udelay(35000, &buf[pkt_size], buf_size - pkt_size);//delay 35000
    //disable interrupt before rampdown
    pkt_size += fm_bop_write(0x6B, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 6b 0000
    pkt_size += fm_bop_modify(0x63, 0xFFF0, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 63[3:0] = 0, ramp down
    pkt_size += fm_bop_rd_until(0x6f, 0x0001, 0x0000, &buf[pkt_size], buf_size - pkt_size);//Poll 6f[0] = b'0
    pkt_size += fm_bop_write(0x6B, 0x2100, &buf[pkt_size], buf_size - pkt_size);//wr 6b 2100
    //enable interrupt after rampdown

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}
#if 0//ramp down tx will do in tx tune  flow
fm_s32 mt6620_rampdown_tx(fm_u8 *tx_buf, fm_s32 tx_buf_size)
{
    fm_s32 pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_RAMPDOWN_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_write(0x3B, 0x0500, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3b 0500
    pkt_size += fm_bop_write(0x3A, 0x00FF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3a ff
    pkt_size += fm_bop_write(0x65, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 65 1
    pkt_size += fm_bop_write(0x48, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 48 0
    
    tx_buf[2] = (fm_u8)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (fm_u8)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}
#endif
/*
 * mt6620_tune - execute tune action,
 * @buf - target buf
 * @buf_size - buffer size
 * @freq - 760 ~ 1080, 100KHz unit
 * return package size
 */
fm_s32 mt6620_tune_1(fm_u8 *buf, fm_s32 buf_size, fm_u16 freq)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }
    if (0 == fm_get_channel_space(freq)) {
        freq *= 10;
    }

    freq = (freq - 6400) * 2 /10;

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_TUNE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_modify(0x0F, 0xFC00, freq, &buf[pkt_size], buf_size - pkt_size);// set 0x0F[9:0] = 0x029e, => ((97.5 - 64) * 20)
    pkt_size += fm_bop_modify(0x63, 0xFFFF, 0x0001, &buf[pkt_size], buf_size - pkt_size);// set 0x63[0] = 1

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

fm_s32 mt6620_tune_2(fm_u8 *buf, fm_s32 buf_size, fm_u16 freq)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }
/*    
    if (0 == fm_get_channel_space(freq)) {
        freq *= 10;
    }

    freq = (freq - 640) * 2 / 10;
*/
    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_TUNE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_modify(0x9C, 0xFFF7, 0x0000, &buf[pkt_size], buf_size - pkt_size);// set 0x9C[3] = 0

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

fm_s32 mt6620_tune_3(fm_u8 *buf, fm_s32 buf_size, fm_u16 freq)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }
/*    
    if (0 == fm_get_channel_space(freq)) {
        freq *= 10;
    }

    freq = (freq - 640) * 2 / 10;
*/
    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_TUNE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_write(0x9F, 0x0001, &buf[pkt_size], buf_size - pkt_size);//wr 9F 1
    pkt_size += fm_bop_write(0xC8, 0x0232, &buf[pkt_size], buf_size - pkt_size);//wr C8 232
    pkt_size += fm_bop_write(0xDD, 0x8833, &buf[pkt_size], buf_size - pkt_size);//wr DD 8833
    pkt_size += fm_bop_write(0xD8, 0x00E8, &buf[pkt_size], buf_size - pkt_size);//wr D8 E8
    pkt_size += fm_bop_write(0x9F, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 9F 0

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

fm_s32 mt6620_fast_tune(fm_u8 *tx_buf, fm_s32 tx_buf_size, fm_u16 freq)
{
    fm_s32 pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    if (0 == fm_get_channel_space(freq)) {
        freq *= 10;
    }

    freq = (freq - 6400) * 2 / 10;

    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_TUNE_OPCODE;
    pkt_size = 4;
    
    pkt_size += fm_bop_modify(0x63, 0xFFF0, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x63[3:0] = 0
    pkt_size += fm_bop_modify(0x6F, 0xFFFE, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x6F[0] = 0
    pkt_size += fm_bop_modify(0x0F, 0xFC00, freq, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x0F[9:0] = 0x029e, => ((97.5 - 64) * 20)
    //disable interrupt before rampdown
    pkt_size += fm_bop_write(0x6B, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6b 0000
    pkt_size += fm_bop_modify(0x63, 0xFFFE, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x63[0] = 1
    pkt_size += fm_bop_rd_until(0x69, 0x0001, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Poll 69[0] = b'1
    pkt_size += fm_bop_modify(0x69, 0xFFFE, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x69[0] = 1
    pkt_size += fm_bop_write(0x6B, 0x2100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6b 2100
    //enable interrupt after rampdown
    
    tx_buf[2] = (fm_u8)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (fm_u8)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

/*
 * mt6620_full_cqi_req - execute request cqi info action,
 * @buf - target buf
 * @buf_size - buffer size
 * @freq - 7600 ~ 10800, freq array
 * @cnt - channel count
 * @type - request type, 1: a single channel; 2: multi channel; 3:multi channel with 100Khz step; 4: multi channel with 50Khz step
 * 
 * return package size
 */

fm_s32 mt6620_full_cqi_req(fm_u8 *buf, fm_s32 buf_size, fm_u16 freq, fm_s32 cnt, fm_s32 type)
{
    fm_s32 pkt_size = 0;

    if(buf_size < TX_BUF_SIZE)
    {
        return (-1);
    }
    if (0 == fm_get_channel_space(freq))
    {
        freq *= 10;
    }
    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_SOFT_MUTE_TUNE_OPCODE;
    pkt_size = 4;

    switch (type) {
        case 1:
            buf[pkt_size] = 0x0001;
            pkt_size++;
            buf[pkt_size] = (fm_u8)(freq & 0x00FF);
            pkt_size++;
            buf[pkt_size] = (fm_u8)((freq >> 8) & 0x00FF);
            pkt_size++;
            break;
        default:
            buf[pkt_size] = (fm_u16)type;
            pkt_size++;
            break;
    }

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
	
}

// freq: 760 ~ 1080, 100KHz unit
fm_s32 mt6620_tune_tx(fm_u8 *tx_buf, fm_s32 tx_buf_size, fm_u16 freq)
{
    fm_s32 pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    if (0 == fm_get_channel_space(freq)) {
        freq *= 10;
    }

    freq = (freq - 6400) * 2 / 10;

    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_TUNE_OPCODE;
    pkt_size = 4;
    
    //XHC, 2011/04/20, ramp down before tune
    pkt_size += fm_bop_write(0x3B, 0x0500, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3b 0500
    pkt_size += fm_bop_write(0x3A, 0x00FF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3a ff
    pkt_size += fm_bop_write(0x65, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 65 1
    pkt_size += fm_bop_write(0x48, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 48 0
    //XHC, 2011/04/14
    pkt_size += fm_bop_modify(0x41, 0xFFFE, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x41[0] = 1
    //XHC, 2011/04/18
    pkt_size += fm_bop_modify(0x12, 0x7FFF, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x12[15] = 0
    //XHC, 2011/04/22, clear RTC compensation info
    pkt_size += fm_bop_modify(0x47, 0x003F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x47[15:6] = 0
    pkt_size += fm_bop_modify(0x0F, 0xFC00, freq, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x0F[9:0] = freq
    pkt_size += fm_bop_write(0x26, 0x002C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 26 002C --> SCAL Related --> SCAL_EN and SCAL_GM_EN
    pkt_size += fm_bop_udelay(1000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000us
    pkt_size += fm_bop_write(0x26, 0x003C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 26 003C --> SCAL_BUF_EN
    pkt_size += fm_bop_udelay(1000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000us
    pkt_size += fm_bop_write(0x10, 0x1E8D, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 10 1e8d
    pkt_size += fm_bop_udelay(1000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000us
    pkt_size += fm_bop_write(0x10, 0x9E8D, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 10 9e8d
    pkt_size += fm_bop_udelay(10000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 10000us
    pkt_size += fm_bop_write(0x26, 0x0024, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 26 0024 --> Turn off SCAL gm and BUF
    pkt_size += fm_bop_write(0x65, 0x400F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 65 400f
    pkt_size += fm_bop_write(0x48, 0x8000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 48 8000
    pkt_size += fm_bop_write(0x3B, 0x0420, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3b 0420
    pkt_size += fm_bop_write(0x3A, 0x01FF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3a 1ff
    //XHC, 2011/04/14
    //pkt_size += fm_bop_udelay(10000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 10000us
    //pkt_size += fm_bop_modify(0x41, 0xFFFE, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x41[0] = 0
    
    tx_buf[2] = (fm_u8)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (fm_u8)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

// freq: 760 ~ 1080, 100KHz unit
fm_s32 mt6620_tune_txscan(fm_u8 *tx_buf, fm_s32 tx_buf_size, fm_u16 freq)
{
    fm_s32 pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    if (0 == fm_get_channel_space(freq)) {
        freq *= 10;
    }

    freq = (freq - 6400) * 2 / 10;

    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_TUNE_OPCODE;
    pkt_size = 4;

	//rampdown
    //disable interrupt before rampdown
    pkt_size += fm_bop_write(0x6B, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6b 0000
	pkt_size += fm_bop_modify(0x63, 0xFFF0, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 63[3:0] = 0, ramp down
    pkt_size += fm_bop_rd_until(0x6f, 0x0001, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Poll 6f[0] = b'0
    pkt_size += fm_bop_modify(0x9C, 0xFFFF, 0x0008, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9c[3] = 1, ramp down
    pkt_size += fm_bop_write(0x6B, 0x2100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 6b 2100
    //enable interrupt after rampdown

	//set desired channel
    pkt_size += fm_bop_modify(0x0F, 0xFC00, freq, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set 0x0F[9:0] = 0x029e, => ((97.5 - 64) * 20)

	//only for short antennal tune
#ifdef MT6620_FM_USE_SHORT_ANTENNA
	pkt_size += fm_bop_write(0x28, 0x3800, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 28 3800
	pkt_size += fm_bop_write(0x03, 0x90F0, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3 90f0
	pkt_size += fm_bop_write(0x2E, 0x0028, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2e 28
	pkt_size += fm_bop_write(0x2F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2f 0
	pkt_size += fm_bop_write(0x26, 0x003C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 26 3c
	pkt_size += fm_bop_write(0x2E, 0x002C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2e 2c
	pkt_size += fm_bop_udelay(10000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wait 10ms
	pkt_size += fm_bop_write(0x26, 0x0024, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 26 24
	pkt_size += fm_bop_write(0x28, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 28 00
#endif

	//only for E1
#ifdef MT6620E1
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 0
    pkt_size += fm_bop_write(0xAF, 0x2210, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr AF 2210
#endif

	//mask STC_DONE interrupt, 6a(D0) 0 
	//pkt_size += fm_bop_modify(0x6A, 0xFFFE,  0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);

	//enable hardware are controlled tuning sequence
    pkt_size += fm_bop_modify(0x63, 0xFFFF,  0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set rgf_tune (63H D0) =1

	//check STC_DONE interrupt status flag
	//pkt_size += fm_bop_rd_until(0x69, 0x0001, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Poll fm_intr_stc_done (69H D0) = 1

	//write 1 clear the STC_DONE status flag
    //pkt_size += fm_bop_modify(0x69, 0xFFFF, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);// set stc_done (6FH D0) =1

	//unmask STC_DONE interrupt, 6a(D0) 1
	//pkt_size += fm_bop_modify(0x6A, 0xFFFF,  0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);

	//only for E1
#ifdef MT6620E1
	//pkt_size += fm_bop_write(0xAF, 0x7710, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr AF 7710
#endif

    tx_buf[2] = (fm_u8)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (fm_u8)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

/*
 * mt6620_seek - execute seek action,
 * @buf - target buf
 * @buf_size - buffer size
 * @seekdir - 0=seek up, 1=seek down
 * @space - step, 50KHz:001, 100KHz:010, 200KHz:100
 * @max_freq - upper bound
 * @min_freq - lower bound
 * return package size
 */
fm_s32 mt6620_seek_1(fm_u8 *buf, fm_s32 buf_size, fm_u16 seekdir, fm_u16 space, fm_u16 max_freq, fm_u16 min_freq)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_SEEK_OPCODE;
    pkt_size = 4;

    //A1 Program seek direction, 0x66[10]: 0=seek up, 1=seek down
    if (seekdir == 0) {
        pkt_size += fm_bop_modify(0x66, 0xFBFF, 0x0000, &buf[pkt_size], buf_size - pkt_size);//0x66[10] = 0, seek up
    } else {
        pkt_size += fm_bop_modify(0x66, 0xFFFF, 0x0400, &buf[pkt_size], buf_size - pkt_size);//0x66[10] = 1, seek down
    }

    //0x66[11] 0=no wrarp, 1=wrap
    pkt_size += fm_bop_modify(0x66, 0xFFFF, 0x0800, &buf[pkt_size], buf_size - pkt_size);//0x66[11] = 1, wrap
    //A2 Program scan channel spacing, 0x66[14:12] step 50KHz:001/100KHz:010/200KHz:100
    if (space == 4) {
        pkt_size += fm_bop_modify(0x66, 0x8FFF, 0x4000, &buf[pkt_size], buf_size - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=001
    } else {
        pkt_size += fm_bop_modify(0x66, 0x8FFF, 0x2000, &buf[pkt_size], buf_size - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=010
    }

    //0x66[9:0] freq upper bound
    max_freq = (max_freq - 640) * 2;
    pkt_size += fm_bop_modify(0x66, 0xFC00, max_freq, &buf[pkt_size], buf_size - pkt_size);

    //0x67[9:0] freq lower bound
    min_freq = (min_freq - 640) * 2;
    pkt_size += fm_bop_modify(0x67, 0xFC00, min_freq, &buf[pkt_size], buf_size - pkt_size);
    //A3 Enable hardware controlled seeking sequence
    pkt_size += fm_bop_modify(0x63, 0xFFFF, 0x0002, &buf[pkt_size], buf_size - pkt_size);//0x63[1] = 1

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

fm_s32 mt6620_seek_2(fm_u8 *buf, fm_s32 buf_size, fm_u16 seekdir, fm_u16 space, fm_u16 max_freq, fm_u16 min_freq)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_SEEK_OPCODE;
    pkt_size = 4;

    //A10 Set softmute to normal mode
    pkt_size += fm_bop_write(0x9F, 0x0001, &buf[pkt_size], buf_size - pkt_size);//wr 9F 1
    pkt_size += fm_bop_write(0xC8, 0x0232, &buf[pkt_size], buf_size - pkt_size);//wr C8 232
    pkt_size += fm_bop_write(0xDD, 0x8833, &buf[pkt_size], buf_size - pkt_size);//wr DD 8833
    pkt_size += fm_bop_write(0xD8, 0x00E8, &buf[pkt_size], buf_size - pkt_size);//wr D8 E8
    pkt_size += fm_bop_write(0x9F, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 9F 0

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

/*
 * mt6620_scan - execute scan action,
 * @buf - target buf
 * @buf_size - buffer size
 * @scandir - 0=seek up, 1=seek down
 * @space - step, 50KHz:001, 100KHz:010, 200KHz:100
 * @max_freq - upper bound
 * @min_freq - lower bound
 * return package size
 */
fm_s32 mt6620_scan_1(fm_u8 *buf, fm_s32 buf_size, fm_u16 scandir, fm_u16 space, fm_u16 max_freq, fm_u16 min_freq)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_SCAN_OPCODE;
    pkt_size = 4;

    //A1 Program seek direction, 0x66[10]: 0=seek up, 1=seek down
    if (scandir == 0) {
        pkt_size += fm_bop_modify(0x66, 0xFBFF, 0x0000, &buf[pkt_size], buf_size - pkt_size);//0x66[10] = 0, seek up
    } else {
        pkt_size += fm_bop_modify(0x66, 0xFFFF, 0x0400, &buf[pkt_size], buf_size - pkt_size);//0x66[10] = 1, seek down
    }

    //0x66[11] 0=no wrarp, 1=wrap
    pkt_size += fm_bop_modify(0x66, 0xFFFF, 0x0800, &buf[pkt_size], buf_size - pkt_size);//0x66[11] = 1, wrap
    //A2 Program scan channel spacing, 0x66[14:12] step 50KHz:001/100KHz:010/200KHz:100
    if (space == 4) {
        pkt_size += fm_bop_modify(0x66, 0x8FFF, 0x4000, &buf[pkt_size], buf_size - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=001
    } else {
        pkt_size += fm_bop_modify(0x66, 0x8FFF, 0x2000, &buf[pkt_size], buf_size - pkt_size);//clear 0x66[14:12] then 0x66[14:12]=010
    }

    //0x66[9:0] freq upper bound
    max_freq = (max_freq - 640) * 2;
    pkt_size += fm_bop_modify(0x66, 0xFC00, max_freq, &buf[pkt_size], buf_size - pkt_size);

    //0x67[9:0] freq lower bound
    min_freq = (min_freq - 640) * 2;
    pkt_size += fm_bop_modify(0x67, 0xFC00, min_freq, &buf[pkt_size], buf_size - pkt_size);
    //A3 Enable hardware controlled seeking sequence
    pkt_size += fm_bop_modify(0x63, 0xFFFF, 0x0004, &buf[pkt_size], buf_size - pkt_size);//0x63[1] = 1

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

fm_s32 mt6620_scan_2(fm_u8 *buf, fm_s32 buf_size, fm_u16 scandir, fm_u16 space, fm_u16 max_freq, fm_u16 min_freq)
{
    fm_s32 pkt_size = 0;

    if (buf_size < TX_BUF_SIZE) {
        return (-1);
    }

    buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    buf[1] = FM_SCAN_OPCODE;
    pkt_size = 4;

    //A10 Set softmute to normal mode
    pkt_size += fm_bop_write(0x9F, 0x0001, &buf[pkt_size], buf_size - pkt_size);//wr 9F 1
    pkt_size += fm_bop_write(0xC8, 0x0232, &buf[pkt_size], buf_size - pkt_size);//wr C8 232
    pkt_size += fm_bop_write(0xDD, 0x8833, &buf[pkt_size], buf_size - pkt_size);//wr DD 8833
    pkt_size += fm_bop_write(0xD8, 0x00E8, &buf[pkt_size], buf_size - pkt_size);//wr D8 E8
    pkt_size += fm_bop_write(0x9F, 0x0000, &buf[pkt_size], buf_size - pkt_size);//wr 9F 0

    buf[2] = (fm_u8)((pkt_size - 4) & 0x00FF);
    buf[3] = (fm_u8)(((pkt_size - 4) >> 8) & 0x00FF);

    return pkt_size;
}

fm_s32 mt6620_get_reg(fm_u8 *buf, fm_s32 buf_size, fm_u8 addr)
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

fm_s32 mt6620_set_reg(fm_u8 *buf, fm_s32 buf_size, fm_u8 addr, fm_u16 value)
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

fm_s32 mt6620_rds_rx_enable(fm_u8 *tx_buf, fm_s32 tx_buf_size) //IC version
{
    fm_s32 pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = RDS_RX_ENABLE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_write(0x9F, 0x0003, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0x9f 3
    pkt_size += fm_bop_write(0xCB, 0xE016, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Wr 0xcb e016
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0x9f 0
    pkt_size += fm_bop_write(0x63, 0x0491, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Wr 0x63 491
    pkt_size += fm_bop_modify(0x6B, 0xFFFF, 0x2000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//Wr 0x6b [13] = 1

    tx_buf[2] = (fm_u8)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (fm_u8)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

fm_s32 mt6620_rds_rx_disable(fm_u8 *tx_buf, fm_s32 tx_buf_size) //IC version
{
    fm_s32 pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = RDS_RX_ENABLE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_modify(0x6B, 0xDFFF, 0x0000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//Wr 0x6b [13] = 0
    pkt_size += fm_bop_write(0x63, 0x0481, &tx_buf[pkt_size], tx_buf_size - pkt_size);//Wr 0x63 481


    tx_buf[2] = (fm_u8)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (fm_u8)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

// TBD for IC
fm_s32 mt6620_rds_tx(fm_u8 *tx_buf, fm_s32 tx_buf_size, fm_u16 pi, fm_u16 *ps, fm_u16 *other_rds, fm_u8 other_rds_cnt)
{
    fm_s32 pkt_size = 0;
    fm_s32 i;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }

    if(other_rds_cnt > 29){
        return (-2);
    }

    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = RDS_TX_OPCODE;
    pkt_size = 4;

    //RDS Tx config
    pkt_size += fm_bop_modify(0x65, 0xFFFF, 0x0010, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//wr 65[4] = b'1, enable RDS Tx
    pkt_size += fm_bop_write(0x9F, 0x0003, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 3
    pkt_size += fm_bop_write(0xA2, 0x0005, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a2 5, repeat + PI_reg mode
    pkt_size += fm_bop_write(0xA1, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a1 0
    //pkt_size += fm_bop_write(0xA0, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a0 1
    pkt_size += fm_bop_write(0xA4, pi, &tx_buf[pkt_size], tx_buf_size - pkt_size);//write PI to PI_reg
    //program PS buf
    pkt_size += fm_bop_rd_until(0xAA, 0x0001, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);// rd until aa[0] = b'0, ptr in normal buf
    //program normal buf, workaround that PS buf can't work while normal buf is empty
    for(i = 0; i < 12; i++)
    {
        pkt_size += fm_bop_write(0xA8, ps[i], &tx_buf[pkt_size], tx_buf_size - pkt_size);//a8 = RDS Tx data
    }
    pkt_size += fm_bop_modify(0xA2, 0xFFFF, 0x0002, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//wr a2[1] = b'1, mem_addr mode
    for(i = 0; i < 12; i++)
    {
        pkt_size += fm_bop_write(0xA7, (0x0063+i), &tx_buf[pkt_size], tx_buf_size - pkt_size);//a7 = mem_addr
        pkt_size += fm_bop_write(0xA8, ps[i], &tx_buf[pkt_size], tx_buf_size - pkt_size);//a8 = RDS Tx data
    }
    pkt_size += fm_bop_modify(0xA2, 0xFFFF, 0x0010, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//wr a2[4] = b'1, switch to ps buf
    //program normal buf
    pkt_size += fm_bop_rd_until(0xAA, 0x0001, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);// rd until aa[0] = b'1, ptr in ps buf
    pkt_size += fm_bop_modify(0xA2, 0xFFFD, 0x0000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//wr a2[1] = b'0, h/w addr mode
    for(i = 0; i < 12; i++)
    {
        pkt_size += fm_bop_write(0xA8, ps[i], &tx_buf[pkt_size], tx_buf_size - pkt_size);//a8 = RDS Tx data
    }
    for(i = 0; i < (other_rds_cnt * 3); i++)
    {
        pkt_size += fm_bop_write(0xA8, other_rds[i], &tx_buf[pkt_size], tx_buf_size - pkt_size);//a8 = RDS Tx data
    }
    pkt_size += fm_bop_modify(0xA2, 0xFFEF, 0x0000, &tx_buf[pkt_size], TX_BUF_SIZE - pkt_size);//wr a2[4] = b'0, switch to normal buf
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 0

    tx_buf[2] = (fm_u8)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (fm_u8)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

// TBD for IC
fm_s32 mt6620_off_2_tx_shortANA(fm_u8 *tx_buf, fm_s32 tx_buf_size)
{
    fm_s32 pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

    pkt_size += fm_bop_write(0x01, 0x4A00, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1 4A00  --> Turn on Central Bias + FC
    pkt_size += fm_bop_udelay(30, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 30 
    pkt_size += fm_bop_write(0x01, 0x6A00, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1 6A00  --> Turn off FC
    pkt_size += fm_bop_udelay(50, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 50
    pkt_size += fm_bop_write(0x02, 0x299C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2 299C  --> Set the LDOs Output Voltages
    pkt_size += fm_bop_write(0x01, 0x6B82, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1 6B82  --> Turn on DCO, RX and ADDA LDO
    pkt_size += fm_bop_write(0x3B, 0x0500, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3B 0500 --> Turn on PA LDO --> LDO PA = 2.5V [0000 010x xxx0 000] {xxxx} - 0001 = 2.5V
    pkt_size += fm_bop_write(0x04, 0x0548, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 4 0548  --> Set the RX LDO to Low Power Mode + TR Switch Off
    pkt_size += fm_bop_write(0x37, 0x2000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 37 2000 --> Set the Short Antenna Bias
    pkt_size += fm_bop_write(0x42, 0xC002, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 42 C002 --> Set the Short Antenna Bias
    pkt_size += fm_bop_write(0x0A, 0x0060, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr A 0060  --> Update AFCDAC LPF Setting
    pkt_size += fm_bop_write(0x0E, 0x0040, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr E 0040  --> Update SX_VDC_CBANK
    pkt_size += fm_bop_write(0x0C, 0xA88C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c A88C
    pkt_size += fm_bop_write(0x10, 0x1E8D, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 10 1e8d
    pkt_size += fm_bop_write(0x27, 0x0005, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 27 0005 --> Update ACAL and Enable RMS_DET    
    pkt_size += fm_bop_write(0x11, 0x07D8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 11 07D8 --> Set VCO_DIVRST_N = 0 
    pkt_size += fm_bop_write(0x41, 0x0003, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 41 0003 --> Set TX_FVCO_EN = 1 and FVCO_SEL=1
    pkt_size += fm_bop_write(0x08, 0x25B8, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 8 25b8  --> ADC = TX Mode (AU_ADC)
    pkt_size += fm_bop_write(0x09, 0x2964, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9 2964  --> ADC DWA ON
    pkt_size += fm_bop_write(0x3F, 0xAD86, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3F AD86 --> TX DAc RX_TX_SEL = TX MOde 
    pkt_size += fm_bop_write(0x3A, 0x01EF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3A 01EF --> Turn on TX Chain [PA+D2S+HRM+AUPGA+TXDAC+LODIV]
    pkt_size += fm_bop_write(0x3E, 0x3181, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3E 3181 --> TX LPF EN + CSEL from RCCAL
    pkt_size += fm_bop_write(0x00, 0x0100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0  0100 --> Turn on SDADC
    pkt_size += fm_bop_write(0x37, 0x2000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 37 2000 --> Control Signal for DAC_CK output clock gate
    //FM ADPLL Power Up
    pkt_size += fm_bop_write(0x25, 0x040F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 25 040f --> Turn off DIG_CK_EN
    pkt_size += fm_bop_write(0x20, 0x2720, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 20 2720 --> Turn on ADPLL  2320 CW32 
    //XHC,2011/03/18, [wr 22 9980-> 6680]
    pkt_size += fm_bop_write(0x22, 0x6680, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 22 9980 --> Update DLF Gain
    pkt_size += fm_bop_write(0x25, 0x080F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 25 080f --> Update I_CODE_CCAL + ADC_CK_EN + 32KCLKPLL_EN 0803 CW37
    pkt_size += fm_bop_write(0x1E, 0x0863, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1e 0863 --> Turn on DCO + CAL_COARSE_EN
    pkt_size += fm_bop_udelay(5000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 5000
    pkt_size += fm_bop_write(0x1E, 0x0865, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1e 0865 --> Change the CAL_COARSE to CAL_FINE
    pkt_size += fm_bop_udelay(5000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 5000
    pkt_size += fm_bop_write(0x1E, 0x0871, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1e 0871 --> Off the CAL_COARSE and CAL_FINE + Turn on PLL
    pkt_size += fm_bop_udelay(100000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 100000
    pkt_size += fm_bop_write(0x2A, 0x1026, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2a 1022 --> Enable TOP_CG
    //FM RC Calibration
    pkt_size += fm_bop_write(0x00, 0x0080, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0 0080 --> RCCAL
    pkt_size += fm_bop_udelay(1, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000
    pkt_size += fm_bop_write(0x1B, 0x0094, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1b 0094 --> Update RCCAL Target Count
    pkt_size += fm_bop_write(0x1B, 0x0095, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1b 0095 --> Start RCCAL
    pkt_size += fm_bop_udelay(200, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000
    pkt_size += fm_bop_write(0x1B, 0x0094, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1b 0094 --> Off RCCAL
    pkt_size += fm_bop_write(0x00, 0x0100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0 0100 --> Turn off RCCAL Analog Block
    //FM VCO Enable
    pkt_size += fm_bop_write(0x01, 0x6B8A, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 1 6B8A --> Turn on VCO LDO
    pkt_size += fm_bop_udelay(1, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 1000
    pkt_size += fm_bop_write(0x00, 0xC100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 0 C100 --> Turn on VCO, AFCDAC
    pkt_size += fm_bop_write(0x0C, 0xB88C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c B88c --> Turn on Const gm
    pkt_size += fm_bop_udelay(3000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 3000
    pkt_size += fm_bop_write(0x3A, 0x01FF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3A 01FF --> Enable TX Divider
    //Short Antenna Setting
    pkt_size += fm_bop_write(0x42, 0xF002, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 42 F002 --> MSB of HRM_Gain <5> ****>> For Max Pout 
    pkt_size += fm_bop_write(0x3C, 0xABE9, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3C ABE9 --> Max HRM Gain<4:0> - xxxxx [1010 10xx xxx0 1001]
    pkt_size += fm_bop_write(0x3D, 0x027E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 3D 027E --> Max PA Gain<4:0> - [0000 0010 0xxx xx10]
    pkt_size += fm_bop_write(0x33, 0x0008, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 33 0008 --> Use old VCO calibration routine to save calibration time
    pkt_size += fm_bop_write(0x28, 0xFFFF, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 28 FFFF
    pkt_size += fm_bop_write(0x2E, 0x0020, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2E 0020 --> Turn on SCAL_HWTRIG_DIS --> VCO CAL and SCAL are done separately
    pkt_size += fm_bop_write(0x2F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 2F 0000 --> Disable Capbank manual enter mode [4A40 previously]
    pkt_size += fm_bop_write(0x44, 0x006E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 44 6e
    pkt_size += fm_bop_write(0x46, 0xDC22, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 46 DC22
    pkt_size += fm_bop_write(0x49, 0x0080, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 49 80
    pkt_size += fm_bop_write(0x4A, 0x0004, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 4A 4
    pkt_size += fm_bop_write(0x4B, 0x0040, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 4B 40

    tx_buf[2] = (fm_u8)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (fm_u8)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

// TBD for IC
fm_s32 mt6620_dig_init(fm_u8 *tx_buf, fm_s32 tx_buf_size)
{
    fm_s32 pkt_size = 0;

    if(tx_buf_size < TX_BUF_SIZE){
        return (-1);
    }
    
    tx_buf[0] = FM_TASK_COMMAND_PKT_TYPE;
    tx_buf[1] = FM_ENABLE_OPCODE;
    pkt_size = 4;

//fm_rgf_maincon
    pkt_size += fm_bop_write(0x64, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 64 0
    pkt_size += fm_bop_write(0x65, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 65 1
    pkt_size += fm_bop_write(0x68, 0xE100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 68 E100
    pkt_size += fm_bop_udelay(10000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//delay 10000   
    pkt_size += fm_bop_write(0x68, 0xE000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 68 E000
//fm_rgf_dac
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9F 0
    pkt_size += fm_bop_write(0x9E, 0x001C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9E 1C
    pkt_size += fm_bop_write(0x9C, 0xA540, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9c A540
//fm_rgf_front
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 0 --> start rgf_front
    pkt_size += fm_bop_write(0xB8, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b8 1
    pkt_size += fm_bop_write(0xAB, 0x39B6, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ab 39b6
    pkt_size += fm_bop_write(0xAC, 0x3C3E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ac 3c3e
    pkt_size += fm_bop_write(0xAD, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ad 0 
    pkt_size += fm_bop_write(0xAE, 0x03C2, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ae 3c2
    pkt_size += fm_bop_write(0xAF, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr af 1
    pkt_size += fm_bop_write(0xB1, 0x623D, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b1 623d 
    pkt_size += fm_bop_write(0xF4, 0x0020, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr f4 20
    pkt_size += fm_bop_write(0xF5, 0xBF16, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr f5 bf16
    pkt_size += fm_bop_write(0xB9, 0x0050, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b9 0050
    pkt_size += fm_bop_write(0xBA, 0x00C3, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ba 00c3
    pkt_size += fm_bop_write(0xBB, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr bb 0
    pkt_size += fm_bop_write(0xBC, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr bc 0
    pkt_size += fm_bop_write(0xBD, 0x0056, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr bd 56 
    pkt_size += fm_bop_write(0xBE, 0x0089, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr be 89
    pkt_size += fm_bop_write(0xBF, 0x004C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr bf 4c
    pkt_size += fm_bop_write(0xC0, 0x0171, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c0 171
    pkt_size += fm_bop_write(0xC1, 0x002B, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c1 2b
    pkt_size += fm_bop_write(0xC2, 0x001F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c2 1f
    pkt_size += fm_bop_write(0xC3, 0x0066, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c3 66
    pkt_size += fm_bop_write(0xC4, 0x00F6, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c4 f6
    pkt_size += fm_bop_write(0xC5, 0x0066, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c5 66
    pkt_size += fm_bop_write(0xC6, 0x001F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c6 1f
    pkt_size += fm_bop_write(0xC7, 0x0007, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c7 7 
    pkt_size += fm_bop_write(0xFE, 0x0039, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr fe 39
    pkt_size += fm_bop_write(0xFF, 0x3907, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ff 3907
//fm_rgf_fmx
    pkt_size += fm_bop_write(0x9F, 0x0001, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 1 --> start rgf_outer
    pkt_size += fm_bop_write(0xC0, 0x076C, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c0 76c
    pkt_size += fm_bop_write(0xB7, 0x0004, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr b7 4
    pkt_size += fm_bop_write(0xD8, 0x006A, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d8 6a
    pkt_size += fm_bop_write(0xC8, 0x2828, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c8 2828
    pkt_size += fm_bop_write(0xCE, 0x0090, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ce 90
    pkt_size += fm_bop_write(0xFE, 0x000F, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr fe f
    pkt_size += fm_bop_write(0xA2, 0x0100, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a2 100
    pkt_size += fm_bop_write(0xA3, 0x0111, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a3 111
    pkt_size += fm_bop_write(0xA4, 0x0122, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a4 122
    pkt_size += fm_bop_write(0xA5, 0x0135, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a5 135
    pkt_size += fm_bop_write(0xA6, 0x0149, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a6 149
    pkt_size += fm_bop_write(0xA7, 0x015E, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a7 15e
    pkt_size += fm_bop_write(0xDB, 0x0174, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr db 174
    pkt_size += fm_bop_write(0xDC, 0x018D, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr dc 18d
    pkt_size += fm_bop_write(0xC9, 0x01A6, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr c9 1a6
    pkt_size += fm_bop_write(0xCA, 0x01C1, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr ca 1c1
    pkt_size += fm_bop_write(0xCB, 0x01DE, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr cb 1de
    pkt_size += fm_bop_write(0xCC, 0x01FD, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr cc 1fd
    pkt_size += fm_bop_write(0xD4, 0x2657, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr d4 2657
    pkt_size += fm_bop_write(0xA0, 0x85B2, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr a0 85b2
    pkt_size += fm_bop_write(0x9F, 0x0000, &tx_buf[pkt_size], tx_buf_size - pkt_size);//wr 9f 0

    tx_buf[2] = (fm_u8)((pkt_size-4) & 0x00FF);
    tx_buf[3] = (fm_u8)(((pkt_size-4) >> 8) & 0x00FF);

    return pkt_size;
}

