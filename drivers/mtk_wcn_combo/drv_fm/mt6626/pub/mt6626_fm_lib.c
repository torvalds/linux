#include <linux/semaphore.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "fm_typedef.h"
#include "fm_dbg.h"
#include "fm_err.h"
#include "fm_interface.h"
#include "fm_stdlib.h"
#include "fm_patch.h"
#include "fm_config.h"

#include "mt6626_fm_reg.h"
#include "mt6626_fm.h"
#include "mt6626_drv_dsp.h"
#include "mt6626_fm_link.h"
#include "mt6626_fm_lib.h"
#include "mt6626_fm_cmd.h"

#define MT6626_FM_PATCH_PATH "/etc/firmware/mt6626_fm_patch.bin"
#define MT6626_FM_COEFF_PATH "/etc/firmware/mt6626_fm_coeff.bin"
#define MT6626_FM_HWCOEFF_PATH "/etc/firmware/mt6626_fm_hwcoeff.bin"
#define MT6626_FM_ROM_PATH "/etc/firmware/mt6626_fm_rom.bin"

extern void fm_low_power_wa(int fmon);
extern void mt66x6_poweron(int idx);
extern void mt66x6_poweroff(int idx);

static struct fm_callback *fm_cb_op;

/* mt6626 FM Receiver Power Up Sequence*/
static const struct ctrl_word_operation PowerOnSetting[] = {
    //@Wholechip FM Power Up: FM Digital Clock enable
    {0x60, 0x0, 0x3000},
    {0x60, 0x0, 0x3001},
    {MSDELAY, 0x0, 0x0003},//Delay 3ms
    {0x60, 0x0, 0x3003},
    {0x60, 0x0, 0x3007},
    {HW_VER, 0x99, 0x0000},
    //antenna and audio path config
#ifdef FMRADIO_I2S_SUPPORT
#ifdef FM_PowerOn_with_ShortAntenna
    {0x61, 0xFF73, 0x0090},//no low power mode, I2S, short antenna
#else
    {0x61, 0xFF73, 0x0080},//no low power mode, I2S, long antenna
#endif
    {0x9B, 0xFFF7, 0x0008},//0000->master, 0008->slave
    {0x5F, 0xE7FF, 0x0000},//0000->32K, 0800->44.1K, 1000->48K
    //{0x61, 0xFF73, 0x0080},//no low power mode, I2S, long antenna, 0xff63
    //{0x9B, 0xFFF7, 0x0008},//0000->master, 0008->slave
    //{0x5F, 0xE7FF, 0x0000},//0000->32K, 0800->44.1K, 1000->48K
#else
#ifdef FM_PowerOn_with_ShortAntenna
    {0x61, 0xFF63, 0x0010},//no low power mode, analog line in, short antenna
#else
    {0x61, 0xFF63, 0x0000},//no low power mode, analog line in, long antenna
#endif
#endif
    {HW_VER, 0x0062, 0x0000},//read the HW version

    //@Wholechip FM Power Up: FM Digital Init: download patch/DSP coefficient/HWACC coefficient
    {DSPPATCH, 0x0, DSP_PATH},
    {DSPPATCH, 0x0, DSP_COEFF},
    {DSPPATCH, 0x0, DSP_HW_COEFF},
    {0x90, 0x0, 0x0040},
    {0x90, 0x0, 0x0000},

    //@Wholechip FM Power Up: FM Digital Init: fm_rgf_maincon
    {0x6A, 0x0, 0x0020},
    {0x6B, 0x0, 0x0020},
    {0x60, 0x0, 0x300F},
    {0x61, 0xFFFF, 0x0002},
    {0x61, 0xFFFE, 0x0000},
    {POLL_P, 0x64, 0x2}
};
#define POWER_ON_COMMAND_COUNT (sizeof(PowerOnSetting)/sizeof(PowerOnSetting[0]))

static int Chip_Version = mt6626_E1;


static fm_s32 mt6626_pwron(fm_s32 data)
{
    mt66x6_poweron(MT66x6_FM);
    return 0;
}


static fm_s32 mt6626_pwroff(fm_s32 data)
{
    mt66x6_poweroff(MT66x6_FM);
    return 0;
}

static fm_s32 Delayms(fm_u32 data)
{
    WCN_DBG(FM_DBG | CHIP, "delay %dms\n", data);
    msleep(data);
    return 0;
}

static fm_s32 Delayus(fm_u32 data)
{
    WCN_DBG(FM_DBG | CHIP, "delay %dus\n", data);
    udelay(data);
    return 0;
}

static fm_s32 mt6626_read(fm_u8 addr, fm_u16 *val)
{
    fm_s32 ret = 0;

    ret = fm_ctrl_rx(addr, val);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "rd 0x%02x err\n", addr);
        return ret;
    }

    WCN_DBG(FM_DBG | CHIP, "rd 0x%02x 0x%04x\n", addr, *val);
    return ret;
}

static fm_s32 mt6626_write(fm_u8 addr, fm_u16 val)
{
    fm_s32 ret = 0;

    ret = fm_ctrl_tx(addr, val);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "wr 0x%02x err\n", addr);
        return ret;
    }

    WCN_DBG(FM_DBG | CHIP, "wr 0x%02x 0x%04x\n", addr, val);
    return ret;
}

static fm_s32 mt6626_write1(fm_u8 addr, fm_u16 val)
{
    return fm_ctrl_tx(addr, val);
}

