#include <linux/semaphore.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "stp_exp.h"
#include "wmt_exp.h"

#include "fm_typedef.h"
#include "fm_dbg.h"
#include "fm_err.h"
#include "fm_interface.h"
#include "fm_stdlib.h"
#include "fm_utils.h"
#include "fm_patch.h"
#include "fm_link.h"
#include "fm_config.h"
#include "fm_private.h"

#include "mt6620_fm_reg.h"
#include "mt6620_fm.h"
//#include "mt6620_drv_dsp.h"
//#include "mt6620_fm_link.h"
#include "mt6620_fm_lib.h"
#include "mt6620_fm_cmd.h"
#include "mt6620_fm_cust_cfg.h"
#ifdef MTK_FM_50KHZ_SUPPORT
static struct fm_fifo *cqi_fifo = NULL;
#endif
extern fm_cust_cfg mt6620_fm_config;
static struct fm_hw_info mt6620_hw_info = {
    .chip_id = 0x00006620,
    .eco_ver = 0x00000000,
    .rom_ver = 0x00000002,
    .patch_ver = 0x00000111,
    .reserve = 0x00000000,
};
static struct fm_i2s_info mt6620_i2s_inf = {
    .status = 0,    //i2s off
    .mode = 0,      //slave mode
    .rate = 48000,  //48000 sample rate
};

fm_s32 MT6620_HL_Side_Adj(fm_u16 freq, fm_s32 *hl);
fm_s32 MT6620_ADPLL_Freq_Avoid(fm_u16 freq, fm_s32 *freqavoid);
fm_s32 MT6620_MCU_Freq_Avoid(fm_u16 freq, fm_s32 *freqavoid);
fm_s32 MT6620_ADPLL_Power_OnOff(fm_s32 onoff, fm_s32 ADPLL_clk);
fm_s32 MT6620_TX_PWR_CTRL(fm_u16 freq, fm_s32 *ctr);
fm_s32 MT6620_RTC_Drift_CTRL(fm_u16 freq, fm_s32 *ctr);
fm_s32 MT6620_TX_DESENSE(fm_u16 freq, fm_s32 *ctr);

static fm_s32 mt6620_I2s_Setting(fm_s32 onoff, fm_s32 mode, fm_s32 sample);
static fm_s32 mt6620_desense_check(fm_u16 freq,fm_s32 rssi);
static fm_s32 MT6620_Rds_Tx(fm_u16 pi, fm_u16 *ps, fm_u16 *other_rds, fm_u8 other_rds_cnt);
static fm_s32 MT6620_Rds_Tx_Enable(void);
static fm_s32 MT6620_Rds_Tx_Disable(void);

static fm_u8 *cmd_buf = NULL;
static struct fm_lock *cmd_buf_lock = NULL;
static struct fm_callback *fm_cb_op;
//static struct MT6620fm_priv priv_adv_6620;
static ENUM_WMTHWVER_TYPE_T hw_ver=WMTHWVER_MT6620_E3;
static fm_s32 mt6620_pwron(fm_s32 data)
{
    if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_on(WMTDRV_TYPE_FM)) {
        WCN_DBG(FM_ALT | CHIP, "WMT turn on FM Fail!\n");
        return -FM_ELINK;
    } else {
		 /* GeorgeKuo: turn on function before check stp ready */
		if(fm_false == mtk_wcn_stp_is_ready())
		{
			WCN_DBG(FM_ALT | MAIN,"6620 stp is not ready, please retry later\n");
			return -FM_ELINK;
		}
		hw_ver = mtk_wcn_wmt_hwver_get();
        WCN_DBG(FM_ALT | CHIP, "WMT turn on FM OK!\n");
        return 0;
    }
}


static fm_s32 mt6620_pwroff(fm_s32 data)
{
    if (MTK_WCN_BOOL_FALSE == mtk_wcn_wmt_func_off(WMTDRV_TYPE_FM)) {
        WCN_DBG(FM_ALT | CHIP, "WMT turn off FM Fail!\n");
        return -FM_ELINK;
    } else {
        WCN_DBG(FM_NTC | CHIP, "WMT turn off FM OK!\n");
        return 0;
    }
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

static struct fm_res_ctx *res = NULL;

fm_s32 mt6620_get_read_result(struct fm_res_ctx* result)
{
    FMR_ASSERT(result);
    res = result;

    return 0;
}

static fm_s32 mt6620_read(fm_u8 addr, fm_u16 *val)
{
    fm_s32 ret = 0;
    fm_u16 pkt_size;

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_get_reg(cmd_buf, TX_BUF_SIZE, addr);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_FSPI_RD, SW_RETRY_CNT, FSPI_RD_TIMEOUT, mt6620_get_read_result);

    if (!ret && res) {
        *val = res->fspi_rd;
    }

    FM_UNLOCK(cmd_buf_lock);

    return ret;
}

static fm_s32 mt6620_write(fm_u8 addr, fm_u16 val)
{
    fm_s32 ret = 0;
    fm_u16 pkt_size;

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_set_reg(cmd_buf, TX_BUF_SIZE, addr, val);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_FSPI_WR, SW_RETRY_CNT, FSPI_WR_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);

    return ret;
}

static fm_s32 mt6620_set_bits(fm_u8 addr, fm_u16 bits, fm_u16 mask)
{
    fm_s32 ret = 0;
    fm_u16 val;

    ret = mt6620_read(addr, &val);

    if (ret)
        return ret;

    val = ((val & (mask)) | bits);
    ret = mt6620_write(addr, val);

    return ret;
}

static fm_u16 mt6620_get_chipid(void)
{
    return 0x6620;
}

/*  mt6620_SetAntennaType - set Antenna type
 *  @type - 1,Short Antenna;  0, Long Antenna
 */
static fm_s32 mt6620_SetAntennaType(fm_s32 type)
{
    WCN_DBG(FM_DBG | CHIP, "set ana to %s\n", type ? "short" : "long");

    if (type == FM_ANA_LONG) {
        //Long Antenna RSSI threshold, 0xE0 D0~D9
        mt6620_write(0xE0, ((0xA301 & 0xFC00) | (FMR_RSSI_TH_LONG_MT6620 & 0x03FF)));
        //Turn on Short Antenna LNA and Off TR Switch
        mt6620_write(0x04, 0x0142);
        //Turn off the Short Antenna Capbank biasing
        mt6620_write(0x05, 0x00E7);
        //Turn off the Short Antenna Capbank biasing
        mt6620_write(0x26, 0x0004);
        //Disable concurrent calibration for VCO and SCAL
        mt6620_write(0x2E, 0x0008);
    } else if (type == FM_ANA_SHORT) {
        //Short Antenna RSSI threshold, 0xE0 D0~D9
        mt6620_write(0xE0, ((0xA2E0 & 0xFC00) | (FMR_RSSI_TH_SHORT_MT6620 & 0x03FF)));
        //Turn on Short Antenna LNA and TR Switch
        mt6620_write(0x04, 0x0145);
        //Turn on the Short Antenna Capbank biasing
        mt6620_write(0x05, 0x00FF);
        //Turn on the Short Antenna Capbank biasing
        mt6620_write(0x26, 0x0024);
        //Enable concurrent calibration for VCO and SCAL
        mt6620_write(0x2E, 0x0000);
    } else {
        WCN_DBG(FM_ERR | CHIP, "%s()\n", __func__);
        return -FM_EPARA;
    }

    return 0;
}

static fm_s32 mt6620_GetAntennaType(void)
{
    fm_u16 dataRead;

    mt6620_read(0x2E, &dataRead);

    if (dataRead == 0x0000)
        return FM_ANA_SHORT; //short antenna
    else
        return FM_ANA_LONG; //long antenna
}

static fm_s32 mt6620_Mute(fm_bool mute)
{
    WCN_DBG(FM_DBG | CHIP, "set %s\n", mute ? "mute" : "unmute");

    if (mute) {
        return mt6620_set_bits(0x9C, 0x0008, 0xFFF7); //1:9C D3 = 1
    } else {
        return mt6620_set_bits(0x9c, 0x0000, 0xFFF7); //1:9C D3 = 0
    }
}

static fm_s32 mt6620_RampDown(void)
{
    fm_s32 ret = 0;
    fm_u16 pkt_size;

    WCN_DBG(FM_NTC | CHIP, "ramp down\n");

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_rampdown(cmd_buf, TX_BUF_SIZE);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_RAMPDOWN, SW_RETRY_CNT, RAMPDOWN_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ERR | CHIP, "ramp down failed\n");
    }

    return ret;
}
#if 0//ramp down tx will do in tx tune  flow
static fm_s32 MT6620_RampDown_Tx(void)
{
    fm_s32 ret = 0;
    fm_u16 pkt_size = 0;

    WCN_DBG(FM_NTC | CHIP, "ramp down TX\n");
    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    
    pkt_size = mt6620_rampdown_tx(cmd_buf, TX_BUF_SIZE);
    ret = fm_cmd_tx(cmd_buf, pkt_size,FLAG_RAMPDOWN, SW_RETRY_CNT, RAMPDOWN_TIMEOUT,NULL);
    
    FM_UNLOCK(cmd_buf_lock);
    if (ret) {
        WCN_DBG(FM_ERR | CHIP, "ramp down TX failed\n");
    }
    return ret;
}
#endif
static fm_s32 mt6620_PowerUp(fm_u16 *chip_id, fm_u16 *device_id)
{
    fm_s32 ret = 0;
    fm_u16 pkt_size;

    FMR_ASSERT(chip_id);
    FMR_ASSERT(device_id);

    WCN_DBG(FM_DBG | CHIP, "pwr on seq......\n");

    //Wholechip FM Power Up: step 1, mt6620_off_2_longANA
    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_off_2_longANA_1(cmd_buf, TX_BUF_SIZE);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_off_2_longANA_1 failed\n");
        return ret;
    }

    Delayms(100);

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_off_2_longANA_2(cmd_buf, TX_BUF_SIZE);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_off_2_longANA_2 failed\n");
        return ret;
    }

    Delayms(50);

   // *chip_id = 0x6620;
   // *device_id = 0x6620;
   // WCN_DBG(FM_NTC | CHIP, "chip_id:0x%04x\n", 0x6620);

    //Wholechip FM Power Up: step 2, FM Digital Init: fm_rgf_maincon
    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_pwrup_digital_init_1(cmd_buf, TX_BUF_SIZE);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_pwrup_digital_init_2 failed\n");
        return ret;;
    }

    Delayms(10);

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_pwrup_digital_init_2(cmd_buf, TX_BUF_SIZE);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_pwrup_digital_init_2 failed\n");
        return ret;;
    }

    Delayms(10);

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_pwrup_digital_init_3(cmd_buf, TX_BUF_SIZE);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_pwrup_digital_init_3 failed\n");
        return ret;;
    }
