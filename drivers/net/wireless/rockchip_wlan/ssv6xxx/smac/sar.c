/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <ssv6200_reg.h>
#include <ssv6200_aux.h>
#include <linux/fs.h>
#include "dev.h"
#include "sar.h"

WIFI_FLASH_CCFG flash_cfg = {
    //16bytes
    0x6051, 0x3009, 0x20170519, 0x1, 0x0, 0x0,
    {   //16bytes
        {0x47c000, 0x47c000, 0x47c000, 0x9, 0x1d, 0x0},
        //16bytes
        {0x79807980, 0x79807980, 0x79807980, 0x9, 0x1d, 0x0}
    }
};
WIFI_FLASH_CCFG *pflash_cfg;

struct t_sar_info sar_info[] = {
    { SAR_LVL_INVALID, 0x0047c000, NULL},
    { SAR_LVL_INVALID, 0x79807980, NULL}
};

int sar_info_size = sizeof(sar_info) / sizeof(sar_info[0]); 

void flash_hexdump(void)
{
    unsigned i;
    const u8 *buf = (const char *)pflash_cfg;
    size_t len = sizeof(flash_cfg);
    printk("-----------------------------\n");
    printk("0x00h:");
    if (buf == NULL) {
        printk(" [NULL]");
    } else {
        for (i = 0; i < len; i++) {
            printk(" %02x", buf[i]);
            if ((i + 1) % 16 == 0) {
                printk("\n");
                if (i + 1 < len)
                    printk("0x%02xh:", i + 1);
            }
        }
    }
    printk("-----------------------------\n");
}

static u8 get_sar_lvl(u32 sar)
{
    static u32 prev_sar = 0;
    int i;
    u8 changed = 0x0;
 
    if (sar == prev_sar)
        return changed;
 
    printk("[thermal_sar] %d\n", (int)sar);

    for (i = 0; i < sar_info_size; i++) {
        if (sar_info[i].lvl == SAR_LVL_INVALID) {//if driver loaded under LT/HT env, it would cause wrong settings at this time.
            sar_info[i].lvl = SAR_LVL_RT;
            sar_info[i].value = sar_info[i].p->rt;	
            changed |= BIT(i);
        }
        else if (sar_info[i].lvl == SAR_LVL_RT) {
            if (sar < prev_sar) {
                if (sar <= (u32)(sar_info[i].p->lt_ts - 2)) { //we need check if (g_tt_lt - 1) < SAR_MIN
                    sar_info[i].lvl = SAR_LVL_LT;
                    sar_info[i].value = sar_info[i].p->lt;
                    changed |= BIT(i);
                }
            }
            else if (sar > prev_sar){
                if (sar >= (u32)(sar_info[i].p->ht_ts + 2)) { //we need check if (g_tt_lt + 1) > SAR_MAX
                    sar_info[i].lvl = SAR_LVL_HT;
                    sar_info[i].value = sar_info[i].p->ht;
                    changed |= BIT(i);
                }    
            }
        }
        else if (sar_info[i].lvl == SAR_LVL_LT) {
            if (sar >= (u32)(sar_info[i].p->lt_ts + 2)) {
                sar_info[i].lvl = SAR_LVL_RT;
                sar_info[i].value = sar_info[i].p->rt;
                changed |= BIT(i);
            }
        }
        else if (sar_info[i].lvl == SAR_LVL_HT) {
            if (sar <= (u32)(sar_info[i].p->ht_ts - 2)) {
                sar_info[i].lvl = SAR_LVL_RT;
                sar_info[i].value = sar_info[i].p->rt;
                changed |= BIT(i);
            }
        }
    }
    if (changed) {
        printk("changed: 0x%x\n", changed);
	}
    prev_sar = sar;
    return changed;
}

void sar_monitor(u32 curr_sar, struct ssv_softc *sc)
{
    //static u32 prev_sar_lvl = SAR_LVL_INVALID; //sar = 0, temparature < -25C
    u8 changed;
    changed = get_sar_lvl(curr_sar);

    if (changed & BIT(SAR_TXGAIN_INDEX)) {
        printk("TXGAIN: 0x%08x\n", sar_info[SAR_TXGAIN_INDEX].value);
        SMAC_REG_WRITE(sc->sh, ADR_TX_GAIN_FACTOR, sar_info[SAR_TXGAIN_INDEX].value);
    }
    if (changed & BIT(SAR_XTAL_INDEX)) {
        printk("XTAL: 0x%08x\n", sar_info[SAR_XTAL_INDEX].value);
        SMAC_REG_WRITE(sc->sh, ADR_SYN_KVCO_XO_FINE_TUNE_CBANK, sar_info[SAR_XTAL_INDEX].value);
    }
}