static fm_s32 mt6626_set_bits(fm_u8 addr, fm_u16 bits, fm_u16 mask)
{
    fm_s32 ret = 0;
    fm_u16 val;

    ret = mt6626_read(addr, &val);

    if (ret)
        return ret;

    val = ((val & (mask)) | bits);
    ret = mt6626_write(addr, val);

    return ret;
}

static fm_u16 mt6626_get_chipid(void)
{
    return 0x6626;
}

static void mt6626_TUNE_ON(void)
{
    fm_u16 dataRead;

    WCN_DBG(FM_DBG | CHIP, "tune on\n");
    mt6626_read(FM_MAIN_CTRL, &dataRead);
    //mt6626_write(FM_MAIN_CTRL, (dataRead&0xFFFE)|TUNE);
    mt6626_write(FM_MAIN_CTRL, (dataRead&0xFFF8) | TUNE);
}

static void mt6626_SEEK_ON(void)
{
    fm_u16 dataRead;

    WCN_DBG(FM_DBG | CHIP, "seek on\n");
    mt6626_read(FM_MAIN_CTRL, &dataRead);
    //mt6626_write(FM_MAIN_CTRL, (dataRead&0xFFFD)|SEEK);
    mt6626_write(FM_MAIN_CTRL, (dataRead&0xFFF8) | SEEK);
}

static void mt6626_SCAN_ON(void)
{
    fm_u16 dataRead;

    WCN_DBG(FM_DBG | CHIP, "scan on\n");
    mt6626_read(FM_MAIN_CTRL, &dataRead);
    //mt6626_write(FM_MAIN_CTRL, (dataRead&0xFFFB)|SCAN);
    mt6626_write(FM_MAIN_CTRL, (dataRead&0xFFF8) | SCAN);
}

/*  MT6628_SetAntennaType - set Antenna type
 *  @type - 1,Short Antenna;  0, Long Antenna
 */
static fm_s32 mt6626_SetAntennaType(fm_s32 type)
{
    fm_u16 dataRead;

    WCN_DBG(FM_DBG | CHIP, "set ana to %s\n", type ? "short" : "long");
    mt6626_read(FM_MAIN_CG2_CTRL, &dataRead);

    if (type) {
        dataRead |= ANTENNA_TYPE;
    } else {
        dataRead &= (~ANTENNA_TYPE);
    }

    mt6626_write(FM_MAIN_CG2_CTRL, dataRead);

    return 0;
}

static fm_s32 mt6626_GetAntennaType(void)
{
    fm_u16 dataRead;

    mt6626_read(FM_MAIN_CG2_CTRL, &dataRead);
    WCN_DBG(FM_DBG | CHIP, "get ana type: %s\n", (dataRead&ANTENNA_TYPE) ? "short" : "long");

    if (dataRead&ANTENNA_TYPE)
        return FM_SHORT_ANA; //short antenna
    else
        return FM_LONG_ANA; //long antenna
}

static fm_s32 mt6626_writeFA(fm_u16 *buff, fm_u8 fa)
{
    fm_u8 i = 0;

    for (i = 0; i < 3; i++) {
        if ((fa >> i)& 0x1)
            *buff |= (1 << (12 + i));
        else
            *buff &= ~(1 << (12 + i));
    }

    return 0;
}

static fm_s32 mt6626_Mute(fm_bool mute)
{
    fm_u16 dataRead;

    WCN_DBG(FM_DBG | CHIP, "set %s\n", mute ? "mute" : "unmute");
    mt6626_read(FM_MAIN_CTRL, &dataRead);

    if (mute == 1) {
        mt6626_write(FM_MAIN_CTRL, (dataRead&0xFFDF) | 0x0020);
    } else {
        mt6626_write(FM_MAIN_CTRL, (dataRead&0xFFDF));
    }

    return 0;
}

/*
 * mt6626_WaitSTCDone - wait for stc done flag change to '1'
 * @waittime - the total wait time in ms
 * @interval - the delay time of every polling loop in ms
 * if success, return 0; else error code
 */
static fm_s32 mt6626_WaitSTCDone(fm_u32 waittime, fm_u32 interval)
{
    fm_u16 dataRead;
    fm_s32 cnt;

    if (interval) {
        cnt = waittime / interval;
    } else {
        cnt = 0;
    }

    do {
        if (cnt-- < 0) {
            return -1; //wait for STC done failed
        }

        Delayms(interval);
        mt6626_read(FM_MAIN_INTR, &dataRead);
    } while ((dataRead&FM_INTR_STC_DONE) == 0);

    return 0;
}

static fm_s32 mt6626_ClearSTCDone(void)
{
    fm_u16 dataRead;

    mt6626_read(FM_MAIN_INTR, &dataRead);
    mt6626_write(FM_MAIN_INTR, dataRead | FM_INTR_STC_DONE);//clear status flag
    return 0;
}

static fm_s32 mt6626_RampDown(void)
{
    fm_u16 dataRead;

    WCN_DBG(FM_DBG | CHIP, "ramp down\n");
    //Clear DSP state
    mt6626_read(FM_MAIN_CTRL, &dataRead);
    mt6626_write(FM_MAIN_CTRL, (dataRead&0xFFF0)); //clear rgf_tune/seek/scan/dsp_init

    //Set DSP ramp down state
    mt6626_read(FM_MAIN_CTRL, &dataRead);
    mt6626_write(FM_MAIN_CTRL, (dataRead | RAMP_DOWN));

    //Check STC_DONE status flag (not the interrupt flag!)
    if (mt6626_WaitSTCDone(1000, 1)) {
        WCN_DBG(FM_ALT | CHIP, "ramp down failed\n");
        return -1;
    }

    //Clear DSP ramp down state
    mt6626_read(FM_MAIN_CTRL, &dataRead);
    mt6626_write(FM_MAIN_CTRL, (dataRead&(~RAMP_DOWN)));

    mt6626_ClearSTCDone();
    return 0;
}