#ifdef FM_DIGITAL_INPUT
  #ifdef MT6573 //for MT6573
		if(get_chip_eco_ver() == CHIP_E1){
			ret = mt6620_I2s_Setting(MT6620_I2S_ON, MT6620_I2S_MASTER, MT6620_I2S_48K);
		}else{
			ret = mt6620_I2s_Setting(MT6620_I2S_ON, MT6620_I2S_SLAVE, MT6620_I2S_48K);
		}
  #else
		ret = mt6620_I2s_Setting(MT6620_I2S_ON, MT6620_I2S_SLAVE, MT6620_I2S_48K);
  #endif
		if(ret){
			WCN_DBG(FM_ERR |CHIP,"pwron set I2S on error\n");
			return ret;
		}
		//we will disable 6620 fm chip analog output when use I2S path, set 0x3A bit2 = 0
		//mt6620_set_bits(0x3A, 0, MASK(2));
		WCN_DBG(FM_NTC |CHIP,"pwron set I2S on ok\n");
#endif

    mt6620_hw_info.eco_ver = (fm_s32)mtk_wcn_wmt_hwver_get();
    WCN_DBG(FM_DBG | CHIP, "pwr on seq ok\n");
    return ret;
}
static fm_s32 mt6620_PowerUpTx(void)
{
    fm_s32 ret = 0;
    fm_u16 pkt_size;
    fm_u16 dataRead;

    WCN_DBG(FM_DBG | CHIP, "pwr on Tx seq......\n");

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_off_2_tx_shortANA(cmd_buf, TX_BUF_SIZE);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_off_2_tx_shortANA failed\n");
        return ret;
    }


    mt6620_read(0x62, &dataRead);
	WCN_DBG(FM_NTC | CHIP, "Tx on chipid=%x\n",dataRead);
    
    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_dig_init(cmd_buf, TX_BUF_SIZE);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_dig_init failed\n");
        return ret;
    }
    //get temprature
    if(mtk_wcn_wmt_therm_ctrl(WMTTHERM_ENABLE) != fm_true)
    {
		WCN_DBG(FM_ERR | MAIN, "wmt_therm_ctrl, WMTTHERM_ENABLE failed\n");
       ret = -FM_ELINK;
       return ret;
    }
#ifdef FM_DIGITAL_INPUT
		ret = mt6620_I2s_Setting(MT6620_I2S_ON, MT6620_I2S_SLAVE, MT6620_I2S_48K);
		if(ret){
			WCN_DBG(FM_ERR |CHIP,"pwron tx set I2S on error\n");
			return ret;
		}
		//we will disable 6620 fm chip analog output when use I2S path, set 0x3A bit2 = 0
		//mt6620_set_bits(0x3A, 0, MASK(2));
		WCN_DBG(FM_NTC |CHIP,"pwron set I2S on ok\n");
#endif

    WCN_DBG(FM_DBG | CHIP, "pwr on tx seq ok\n");
    return ret;
}

static fm_s32 mt6620_PowerDown(void)
{
    fm_s32 ret = 0;
    fm_u16 pkt_size;

    WCN_DBG(FM_DBG | CHIP, "pwr down seq\n");

   // mt6620_RampDown();

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_pwrdown(cmd_buf, TX_BUF_SIZE);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_EN, SW_RETRY_CNT, EN_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_pwrdown failed\n");
        return ret;
    }

    return ret;
}

static fm_s32 mt6620_PowerDownTx(void)
{
    fm_s32 ret = 0;

   if(mtk_wcn_wmt_therm_ctrl(WMTTHERM_DISABLE) != fm_true)
   {
	   WCN_DBG(FM_ERR | MAIN, "wmt_therm_ctrl, WMTTHERM_DISABLE failed\n");
	   ret = -FM_ELINK;
   }

    return ret;
}

static fm_bool MT6620_SetFreq_Tx(fm_u16 freq)
{
    fm_s32 ret = 0;
    fm_u16 pkt_size;

    fm_cb_op->cur_freq_set(freq);
    //start tune
    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_tune_tx(cmd_buf, TX_BUF_SIZE, freq);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_TUNE, SW_RETRY_CNT, TUNE_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_tune_tx failed\n");
        return ret;;
    }

  	Delayms(125);

    WCN_DBG(FM_DBG | CHIP, "mt6620_tune_tx to %d ok\n", freq);

    return fm_true;
}

/*
 * fm_print_curCQI -- print cur freq's CQI
 * @cur_freq, current frequency
 * If OK, return 0, else error code
 */
static fm_s32 mt6620_print_curCQI(fm_u16 cur_freq)
{
    fm_s32 ret = 0;
    fm_u16 rssi = 0;
    fm_u16 pamd = 0;
    fm_u16 mr = 0;

    if((ret = mt6620_write(FM_MAIN_PGSEL, FM_PG0)))
        return ret;
    if((ret = mt6620_read(FM_RSSI_IND, &rssi)))
        return ret;
    if((ret = mt6620_read(FM_ADDR_PAMD, &pamd)))
        return ret;
    if((ret = mt6620_read(FM_MR_IND, &mr)))
        return ret;
    
    WCN_DBG(FM_NTC |CHIP,"FREQ=%d, RSSI=0x%04x, PAMD=0x%04x, MR=0x%04x\n", (fm_s32)cur_freq, rssi, pamd, mr);
    return ret;    
}

static fm_bool mt6620_SetFreq(fm_u16 freq)
{
    fm_s32 ret = 0;
    fm_s32 hl_side = -1;
    fm_s32 freq_avoid = -1;
    fm_u16 pkt_size;

    fm_cb_op->cur_freq_set(freq);

	if((ret = MT6620_HL_Side_Adj(freq, &hl_side)))
		return ret;
	WCN_DBG(FM_NTC |CHIP,"%s, [hl_side=%d]\n", __func__, hl_side);
	
	if((ret = MT6620_ADPLL_Freq_Avoid(freq, &freq_avoid)))
		return ret;
	WCN_DBG(FM_NTC |CHIP,"%s, adpll [freq_avoid=%d]\n", __func__, freq_avoid);
	
//	hw_ver = mtk_wcn_wmt_hwver_get();
	if(hw_ver >= WMTHWVER_MT6620_E3)
	{
		if((ret = MT6620_MCU_Freq_Avoid(freq, &freq_avoid)))
			return ret;
		WCN_DBG(FM_NTC |CHIP,"%s, mcu [freq_avoid=%d]\n", __func__, freq_avoid);
	}
	else
	{
		WCN_DBG(FM_NTC |CHIP,"%s, no need do mcu freq avoid[hw_ver=%d]\n", __func__, hw_ver);
	}
	
    //start tune
    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_tune_1(cmd_buf, TX_BUF_SIZE, freq);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_TUNE, SW_RETRY_CNT, TUNE_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_tune_1 failed\n");
        return ret;;
    }

    Delayms(200);

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_tune_2(cmd_buf, TX_BUF_SIZE, freq);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_TUNE, SW_RETRY_CNT, TUNE_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_tune_2 failed\n");
        return ret;;
    }

    Delayms(35);

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_tune_3(cmd_buf, TX_BUF_SIZE, freq);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_TUNE, SW_RETRY_CNT, TUNE_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_tune_3 failed\n");
        return ret;;
    }

    ret = mt6620_print_curCQI(freq);
    WCN_DBG(FM_DBG | CHIP, "set freq to %d ok\n", freq);

    return fm_true;
}