/*
    SET_RG_SARADC_THERMAL(1);     //ce010030[26]
    SET_RG_EN_SARADC(1);          //ce010030[30]
    while(!GET_SAR_ADC_FSM_RDY);  //ce010094[23]
    sar_code = GET_RG_SARADC_BIT; //ce010094[21:16]
    SET_RG_SARADC_THERMAL(0);
    SET_RG_EN_SARADC(0);
*/
void thermal_monitor(struct work_struct *work)
{
    struct ssv_softc *sc = container_of(work, struct ssv_softc, thermal_monitor_work.work);
    u32 curr_sar;

    u32 temp;
    if (sc->ps_status == PWRSV_PREPARE) {
        printk("sar PWRSV_PREPARE\n");
        return;
    }

    mutex_lock(&sc->mutex);
    SMAC_REG_READ(sc->sh, ADR_RX_11B_CCA_1, &temp);
    if (temp == RX_11B_CCA_IN_SCAN) {
        printk("in scan\n");
        mutex_unlock(&sc->mutex);
        queue_delayed_work(sc->thermal_wq, &sc->thermal_monitor_work, THERMAL_MONITOR_TIME);
        return;
    }
    SMAC_REG_READ(sc->sh, ADR_RX_ADC_REGISTER, &temp);
    //printk("ori %08x:%08x\n", ADR_RX_ADC_REGISTER, temp);
    SMAC_REG_SET_BITS(sc->sh, ADR_RX_ADC_REGISTER, (1 << RG_SARADC_THERMAL_SFT),
              RG_SARADC_THERMAL_MSK);
    SMAC_REG_SET_BITS(sc->sh, ADR_RX_ADC_REGISTER, (1 <<  RG_EN_SARADC_SFT),
              RG_EN_SARADC_MSK);

    do {
        msleep(1);
        SMAC_REG_READ(sc->sh, ADR_READ_ONLY_FLAGS_1, &temp);
    } while (((temp & SAR_ADC_FSM_RDY_MSK) >> SAR_ADC_FSM_RDY_SFT) != 1);
    //printk("SAR_ADC_FSM_RDY_STAT %d\n", (temp & SAR_ADC_FSM_RDY_MSK) >> SAR_ADC_FSM_RDY_SFT);
    curr_sar = (temp & RG_SARADC_BIT_MSK) >> RG_SARADC_BIT_SFT;
    SMAC_REG_READ(sc->sh, ADR_RX_ADC_REGISTER, &temp);

    //printk("new %08x:%08x\n", ADR_RX_ADC_REGISTER, temp);
    
    SMAC_REG_SET_BITS(sc->sh, ADR_RX_ADC_REGISTER, (0 << RG_SARADC_THERMAL_SFT),
              RG_SARADC_THERMAL_MSK);
    SMAC_REG_SET_BITS(sc->sh, ADR_RX_ADC_REGISTER, (0 <<  RG_EN_SARADC_SFT),
              RG_EN_SARADC_MSK);
    sar_monitor(curr_sar, sc);

    mutex_unlock(&sc->mutex);

    queue_delayed_work(sc->thermal_wq, &sc->thermal_monitor_work, THERMAL_MONITOR_TIME);
}

int get_flash_info(struct ssv_softc *sc)
{
    struct file *fp = (struct file *)NULL;
    mm_segment_t fs;
    int i, ret;

    pflash_cfg = &flash_cfg;

    if (sc->sh->cfg.flash_bin_path[0] != 0x00) {
        fp = filp_open(sc->sh->cfg.flash_bin_path, O_RDONLY, 0);
		 if (IS_ERR(fp) || fp == NULL) {
             fp = filp_open(SEC_CFG_BIN_NAME, O_RDONLY, 0);
         }
    }
    else{
        fp = filp_open(DEFAULT_CFG_BIN_NAME, O_RDONLY, 0);
		if (IS_ERR(fp) || fp == NULL) {
			fp = filp_open(SEC_CFG_BIN_NAME, O_RDONLY, 0);
		}
    }
    if (IS_ERR(fp) || fp == NULL) {
        printk("flash_file %s not found, disable sar\n",DEFAULT_CFG_BIN_NAME);
        //WARN_ON(1);
        ret = 0;
        return ret;
    }

    fs = get_fs();
    set_fs(KERNEL_DS);
    fp->f_op->read(fp, (char *)pflash_cfg, sizeof(flash_cfg), &fp->f_pos);
    set_fs(fs);
    
    filp_close(fp, NULL);
    ret = 1;

    flash_hexdump();
    for (i = 0; i < sar_info_size; i++) {
        sar_info[i].p = &flash_cfg.sar_rlh[i];
        printk("rt = %x, lt = %x, ht = %x\n", sar_info[i].p->rt, sar_info[i].p->lt, sar_info[i].p->ht);
        printk("lt_ts = %x, ht_ts = %x\n", sar_info[i].p->lt_ts, sar_info[i].p->ht_ts);
    }
    return ret;
}