/*
*  mt6626_DspPatch - DSP download procedure
*  @img - source dsp bin code
*  @type - rom/patch/coefficient/hw_coefficient
*
*/
static fm_s32 mt6626_DspPatch(const fm_u16 *img, enum IMG_TYPE type)
{
    fm_u32 ctrl_code = 0;
    fm_u16 data_len = 0;  	// in words
    fm_u16 i;

    FMR_ASSERT(img);

    WCN_DBG(FM_DBG | CHIP, "down load DSP patch %d (1-rom, 2-patch, 3-coe, 4-hwcoe)\n", type);

    switch (type) {
    case IMG_ROM:  			//rom code
    case IMG_PATCH:  		//patch
        ctrl_code = 0x10;
        break;
    case IMG_COEFFICIENT:  	//coeff
        ctrl_code = 0xe;
        break;
    case IMG_HW_COEFFICIENT:	//HW coeff
        ctrl_code = 0xd;
        break;
    default:
        break;
    }

    data_len = img[1] - img[0] + 1;
    WCN_DBG(FM_DBG | CHIP, "patch len: %d\n", data_len);

    if (!(data_len > 0)) {
        ; //error
        return -FM_EPARA;
    }

    mt6626_write(FM_DSP_PATCH_CTRL, 0);
    mt6626_write(FM_DSP_PATCH_OFFSET, img[0]);		//Start address
    mt6626_write(FM_DSP_PATCH_CTRL, 0x40); 			//Reset download control
    mt6626_write(FM_DSP_PATCH_CTRL, ctrl_code); 	//Set download control

    switch (type) {
    case IMG_ROM:
    case IMG_PATCH:
    case IMG_HW_COEFFICIENT:
        WCN_DBG(FM_DBG | CHIP, "rom/patch/hw_coefficient downloading......\n");

        for (i = 0; i < data_len; i++) {
            mt6626_write1(FM_DSP_PATCH_DATA, img[2+i]);
        }

        break;
    case IMG_COEFFICIENT:
        WCN_DBG(FM_DBG | CHIP, "coefficient downloading......\n");

        if (MT6626_DEEMPHASIS_50us) {
            for (i = 0; i < data_len; i++) {
                if (i == 86) {
                    mt6626_write1(FM_DSP_PATCH_DATA, fm_cust_config_fetch(FM_CFG_RX_RSSI_TH_LONG));
                } else if (i == 292) {
                    mt6626_write1(FM_DSP_PATCH_DATA, 0x332B);
                    mt6626_write1(FM_DSP_PATCH_DATA, 0x2545);
                    mt6626_write1(FM_DSP_PATCH_DATA, 0x1344);
                    mt6626_write1(FM_DSP_PATCH_DATA, 0x09F5);
                    mt6626_write1(FM_DSP_PATCH_DATA, 0x0526);
                    mt6626_write1(FM_DSP_PATCH_DATA, 0x02A9);
                    mt6626_write1(FM_DSP_PATCH_DATA, 0x0160);
                    mt6626_write1(FM_DSP_PATCH_DATA, 0x00B6);
                    mt6626_write1(FM_DSP_PATCH_DATA, 0x005E);
                    mt6626_write1(FM_DSP_PATCH_DATA, 0x0031);
                    mt6626_write1(FM_DSP_PATCH_DATA, 0x0000);
                    mt6626_write1(FM_DSP_PATCH_DATA, 0x0000);
                    mt6626_write1(FM_DSP_PATCH_DATA, 0x0000);
                    i += 12;
                } else if (i == 505) {
                    mt6626_write1(FM_DSP_PATCH_DATA, fm_cust_config_fetch(FM_CFG_RX_RSSI_TH_SHORT));
                } else {
                    mt6626_write1(FM_DSP_PATCH_DATA, img[2+i]);
                }
            }
        } else {
            for (i = 0; i < data_len; i++) {
                if (i == 86) {
                    mt6626_write1(FM_DSP_PATCH_DATA, fm_cust_config_fetch(FM_CFG_RX_RSSI_TH_LONG));
                } else if (i == 505) {
                    mt6626_write1(FM_DSP_PATCH_DATA, fm_cust_config_fetch(FM_CFG_RX_RSSI_TH_SHORT));
                } else {
                    mt6626_write1(FM_DSP_PATCH_DATA, img[2+i]);
                }
            }
        }

        break;
    default:
        break;
    }

    WCN_DBG(FM_DBG | CHIP, "down load DSP patch %d ok\n", type);
    return 0;
}