static fm_s32 mt6620_TxScan_SetFreq(fm_u16 freq)
{
    fm_s32 ret = 0;
    fm_u16 pkt_size = 0;

    WCN_DBG(FM_NTC | CHIP,"+%s():[freq=%d]\n", __func__, freq);
    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    
    pkt_size = mt6620_tune_txscan(cmd_buf, TX_BUF_SIZE, freq);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_TUNE, SW_RETRY_CNT, TUNE_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);
    
    FM_UNLOCK(cmd_buf_lock);
    WCN_DBG(FM_NTC | CHIP,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static fm_s32 mt6620_TxScan_GetCQI(fm_s16 *pRSSI, fm_s16 *pPAMD, fm_s16 *pMR)
{
    fm_s32 cnt = 0;
    fm_s32 ret = 0;
	fm_s16 tmp_reg = 0;
	fm_s16 aRSSI = 0;
	fm_s16 aPAMD = 0;
	fm_s16 aMR = 0;
	
    WCN_DBG(FM_NTC | CHIP,"+%s():\n", __func__);

	if((pRSSI == NULL) || (pPAMD == NULL) || (pMR == NULL)){
        WCN_DBG(FM_ERR | CHIP,"%s():para error, [pRSSI=%p],[aPAMD=%p],[pMR=%p]\n", 
            __func__, pRSSI, pPAMD, pMR);
        ret = -FM_EPARA;
		goto out;
	}
	
	for(cnt = 0; cnt < 8; cnt++)
	{
		Delayms(3);
		if((ret = mt6620_read(FM_RSSI_IND, &tmp_reg)))
            goto out;
		tmp_reg = tmp_reg&0x03ff;
		tmp_reg = (tmp_reg > 511) ? ((fm_s16)(tmp_reg-1024)) : tmp_reg;
		aRSSI += tmp_reg;
		
		if((ret = mt6620_read(FM_ADDR_PAMD, &tmp_reg)))
            goto out;
		tmp_reg = tmp_reg&0x00ff;
		tmp_reg = (tmp_reg > 127) ? ((fm_s16)(tmp_reg-256)) : tmp_reg;
		aPAMD += tmp_reg;
		
		if((ret = mt6620_read(FM_MR_IND, &tmp_reg)))
            goto out;
		tmp_reg = tmp_reg&0x01ff;
		tmp_reg = (tmp_reg > 255) ? ((fm_s16)(tmp_reg-512)) : tmp_reg;
		aMR += tmp_reg;
	}

	*pRSSI = aRSSI>>3;
	*pPAMD = aPAMD>>3;
	*pMR = aMR>>3;

    WCN_DBG(FM_NTC | CHIP,"%s():[RSSI=%d],[PAMD=%d],[MR=%d]\n", 
            __func__, *pRSSI, *pPAMD, *pMR);
    
out:
    WCN_DBG(FM_NTC | CHIP,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static fm_s32 mt6620_TxScan_IsEmptyChannel(fm_s16 RSSI, fm_s16 PAMD, fm_s16 MR, fm_s32 *empty)
{
    fm_s32 ret = 0;
    
    WCN_DBG(FM_NTC | CHIP,"+%s():[RSSI=%d],[PAMD=%d],[MR=%d]\n", __func__, RSSI, PAMD, MR);

    if(empty == NULL)
    {
        WCN_DBG(FM_NTC | CHIP,"invalid pointer [empty=0x%x]!\n", (fm_u32)empty);
        ret = -FM_EPARA;
        goto out;
    }

    *empty = fm_true; 
	if(RSSI > FM_TXSCAN_RSSI_TH){
	    *empty = fm_false;
        goto out;
	}

	if(PAMD < FM_TXSCAN_PAMD_TH){
		*empty = fm_false;
        goto out;
	}

	if(MR < FM_TXSCAN_MR_TH){
		*empty = fm_false;
        goto out;
	}

out:
    WCN_DBG(FM_NTC | CHIP,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

static fm_s32 mt6620_TxScan(fm_u16 min_freq, 
								fm_u16 max_freq,
		                        fm_u16 *pFreq,
		                        fm_u16 *pScanTBL,
		                        fm_u16 *ScanTBLsize,
		                        fm_u16 scandir,
		                        fm_u16 space)
{
	fm_s32 ret = 0;
 	fm_u16 freq = *pFreq;
	fm_u16 scan_cnt = *ScanTBLsize;
	fm_u16 cnt = 0; 
	fm_s16 rssi = 0;
	fm_s16 pamd = 0;
	fm_s16 mr = 0;
	fm_s32 counter = 0;
    fm_s32 empty = -1;
    fm_s32 step;

    WCN_DBG(FM_NTC | CHIP,"+%s():\n", __func__);
    
    if((!pScanTBL) || (*ScanTBLsize < FM_TX_SCAN_MIN) || (*ScanTBLsize >  FM_TX_SCAN_MAX))
    {
        WCN_DBG(FM_ERR | CHIP,"+%s():invalid scan table\n", __func__);
        ret = -FM_EPARA;
        goto out;
    }

    WCN_DBG(FM_NTC | CHIP, "[freq=%d], [max_freq=%d],[min_freq=%d],[scan BTL size=%d],[scandir=%d],[space=%d]\n", 
			*pFreq, max_freq, min_freq, *ScanTBLsize, scandir, space);

	cnt = 0;
   /* if (space == MT6620_FM_SPACE_100K) {
        step = 1;
    } else if (space == MT6620_FM_SPACE_50K) {
        step = 5;
    } else if (space == MT6620_FM_SPACE_200K) {
        step = 2;
    } else {
        //default
        step = 1;
    }*/
    step = space;
	while(!(!(cnt < scan_cnt) || (freq > max_freq) || (freq < min_freq))){
		//MT6620_RampDown();
		//Set desired channel, tune to the channel, and perform fast AGC
		counter++; //just for debug
		
		ret = mt6620_TxScan_SetFreq(freq);
		if(ret){
            WCN_DBG(FM_ERR | CHIP,"%s():set freq failed\n", __func__);
			goto out;
		}
		
		//wait 8~10ms for RF setting
		Delayms(10);
		//wait 4 AAGC period for AAGC setting, AAGC period = 1024/480k = 2.13ms
		Delayms(9);
		
		ret = mt6620_TxScan_GetCQI(&rssi, &pamd, &mr);
		if(ret){
            WCN_DBG(FM_ERR | CHIP,"%s():get CQI failed\n", __func__);
			goto out;
		}

		ret = mt6620_TxScan_IsEmptyChannel(rssi, pamd, mr, &empty);
		if(!ret){
            if((empty == fm_true) && ((freq < FM_TX_SCAN_HOLE_LOW) || (freq > FM_TX_SCAN_HOLE_HIGH))){
                *(pScanTBL + cnt) = freq; //strore the valid empty channel 
			    cnt++;
                WCN_DBG(FM_NTC | CHIP,"empty channel:[freq=%d] [cnt=%d]\n", freq, cnt);
            }			
		}else{ 
		    WCN_DBG(FM_ERR | CHIP,"%s():IsEmptyChannel failed\n", __func__);
			goto out;
        }

		if(scandir == FM_TX_SCAN_UP){
            if(freq == FM_TX_SCAN_HOLE_LOW){
                freq += (FM_TX_SCAN_HOLE_HIGH - FM_TX_SCAN_HOLE_LOW + step);
            }else{
                freq += step;
            }
		}else if(scandir == FM_TX_SCAN_DOWN){
		    if(freq == FM_TX_SCAN_HOLE_HIGH){
                freq -= (FM_TX_SCAN_HOLE_HIGH - FM_TX_SCAN_HOLE_LOW + step);
            }else{
                freq -= step;
            }
		}else{
            WCN_DBG(FM_ERR | CHIP,"%s():scandir para error\n", __func__);
			ret = -FM_EPARA;
            goto out;
		}
	}

	*ScanTBLsize = cnt;
	*pFreq = *(pScanTBL + cnt);
	WCN_DBG(FM_NTC | CHIP, "completed, [cnt=%d],[freq=%d],[counter=%d]\n", cnt, freq, counter);
    
out:
    WCN_DBG(FM_NTC | CHIP,"-%s():[ret=%d]\n", __func__, ret);
    return ret;
}

/*
* mt6620_Seek
* @pFreq - IN/OUT parm, IN start freq/OUT seek valid freq
* @seekdir - 0:up, 1:down
* @space - 1:50KHz, 2:100KHz, 4:200KHz
* return fm_true:seek success; fm_false:seek failed
*/
static fm_bool mt6620_Seek(fm_u16 min_freq, fm_u16 max_freq, fm_u16 *pFreq, fm_u16 seekdir, fm_u16 space)
{
    fm_s32 ret = 0;
    fm_u16 pkt_size,temp;

    mt6620_RampDown();
    mt6620_read(0x9C, &temp);
    mt6620_Mute(fm_true);

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_seek_1(cmd_buf, TX_BUF_SIZE, seekdir, space, max_freq, min_freq);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_SEEK | FLAG_SEEK_DONE, SW_RETRY_CNT, SEEK_TIMEOUT, mt6620_get_read_result);
    FM_UNLOCK(cmd_buf_lock);

    if (!ret && res) {
        *pFreq = res->seek_result;
        fm_cb_op->cur_freq_set(*pFreq);
    } else {
        WCN_DBG(FM_ALT | CHIP, "mt6620_seek_1 failed\n");
        return ret;
    }

    Delayms(35);

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_seek_2(cmd_buf, TX_BUF_SIZE, seekdir, space, max_freq, min_freq);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_SEEK | FLAG_SEEK_DONE, SW_RETRY_CNT, SEEK_TIMEOUT, mt6620_get_read_result);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_seek_2 failed\n");
        return ret;
    }

    //get the result freq
    WCN_DBG(FM_NTC | CHIP, "seek, result freq:%d\n", *pFreq);
	if((temp&0x0008) == 0)
	{
    mt6620_Mute(fm_false);
	}

    return fm_true;
}

static fm_bool mt6620_Scan(fm_u16 min_freq, fm_u16 max_freq, fm_u16 *pFreq, fm_u16 *pScanTBL,
                           fm_u16 *ScanTBLsize, fm_u16 scandir, fm_u16 space)
{
    fm_s32 ret = 0;
    fm_u16 pkt_size,temp;
    fm_u16 offset = 0;
    fm_u16 tmp_scanTBLsize = *ScanTBLsize;

    if ((!pScanTBL) || (tmp_scanTBLsize == 0)) {
        WCN_DBG(FM_ALT | CHIP, "scan, failed:invalid scan table\n");
        return fm_false;
    }

    WCN_DBG(FM_DBG | CHIP, "start freq: %d, max_freq:%d, min_freq:%d, scan BTL size:%d, scandir:%d, space:%d\n", *pFreq, max_freq, min_freq, *ScanTBLsize, scandir, space);

    mt6620_RampDown();
    mt6620_read(0x9C, &temp);
    mt6620_Mute(fm_true);

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_scan_1(cmd_buf, TX_BUF_SIZE, scandir, space, max_freq, min_freq);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_SCAN | FLAG_SCAN_DONE, SW_RETRY_CNT, SCAN_TIMEOUT, mt6620_get_read_result);
    FM_UNLOCK(cmd_buf_lock);

    if (!ret && res) {
        fm_memcpy(pScanTBL, res->scan_result, sizeof(fm_u16)*FM_SCANTBL_SIZE);
        WCN_DBG(FM_INF | CHIP, "Rx Scan Result:\n");

        for (offset = 0; offset < tmp_scanTBLsize; offset++) {
            WCN_DBG(FM_INF | CHIP, "%d: %04x\n", (fm_s32)offset, *(pScanTBL + offset));
        }

        *ScanTBLsize = tmp_scanTBLsize;
    } else {
        WCN_DBG(FM_ALT | CHIP, "mt6620_scan_1 failed\n");
        return ret;
    }

    Delayms(35);

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_scan_2(cmd_buf, TX_BUF_SIZE, scandir, space, max_freq, min_freq);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_SEEK | FLAG_SEEK_DONE, SW_RETRY_CNT, SEEK_TIMEOUT, mt6620_get_read_result);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_scan_2 failed\n");
        return ret;
    }

	if((temp&0x0008) == 0)
	{
    mt6620_Mute(fm_false);
	}

    return fm_true;
}

/*
 * mt6620_GetCurRSSI - get current freq's RSSI value
 * RS=RSSI
 * If RS>511, then RSSI(dBm)= (RS-1024)/16*6
 *				   else RSSI(dBm)= RS/16*6
 */