static fm_s32 mt6626_PowerUp(fm_u16 *chip_id, fm_u16 *device_id)
{
    fm_s32 ret = 0;
    fm_s32 i;
    fm_u16 tmp_reg, cnt = 0;

    const fm_u16 *bin_patch = NULL;
    const fm_u16 *bin_coeff = NULL;

    FMR_ASSERT(chip_id);
    FMR_ASSERT(device_id);

    WCN_DBG(FM_DBG | CHIP, "pwr on seq\n");

    // mt6626 FM power on sequence
    for (i = 0; i < POWER_ON_COMMAND_COUNT; i++) {
        switch (PowerOnSetting[i].addr) {
        case FM_PUS_DSPPATCH:

            switch (PowerOnSetting[i].or) {
            case DSP_PATH:  //DSP path download
                mt6626_DspPatch(bin_patch, IMG_PATCH);
                break;
            case DSP_COEFF:  //DSP coefficient download
                mt6626_DspPatch(bin_coeff, IMG_COEFFICIENT);
                break;
            case DSP_HW_COEFF:  //DSP HW coefficient download
                mt6626_DspPatch(bin_hw_coeff, IMG_HW_COEFFICIENT);
                break;
            default:
                break;
            }

            break;
        case FM_PUS_POLL_P:
            cnt = 0;

            do {
                mt6626_read((fm_u8)PowerOnSetting[i].and, &tmp_reg);
                tmp_reg &= PowerOnSetting[i].or;

                if (tmp_reg == 0) {
                    Delayms(10);
                    cnt++;
                }
            } while ((tmp_reg == 0) && (cnt < (MT6626_MAX_COUNT << 1)));

            if (cnt == (MT6626_MAX_COUNT << 1)) {
                WCN_DBG(FM_ALT | CHIP, "polling status Active failed:0x%02X\n", (fm_u8)PowerOnSetting[i].and);
                return -FM_EPARA;
            }

            break;
        case FM_PUS_POLL_N:
            cnt = 0;

            do {
                mt6626_read((fm_u8)PowerOnSetting[i].and, &tmp_reg);
                tmp_reg &= PowerOnSetting[i].or;

                if (tmp_reg != 0) {
                    Delayms(10);
                    cnt++;
                }
            } while ((tmp_reg != 0) && (cnt < MT6626_MAX_COUNT));

            if (cnt == MT6626_MAX_COUNT) {
                WCN_DBG(FM_ALT | CHIP, "polling status Negative failed:0x%02X\n", (fm_u8)PowerOnSetting[i].and);
                return -FM_EPARA;
            }

            break;
        case FM_PUS_USDELAY:
            Delayus(PowerOnSetting[i].or);
            break;
        case FM_PUS_MSDELAY:
            Delayms(PowerOnSetting[i].or);
            break;
        case FM_PUS_HW_VER:

            switch (PowerOnSetting[i].and) {
            case 0x99:
                mt6626_read(0x99, &tmp_reg);

                switch (tmp_reg) {
                case 0x0:
                    Chip_Version = mt6626_E1;
                    bin_patch = bin_patch_E1;
                    bin_coeff = bin_coeff_E1;
                    break;
                case 0x8A01:
                default:
                    Chip_Version = mt6626_E2;
                    bin_patch = bin_patch_E2;
                    bin_coeff = bin_coeff_E2;
                    break;
                }

                break;
            case 0x62:
                mt6626_read((fm_u8)PowerOnSetting[i].and, &tmp_reg);
                //record chip id & device id
                *chip_id = tmp_reg;
                *device_id = tmp_reg;
                WCN_DBG(FM_NTC | CHIP, "chip_id:0x%04x\n", tmp_reg);
                break;
            case 0x1C:
                mt6626_read((fm_u8)PowerOnSetting[i].and, &tmp_reg);

                if (PowerOnSetting[i].or) {
                    mt6626_write(PowerOnSetting[i].and, (tmp_reg | 0x8000));
                } else {
                    mt6626_write(PowerOnSetting[i].and, (tmp_reg&0x7FFF));
                }

                break;
            default:
                break;
            }

            break;
        default:

            if (PowerOnSetting[i].and != 0) {
                if (mt6626_read((fm_u8)PowerOnSetting[i].addr, &tmp_reg)) {
                    WCN_DBG(FM_ALT | CHIP, "power up failed, can't read reg %02X\n", (fm_u8)PowerOnSetting[i].and);
                    return -FM_EPARA;
                }

                tmp_reg &= PowerOnSetting[i].and;
                tmp_reg |= PowerOnSetting[i].or;
            } else {
                tmp_reg = PowerOnSetting[i].or;
            }

            if (mt6626_write((fm_u8)PowerOnSetting[i].addr, tmp_reg)) {
                WCN_DBG(FM_ALT | CHIP, "power up failed, can't write reg %02X\n", (fm_u8)PowerOnSetting[i].addr);
                return -FM_EPARA;
            }

            break;
        }
    }

    WCN_DBG(FM_DBG | CHIP, "pwr on seq done\n");
    return ret;
}

static fm_s32 mt6626_PowerDown(void)
{
    fm_s32 ret = 0;
    fm_s16 i;
    fm_u16 dataRead;

    /*SW work around for MCUFA issue.
     *if interrupt happen before doing rampdown, DSP can't switch MCUFA back well.
     * In case read interrupt, and clean if interrupt found before rampdown.
     */
    WCN_DBG(FM_DBG | CHIP, "pwr down seq\n");
    mt6626_read(FM_MAIN_INTR, &dataRead);

    if (dataRead & 0x1) {
        mt6626_write(FM_MAIN_INTR, dataRead);//clear status flag
    }

    mt6626_RampDown();

    mt6626_write(0x60, 0x330F);
    mt6626_write(FM_MAIN_CG2_CTRL, 1);

    for (i = 0; i < 4; i++) {
        mt6626_read(0x6E, &dataRead);
        mt6626_write(0x6E, (dataRead&0xFFF8));
    }

    mt6626_write(FM_MAIN_CG1_CTRL, 0);
    mt6626_write(FM_MAIN_CG1_CTRL, 0x4000);
    mt6626_write(FM_MAIN_CG1_CTRL, 0);

    return ret;
}

static fm_bool mt6626_SetFreq(fm_u16 freq)
{
    fm_u32 CHAN = 0x0000;
    fm_u16 dataRead, cnt = 0, tempbuff = 0;

Rampdown_again:
    mt6626_RampDown();

    fm_cb_op->cur_freq_set(freq);
    CHAN = (freq - 640) << 1;
    mt6626_read(FM_CHANNEL_SET, &dataRead);

    switch (Chip_Version) {
    case mt6626_E1:

        if (((fm_u8)((dataRead & 0x1000) >> 12)) ^(channel_parameter[freq - 760] & 0x1)) {
            mt6626_read(0x61, &tempbuff);
            mt6626_write(0x60, 0x330F);
            mt6626_write(0x61, 1);
            mt6626_write(0x6e, 0x0);
            mt6626_write(0x6e, 0x0);
            mt6626_write(0x6e, 0x0);
            mt6626_write(0x6e, 0x0);
            mt6626_write(0x60, 0x0);
            mt6626_write(0x60, 0x4000);
            mt6626_write(0x60, 0x0);
            mt6626_write(0x60, 0x3000);
            mt6626_write(0x60, 0x3001);
            Delayms(3);
            mt6626_write(0x60, 0x3003);
            mt6626_write(0x60, 0x3007);
            mt6626_write(0x60, 0x300f);
            mt6626_write(0x61, tempbuff | 0x3);
            mt6626_write(0x61, tempbuff | 0x2);
            mt6626_write(0x6A, 0x20);
            mt6626_write(0x6B, 0x20);
            Delayms(200);
        }

        break;
    case mt6626_E2:
        break;
    default:
        break;

    }

    mt6626_writeFA(&dataRead, (channel_parameter[freq - 760]));
    mt6626_write(FM_CHANNEL_SET, (dataRead&0xFC00) | CHAN);

    mt6626_TUNE_ON();

    if (mt6626_WaitSTCDone(5000, 15)) {
        if (cnt++ > 100) {
            WCN_DBG(FM_ALT | CHIP, "set freq failed\n");
            return FALSE;
        } else {
            WCN_DBG(FM_WAR | CHIP, "set freq retry, cnt=%d\n", cnt);
            goto Rampdown_again;
        }
    }

    mt6626_ClearSTCDone();//clear status flag

    WCN_DBG(FM_DBG | CHIP, "set freq to %d ok\n", freq);
    return TRUE;
}

/*
* mt6626_Seek
* pFreq: IN/OUT parm, IN start freq/OUT seek valid freq
* return fm_true:seek success; fm_false:seek failed
*/
static fm_bool mt6626_Seek(fm_u16 min_freq, fm_u16 max_freq, fm_u16 *pFreq, fm_u16 seekdir, fm_u16 space)
{
    fm_u16 dataRead;
    fm_u16 freq_l;
    fm_u16 freq_h;

    mt6626_RampDown();
    mt6626_Mute(fm_true);

    WCN_DBG(FM_DBG | CHIP, "min_freq:%d, max_freq:%d\n", min_freq, max_freq);

    //Program seek direction
    mt6626_read(FM_MAIN_CFG1, &dataRead);
    dataRead &= 0xFBFF;

    if (seekdir == 0) {
        dataRead |= 0x0000;
    } else {
        dataRead |= 0x0400;
    }

    WCN_DBG(FM_DBG | CHIP, "seek %s\n", seekdir ? "down" : "up");
    //Program scan channel spacing
    dataRead &= 0x8FFF;

    if (space == 4) {
        dataRead |= 0x4000;
    } else {
        dataRead |= 0x2000;
    }

    WCN_DBG(FM_DBG | CHIP, "seek space %d\n", space);
    //enable wrap , if it is not auto scan function, 0x66[11] 0=no wrarp, 1=wrap
    dataRead &= 0xF7FF;
    dataRead |= 0x0800;
    //0x66[9:0] freq upper bound
    max_freq = (max_freq - 640) * 2;
    dataRead &= 0xFC00;
    dataRead |= max_freq;
    mt6626_write(FM_MAIN_CFG1, dataRead);
    //0x67[9:0] freq lower bound
    mt6626_read(FM_MAIN_CFG2, &dataRead);
    min_freq = (min_freq - 640) * 2;
    dataRead &= 0xFC00;
    dataRead |= min_freq;
    mt6626_write(FM_MAIN_CFG2, dataRead);
    //Enable STC done intr
    mt6626_set_bits(FM_MAIN_EXTINTRMASK, FM_EXT_STC_DONE_MASK, 0xFFFE);
    mt6626_SEEK_ON();

    if (fm_wait_stc_done(MT6626_FM_STC_DONE_TIMEOUT) == fm_false) {
        WCN_DBG(FM_ALT | CHIP, "seek, get stc done failed\n");
        mt6626_set_bits(FM_MAIN_INTR, 0x0001, 0xFFFF);
        mt6626_RampDown();
        return fm_false;
    }

    //Disable STC done intr
    mt6626_set_bits(FM_MAIN_EXTINTRMASK, 0, 0xFFFE);
    //get the result freq
    mt6626_read(FM_MAIN_CHANDETSTAT, &dataRead);
    mt6626_write(FM_CHANNEL_SET, (dataRead&FM_HOST_CHAN) >> 4);
    *pFreq = 640 + ((dataRead & FM_MAIN_CHANDET_MASK) >> (FM_MAIN_CHANDET_SHIFT + 1));
    freq_l = fm_cust_config_fetch(FM_CFG_RX_BAND_FREQ_L);
    freq_h = fm_cust_config_fetch(FM_CFG_RX_BAND_FREQ_H);
    *pFreq = (*pFreq > freq_h) ? freq_h : *pFreq;
    *pFreq = (*pFreq < freq_l) ? freq_l : *pFreq;
    fm_cb_op->cur_freq_set(*pFreq);
    WCN_DBG(FM_NTC | CHIP, "seek, result freq:%d\n", *pFreq);
    mt6626_Mute(fm_false);

    return fm_true;
}