static fm_s32 mt6620_GetCurRSSI(fm_s32 *pRSSI)
{
    fm_u16 tmp_reg;

    mt6620_read(FM_RSSI_IND, &tmp_reg);
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

static fm_s32 MT6620_Fast_SetFreq(fm_u16 freq)
{
    fm_s32 ret = 0;
    fm_u16 pkt_size = 0;

    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_fast_tune(cmd_buf, TX_BUF_SIZE, freq);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_TUNE, SW_RETRY_CNT, SEEK_TIMEOUT, mt6620_get_read_result);
    FM_UNLOCK(cmd_buf_lock);

    if (ret) {
        WCN_DBG(FM_ALT | CHIP, "mt6620_fast_tune failed\n");
        return ret;
    }
    return ret;
}

/*
 * mt6620_get_RSSI - set freq and return RSSI value
*
 */
static fm_s32 mt6620_GetFreqCQI(fm_u16 freq,fm_s32 *pRSSI)
{
	if(MT6620_Fast_SetFreq(freq))
	{
		mt6620_GetCurRSSI(pRSSI);
	}

    return 0;
}

static fm_u8 mt6620_vol_tbl[16] = {
    0, 1, 2, 3, 4, 5, 7, 9, 11, 14, 17, 21, 25, 30, 36, 43
};
static fm_s32 mt6620_SetVol(fm_u8 vol)
{
    fm_s32 ret = 0;
    fm_u8 tmp_vol;// = vol & 0x3f;

    vol = (vol > 15) ? 15 : vol;
    tmp_vol = mt6620_vol_tbl[vol] & 0x3f;
    if (tmp_vol > MT6620_VOL_MAX)
        tmp_vol = MT6620_VOL_MAX;

    ret = mt6620_set_bits(0x9C, (tmp_vol << 8), 0xC0FF);

    if (ret) {
        WCN_DBG(FM_ERR | CHIP, "Set vol=%d Failed\n", tmp_vol);
        return ret;
    } else {
        WCN_DBG(FM_DBG | CHIP, "Set vol=%d OK\n", tmp_vol);
    }

    return 0;
}

static fm_s32 mt6620_GetVol(fm_u8 *pVol)
{
    fm_s32 ret = 0;
    fm_u16 tmp_reg;

    FMR_ASSERT(pVol);

    ret = mt6620_read(0x9C, &tmp_reg);

    if (ret) {
        *pVol = 0;
        WCN_DBG(FM_ERR | CHIP, "Get vol Failed\n");
        return ret;
    } else {
        *pVol = (tmp_reg >> 8) & 0x3f;
		if(*pVol == MT6620_VOL_MAX)
			*pVol = 15;
		else
			*pVol = (*pVol/3);
        WCN_DBG(FM_DBG | CHIP, "Get vol=%d OK\n", *pVol);
    }

    return 0;

}

static fm_s32 mt6620_dump_reg(void)
{
	fm_s32 i;
	fm_u16 TmpReg;
	for(i=0; i<0xff; i++)
	{
		mt6620_read(i, &TmpReg);
		WCN_DBG(FM_NTC | CHIP, "0x%02x=0x%04x\n",i,TmpReg);
	}
	return 0;
}


static fm_bool mt6620_GetMonoStereo(fm_u16 *pMonoStereo)
{
    fm_u16 tmp_reg;

    mt6620_write(FM_MAIN_PGSEL, 0x0001);

    if (pMonoStereo) {
        mt6620_read(0xF8, &tmp_reg);
        *pMonoStereo = (tmp_reg & 0x0400) >> 10;
    } else {
        WCN_DBG(FM_ERR | CHIP, "MonoStero: para err\n");
        return fm_false;
    }

    mt6620_write(FM_MAIN_PGSEL, 0x0000);

    WCN_DBG(FM_DBG | CHIP, "MonoStero:0x%04x\n", *pMonoStereo);
    return fm_true;
}

/*
 * MT6620_SetMonoStereo
 * Force set to stero/mono mode
 * @MonoStereo -- 0, auto; 1, force mono
 * If success, return 0; else error code
 */
static fm_s32 MT6620_SetMonoStereo(fm_s32 MonoStereo)
{
    fm_u16 tmp_reg;
    fm_s32 ret = 0;
    
    if((ret = mt6620_write(FM_MAIN_PGSEL, FM_PG1)))
        goto out;

    tmp_reg = MonoStereo ? BITn(1) : 0; //MonoStereo, 1: force mono; 0:auto
    if((ret = mt6620_set_bits(FM_STEROMONO_CTR, tmp_reg, MASK(1))))//set E0 D1=0
        goto out;
    
    if((ret = mt6620_write(FM_MAIN_PGSEL, FM_PG0)))
        goto out;
    
    WCN_DBG(FM_DBG | CHIP,"set to %s\n", MonoStereo ? "auto" : "force mono");
    
out:
    return ret;
}

static fm_s32 mt6620_GetCapArray(fm_s32 *ca)
{
    fm_u16 dataRead;

    FMR_ASSERT(ca);

    mt6620_read(0x26, &dataRead);
    *ca = dataRead;

    return 0;
}

/*
 * mt6620_GetCurPamd - get current freq's PAMD value
 * PA=PAMD
 * If PA>511 then PAMD(dB)=  (PA-1024)/16*6,
 *				else PAMD(dB)=PA/16*6
 */
static fm_bool mt6620_GetCurPamd(fm_u16 *pPamdLevl)
{
    fm_u16 tmp_reg;
    fm_u16 dBvalue=0,valid_cnt=0;
    fm_s32 i,total=0;

    for (i = 0; i < 8; i++) 
    {
	    if (mt6620_read(FM_ADDR_PAMD, &tmp_reg))
	    {
	    	*pPamdLevl = 0;
	        return fm_false;
		}

	    tmp_reg &= 0x00FF;
	    dBvalue = (tmp_reg>127) ? ((256-tmp_reg)*6/16):0;
		if(dBvalue != 0)
		{
			total += dBvalue;
			valid_cnt++;
			WCN_DBG(FM_DBG | CHIP, "[%d]PAMD=%d\n",i,dBvalue);
		}
        Delayms(3);
	}
	if(valid_cnt != 0)
	{
	    *pPamdLevl = total/valid_cnt;
	}
	else
	{
		*pPamdLevl = 0;
	}
    WCN_DBG(FM_DBG | CHIP,"PamdLevl=%d\n", *pPamdLevl);
    return fm_true;
}

static fm_s32 mt6620_ScanStop(void)
{
    return fm_force_active_event(FLAG_SCAN);
}

static fm_s32 mt6620_SeekStop(void)
{
    return fm_force_active_event(FLAG_SEEK);
}

/*
 * mt6620_I2s_Setting - set the I2S state on MT6620
 * @onoff - I2S on/off
 * @mode - I2S mode: Master or Slave
 *
 * Return:0, if success; error code, if failed
 */
static fm_s32 mt6620_I2s_Setting(fm_s32 onoff, fm_s32 mode, fm_s32 sample)
{
    fm_u16 tmp_state = 0;
    fm_u16 tmp_mode = 0;
    fm_u16 tmp_sample = 0;
    fm_s32 ret = 0;

    if (onoff == MT6620_I2S_ON) {
        tmp_state = 0x01; //I2S Frequency tracking on
        mt6620_i2s_inf.status = 1;
    } else if (onoff == MT6620_I2S_OFF) {
        tmp_state = 0x00; //I2S Frequency tracking off
        mt6620_i2s_inf.status = 0;
    } else {
        WCN_DBG(FM_ERR | CHIP, "%s():[onoff=%d]\n", __func__, onoff);
        ret = -FM_EPARA;
        goto out;
    }

    if (mode == MT6620_I2S_MASTER) {
        tmp_mode = 0x03; //6620 as I2S master
        mt6620_i2s_inf.mode = 1;
    } else if (mode == MT6620_I2S_SLAVE) {
        tmp_mode = 0x0B; //6620 as I2S slave
        mt6620_i2s_inf.mode = 0;
    } else {
        WCN_DBG(FM_ERR | CHIP, "%s():[mode=%d]\n", __func__, mode);
        ret = -FM_EPARA;
        goto out;
    }

    if (sample == MT6620_I2S_32K) {
        tmp_sample = 0x0000; //6620 I2S 32KHz sample rate
        mt6620_i2s_inf.rate = 32000;
    } else if (sample == MT6620_I2S_44K) {
        tmp_sample = 0x0800; //6620 I2S 44.1KHz sample rate
        mt6620_i2s_inf.rate = 44100;
    } else if (sample == MT6620_I2S_48K) {
        tmp_sample = 0x1000; //6620 I2S 48KHz sample rate
        mt6620_i2s_inf.rate = 48000;
    } else {
        WCN_DBG(FM_ERR | CHIP, "%s():[sample=%d]\n", __func__, sample);
        ret = -FM_EPARA;
        goto out;
    }

    if ((ret = mt6620_set_bits(0x5F, tmp_sample, 0xE7FF)))
        goto out;

    if ((ret = mt6620_write(0x9B, tmp_mode)))
        goto out;

    if ((ret = mt6620_set_bits(0x56, tmp_state, 0xFF7F)))
        goto out;

    WCN_DBG(FM_NTC | CHIP, "[onoff=%s][mode=%s][sample=%d](0)33KHz,(1)44.1KHz,(2)48KHz\n",
            (onoff == MT6620_I2S_ON) ? "On" : "Off",
            (mode == MT6620_I2S_MASTER) ? "Master" : "Slave",
            sample);
out:
    return ret;
}
static fm_s32 mt6620_Tx_Support(fm_s32 *sup)
{
	*sup=1;
	return 0;
}

static fm_s32 mt6620_rdsTx_Support(fm_s32 *sup)
{
	*sup=1;
	return 0;
}