static fm_bool mt6626_Scan(
    fm_u16 min_freq, fm_u16 max_freq,
    fm_u16 *pFreq,
    fm_u16 *pScanTBL,
    fm_u16 *ScanTBLsize,
    fm_u16 scandir,
    fm_u16 space)
{
    fm_u16 tmp_reg, space_val, startfreq, offset = 0;
    fm_u16 tmp_scanTBLsize = *ScanTBLsize;
    fm_u16 dataRead;

    if ((!pScanTBL) || (tmp_scanTBLsize == 0)) {
        WCN_DBG(FM_ALT | CHIP, "scan, failed:invalid scan table\n");
        return fm_false;
    }

    WCN_DBG(FM_DBG | CHIP, "scan start freq: %d, max_freq:%d, min_freq:%d, scan BTL size:%d, scandir:%d, space:%d\n", *pFreq, max_freq, min_freq, *ScanTBLsize, scandir, space);

    if (tmp_scanTBLsize > MT6626_SCANTBL_SIZE) {
        tmp_scanTBLsize = MT6626_SCANTBL_SIZE;
    }

    if (space == MT6626_FM_SPACE_200K) {
        space_val = 2; //200K
    } else if (space == MT6626_FM_SPACE_100K) {
        space_val = 1;  //100K
    } else {
        space_val = 1;  //100K
    }

    //scan up
    if (scandir == MT6626_FM_SCAN_UP) {
        startfreq = min_freq - space_val;
    } else {
        startfreq = max_freq + space_val;//max_freq compare need or not
    }

    mt6626_RampDown();
    mt6626_Mute(fm_true);

    //set freq
    if (fm_false == mt6626_SetFreq(startfreq)) {
        WCN_DBG(FM_ALT | CHIP, "scan, failed set freq\n");
        return fm_false;
    }

    mt6626_RampDown();

    //set space(100k/200k)and band(min_freq~max_freq) and up/down and disable wrap
    mt6626_read(FM_MAIN_CFG2, &dataRead);
    mt6626_write(FM_MAIN_CFG2, (dataRead&0xFC00) | ((min_freq - 640) << 1));//set space(100k/200k)and band(875~1080)and up/down
    mt6626_read(FM_MAIN_CFG1, &dataRead);
    mt6626_write(FM_MAIN_CFG1, (dataRead&0x8800) | (scandir << 10) | (1 << (12 + space)) | ((max_freq - 640) << 1));//set space(100k/200k)and band(875~1080)and up/down
    mt6626_read(FM_MAIN_CFG1, &dataRead);
    mt6626_write(FM_MAIN_CFG1, dataRead&0xF7FF); //disable wrap , if it is auto scan function

    //Enable STC done intr
    mt6626_set_bits(FM_MAIN_EXTINTRMASK, FM_EXT_STC_DONE_MASK, 0xFFFE);
    //scan on
    mt6626_SCAN_ON();

    if (fm_wait_stc_done(MT6626_FM_STC_DONE_TIMEOUT) == fm_false) {
        WCN_DBG(FM_ALT | CHIP, "scan, get stc done failed\n");
        mt6626_set_bits(FM_MAIN_INTR, 0x0001, 0xFFFF);
        mt6626_RampDown();

        //get the valid freq after scan
        mt6626_read(FM_MAIN_CHANDETSTAT, &tmp_reg);
        tmp_reg = 640 + ((tmp_reg & FM_MAIN_CHANDET_MASK) >> (FM_MAIN_CHANDET_SHIFT + 1));
        *pFreq = tmp_reg;
        WCN_DBG(FM_DBG | CHIP, "scan, failed freq:%d\n", *pFreq);
        return fm_false;
    }

    //Disable STC done intr
    mt6626_set_bits(FM_MAIN_EXTINTRMASK, 0, 0xFFFE);

    //get scan Table
    WCN_DBG(FM_DBG | CHIP, "mt6626_Scan tbl:");

    for (offset = 0; offset < tmp_scanTBLsize; offset++) {
        mt6626_read(FM_RDS_DATA_REG, &tmp_reg);
        *(pScanTBL + offset) = tmp_reg;
    }

    *ScanTBLsize = tmp_scanTBLsize;

    //get the valid freq after scan
    mt6626_read(FM_MAIN_CHANDETSTAT, &tmp_reg);
    tmp_reg = 640 + ((tmp_reg & FM_MAIN_CHANDET_MASK) >> (FM_MAIN_CHANDET_SHIFT + 1));
    *pFreq = tmp_reg;
    WCN_DBG(FM_DBG | CHIP, "scan, after scan freq:%d\n", *pFreq);
    mt6626_Mute(fm_false);

    return fm_true;
}

/*
 * mt6626_GetCurRSSI - get current freq's RSSI value
 * RS=RSSI
 * If RS>511, then RSSI(dBm)= (RS-1024)/16*6
 *				   else RSSI(dBm)= RS/16*6
 */
static fm_s32 mt6626_GetCurRSSI(fm_s32 *pRSSI)
{
    fm_u16 tmp_reg;

    mt6626_read(FM_RSSI_IND, &tmp_reg);
    tmp_reg = tmp_reg & 0x03ff;

    if (pRSSI) {
        *pRSSI = (tmp_reg > 511) ? (((tmp_reg - 1024) * 6) >> 4) : ((tmp_reg * 6) >> 4);
        WCN_DBG(FM_DBG | CHIP, "rssi:%d, dBm:%d\n", tmp_reg, *pRSSI);
    } else {
        WCN_DBG(FM_ERR | CHIP, "get rssi para error\n");
        return -FM_EPARA;
    }

    return 0;
}

static fm_s32 mt6626_SetVol(fm_u8 vol)
{
#define MT6626_VOL_MAX   0x2B	// 43 volume(0-15)
    int ret = 0;
    fm_u8 tmp_vol = vol & 0x3f;
    fm_u16 tmp = 0;

    mt6626_read(0x60, &tmp);
    mt6626_write(0x60, tmp&0xFFF7); //0x60 D3=0

    tmp_vol = vol * 3;
    if (tmp_vol > MT6626_VOL_MAX)
        tmp_vol = MT6626_VOL_MAX;

    ret = mt6626_set_bits(0x9C, (tmp_vol << 8), 0xC0FF);

    if (ret) {
        WCN_DBG(FM_ERR | CHIP, "Set vol=%d Failed\n", tmp_vol);
        return ret;
    } else {
        WCN_DBG(FM_DBG | CHIP, "Set vol=%d OK\n", tmp_vol);
    }

    mt6626_write(0x60, tmp); //0x60 D3=1
    return 0;
}

static fm_s32 mt6626_GetVol(fm_u8 *pVol)
{
    int ret = 0;
    fm_u16 tmp_reg;
    fm_u16 tmp = 0;

    FMR_ASSERT(pVol);

    mt6626_read(0x60, &tmp);
    mt6626_write(0x60, tmp&0xFFF7); //0x60 D3=0

    ret = mt6626_read(0x9C, &tmp_reg);

    if (ret) {
        *pVol = 0;
        WCN_DBG(FM_ERR | CHIP, "Get vol Failed\n");
        return ret;
    } else {
        *pVol = (tmp_reg >> 8) & 0x3f;
        WCN_DBG(FM_DBG | CHIP, "Get vol=%d OK\n", *pVol);
    }

    mt6626_write(0x60, tmp); //0x60 D3=1
    return 0;
}

static fm_s32 mt6626_dump_reg(void)
{
    return 0;
}

static fm_bool mt6626_GetMonoStereo(fm_u16 *pMonoStereo)
{
#define FM_BF_STEREO 0x1000
    fm_u16 TmpReg;

    if (pMonoStereo) {
        mt6626_read(FM_RSSI_IND, &TmpReg);
        *pMonoStereo = (TmpReg & FM_BF_STEREO) >> 12;
    } else {
        WCN_DBG(FM_ERR | CHIP, "MonoStero: para err\n");
        return fm_false;
    }

    WCN_DBG(FM_DBG | CHIP, "MonoStero:0x%04x\n", *pMonoStereo);
    return fm_true;
}

static fm_s32 mt6626_SetMonoStereo(fm_s32 MonoStereo)
{
    fm_s32 ret = 0;
#define FM_FORCE_MS 0x0008

    WCN_DBG(FM_DBG | CHIP, "set to %s\n", MonoStereo ? "mono" : "auto");

    mt6626_write(0x60, 0x3007);

    if (MonoStereo) {
        ret = mt6626_set_bits(0x75, FM_FORCE_MS, ~FM_FORCE_MS);
    } else {
        ret = mt6626_set_bits(0x75, 0x0000, ~FM_FORCE_MS);
    }

    return ret;
}

static fm_s32 mt6626_GetCapArray(fm_s32 *ca)
{
    fm_u16 dataRead;
    fm_u16 tmp = 0;

    FMR_ASSERT(ca);
    mt6626_read(0x60, &tmp);
    mt6626_write(0x60, tmp&0xFFF7); //0x60 D3=0

    mt6626_read(0x25, &dataRead);
    *ca = dataRead;

    mt6626_write(0x60, tmp); //0x60 D3=1
    return 0;
}


/*
 * mt6626_GetCurPamd - get current freq's PAMD value
 * PA=PAMD
 * If PA>511 then PAMD(dB)=  (PA-1024)/16*6,
 *				else PAMD(dB)=PA/16*6
 */
static fm_bool mt6626_GetCurPamd(fm_u16 *pPamdLevl)
{
    fm_u16 tmp_reg;
    fm_u16 dBvalue;

    if (mt6626_read(FM_ADDR_PAMD, &tmp_reg))
        return fm_false;

    tmp_reg &= 0x03FF;
    dBvalue = (tmp_reg > 511) ? ((1024 - tmp_reg) * 6 / 16) : 0;

    *pPamdLevl = dBvalue;
    return fm_true;
}

static fm_s32 mt6626_ScanStop(void)
{
    return fm_force_active_event(FLAG_SCAN);
}