static fm_s32 MT6620_FMOverBT(fm_bool enable)
{
    fm_s32 ret = 0;
    static fm_u16 state = 0;
    static fm_u16 mode = 0;
    static fm_u16 sample = 0;
    static fm_u16 inited = fm_false;

    WCN_DBG(FM_NTC | CHIP,"+%s():\n", __func__);
    if(inited == fm_false)
    {
        // record priv val
        if((ret = mt6620_read(0x56, &state)))
            goto out;
        if((ret = mt6620_read(0x9B, &mode)))
            goto out;
        if((ret = mt6620_read(0x5F, &sample)))
            goto out;
        inited = fm_true;
        WCN_DBG(FM_NTC | CHIP,"init, record priv seetings\n");
    }
    
    if(enable == fm_true){
        //disable analog output when FM over BT
        if((ret = mt6620_set_bits(0x3A, 0, MASK(2))))
            goto out;
        //set FM over BT
        if((ret = mt6620_write(0x56, 0x0001)))
            goto out;
        if((ret = mt6620_write(0x9B, 0x000b)))
            goto out;
        if((ret = mt6620_write(0x5F, 0x1175)))
            goto out;
        WCN_DBG(FM_NTC | CHIP,"set FM via BT controller\n");
    }
    else if(enable == fm_false)
    {
        //enable analog output when FM normal mode
        if((ret = mt6620_set_bits(0x3A, BITn(2), MASK(2))))
            goto out;
        //recover to priv val
        if((ret = mt6620_write(0x56, state)))
            goto out;
        if((ret = mt6620_write(0x9B, mode)))
            goto out;
        if((ret = mt6620_write(0x5F, sample)))
            goto out;
        WCN_DBG(FM_NTC | CHIP,"set FM via Host\n");
    }
    else
    {
        WCN_DBG(FM_ERR | CHIP,"%s()\n", __func__);
        ret = -FM_EPARA;
        goto out;
    }
out:
    WCN_DBG(FM_NTC | CHIP,"-%s():[ret=%d]\n", __func__, ret);
    return ret; 
}

/*fm soft mute tune function*/
static fm_s32 mt6620_soft_mute_tune(fm_u16 freq,fm_s32 *rssi,fm_bool *valid)
{
    fm_s32 ret=0;
    fm_u16 pkt_size;    
    fm_s32 hilo_side = -1;    
    struct mt6620_fm_softmute_tune_cqi_t *p_cqi;
	fm_s32 RSSI=0, PAMD=0,MR=0, ATDC=0;
	fm_u32 PRX=0;
	fm_u16 softmuteGainLvl=0;

	/*set hilo side first(f/w won't do this), need to check whether work if set before rampdown*/
	if((ret = MT6620_HL_Side_Adj(freq, &hilo_side)))
		return ret;
	
	if (FM_LOCK(cmd_buf_lock)) return (-FM_ELOCK);
	pkt_size = mt6620_full_cqi_req(cmd_buf, TX_BUF_SIZE, freq, 1, 1);
	ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_SM_TUNE, SW_RETRY_CNT, SM_TUNE_TIMEOUT, mt6620_get_read_result);
	FM_UNLOCK(cmd_buf_lock);

	if (!ret && res) 
	{
		WCN_DBG(FM_NTC | CHIP, "smt cqi size %d\n", res->cqi[0]);
		p_cqi = (struct mt6620_fm_softmute_tune_cqi_t*)&res->cqi[2];
		// just for debug
		WCN_DBG(FM_NTC | CHIP, "freq %d, 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x, 0x%04x\n", 
			p_cqi->ch, 
			p_cqi->rssi,
			p_cqi->pamd,
			p_cqi->mr,
			p_cqi->atdc,
			p_cqi->prx,
			p_cqi->smg); 
		RSSI = ((p_cqi->rssi & 0x03FF) >= 512) ? ((p_cqi->rssi & 0x03FF) - 1024) : (p_cqi->rssi & 0x03FF);
		PAMD = ((p_cqi->pamd & 0xFF) >= 128) ? ((p_cqi->pamd & 0x0FF) - 256) : (p_cqi->pamd & 0x0FF);
		MR = ((p_cqi->mr & 0x01FF) >= 256) ? ((p_cqi->mr & 0x01FF) - 512) : (p_cqi->mr & 0x01FF);
		ATDC =((p_cqi->atdc & 0x0FFF) >= 2048) ? ((p_cqi->atdc & 0x0FFF) - 4096) : (p_cqi->atdc & 0x0FFF);
		if(ATDC < 0)
		{
			ATDC = (~(ATDC)) - 1;//Get abs value of ATDC
		}
		PRX = (p_cqi->prx & 0x00FF);
		softmuteGainLvl = p_cqi->smg;
		//check if the channel is valid according to each CQIs
		if((RSSI >= mt6620_fm_config.rx_cfg.long_ana_rssi_th) 
		 && (PAMD <= mt6620_fm_config.rx_cfg.pamd_th)
		 && (ATDC <= mt6620_fm_config.rx_cfg.atdc_th)
		 && (MR >= mt6620_fm_config.rx_cfg.mr_th)
		 && (PRX >= mt6620_fm_config.rx_cfg.prx_th)
		 && (softmuteGainLvl <= mt6620_fm_config.rx_cfg.smg_th))
		{	 
			*valid = fm_true;
		}
		else
		{
			*valid = fm_false;
		}
		*rssi = RSSI;
	}
	else 
	{
		WCN_DBG(FM_ALT | CHIP, "smt get CQI failed\n");
		return fm_false;
	}
	WCN_DBG(FM_NTC | CHIP, "valid=%d\n",*valid);
	return fm_true;
}
static fm_s32 mt6620_i2s_info_get(fm_s32 *ponoff, fm_s32 *pmode, fm_s32 *psample)
{
    FMR_ASSERT(ponoff);
    FMR_ASSERT(pmode);
    FMR_ASSERT(psample);

    *ponoff = mt6620_i2s_inf.status;
    *pmode = mt6620_i2s_inf.mode;
    *psample = mt6620_i2s_inf.rate;

    return 0;
}

static fm_s32 mt6620_hw_info_get(struct fm_hw_info *req)
{
    FMR_ASSERT(req);

    req->chip_id = mt6620_hw_info.chip_id;
    req->eco_ver = mt6620_hw_info.eco_ver;
    req->patch_ver = mt6620_hw_info.patch_ver;
    req->rom_ver = mt6620_hw_info.rom_ver;

    return 0;
}

static fm_bool mt6620_em_test(fm_u16 group_idx, fm_u16 item_idx, fm_u32 item_value)
{
	fm_s32 ret = 0;
	
	WCN_DBG(FM_NTC | CHIP,"+%s():[group_idx=%d],[item_idx=%d],[item_value=%d]\n", 
		__func__, group_idx, item_idx, item_value);
	
	switch (group_idx){
		case mono:
			if((ret = mt6620_write(FM_MAIN_PGSEL, 0x01)))
				goto out;

			if(item_value == 1){
				if((ret = mt6620_set_bits(0xE0, 0x02, 0xFFFF)))
					goto out;
			}else{
				if((ret = mt6620_set_bits(0xE0, 0x0, 0xFFFD)))
					goto out;
			}
			if((ret = mt6620_write(FM_MAIN_PGSEL, 0x0)))
				goto out;
			break;
		case stereo:
			if((ret = mt6620_write(FM_MAIN_PGSEL, 0x01)))
				goto out;
			if(item_value == 0){
				if((ret = mt6620_set_bits(0xE0, 0x0, 0xFFFD)))
					goto out;
			}else{
				switch (item_idx){
					case Sblend_ON:
						if((ret = mt6620_set_bits(0xD8, item_idx<<15, 0x7FFF)))
							goto out;
						break;
					case Sblend_OFF:
						if((ret = mt6620_set_bits(0xD8, 0, 0x7FFF)))
							goto out;
						break;
				}
			}
			if((ret = mt6620_write(FM_MAIN_PGSEL, 0x0)))
				goto out;
			break;
		case RSSI_threshold:
			if((ret = mt6620_set_bits(0xe0, item_value, 0xFC00)))
				goto out;
			break;
		case HCC_Enable:
			if((ret = mt6620_write(FM_MAIN_PGSEL, 0x01)))
				goto out;
			if(item_idx){
				if((ret = mt6620_set_bits(0xCF, 0x10, 0xFFFF)))
					goto out;
			}else{
				if((ret = mt6620_set_bits(0xCF, 0x0, 0xFFEF)))
					goto out;
			}
			if((ret = mt6620_write(FM_MAIN_PGSEL, 0x0)))
				goto out;
			break;
		case PAMD_threshold:
			if((ret = mt6620_set_bits(0xE1, item_value, 0xFF00)))
				goto out;
			break;
		case Softmute_Enable:
			if((ret = mt6620_write(FM_MAIN_PGSEL, 0x01)))
				goto out;
			if(item_idx){
				if((ret = mt6620_set_bits(0xCF, 0x0020, 0xFFFF))) //1:CF[5] = 1
					goto out; 
			}else{
				if((ret = mt6620_set_bits(0xCF, 0x0000, 0xFFDF))) //1:CF[5] = 0
					goto out; 
			}
			if((ret = mt6620_write(FM_MAIN_PGSEL, 0x0)))
				goto out;
			break;
		case De_emphasis:
			if((ret = mt6620_write(FM_MAIN_PGSEL, 0x01)))
				goto out;
			if(item_idx == 2){
				if((ret = mt6620_set_bits(0xd4, 0x2000, 0xCFFF)))
					goto out;
			}else if(item_idx == 1){
				if((ret = mt6620_set_bits(0xd4, 0x1000, 0xCFFF)))
					goto out;
			}else if(item_idx == 0){
				if((ret = mt6620_set_bits(0xd4, 0x0000, 0xCFFF)))
					goto out;
			}
			if((ret = mt6620_write(FM_MAIN_PGSEL, 0x0)))
				goto out;
			break;
		case HL_Side:
			if(item_idx == 2){
				 //H-Side
				if((ret = mt6620_set_bits(0xCB, 0x11, 0xFFFE)))
					goto out;
				if((ret = mt6620_set_bits(0xF, 0x0400,	0xFBFF)))
					goto out;
			}else if(item_idx == 1){
				 //L-Side
				if((ret = mt6620_set_bits(0xCB, 0x10, 0xFFFE)))
					goto out;
				if((ret = mt6620_set_bits(0xF, 0x0,  0xFBFF)))
					goto out;
			}
			break;

		case Dynamic_Limiter:
			if((ret = mt6620_write(FM_MAIN_PGSEL, 0x01)))
				goto out;
			if(item_idx){
				if((ret = mt6620_set_bits(0xFA, 0x0, 0xFFF7)))
					goto out;
			}else{
				if((ret = mt6620_set_bits(0xFA, 0x08, 0xFFF7)))
					goto out;
			}
			if((ret = mt6620_write(FM_MAIN_PGSEL, 0x0)))
				goto out;
			break;

		case Softmute_Rate:
			if((ret = mt6620_write(FM_MAIN_PGSEL, 0x01)))
				goto out;
			if((ret = mt6620_set_bits(0xc8, item_value<<8, 0x80FF)))
				goto out;
			if((ret = mt6620_write(FM_MAIN_PGSEL, 0x0)))
				goto out;
			break;

		case AFC_Enable:
			if (item_idx){
				if((ret = mt6620_set_bits(0x63, 0x0400, 0xFBFF)))
					goto out;
			}else{
				if((ret = mt6620_set_bits(0x63, 0x0,	0xFBFF)))
					goto out;
			}
			break;

#if 0
		case Softmute_Level:
			mt6620_write(FM_MAIN_PGSEL, 0x01);
			if(item_value > 0x24)
				item_value = 0x24;
			mt6620_set_bits(0xD1, item_value, 0xFFC0);
			mt6620_write(FM_MAIN_PGSEL, 0x0);
			break;
#endif
		case Analog_Volume:
			if((ret = mt6620_set_bits(0x9C, item_value<<8, 0xC0FF)))
				goto out;
			break;

		default:
			break;
	}

out:
	WCN_DBG(FM_NTC | CHIP,"-%s():[ret=%d]\n", __func__, ret);
	return ret;
return fm_true;
}