static fm_s32 mt6626_SeekStop(void)
{
    return fm_force_active_event(FLAG_SEEK);
}

/*
 * mt6626_I2s_Setting - set the I2S state on MT6626
 * @onoff - I2S on/off
 * @mode - I2S mode: Master or Slave
 *
 * Return:0, if success; error code, if failed
 */
static fm_s32 mt6626_I2s_Setting(fm_s32 onoff, fm_s32 mode, fm_s32 sample)
{
    fm_u16 tmp_state = 0;
    fm_u16 tmp_mode = 0;
    fm_u16 tmp_sample = 0;
    fm_s32 ret = 0;

    if (onoff == MT6626_I2S_ON) {
        tmp_state = 0x0080; //I2S Frequency tracking on, 0x61 D7=1
    } else if (onoff == MT6626_I2S_OFF) {
        tmp_state = 0x0000; //I2S Frequency tracking off, 0x61 D7=0
    } else {
        WCN_DBG(FM_ERR | CHIP, "%s():[onoff=%d]\n", __func__, onoff);
        ret = -FM_EPARA;
        goto out;
    }

    if (mode == MT6626_I2S_MASTER) {
        tmp_mode = 0x03; //6620 as I2S master
    } else if (mode == MT6626_I2S_SLAVE) {
        tmp_mode = 0x0B; //6620 as I2S slave
    } else {
        WCN_DBG(FM_ERR | CHIP, "%s():[mode=%d]\n", __func__, mode);
        ret = -FM_EPARA;
        goto out;
    }

    if (sample == MT6626_I2S_32K) {
        tmp_sample = 0x0000; //6620 I2S 32KHz sample rate
    } else if (sample == MT6626_I2S_44K) {
        tmp_sample = 0x0800; //6620 I2S 44.1KHz sample rate
    } else if (sample == MT6626_I2S_48K) {
        tmp_sample = 0x1000; //6620 I2S 48KHz sample rate
    } else {
        WCN_DBG(FM_ERR | CHIP, "%s():[sample=%d]\n", __func__, sample);
        ret = -FM_EPARA;
        goto out;
    }

    if ((ret = mt6626_set_bits(0x5F, tmp_sample, 0xE7FF)))
        goto out;

    if ((ret = mt6626_write(0x9B, tmp_mode)))
        goto out;

    if ((ret = mt6626_set_bits(0x61, tmp_state, 0xFF7F)))
        goto out;

    WCN_DBG(FM_NTC | CHIP, "[onoff=%s][mode=%s][sample=%d](0)33KHz,(1)44.1KHz,(2)48KHz\n",
            (onoff == MT6626_I2S_ON) ? "On" : "Off",
            (mode == MT6626_I2S_MASTER) ? "Master" : "Slave",
            sample);
out:
    return ret;
}

static fm_bool mt6626_em_test(fm_u16 group_idx, fm_u16 item_idx, fm_u32 item_value)
{
    return fm_true;
}

static fm_s32 fm_low_power_wa_default(fm_s32 fmon)
{
    return 0;
}

fm_s32 fm_low_ops_register(struct fm_lowlevel_ops *ops)
{
    fm_s32 ret = 0;
    //Basic functions.

    FMR_ASSERT(ops);
    FMR_ASSERT(ops->cb.cur_freq_get);
    FMR_ASSERT(ops->cb.cur_freq_set);
    fm_cb_op = &ops->cb;

    //ops->bi.low_pwr_wa = mt6626_low_pwr_wa;
    ops->bi.low_pwr_wa = fm_low_power_wa_default;
    ops->bi.pwron = mt6626_pwron;
    ops->bi.pwroff = mt6626_pwroff;
    ops->bi.msdelay = Delayms;
    ops->bi.usdelay = Delayus;
    ops->bi.read = mt6626_read;
    ops->bi.write = mt6626_write;
    ops->bi.setbits = mt6626_set_bits;
    ops->bi.chipid_get = mt6626_get_chipid;
    ops->bi.mute = mt6626_Mute;
    ops->bi.rampdown = mt6626_RampDown;
    ops->bi.pwrupseq = mt6626_PowerUp;
    ops->bi.pwrdownseq = mt6626_PowerDown;
    ops->bi.setfreq = mt6626_SetFreq;
    ops->bi.seek = mt6626_Seek;
    ops->bi.seekstop = mt6626_SeekStop;
    ops->bi.scan = mt6626_Scan;
    ops->bi.scanstop = mt6626_ScanStop;
    ops->bi.rssiget = mt6626_GetCurRSSI;
    ops->bi.volset = mt6626_SetVol;
    ops->bi.volget = mt6626_GetVol;
    ops->bi.dumpreg = mt6626_dump_reg;
    ops->bi.msget = mt6626_GetMonoStereo;
    ops->bi.msset = mt6626_SetMonoStereo;
    ops->bi.pamdget = mt6626_GetCurPamd;
    ops->bi.em = mt6626_em_test;
    ops->bi.anaswitch = mt6626_SetAntennaType;
    ops->bi.anaget = mt6626_GetAntennaType;
    ops->bi.caparray_get = mt6626_GetCapArray;
    ops->bi.i2s_set = mt6626_I2s_Setting;

    return ret;
}

fm_s32 fm_low_ops_unregister(struct fm_lowlevel_ops *ops)
{
    fm_s32 ret = 0;
    //Basic functions.

    FMR_ASSERT(ops);

    fm_memset(&ops->bi, 0, sizeof(struct fm_basic_interface));
    return ret;
}