static const fm_u16 DesenseChMap[] = {
    0x0000, 0x0000, 0x0000, 0x0000, /* 7675~7600, 7755~7680, 7835~7760, 7915~7840 */
    0x0000, 0x0000, 0x0000, 0x0000, /* 7995~7920, 8075~8000, 8155~8080, 8235~8160 */
    0x0000, 0x0000, 0x0000, 0x0000, /* 8315~8240, 8395~8320, 8475~8400, 8555~8480 */
    0x0000, 0x0000, 0x0000, 0x0400, /* 8635~8560, 8715~8640, 8795~8720, 8875~8800 */
    0x0000, 0x0000, 0x0000, 0x0000, /* 8955~8880, 9035~8960, 9115~9040, 9195~9120 */
    0x00FC, 0x0000, 0x0000, 0x1C00, /* 9275~9200, 9355~9280, 9435~9360, 9515~9440 */
    0x0000, 0x0001, 0x0000, 0x0000, /* 9595~9520, 9675~9600, 9755~9680, 9835~9760 */
    0x0000, 0x7800, 0x0000, 0x0000, /* 9915~9840, 9995~9920, 10075~10000, 10155~10080 */
    0x0000, 0x0000, 0x0000, 0x0000, /* 10235~10160, 10315~10240, 10395~10320, 10475~10400 */
    0x0000, 0x0000, 0x0000, 0x0000, /* 10555~10480, 10635~10560, 10715~10640, 10795~10720 */
    0x0000                          /* 10875~10800 */
};
// return value: 0, not a de-sense channel; 1, this is a de-sense channel; else error no
static fm_s32 mt6620_is_dese_chan(fm_u16 freq)
{
	fm_u8 bDesenseCh = 0;

    //caculate and applye compensation
    if (0 == fm_get_channel_space(freq))
    {
        freq *= 10;
    }
	WCN_DBG(FM_NTC | CHIP,"%s, freq=%d\n",__func__,freq);
    
	bDesenseCh = ((0x0001 << (((freq - 7600)%80)/5)) & DesenseChMap[((freq - 7600)/80)])>>(((freq - 7600)%80)/5); 
	WCN_DBG(FM_NTC | CHIP,"freq[%d] desense=[%d]\n",freq,bDesenseCh);

    return bDesenseCh;
}

/*  return value:
1, is desense channel and rssi is less than threshold; 
0, not desense channel or it is but rssi is more than threshold.*/
static fm_s32 mt6620_desense_check(fm_u16 freq,fm_s32 rssi)
{
	if(mt6620_is_dese_chan(freq))
	{
		if(rssi<mt6620_fm_config.rx_cfg.desene_rssi_th)
		{
			return 1;
		}
	}
    return 0;
}
static fm_s32 MT6620fm_low_power_wa_default(fm_s32 fmon)
{
    return 0;
}

fm_s32 MT6620fm_low_ops_register(struct fm_lowlevel_ops *ops)
{
    fm_s32 ret = 0;
    //Basic functions.

    FMR_ASSERT(ops);
    FMR_ASSERT(ops->cb.cur_freq_get);
    FMR_ASSERT(ops->cb.cur_freq_set);
    fm_cb_op = &ops->cb;

    ops->bi.low_pwr_wa = MT6620fm_low_power_wa_default;
    ops->bi.pwron = mt6620_pwron;
    ops->bi.pwroff = mt6620_pwroff;
    ops->bi.msdelay = Delayms;
    ops->bi.usdelay = Delayus;
    ops->bi.read = mt6620_read;
    ops->bi.write = mt6620_write;
    ops->bi.setbits = mt6620_set_bits;
    ops->bi.chipid_get = mt6620_get_chipid;
    ops->bi.mute = mt6620_Mute;
    ops->bi.rampdown = mt6620_RampDown;
    ops->bi.pwrupseq = mt6620_PowerUp;
    ops->bi.pwrdownseq = mt6620_PowerDown;
    ops->bi.setfreq = mt6620_SetFreq;
    ops->bi.seek = mt6620_Seek;
    ops->bi.seekstop = mt6620_SeekStop;
    ops->bi.scan = mt6620_Scan;
    ops->bi.scanstop = mt6620_ScanStop;
    ops->bi.rssiget = mt6620_GetCurRSSI;
    ops->bi.volset = mt6620_SetVol;
    ops->bi.volget = mt6620_GetVol;
    ops->bi.dumpreg = mt6620_dump_reg;
    ops->bi.msget = mt6620_GetMonoStereo;
    ops->bi.msset = MT6620_SetMonoStereo;
    ops->bi.pamdget = mt6620_GetCurPamd;
    ops->bi.em = mt6620_em_test;
    ops->bi.anaswitch = mt6620_SetAntennaType;
    ops->bi.anaget = mt6620_GetAntennaType;
    ops->bi.caparray_get = mt6620_GetCapArray;
    ops->bi.i2s_set = mt6620_I2s_Setting;
    ops->bi.i2s_get = mt6620_i2s_info_get;
	ops->bi.is_dese_chan = mt6620_is_dese_chan;
	ops->bi.softmute_tune = mt6620_soft_mute_tune;
	ops->bi.desense_check = mt6620_desense_check;
	ops->bi.get_freq_cqi = mt6620_GetFreqCQI;
    ops->bi.hwinfo_get = mt6620_hw_info_get;
    ops->bi.fm_via_bt = MT6620_FMOverBT;
	/*****tx function****/
	ops->bi.tx_support = mt6620_Tx_Support;
	ops->bi.pwrupseq_tx = mt6620_PowerUpTx;
	ops->bi.tune_tx = MT6620_SetFreq_Tx;
	ops->bi.pwrdownseq_tx = mt6620_PowerDownTx;
	ops->bi.tx_scan = mt6620_TxScan;
    ops->ri.rds_tx = MT6620_Rds_Tx;
    ops->ri.rds_tx_enable = MT6620_Rds_Tx_Enable;
    ops->ri.rds_tx_disable = MT6620_Rds_Tx_Disable;
    ops->ri.rdstx_support = mt6620_rdsTx_Support;
    ops->bi.tx_pwr_ctrl = MT6620_TX_PWR_CTRL;
    ops->bi.rtc_drift_ctrl = MT6620_RTC_Drift_CTRL;
    ops->bi.tx_desense_wifi = MT6620_TX_DESENSE;

	cmd_buf_lock = fm_lock_create("20_cmd");
	ret = fm_lock_get(cmd_buf_lock);

	cmd_buf = fm_zalloc(TX_BUF_SIZE + 1);

	if (!cmd_buf) {
		WCN_DBG(FM_ALT | CHIP, "6620 fm lib alloc tx buf failed\n");
		ret = -1;
	}

#if 0//def MTK_FM_50KHZ_SUPPORT
	cqi_fifo = fm_fifo_create("6620_cqi_fifo", sizeof(struct adapt_fm_cqi), 640);
	if (!cqi_fifo) {
		WCN_DBG(FM_ALT | CHIP, "6620 fm lib create cqi fifo failed\n");
		ret = -1;
	}
#endif

    return ret;
}

fm_s32 MT6620fm_low_ops_unregister(struct fm_lowlevel_ops *ops)
{
    fm_s32 ret = 0;
    //Basic functions.
#if 0//def MTK_FM_50KHZ_SUPPORT
	fm_fifo_release(cqi_fifo);
#endif
	
	if (cmd_buf) {
		fm_free(cmd_buf);
		cmd_buf = NULL;
	}

	ret = fm_lock_put(cmd_buf_lock);

    FMR_ASSERT(ops);

    fm_memset(&ops->bi, 0, sizeof(struct fm_basic_interface));
    return ret;
}


/***********************************************************************
*  Hi-Lo Side Injection
*
***********************************************************************/
fm_s32 MT6620_HL_Side_Adj(fm_u16 freq, fm_s32 *hl)
{
    fm_s32 ret = 0;
    fm_s32 isHiSide= 0;
    fm_s32 tblsize = 0;
    fm_s32 indx = 0;
    fm_u16 tmp;
    static fm_u16 Hi_Channels[] = {7950, 8070, 8210, 10640};

    if (0 == fm_get_channel_space(freq)) {
            freq *= 10;
    }

    WCN_DBG(FM_DBG | CHIP,"+%s, [freq=%d]\n", __func__, (fm_s32)freq);
    
    *hl = 0;
    
    if(sizeof(Hi_Channels) == 0)
        goto out;
    
    tblsize = sizeof(Hi_Channels)/sizeof(Hi_Channels[0]);
    for(indx = 0; indx < tblsize; indx++){
        if(Hi_Channels[indx] == freq)
        {
            isHiSide = 1;
            *hl = 1;
            //goto set_HL;
            break;
        }     
    }
    
    if(isHiSide){
  	    //Set high-side injection (AFC)
  	    if((ret = mt6620_read(0x0F, &tmp)))
            goto out;
  	    if((ret = mt6620_write(0x0F, tmp |0x0400)))
            goto out;
  	    if((ret = mt6620_write(FM_MAIN_PGSEL, 0)))
            goto out;
  	    //Set high-side injection (DFE)
  	    if((ret = mt6620_read(0xCB, &tmp)))
            goto out;
  	    if((ret = mt6620_write(0xCB, tmp | 0x01)))
            goto out;
  	    //mt6620_write(0xCB, dataRead&0xFFFE);
    }else{
        //Set low-side injection (AFC)
        if((ret = mt6620_read(0x0F, &tmp)))
            goto out;
  	    if((ret = mt6620_write(0x0F, tmp&0xFBFF)))
            goto out;
  	    if((ret = mt6620_write(FM_MAIN_PGSEL, 0)))
            goto out;
  	    //Set low-side injection (DFE)
  	    if((ret = mt6620_read(0xCB, &tmp)))
            goto out;
  	    //mt6620_write(0xCB, dataRead | 0x01);
        if((ret = mt6620_write(0xCB, tmp&0xFFFE)))
            goto out;
    }
 out:   
    WCN_DBG(FM_NTC | CHIP,"-%s, [isHiSide=%d][ret=%d]\n", __func__, (fm_s32)isHiSide, ret);
    return ret;
}

/***********************************************************************
*  ADPLL Power On or Off
*
***********************************************************************/
fm_s32 MT6620_ADPLL_Power_OnOff(fm_s32 onoff, fm_s32 ADPLL_clk)
{
    fm_s32 ret = 0;

    
    switch(onoff){
        case FM_ADPLL_ON:
            if((ret = mt6620_write(0x25, 0x040F)))
                goto out;
            //Remove the Reset_N
            if((ret = mt6620_write(0x20, 0x2720)))
                goto out;
            // change DLF loop gain  
            // Set FMCR_DLF_GAIN_A = "9"
            // Set FMCR_DLF_GAIN_B = "9"
            if((ret = mt6620_write(0x22, 0x9980)))
                goto out;
            //Configure initial I_CODE for calibration
            if((ret = mt6620_write(0x25, 0x080F)))
                goto out;
            //Enable ADPLL DCO
            //Set FMCR_DCO_ EN = "1
            if(ADPLL_clk == FM_ADPLL_16M){
                if((ret = mt6620_write(0x1E, 0x0A63)))
                    goto out;
                // wait 5ms 
                Delayms(5);
                if((ret = mt6620_write(0x1E, 0x0A65)))
                    goto out;
                // wait 5ms 
                Delayms(5);
                if((ret = mt6620_write(0x1E, 0x0A71)))
                    goto out;
            }else if(ADPLL_clk == FM_ADPLL_15M){
            if((ret = mt6620_write(0x1E, 0x0863)))
                goto out;
            // wait 5ms 
            Delayms(5);
            if((ret = mt6620_write(0x1E, 0x0865)))
                goto out;
            // wait 5ms 
            Delayms(5);
            if((ret = mt6620_write(0x1E, 0x0871)))
                goto out;
            }else{
                ret = -FM_EPARA;
                goto out;
            }
            // wait 100ms 
            Delayms(100);
            if((ret = mt6620_write(0x2A, 0x1026)))
                goto out;
            break;
            
        //ADPLL Power Off Sequence
        case FM_ADPLL_OFF:
            // Set rgfrf_top_ck = "0"
            if((ret = mt6620_set_bits(0x2A, 0, MASK(12))))//set 2A D12=0
                goto out;
            // Set FMCR_OPEN_LOOP_EN = "0"
            // Set FMCR_PLL_EN = "0"
            // Set FMCR_DCO_EN = "0"
            if((ret = mt6620_set_bits(0x1E, 0, MASK(7)&MASK(4)&MASK(0))))//set 1E D7 D4 D0=0
                goto out;
            // Set rgfrf_adpll_reset_n = "0"
            if((ret = mt6620_set_bits(0x20, 0, MASK(13))))//set 20 D13=0
                goto out;
            // Set rgfrf_adpll_reset_n = "1"
            if((ret = mt6620_set_bits(0x20, BITn(13), MASK(13))))//set 20 D13=1
                goto out;
            break;
        default:
            break;
    }
out:
    return ret;
}

/***********************************************************************
*  Frequency Avoidance
*
***********************************************************************/
fm_s32 MT6620_ADPLL_Freq_Avoid(fm_u16 freq, fm_s32 *freqavoid)
{
    fm_s32 ret = 0;
    fm_s32 ADPLL_clk = FM_ADPLL_15M;
    fm_u16 dataRead = 0;
    fm_u16 indx = 0;
    static fm_u16 Avoid_Channels[] ={ 
        7670, 7680, 7690, 7700, 8060, 8070, 8080, 8440, 8450, 8460, 8720, 8830, 8840, 9200,
        9210, 9220, 9230, 9360, 9490, 9600, 9610, 9980, 9990, 10000, 10130, 10360, 10370, 10380, 10740,
        10750, 10760, 10770
    };

    if (0 == fm_get_channel_space(freq)) {
        freq *= 10;
    }

    WCN_DBG(FM_DBG | CHIP,"+%s, [freq=%d]\n", __func__, (fm_s32)freq);

    *freqavoid = 0;
    
    dataRead = sizeof(Avoid_Channels)/sizeof(Avoid_Channels[0]);
    indx = 0;
    while((indx < dataRead) && (ADPLL_clk != FM_ADPLL_16M)){
        if(Avoid_Channels[indx] == freq){
            ADPLL_clk = FM_ADPLL_16M;
            *freqavoid = 1;
        }
        indx++;
    }
    //isADPLL_16M = 1;
    if((ret = mt6620_read(0x1E, &dataRead)))
        goto out;
    if(((dataRead&BITn(9))&&(ADPLL_clk == FM_ADPLL_16M))||(!(dataRead&BITn(9))&&(ADPLL_clk == FM_ADPLL_15M)))//1EH, D9
        goto out; //we need not do freq avoid at these caes

    if(ADPLL_clk == FM_ADPLL_16M){       
        //Set rgf_f16mode_en = X	
      	if((ret = mt6620_set_bits(0x61, BITn(0), MASK(0))))//set 61H D0=1, 16.384MHZ
                goto out;
    }else if(ADPLL_clk == FM_ADPLL_15M){
        //Set rgf_f16mode_en = X  		
      	if((ret = mt6620_set_bits(0x61, 0, MASK(0))))//set 61H D0=0, 15.36MHZ
                goto out;
    }else{
        ret = -FM_EPARA;
        goto out;
    } 
    
    // Disable ADPLL
    ret = MT6620_ADPLL_Power_OnOff(FM_ADPLL_OFF, ADPLL_clk);
    if(ret){
        WCN_DBG(FM_NTC | CHIP,"%s, ADPLL OFF failed, [ret=%d]n", __func__, ret);
        goto out;
    }
    
    //Set FMCR_DCO_CK_SEL = ? (default = 0, 15.36)
    if(ADPLL_clk == FM_ADPLL_16M){		
        if((ret = mt6620_set_bits(0x1E, BITn(9), MASK(9))))//set 1EH D9=1, 16.384MHZ
                goto out;
    }else if(ADPLL_clk == FM_ADPLL_15M){
        if((ret = mt6620_set_bits(0x1E, 0, MASK(9))))//set 1EH D9=0, 15.36MHZ
                goto out;
    }else{
        ret = -FM_EPARA;
        goto out;
    }
    
    // Ensable ADPLL
    ret = MT6620_ADPLL_Power_OnOff(FM_ADPLL_ON, ADPLL_clk);
    if(ret){
        WCN_DBG(FM_NTC | CHIP,"%s, ADPLL ON failed, [ret=%d]\n", __func__, ret);
        goto out;
    }
    //Set rgfrf_cnt_resync_b = 0
    if((ret = mt6620_set_bits(0x2A, 0, MASK(1))))//set 2AH D1=0
        goto out;
    //Set rgfrf_cnt_resync_b = 1
    if((ret = mt6620_set_bits(0x2A, BITn(1), MASK(1))))//set 2AH D1=1
        goto out; 
out:
    WCN_DBG(FM_NTC | CHIP,"-%s, [ADPLL_clk=%d][ret=%d]\n", __func__, (fm_s32)ADPLL_clk, ret);
    return ret;
}

/***********************************************************************
*  Frequency Avoidance
*
***********************************************************************/
fm_s32 MT6620_MCU_Freq_Avoid(fm_u16 freq, fm_s32 *freqavoid)
{
    fm_s32 ret = 0;
    fm_s32 mcuDsense = FM_MCU_DESE_DISABLE;
    fm_u16 len = 0;
    fm_u16 indx = 0;
    static fm_u16 FreqList[] ={7800, 7940, 8320, 9260, 9600, 10400};

    if (0 == fm_get_channel_space(freq)) {
        freq *= 10;
    }

    WCN_DBG(FM_DBG | CHIP,"+%s, [freq=%d]\n", __func__, (fm_s32)freq);

    *freqavoid = 0;
    
    len = sizeof(FreqList)/sizeof(FreqList[0]);
    indx = 0;
    while((indx < len) && (mcuDsense != FM_MCU_DESE_ENABLE)){
        if(FreqList[indx] == freq){
            mcuDsense = FM_MCU_DESE_ENABLE;
            *freqavoid = 1;
        }
        indx++;
    }

	if(mcuDsense == FM_MCU_DESE_DISABLE){
		if(mtk_wcn_wmt_dsns_ctrl(WMTDSNS_FM_DISABLE)){
			ret = 0;
		}else{
			ret = -FM_ELINK;
		}
	}else if(mcuDsense == FM_MCU_DESE_ENABLE){
		if(mtk_wcn_wmt_dsns_ctrl(WMTDSNS_FM_ENABLE)){
			ret = 0;
		}else{
			ret = -FM_ELINK;
		}
	}else{
		WCN_DBG(FM_ERR | CHIP,"para error!\n");
		ret = -FM_EPARA;
	}
    
    WCN_DBG(FM_NTC | CHIP,"-%s, [mcuDsense=%d][ret=%d]\n", __func__, (fm_s32)mcuDsense, ret);
    return ret;
}

/***********************************************************************
*  TX PWR CTRL
*
***********************************************************************/
fm_s32 MT6620_TX_PWR_CTRL(fm_u16 freq, fm_s32 *ctr)
{
    #define MT6620_TX_PWR_LEV_MAX 120
    #define MT6620_TX_PWR_LEV_MIN 85
    fm_s32 ret = 0;
    fm_s32 tmp = 0;
    fm_u16 reg = 0;
    fm_u16 coarse;
    fm_u16 fine;
    
    WCN_DBG(FM_DBG | CHIP,"+%s, [freq=%d]\n", __func__, (fm_s32)freq);

    if(freq < FM_TX_PWR_CTRL_FREQ_THR){
        //Power setting - 1dB, 3C(HEX)=A9E9
        *ctr -= 1;
    }else{
        //Power setting -2 dB, 3C(HEX)=A8E9
        *ctr -= 2;
    }
    
    if(*ctr > MT6620_TX_PWR_LEV_MAX){
        *ctr = MT6620_TX_PWR_LEV_MAX;
    }else if(*ctr < MT6620_TX_PWR_LEV_MIN){
        *ctr = MT6620_TX_PWR_LEV_MIN;
    }
    fine = 43017 + ((1<<((*ctr-85)%6))-1)*32;
    WCN_DBG(FM_DBG | CHIP,"0x3C = 0x%04x \n", fine);
    coarse = 514 + ((1<<((*ctr-85)/6))-1)*4;
    WCN_DBG(FM_DBG | CHIP,"0x3D = 0x%04x \n", coarse);
    
    if((ret = mt6620_write(0x3C, fine)))
            goto out;
    if((ret = mt6620_write(0x3D, coarse)))
            goto out;

    tmp = mtk_wcn_wmt_therm_ctrl(WMTTHERM_READ);
    if((ret = mt6620_read(0x9C, &reg)))
        goto out;
    reg &= 0xC0FF;
    if(tmp < FM_TX_PWR_CTRL_TMP_THR_DOWN){
        reg |= (0x1C << 8);  //9CH, D13~D8 = 1C
    }else if(tmp > FM_TX_PWR_CTRL_TMP_THR_UP){
        reg |= (0x33 << 8);  //9CH, D13~D8 ==33
    }else{
        reg |= (0x25 << 8);  //9CH, D13~D8 =25
    }
    if((ret = mt6620_write(0x9C, reg)))
        goto out;

out:
    WCN_DBG(FM_NTC | CHIP,"-%s, [temp=%d][ret=%d]\n", __func__, (fm_s32)tmp, ret);
    return ret;
}

/***********************************************************************
*  TX RTC PWR CTRL
*
***********************************************************************/
fm_s32 MT6620_RTC_Drift_CTRL(fm_u16 freq, fm_s32 *ctr)
{
    fm_s32 ret = 0;
    fm_u16 reg = 0;
    fm_s32 chanel_resolution = 1;
    fm_s16 compensation_int16 = 0;
    fm_s32 tmp = 0;
    fm_s32 drift = *ctr;
    
    WCN_DBG(FM_DBG | CHIP,"+%s, [freq=%d]\n", __func__, (fm_s32)freq);

    //turn off VCO tracking
    if((ret = mt6620_set_bits(0x48, 0, MASK(15))))//set 48 D15=0
        goto out;
    
    //get channel resolution
    if((ret = mt6620_read(0x46, &reg)))
        goto out;
    reg &= 0xC000;
    switch(reg >> 14){
        case 0:
            chanel_resolution = 1024;
            break;
        case 1:
            chanel_resolution = 512;
            break;
        case 2:
            chanel_resolution = 256;
            break;
        case 3:
            chanel_resolution = 128;
            break;
        default:
            WCN_DBG(FM_ERR | CHIP,"chanel_resolution error[%d]\n", (fm_s32)(reg >> 14));
            break;
    }

    //caculate and applye compensation
    if (0 == fm_get_channel_space(freq)) {
        freq *= 10;
    }
    WCN_DBG(FM_DBG | CHIP,"[resolution=%d][freq=%d][drift=%d]\n", chanel_resolution, (fm_s32)(freq/100), (*ctr));
    tmp = (2*drift*(freq/100))/chanel_resolution;
    compensation_int16 = (fm_s16)tmp;
    if(compensation_int16 >= 511){
        compensation_int16 = 511;
    }else if(compensation_int16 <= -512){
        compensation_int16 = -512;
    }
    if((ret = mt6620_read(0x47, &reg)))
        goto out;
    reg &= 0x003F;
    reg |= (compensation_int16 << 6);
    if((ret = mt6620_write(0x47, reg)))
        goto out;

    /*
    //turn on VCO tracking
    if((ret = mt6620_set_bits(0x48, BITn(15), MASK(15))))//set 48 D15=1
        goto out;
        */
out:
    WCN_DBG(FM_NTC | CHIP,"-%s, [compensation=%d][ret=%d]\n", __func__, (fm_s32)(compensation_int16), ret);
    return ret;
}

/***********************************************************************
*  TX desense with WIFI/BT
*
***********************************************************************/
fm_s32 MT6620_TX_DESENSE(fm_u16 freq, fm_s32 *ctr)
{
    fm_s32 ret = 0;
    fm_u16 dataRead = 0;
    fm_u16 tmp = 0;
    
    WCN_DBG(FM_DBG|CHIP,"+%s, [freq=%d]\n", __func__, (fm_s32)freq);

    // enable FM TX VCO tracking
    if((ret = mt6620_read(0x29, &dataRead)))//read 29 
        goto out;
    WCN_DBG(FM_NTC | CHIP,"Before VCO On, [0x29=0x%04x]\n", dataRead);
    if((ret = mt6620_read(0x12, &dataRead)))//read 12 
        goto out;
    WCN_DBG(FM_NTC | CHIP,"Before VCO On, [0x12=0x%04x]\n", dataRead);
    
    if((ret = mt6620_set_bits(0x12, 0, MASK(15))))//set 12 D15=0
        goto out;
    if((ret = mt6620_set_bits(0x41, BITn(0), MASK(0))))//set 41 D0=1
        goto out;
    if((ret = mt6620_set_bits(0x48, BITn(15), MASK(15))))//set 48 D15=1
        goto out;

    // wait 100ms (VCO tracking 100ms)
    if(*ctr > FM_TX_TRACKING_TIME_MAX){
        *ctr = FM_TX_TRACKING_TIME_MAX;
    }
    Delayms(*ctr);

    // disable FM TX VCO tracking
    if((ret = mt6620_set_bits(0x28, BITn(2), MASK(2))))//set 28 D2=1
        goto out;
    if((ret = mt6620_read(0x29, &dataRead)))//read 29 D11~D0
        goto out;
    WCN_DBG(FM_NTC | CHIP,"Before VCO Off, [0x29=0x%04x]\n", dataRead);
    tmp = dataRead&0x0FFF; // Read 0x29 D11~D0
    if((ret = mt6620_read(0x12, &dataRead)))//read 12 
        goto out;
    //Set 0x12 D15 to 1, D11:D0 to read(0x29 D11~D0)
    dataRead &= 0xF000; 
    dataRead |= tmp;
    dataRead |= 1<<15;
    if((ret = mt6620_write(0x12, dataRead)))
        goto out;
    WCN_DBG(FM_NTC | CHIP,"Before VCO Off, [0x12=0x%04x]\n", dataRead);
    if((ret = mt6620_set_bits(0x48, 0, MASK(15))))//set 48 D15=0
        goto out;
    if((ret = mt6620_set_bits(0x41, 0, MASK(0))))//set 41 D0=0
        goto out;
    
out:
    WCN_DBG(FM_DBG | CHIP,"-%s, [freq=%d][delay=%dms][ret=%d]\n", __func__, (fm_s32)freq, *ctr, ret);
    return ret;
}

static fm_s32 MT6620_Rds_Tx_Enable(void)
{
	mt6620_write(0x9F, 0x0000);
	mt6620_write(0xAB, 0x3872);
	mt6620_write(0xAC, 0x3B3A);
	mt6620_write(0xAD, 0x0113);
	mt6620_write(0xAE, 0x03B2);
	mt6620_write(0xAF, 0x0001);
	mt6620_write(0xB1, 0x63EB);
	mt6620_write(0xF4, 0x0020);
	mt6620_write(0xF5, 0x3222);

    return 0;
}

static fm_s32 MT6620_Rds_Tx_Disable(void)
{
	mt6620_write(0x9F, 0x0000);
	mt6620_write(0xAB, 0x39B6);
	mt6620_write(0xAC, 0x3C3E);
	mt6620_write(0xAD, 0x0000);
	mt6620_write(0xAE, 0x03C2);
	mt6620_write(0xAF, 0x0001);
	mt6620_write(0xF4, 0x0020);
	mt6620_write(0xF5, 0xBF16);
	mt6620_write(0xB1, 0x623D);

    return 0;
}

static fm_s32 MT6620_Rds_Tx(fm_u16 pi, fm_u16 *ps, fm_u16 *other_rds, fm_u8 other_rds_cnt)
{
    fm_s32 ret = 0;
    fm_u16 pkt_size = 0;

	WCN_DBG(FM_NTC | RDSC, "+%s():PI=0x%04x, PS=0x%04x/0x%04x/0x%04x, other_rds_cnt=%d \n",__func__, pi, ps[0], ps[1], ps[2], other_rds_cnt);
    if (FM_LOCK(cmd_buf_lock)) 
		return (-FM_ELOCK);
    pkt_size = mt6620_rds_tx(cmd_buf, TX_BUF_SIZE, pi, ps, other_rds, other_rds_cnt);
    ret = fm_cmd_tx(cmd_buf, pkt_size, FLAG_RDS_TX, SW_RETRY_CNT, RDS_TX_TIMEOUT, NULL);
    FM_UNLOCK(cmd_buf_lock);
        
    return ret;
}

