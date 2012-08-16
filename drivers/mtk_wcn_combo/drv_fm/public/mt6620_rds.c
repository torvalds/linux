/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h> // udelay()
#include <linux/device.h> // device_create()
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/cdev.h>
#include <linux/fs.h>

#include <asm/uaccess.h> // get_user()

#include "fm.h"
#include "mt6620_fm.h"
#include "mt6620_fm_lib.h"

/******************************************************************************
 * GLOBAL DATA
 *****************************************************************************/
#define MT6620_RDS_BLER_TH1 90
#define MT6620_RDS_BLER_TH2 60
#define MT6620_RDS_BLER_C1  12
#define MT6620_RDS_BLER_C2  6
#define MT6620_RDS_BLER_T1  5000
#define MT6620_RDS_BLER_T2  5000

//FM_RDS_DATA_CRC_FFOST(0xB2)
#define FM_RDS_GDBK_IND_A	 (0x08)	
#define FM_RDS_GDBK_IND_B	 (0x04)	
#define FM_RDS_GDBK_IND_C	 (0x02)	
#define FM_RDS_GDBK_IND_D	 (0x01)	
#define FM_RDS_DCO_FIFO_OFST (0x01E0)
#define	FM_RDS_READ_DELAY	 (0x80)


/******************************************************************************
 * GLOBAL VARIABLE
 *****************************************************************************/
static bool bRDS_FirstIn = false;
static uint16_t RDS_Sync_Cnt = 0, RDS_Block_Reset_Cnt = 0;

extern int16_t _current_frequency;
uint32_t gBLER_CHK_INTERVAL = 5000;
static int16_t preAF_Num = 0;
static int16_t preAFON_Num = 0;
uint16_t GOOD_BLK_CNT = 0, BAD_BLK_CNT = 0;
uint8_t BAD_BLK_RATIO = 0;

#ifndef FM_ASSERT
#define FM_ASSERT(a) { \
			if ((a) == NULL) { \
				printk("%s, invalid buf\n", __func__);\
				return -ERR_INVALID_BUF; \
			} \
		}
#endif

#define RDS_RT_MULTI_REV_TH 16
enum
{
    RDS_GRP_VER_A = 0,  //group version A
    RDS_GRP_VER_B
};

typedef enum RDS_RT_STATE_MACHINE
{
    RDS_RT_START = 0,
    RDS_RT_DECISION,
    RDS_RT_GETLEN,
    RDS_RT_DISPLAY,
    RDS_RT_FINISH,
    RDS_RT_MAX
}RDS_RT_STATE_MACHINE;

typedef enum RDS_BLK_T
{
    RDS_BLK_A = 0,
    RDS_BLK_B,
    RDS_BLK_C,
    RDS_BLK_D,
    RDS_BLK_MAX
}RDS_BLK_T;

static RDS_RT_STATE_MACHINE rt_state_machine = RDS_RT_START;

/******************************************************************************
 * Local function extern
 *****************************************************************************/
static int MT6620_RDS_enable(void);
static int MT6620_RDS_disable(void);
static int MT6620_RDS_Get_GoodBlock_Counter(uint16_t* pCnt);
static int MT6620_RDS_Get_BadBlock_Counter(uint16_t* pCnt);
static int MT6620_RDS_Reset_Block_Counter(void);
static int MT6620_RDS_Reset(void);
static int MT6620_RDS_Reset_Block(void);
static int MT6620_RDS_Init_Data(RDSData_Struct *pstRDSData);
static int MT6620_RDS_RetrieveGroup0(uint16_t *block_data, uint8_t SubType, RDSData_Struct *pstRDSData);
static int MT6620_RDS_RetrieveGroup1(uint16_t *block_data, uint8_t SubType, RDSData_Struct *pstRDSData);
static int MT6620_RDS_RetrieveGroup2(uint16_t *block_data, uint8_t SubType, RDSData_Struct *pstRDSData);
static int MT6620_RDS_RetrieveGroup4(uint16_t *block_data, uint8_t SubType, RDSData_Struct *pstRDSData);
static int MT6620_RDS_RetrieveGroup14(uint16_t *block_data, uint8_t SubType, RDSData_Struct *pstRDSData);
extern int MT6620_read(uint8_t addr, uint16_t *val);
extern int MT6620_write(uint8_t addr, uint16_t val);
extern int MT6620_set_bits(uint8_t addr, uint16_t bits, uint16_t mask);
extern int  Delayms(uint32_t data);
extern int  Delayus(uint32_t data);

/*
 * rds_cnt_get
 * To get rds group count form raw data
 * If success return 0, else return error code
*/
static int rds_cnt_get(struct rds_rx *rds_raw, int raw_size, int *cnt){
    int ret = 0;
    int gap = sizeof(rds_raw->cos) + sizeof(rds_raw->sin);

    FM_ASSERT(rds_raw);
    FM_ASSERT(cnt);
    *cnt = (raw_size - gap)/sizeof(rds_packet_struct);
    FM_LOG_INF(D_RDS,"group cnt=%d\n", *cnt);
    
    return ret;
}

/*
 * rds_group_get
 * To get rds group[n] data form raw data with index
 * If success return 0, else return error code
*/
static int rds_group_get(uint16_t *dst, struct rds_rx *raw, int idx){
    int ret = 0;

    FM_ASSERT(dst);
    FM_ASSERT(raw);
    if(idx > (MAX_RDS_RX_GROUP_CNT - 1)){
        ret = -ERR_INVALID_PARA;
        return ret;
    }
    dst[0] = raw->data[idx].blkA;
    dst[1] = raw->data[idx].blkB;
    dst[2] = raw->data[idx].blkC;
    dst[3] = raw->data[idx].blkD;
    dst[4] = raw->data[idx].crc;
    dst[5] = raw->data[idx].cbc;
    
    FM_LOG_INF(D_RDS,"BLOCK:%04x %04x %04x %04x, CRC:%04x\n", dst[0], dst[1], dst[2], dst[3], dst[4]);
    
    return ret;
}

/*
 * rds_checksum_check
 * To check CRC rerult, if OK, *valid=TRUE, else *valid=FALSE
 * If success return 0, else return error code
*/
static int rds_checksum_check(uint16_t crc, int mask, bool *valid){
    int ret = 0;

    FM_ASSERT(valid);
    if((crc & mask) == mask){
        *valid = TRUE;
    }else{
        *valid = FALSE;
    }  
    
    return ret;
}

/*
 * rds_cbc_get - To get block_n's correct bit count form cbc
 * @cbc, the group's correct bit count
 * @blk, target the block 
 * 
 * If success, return block_n's cbc, else error code
*/
static int rds_cbc_get(uint16_t cbc, enum RDS_BLK_T blk){
    int ret = 0;

    switch(blk){
        case RDS_BLK_A:
            ret = (cbc & 0xF000) >> 12;
            break;
        case RDS_BLK_B:
            ret = (cbc & 0x0F00) >> 8;
            break;
        case RDS_BLK_C:
            ret = (cbc & 0x00F0) >> 4;
            break;
        case RDS_BLK_D:
            ret = (cbc & 0x000F) >> 0;
            break;
        default:
            break;
    }
    FM_LOG_INF(D_RDS,"group cbc=0x%04x\n", cbc);
    return ret;
}

/*
 * rds_event_set
 * To set rds event, and user space can use this flag to juge which event happened
 * If success return 0, else return error code
*/
static int rds_event_set(uint16_t *events, int event_mask){
    int ret = 0;

    FM_ASSERT(events);
    *events |= event_mask;
    
    return ret;
}

/*
 * rds_flag_set
 * To set rds event flag, and user space can use this flag to juge which event happened
 * If success return 0, else return error code
*/
static int rds_flag_set(uint32_t *flags, int flag_mask){
    int ret = 0;

    FM_ASSERT(flags);
    *flags |= flag_mask;
    
    return ret;
}

/*
 * rds_group_type_get
 * To get rds group type form blockB
 * If success return 0, else return error code
*/
static int rds_group_type_get(uint16_t crc, uint16_t blk, uint8_t *type, uint8_t *subtype){
    int ret = 0;
    bool valid = FALSE;

    FM_ASSERT(type);
    FM_ASSERT(subtype);
    //to get the group type from block B
    ret = rds_checksum_check(crc, FM_RDS_GDBK_IND_B, &valid);
    if(valid == TRUE){
        *type = (blk & 0xF000)>>12; //Group type(4bits)
        *subtype = (blk & 0x0800)>>11; //version code(1bit), 0=vesionA, 1=versionB
    }else{
        FM_LOG_WAR(D_RDS,"Block1 CRC err\n");
        return -ERR_RDS_CRC;
    }
    
    FM_LOG_INF(D_RDS,"Type=%d, subtype:%s\n", (int)*type, *subtype ? "version B" : "version A");
    return ret;
}

/*
 * rds_group_counter_add
 * @type -- group type, rang: 0~15
 * @subtype -- sub group type, rang:0~1
 *
 * add group counter, group0a~group15b
 * we use type value as the index
 * If success return 0, else return error code
*/
static int rds_group_counter_add(uint8_t type, uint8_t subtype, struct rds_group_cnt *gc)
{
    FM_ASSERT(gc);

    if(type > 15){
        return -ERR_INVALID_PARA;
    }
    switch(subtype){
        case RDS_GRP_VER_A:
            gc->groupA[type]++;
            break;
        case RDS_GRP_VER_B:
            gc->groupB[type]++;
            break;
        default:
            return -ERR_INVALID_PARA;
            break;
    }
    gc->total++;
    FM_LOG_INF(D_RDS,"group counter:%d\n", (int)gc->total);
    return 0;
}

/*
 * rds_group_counter_get
 *
 * read group counter , group0a~group15b
 * If success return 0, else return error code
*/
extern int rds_group_counter_get(struct rds_group_cnt *dst, struct rds_group_cnt *src)
{
    FM_ASSERT(dst);
    FM_ASSERT(src);
    memcpy(dst, src, sizeof(struct rds_group_cnt));
    return 0;
}

/*
 * rds_group_counter_reset
 *
 * clear group counter to 0, group0a~group15b
 * If success return 0, else return error code
*/
extern int rds_group_counter_reset(struct rds_group_cnt *gc)
{
    FM_ASSERT(gc);
    memset(gc, 0, sizeof(struct rds_group_cnt));
    return 0;
}

/*
 * rds_group_pi_get
 * To get rds group pi code form blockA
 * If success return 0, else return error code
*/
static int rds_group_pi_get(uint16_t crc, uint16_t blk, uint16_t *pi, bool *dirty){
    int ret = 0;
    bool valid = FALSE;

    FM_ASSERT(pi);
    FM_ASSERT(dirty);
    
    //to get the group pi code from block A
    ret = rds_checksum_check(crc, FM_RDS_GDBK_IND_A, &valid);
    if(valid == TRUE){
        if(*pi != blk){
	        //PI=program Identication
            *pi = blk;
			*dirty = TRUE; // yes, we got new PI code
        }else{
            *dirty = FALSE; // PI is the same as last one
        }
    }else{
        FM_LOG_WAR(D_RDS,"Block0 CRC err\n");
        return -ERR_RDS_CRC;
    }

    FM_LOG_INF(D_RDS,"PI=0x%04x, %s\n", *pi, *dirty ? "new" : "old");
    return ret;
}

/*
 * rds_group_pty_get
 * To get rds group pty code form blockB
 * If success return 0, else return error code
*/
static int rds_group_pty_get(uint16_t crc, uint16_t blk, uint8_t *pty, bool *dirty){
    int ret = 0;
    bool valid = FALSE;

    FM_ASSERT(pty);
    FM_ASSERT(dirty);
    
    //to get PTY code from block B
    ret = rds_checksum_check(crc, FM_RDS_GDBK_IND_B, &valid);
    if(valid == FALSE){
        FM_LOG_WAR(D_RDS,"Block1 CRC err\n");
        return -ERR_RDS_CRC;
    }

    if(*pty != ((blk & 0x03E0)>>5)){
        //PTY=Program Type Code
        *pty = (blk&0x03E0)>>5;
        *dirty = TRUE; // yes, we got new PTY code
    }else{
        *dirty = FALSE; // PTY is the same as last one
    }

    FM_LOG_INF(D_RDS,"PTY=%d, %s\n", (int)*pty, *dirty ? "new" : "old");
    return ret;
}

/*
 * rds_group_tp_get
 * To get rds group tp code form blockB
 * If success return 0, else return error code
*/
static int rds_group_tp_get(uint16_t crc, uint16_t blk, uint8_t *tp, bool *dirty){
    int ret = 0;
    bool valid = FALSE;

    FM_ASSERT(tp);
    FM_ASSERT(dirty);
    
    //to get TP code from block B
    ret = rds_checksum_check(crc, FM_RDS_GDBK_IND_B, &valid);
    if(valid == FALSE){
        FM_LOG_WAR(D_RDS,"Block1 CRC err\n");
        return -ERR_RDS_CRC;
    }

    if(*tp != ((blk&0x0400)>>10)){
        //Tranfic Program Identification
        *tp = (blk&0x0400)>>10;
        *dirty = TRUE; // yes, we got new TP code
    }else{
        *dirty = FALSE; // TP is the same as last one
    }

    FM_LOG_INF(D_RDS,"TP=%d, %s\n", (int)*tp, *dirty ? "new" : "old");
    return ret;
}

/*
 * rds_group0_ta_get
 * To get rds group ta code form blockB
 * If success return 0, else return error code
*/
static int rds_group0_ta_get(uint16_t blk, uint8_t *ta, bool *dirty){
    int ret = 0;

    FM_ASSERT(ta);
    FM_ASSERT(dirty);
    //TA=Traffic Announcement code
    if(*ta != ((blk & 0x0010)>>4)){
		*ta = (blk & 0x0010)>>4;		   	
		*dirty = TRUE; // yes, we got new TA code	
	}else{
	    *dirty = FALSE; // TA is the same as last one
	}

    FM_LOG_INF(D_G0,"TA=%d, %s\n", (int)*ta, *dirty ? "new" : "old");
    return ret;
}

/*
 * rds_group0_music_get
 * To get music-speech switch code form blockB
 * If success return 0, else return error code
*/
static int rds_group0_music_get(uint16_t blk, uint8_t *music, bool *dirty){
    int ret = 0;

    FM_ASSERT(music);
    FM_ASSERT(dirty);
    //M/S=music speech switch code
    if(*music != ((blk & 0x0008)>>3)){
		*music = (blk & 0x0008)>>3;		   	
		*dirty = TRUE; // yes, we got new music code	
	}else{
	    *dirty = FALSE; // music  is the same as last one
	}

    FM_LOG_INF(D_G0,"Music=%d, %s\n", (int)*music, *dirty ? "new" : "old");
    return ret;
}

/*
 * rds_group2_rt_addr_get
 * To get rt addr form blockB
 * If success return 0, else return error code
*/
static int rds_group2_rt_addr_get(uint16_t blk, uint8_t *addr){
    int ret = 0;

    FM_ASSERT(addr);
    *addr = (uint8_t)blk & 0x0F;
    
    FM_LOG_INF(D_G2,"addr=0x%02x\n", *addr);
    return ret;
}

static int rds_group2_txtAB_get(uint16_t blk, uint8_t *txtAB, bool *dirty){
    int ret = 0;

    FM_ASSERT(txtAB);
    FM_ASSERT(dirty);

    if(*txtAB != ((blk&0x0010)>>4)){
		*txtAB = (blk&0x0010)>>4;
        *dirty = TRUE; // yes, we got new txtAB code
    }else{
        *dirty = FALSE; // txtAB is the same as last one
    }
    
    FM_LOG_INF(D_G2,"txtAB=%d, %s\n", *txtAB, *dirty ? "new" : "old");
    return ret;
}

static int rds_group2_rt_get(uint16_t crc, uint8_t subtype, uint16_t blkC, uint16_t blkD, uint8_t addr, uint8_t *buf){
    int ret = 0;
    bool valid = FALSE;
    int idx = 0;

    FM_ASSERT(buf);
    //text segment addr rang 0~15
    if(addr > 0xFF){
        FM_LOG_ERR(D_RDS,"addr invalid(0x%02x)\n", addr);
        ret = -ERR_INVALID_PARA;
        return ret;
    }
    switch(subtype){
        case RDS_GRP_VER_A:
            idx = 4*addr;
            ret = rds_checksum_check(crc, FM_RDS_GDBK_IND_C | FM_RDS_GDBK_IND_D, &valid);
            if(valid == TRUE){
                buf[idx] = blkC>>8;
                buf[idx+1] = blkC&0xFF;
                buf[idx+2] = blkD>>8;
                buf[idx+3] = blkD&0xFF;
            }else{
                FM_LOG_ERR(D_RDS,"rt crc check err\n");
                ret = -ERR_RDS_CRC;
            }
            break;
        case RDS_GRP_VER_B:
            idx = 2*addr;
            ret = rds_checksum_check(crc, FM_RDS_GDBK_IND_D, &valid);
            if(valid == TRUE){
                buf[idx] = blkD>>8;
                buf[idx+1] = blkD&0xFF;
            }else{
                FM_LOG_ERR(D_RDS,"rt crc check err\n");
                ret = -ERR_RDS_CRC;
            }
            break;
        default:
            break;
    }
    
    FM_LOG_INF(D_G2,"addr[%02x]:0x%02x 0x%02x 0x%02x 0x%02x\n", addr, buf[idx], buf[idx+1], buf[idx+2], buf[idx+3]);
    return ret;
}

static int rds_group2_rt_get_len(uint8_t subtype, int pos, int *len){
    int ret = 0;

    FM_ASSERT(len);
    if(subtype == RDS_GRP_VER_A){
        *len = 4*(pos+1);
    }else{
        *len = 2*(pos+1);
    }
    
    return ret;
}

/*
 * rds_group2_rt_cmp
 * this function is the most importent flow for RT parsing
 * 1.Compare fresh buf with once buf per byte, if eque copy this byte to twice buf, else copy it to once buf
 * 2.Check wether we got a full segment, for typeA if copyed 4bytes to twice buf, for typeB 2bytes copyed to twice buf
 * 3.Check wether we got the end of RT, if we got 0x0D
 * 4.If we got the end, then caculate the RT lenth
 * If success return 0, else return error code
*/
static int rds_group2_rt_cmp(uint8_t addr, uint16_t cbc, uint8_t subtype, uint8_t *fresh, 
                uint8_t *once, uint8_t *twice, bool *valid, bool *end, int *len){
    int ret = 0;
    int i = 0;
    int j = 0; 
    int cnt = 0;

    FM_ASSERT(fresh);
    FM_ASSERT(once);
    FM_ASSERT(twice);
    FM_ASSERT(valid);
    FM_ASSERT(end);

    j = (subtype == RDS_GRP_VER_A) ? 4 : 2; // RT segment width
    if(subtype == RDS_GRP_VER_A){
        if(rds_cbc_get(cbc, RDS_BLK_C) == 0){
            once[j*addr+0] = fresh[j*addr+0];
            once[j*addr+1] = fresh[j*addr+1];
        }
        if(rds_cbc_get(cbc, RDS_BLK_D) == 0){
            once[j*addr+2] = fresh[j*addr+2];
            once[j*addr+3] = fresh[j*addr+3];
        }
    }else if(subtype == RDS_GRP_VER_B){
        if(rds_cbc_get(cbc, RDS_BLK_D) == 0){
            once[j*addr+0] = fresh[j*addr+0];
            once[j*addr+1] = fresh[j*addr+1];
        }
    }
    
    for(i = 0; i < j; i++){
        if(fresh[j*addr+i] == once[j*addr+i]){
            twice[j*addr+i] = once[j*addr+i]; //get the same byte 2 times
            cnt++;
        }else{
            once[j*addr+i] = fresh[j*addr+i]; //use new val
        }

        //if we got 0x0D twice, it means a RT end
        if(twice[j*addr+i] == 0x0D){
            *end = TRUE;
            *len = j*addr+i+1; //record the length of RT
        }
    }  		    

    //check if we got a valid segment 4bytes for typeA, 2bytes for typeB
    if(cnt == j){
        *valid = TRUE;
    }else{
        *valid = FALSE;
    }
    FM_LOG_INF(D_G2,"RT seg=%s\n", *valid == TRUE ? "TRUE" : "FALSE");
    FM_LOG_INF(D_G2,"RT end=%s\n", *end == TRUE ? "TRUE" : "FALSE");
    FM_LOG_INF(D_G2,"RT len=%d\n", *len);
    return ret;
}

static uint16_t rds_group2_rt_addr_bitmap_get(uint16_t bitmap){
    return bitmap;
}

static int rds_group2_rt_addr_bitmap_get_pos(uint16_t bitmap){
    int i = 15;
    while(!(bitmap & (1<<i)) && (i > -1)){	
        i--;
    }
    return i;
}

#if 0
static bool rds_group2_rt_addr_bitmap_test(uint16_t bitmap, uint8_t addr){
    return (bitmap & (1<<addr)) ? TRUE : FALSE;
}
#endif

static int rds_group2_rt_addr_bitmap_clear(uint16_t *bitmap, int *bm_cnt){
    int ret = 0;

    FM_ASSERT(bitmap);
    FM_ASSERT(bm_cnt);
    *bitmap = 0x0000;
    *bm_cnt = 0;
    return ret;
}

/*
 * rds_group2_rt_addr_bitmap_cmp - compare two bitmaps
 * @bitmap1
 * @bitmap2
 * If bitmap1 > bitmap2, return positive(+)
 * If bitmap1 = bitmap2, return 0
 * If bitmap1 < bitmap2, return nagotive(-)
 */
static int rds_group2_rt_addr_bitmap_cmp(uint16_t bitmap1, uint16_t bitmap2)
{
    return (int)(bitmap1 - bitmap2);
}

static int rds_group2_rt_addr_bitmap_set(uint16_t *bitmap, int *bm_cnt, uint8_t addr){
    int ret = 0;
    uint16_t bm_old = 0;

    FM_ASSERT(bitmap);
    FM_ASSERT(bm_cnt);
    //text segment addr rang 0~15
    if(addr > 0xFF){
        FM_LOG_ERR(D_RDS,"addr invalid(0x%02x)\n", addr);
        ret = -ERR_INVALID_PARA;
        return ret;
    }
    bm_old = *bitmap;
    *bitmap |= (1<<addr); //set bitmap
    if(!rds_group2_rt_addr_bitmap_cmp(bm_old, *bitmap)){
        (*bm_cnt)++;  // multi get a segment
    }else if(*bm_cnt > 0){
        (*bm_cnt)--;
    }
    FM_LOG_NTC(D_G2,"RT bitmap=0x%04x, bmcnt=%d\n", *bitmap, *bm_cnt);
    return ret;
}

static RDS_RT_STATE_MACHINE rds_rt_state_get(void){
    return rt_state_machine;
}

static RDS_RT_STATE_MACHINE rds_rt_state_set(RDS_RT_STATE_MACHINE state_new){
    rt_state_machine = state_new;
    return rt_state_machine;
}

#if 0
static bool rds_group2_is_rt_finished(uint16_t *event){
    FM_ASSERT(event);
    if(*event&RDS_EVENT_LAST_RADIOTEXT){
        return TRUE;
    }else{
        return FALSE;
    }
}
#endif

static int MT6620_RDS_enable(void)
{
    uint16_t page;
    int ret = 0;
    
    if((ret = MT6620_read(FM_MAIN_PGSEL, &page)))
        return ret;

	if((ret = MT6620_write(0x9F, 0x0003)))
        return ret;
	if((ret = MT6620_write(0xCB, 0xE016)))
        return ret;
	if((ret = MT6620_write(0x9F, 0x0000)))
        return ret;
	if((ret = MT6620_write(0x63, 0x0491)))
        return ret;
	if((ret = MT6620_set_bits(0x6B, 0x2000, 0xFFFF)))
        return ret;

    ret = MT6620_write(FM_MAIN_PGSEL, page);

    return ret;
}

static int MT6620_RDS_disable(void)
{
    int ret = 0;
	if((ret = MT6620_set_bits(0x6B, 0x0000, 0xDFFF)))
        return ret;
	ret = MT6620_write(0x63, 0x0481);

    return ret;
}

static int MT6620_RDS_Get_GoodBlock_Counter(uint16_t* pCnt)
{
    uint16_t tmp_reg;
    uint16_t page;
    int ret = 0;

    if(NULL == pCnt)
        return -ERR_INVALID_BUF;
    
    if((ret = MT6620_read(FM_MAIN_PGSEL, &page)))
        return ret;

    if((ret = MT6620_write(0x9F, 0x0003)))
        return ret;
    if((ret = MT6620_read(0xC6, &tmp_reg)))
        return ret; 

    ret = MT6620_write(FM_MAIN_PGSEL, page);
    
    *pCnt = tmp_reg;
    return ret;  
}

static int MT6620_RDS_Get_BadBlock_Counter(uint16_t* pCnt)
{
    uint16_t tmp_reg;
    uint16_t page;
    int ret = 0;

    if(NULL == pCnt)
        return -ERR_INVALID_BUF;
    
    if((ret = MT6620_read(FM_MAIN_PGSEL, &page)))
        return ret;

    if((ret = MT6620_write(0x9F, 0x0003)))
        return ret;
    if((ret = MT6620_read(0xC7, &tmp_reg)))
        return ret;

    ret = MT6620_write(FM_MAIN_PGSEL, page);

    *pCnt = tmp_reg;
    return ret;  
}


static int MT6620_RDS_Reset_Block_Counter(void)
{
    uint16_t page;
    int ret = 0;
    
    if((ret = MT6620_read(FM_MAIN_PGSEL, &page)))
        return ret;

    if((ret = MT6620_write(0x9F, 0x0003)))
        return ret;
    if((ret = MT6620_write(0xC8, 0x0001)))
        return ret;
    if((ret = MT6620_write(0xC8, 0x0002)))
        return ret;

    ret = MT6620_write(FM_MAIN_PGSEL, page);

    return ret;
}

static int MT6620_RDS_Reset(void)
{
    uint16_t page;
    int ret = 0;
    
    if((ret = MT6620_read(FM_MAIN_PGSEL, &page)))
        return ret;

    if((ret = MT6620_write(0x9F, 0x0003)))
        return ret;
    if((ret = MT6620_write(0xB0, 0x0001)))
        return ret;

    ret = MT6620_write(FM_MAIN_PGSEL, page);

    return ret;
}

static int MT6620_RDS_Reset_Block(void)
{
    uint16_t page;
    int ret = 0;
    
    if((ret = MT6620_read(FM_MAIN_PGSEL, &page)))
        return ret;

    if((ret = MT6620_write(0x9F, 0x0003)))
        return ret;
    if((ret = MT6620_write(0xDD, 0x0001)))
        return ret;

    ret = MT6620_write(FM_MAIN_PGSEL, page);

    return ret;
}

int MT6620_RDS_BlerCheck(struct fm *fm)
{
	RDSData_Struct *pstRDSData = fm->pstRDSData;
    uint16_t TOTAL_CNT;
    int ret = 0;
    
	FM_LOG_DBG(D_BLKC,"+T:%d, MT6620_RDS_BlerCheck\n", jiffies_to_msecs(jiffies));
	
    if(pstRDSData->AF_Data.Addr_Cnt == 0xFF){
	    //AF List Finished
	    pstRDSData->event_status |= RDS_EVENT_AF;  //Need notfiy application
	    //loop pstRDSData->event_status then act 
        if(pstRDSData->event_status != 0){
            fm->RDS_Data_ready = true;
            wake_up_interruptible(&fm->read_wait);
            FM_LOG_DBG(D_BLKC,"RDS_EVENT_AF, trigger read\n");
        }
	}
	
	gBLER_CHK_INTERVAL = MT6620_RDS_BLER_T1;
	if((ret = MT6620_RDS_Get_GoodBlock_Counter(&GOOD_BLK_CNT)))
        return ret;
	FM_LOG_DBG(D_BLKC,"-T:%d, GOOD_BLK_CNT:%d\n", jiffies_to_msecs(jiffies), GOOD_BLK_CNT);
	if((ret = MT6620_RDS_Get_BadBlock_Counter(&BAD_BLK_CNT)))
        return ret;
	TOTAL_CNT = GOOD_BLK_CNT + BAD_BLK_CNT;
	if((ret = MT6620_RDS_Reset_Block_Counter()))
        return ret;
	FM_LOG_DBG(D_BLKC,"BLER: TOTAL_CNT:%d BAD_BLK_CNT:%d, RDS_Sync_Cnt:%d\n", TOTAL_CNT, BAD_BLK_CNT, RDS_Sync_Cnt);
	
	if((GOOD_BLK_CNT==0)&&(BAD_BLK_CNT==0)){
        BAD_BLK_RATIO = 0;
    }else{
        BAD_BLK_RATIO = (BAD_BLK_CNT*100)/TOTAL_CNT;
    }

	//MT6620_RDS_BLER_TH1 90
	//MT6620_RDS_BLER_TH2 60
	//MT6620_RDS_BLER_C1  12
	//MT6620_RDS_BLER_C2  6
	//MT6620_RDS_BLER_T2  5000
	if((BAD_BLK_RATIO < MT6620_RDS_BLER_TH2)&&(RDS_Sync_Cnt > MT6620_RDS_BLER_C1)){
		gBLER_CHK_INTERVAL = MT6620_RDS_BLER_T2;
		if(RDS_Block_Reset_Cnt > 1)
			RDS_Block_Reset_Cnt--;
	}else{
		if(BAD_BLK_RATIO > MT6620_RDS_BLER_TH1){
		    //>90%
			if((ret = MT6620_RDS_Reset_Block_Counter()))
                return ret;
			RDS_Sync_Cnt = 0;   //need clear or not, Question, LCH.
			RDS_Block_Reset_Cnt++;
			if((RDS_Block_Reset_Cnt > MT6620_RDS_BLER_C2)||bRDS_FirstIn){
                if(bRDS_FirstIn)
				    bRDS_FirstIn = false;
				if((ret = MT6620_RDS_Reset()))
                    return ret;
				RDS_Block_Reset_Cnt = 0;
				FM_LOG_DBG(D_BLKC,"RDS Reset, blk_cnt:%d, RDS_FirstIn:%d\n", RDS_Block_Reset_Cnt, bRDS_FirstIn);
			}else if(TOTAL_CNT > 12){
			    //LCH question 2, why 12???
				FM_LOG_DBG(D_BLKC,"RDS Block Reset: %x\n", RDS_Block_Reset_Cnt);
				if((ret = MT6620_RDS_Reset_Block()))
                    return ret;
			}
		}else{    
			RDS_Sync_Cnt++; //(60%-90%)	
			FM_LOG_DBG(D_BLKC,"RDS Sync Cnt: %d\n", RDS_Block_Reset_Cnt);	
			if(RDS_Block_Reset_Cnt > 1)
				RDS_Block_Reset_Cnt--;
			if(RDS_Sync_Cnt > MT6620_RDS_BLER_C1){
				gBLER_CHK_INTERVAL = MT6620_RDS_BLER_T2;
			}
		}
	}

    return ret;
}

static int MT6620_RDS_Init_Data(RDSData_Struct *pstRDSData)
{
	uint8_t indx;
    int ret = 0;

    memset(pstRDSData, 0 ,sizeof(RDSData_Struct)); 
    bRDS_FirstIn = true;

    pstRDSData->PTY = 0xFF; //to avoid "rx PTY == 0" case, this will cause no PTY event
	
	for(indx = 0; indx < 64; indx++){
		pstRDSData->RT_Data.TextData[0][indx]=0x20;
		pstRDSData->RT_Data.TextData[1][indx]=0x20;		
	}
	for(indx = 0; indx < 8; indx++){
		pstRDSData->PS_Data.PS[0][indx] = '\0';
		pstRDSData->PS_Data.PS[1][indx] = '\0';	
		pstRDSData->PS_Data.PS[2][indx] = '\0';
		pstRDSData->PS_ON[indx] = 0x20;
	}	

    return ret;
}

static int MT6620_RDS_RetrieveGroup0(uint16_t *block_data, uint8_t SubType, RDSData_Struct *pstRDSData)
{
    uint8_t indx, indx2, DI_Code, DI_Flag, PS_Num, AF_H, AF_L, num;
    int ret = 0;
    bool valid = FALSE;
    bool dirty = FALSE;
    uint16_t *event = &pstRDSData->event_status;
    uint32_t *flag = &pstRDSData->RDSFlag.flag_status;

	ret = rds_checksum_check(block_data[4], FM_RDS_GDBK_IND_D, &valid);
    if(valid == FALSE){
        FM_LOG_WAR(D_G0,"Group0 BlockD crc err\n");
    	return -ERR_RDS_CRC;
    }
    
    ret = rds_group0_ta_get(block_data[1], &pstRDSData->RDSFlag.TA, &dirty);
    if(ret){
        FM_LOG_WAR(D_G0,"get ta failed[ret=%d]\n", ret);
    }else if(dirty == TRUE){
        ret = rds_event_set(event, RDS_EVENT_FLAGS); // yes, we got new TA code
        ret = rds_flag_set(flag, RDS_FLAG_IS_TA);
	}

    ret = rds_group0_music_get(block_data[1], &pstRDSData->RDSFlag.Music, &dirty);
    if(ret){
        FM_LOG_WAR(D_G0,"get music failed[ret=%d]\n", ret);
    }else if(dirty == TRUE){
        ret = rds_event_set(event, RDS_EVENT_FLAGS); // yes, we got new MUSIC code
        ret = rds_flag_set(flag, RDS_FLAG_IS_MUSIC);
    }

	if((pstRDSData->Switch_TP)&&(pstRDSData->RDSFlag.TP)&&!(pstRDSData->RDSFlag.TA)){
        ret = rds_event_set(event, RDS_EVENT_TAON_OFF);
	}
		
	if(!SubType){
	    //Type A
	    ret = rds_checksum_check(block_data[4], FM_RDS_GDBK_IND_C, &valid);
        if(valid == FALSE){
            FM_LOG_WAR(D_G0,"Group0 BlockC crc err\n");
            return -ERR_RDS_CRC;
        }else{
		    AF_H = (block_data[2]&0xFF00)>>8;
		    AF_L= block_data[2]&0x00FF;
		    
			if((AF_H > 224)&&(AF_H < 250)){
			    //Followed AF Number, see RDS spec Table 11, valid(224-249)
			    FM_LOG_DBG(D_G0,"RetrieveGroup0 AF_H:%d, AF_L:%d\n", AF_H, AF_L);
				pstRDSData->AF_Data.isAFNum_Get = 0;
				preAF_Num = AF_H - 224; //AF Number
				if(preAF_Num != pstRDSData->AF_Data.AF_Num){
					pstRDSData->AF_Data.AF_Num = preAF_Num;
		        }else{
				    //Get the same AFNum two times
					pstRDSData->AF_Data.isAFNum_Get = 1;
				}
					
				if((AF_L < 205) && (AF_L > 0)){
		            //See RDS Spec table 10, valid VHF
					pstRDSData->AF_Data.AF[0][0] = AF_L+875; //convert to 100KHz
					FM_LOG_DBG(D_G0,"RetrieveGroup0 AF[0][0]:%d\n", pstRDSData->AF_Data.AF[0][0]);
					if((pstRDSData->AF_Data.AF[0][0]) != (pstRDSData->AF_Data.AF[1][0])){
						pstRDSData->AF_Data.AF[1][0] = pstRDSData->AF_Data.AF[0][0];
				    }else{ 	                    
						if(pstRDSData->AF_Data.AF[1][0] !=  _current_frequency)
							pstRDSData->AF_Data.isMethod_A = 1;
						else
							pstRDSData->AF_Data.isMethod_A = 0;
					}

					FM_LOG_DBG(D_G0,"RetrieveGroup0 isAFNum_Get:%d, isMethod_A:%d\n", pstRDSData->AF_Data.isAFNum_Get, pstRDSData->AF_Data.isMethod_A);

					//only one AF handle
					if((pstRDSData->AF_Data.isAFNum_Get)&& (pstRDSData->AF_Data.AF_Num == 1)){
				        pstRDSData->AF_Data.Addr_Cnt = 0xFF;
				        pstRDSData->event_status |= RDS_EVENT_AF_LIST;
						FM_LOG_DBG(D_G0,"RetrieveGroup0 RDS_EVENT_AF_LIST update\n");
				    }					
				}				
			}				
			else if((pstRDSData->AF_Data.isAFNum_Get)&&(pstRDSData->AF_Data.Addr_Cnt != 0xFF)){
                //AF Num correct
                num = pstRDSData->AF_Data.AF_Num;
				num = num>>1;				
                FM_LOG_DBG(D_G0,"RetrieveGroup0 +num:%d\n", num);
				
				//Put AF freq into buffer and check if AF freq is repeat again
			    for(indx = 1; indx < (num+1); indx++){
                    if((AF_H == (pstRDSData->AF_Data.AF[0][2*num-1]))&&(AF_L == (pstRDSData->AF_Data.AF[0][2*indx]))){
					    FM_LOG_ERR(D_G0|D_ALL,"RetrieveGroup0 AF same as indx:%d\n", indx);
						break;
				    }else if(!(pstRDSData->AF_Data.AF[0][2*indx-1])){
				        //null buffer
						pstRDSData->AF_Data.AF[0][2*indx-1] = AF_H+875; //convert to 100KHz
						pstRDSData->AF_Data.AF[0][2*indx] = AF_L+875;
						FM_LOG_DBG(D_G0,"RetrieveGroup0 AF[0][%d]:%d, AF[0][%d]:%d\n", 
                                   2*indx-1, pstRDSData->AF_Data.AF[0][2*indx-1], 2*indx, pstRDSData->AF_Data.AF[0][2*indx]);
						break;
					}
		       	}
				num = pstRDSData->AF_Data.AF_Num;
				FM_LOG_DBG(D_G0,"RetrieveGroup0 ++num:%d\n", num);
			    if(num > 0){
			        if((pstRDSData->AF_Data.AF[0][num-1]) != 0){
						num = num>>1;
						FM_LOG_DBG(D_G0,"RetrieveGroup0 +++num:%d\n", num);
						//arrange frequency from low to high:start
						for(indx = 1; indx < num; indx++){
							for(indx2 = indx+1; indx2 < (num+1); indx2++){
								AF_H = pstRDSData->AF_Data.AF[0][2*indx-1];
								AF_L = pstRDSData->AF_Data.AF[0][2*indx];
								if(AF_H > (pstRDSData->AF_Data.AF[0][2*indx2-1])){
									pstRDSData->AF_Data.AF[0][2*indx-1] = pstRDSData->AF_Data.AF[0][2*indx2-1];
									pstRDSData->AF_Data.AF[0][2*indx] = pstRDSData->AF_Data.AF[0][2*indx2];
									pstRDSData->AF_Data.AF[0][2*indx2-1] = AF_H;
									pstRDSData->AF_Data.AF[0][2*indx2] = AF_L;
								}else if(AF_H == (pstRDSData->AF_Data.AF[0][2*indx2-1])){
									if(AF_L > (pstRDSData->AF_Data.AF[0][2*indx2])){
										pstRDSData->AF_Data.AF[0][2*indx-1] = pstRDSData->AF_Data.AF[0][2*indx2-1];
										pstRDSData->AF_Data.AF[0][2*indx] = pstRDSData->AF_Data.AF[0][2*indx2];
										pstRDSData->AF_Data.AF[0][2*indx2-1] = AF_H;
										pstRDSData->AF_Data.AF[0][2*indx2] = AF_L;
									}
								}
							}
						}
						//arrange frequency from low to high:end						
						//compare AF buff0 and buff1 data:start
						num = pstRDSData->AF_Data.AF_Num;
						indx2 = 0;
						
						for(indx = 0; indx < num; indx++){
                            if((pstRDSData->AF_Data.AF[1][indx]) == (pstRDSData->AF_Data.AF[0][indx])){
							    if(pstRDSData->AF_Data.AF[1][indx] != 0)
									indx2++;
						    }else						    
							    pstRDSData->AF_Data.AF[1][indx] = pstRDSData->AF_Data.AF[0][indx];
					    }
						FM_LOG_DBG(D_G0,"RetrieveGroup0 indx2:%d, num:%d\n", indx2, num);
						//compare AF buff0 and buff1 data:end						
						if(indx2 == num){
						    pstRDSData->AF_Data.Addr_Cnt = 0xFF;
						    pstRDSData->event_status |= RDS_EVENT_AF_LIST;
							FM_LOG_DBG(D_G0,"RetrieveGroup0 AF_Num:%d\n", pstRDSData->AF_Data.AF_Num);
						    for(indx = 0; indx < num; indx++){
								if((pstRDSData->AF_Data.AF[1][indx]) == 0){
								    pstRDSData->AF_Data.Addr_Cnt = 0x0F;
								    pstRDSData->event_status &= (~RDS_EVENT_AF_LIST);
							    }
						   }
					    }
					    else
					 	    pstRDSData->AF_Data.Addr_Cnt = 0x0F;
				    }
			    }
		    }			
	    }
	}
			   	
	/*DI_Code[1:0]:   "00" = d3 *
	  *               "01" = d2 *
	  *               "10" = d1 *
	  *               "11" = d0 */
			   	
	DI_Code = block_data[1]&0x0003;  //DI=decoder identification code.
	DI_Flag = (block_data[1]&0x0004)>>2;
				  
	switch(DI_Code){
	    case 3:
  		    if(pstRDSData->RDSFlag.Stereo != DI_Flag){
	   	    	pstRDSData->RDSFlag.Stereo = DI_Flag;
		    	pstRDSData->event_status |= RDS_EVENT_FLAGS;
		    	pstRDSData->RDSFlag.flag_status |= RDS_FLAG_IS_STEREO;
	        }
	   	    break;
        case 2:
  		    if(pstRDSData->RDSFlag.Artificial_Head != DI_Flag){
	   		    pstRDSData->RDSFlag.Artificial_Head = DI_Flag;
			    pstRDSData->event_status |= RDS_EVENT_FLAGS;
			    pstRDSData->RDSFlag.flag_status |= RDS_FLAG_IS_ARTIFICIAL_HEAD;
  		    }
	   	    break;
        case 1:
  		    if(pstRDSData->RDSFlag.Compressed != DI_Flag){
	   		    pstRDSData->RDSFlag.Compressed = DI_Flag;
			    pstRDSData->event_status |= RDS_EVENT_FLAGS;
			    pstRDSData->RDSFlag.flag_status |= RDS_FLAG_IS_COMPRESSED;	
	        }
	      	break;
        case 0:
  		    if(pstRDSData->RDSFlag.Dynamic_PTY != DI_Flag){
	   		    pstRDSData->RDSFlag.Dynamic_PTY = DI_Flag;
			    pstRDSData->event_status |= RDS_EVENT_FLAGS;
			    pstRDSData->RDSFlag.flag_status |= RDS_FLAG_IS_DYNAMIC_PTY;	
  		    }
		    break;		
        default:
	      	break;			 
	}
		            
	PS_Num = block_data[1]&0x0003;  //Figure 12 Type 0 group.
    AF_H = pstRDSData->PS_Data.PS[0][2*PS_Num];
    AF_L = pstRDSData->PS_Data.PS[0][2*PS_Num+1];
    if((AF_H == (block_data[3])>>8)&&(AF_L == (block_data[3]&0xFF))){
	    if((!((pstRDSData->event_status)&RDS_EVENT_PROGRAMNAME))&&((PS_Num == 0)||(pstRDSData->PS_Data.Addr_Cnt))){
            pstRDSData->PS_Data.PS[1][2*PS_Num]=(block_data[3])>>8;
			pstRDSData->PS_Data.PS[1][2*PS_Num+1] = (block_data[3])&0xFF;			
			FM_LOG_DBG(D_G0,"RetrieveGroup0 PS second time, NUM:%x H:%x L:%x\n", 
				      PS_Num, pstRDSData->PS_Data.PS[1][2*PS_Num], pstRDSData->PS_Data.PS[1][2*PS_Num+1]);
			
			//Need clear buff0, LCH question 1, should clear not not?
			if((PS_Num == 0)&&(pstRDSData->PS_Data.Addr_Cnt == 0)){
				for(indx = 2; indx < 8; indx++){
					pstRDSData->PS_Data.PS[0][indx] = '\0'; //clear buff0
				}
			}
			pstRDSData->PS_Data.Addr_Cnt |= 1<<PS_Num;
			FM_LOG_DBG(D_G0,"RetrieveGroup0, Addr_Cnt:%x\n", pstRDSData->PS_Data.Addr_Cnt);
			if(pstRDSData->PS_Data.Addr_Cnt == 0x0F){
			    //Avoid PS transient:Start 
				num = 0;	
				for(indx = 0; indx < 8; indx++){
					if(pstRDSData->PS_Data.PS[0][indx] == pstRDSData->PS_Data.PS[1][indx])
						num++;
				}
				pstRDSData->PS_Data.Addr_Cnt = 0;
			    //Avoid PS transient:END 

				if(num == 8){
                    // get same data 2 times
				    num = 0;
				    for(indx = 0; indx < 8; indx++){
					    if(pstRDSData->PS_Data.PS[1][indx] == pstRDSData->PS_Data.PS[2][indx])
					        num++;
				    }
				    //if(num != 8) //get same data 2 times, and not same as the last show. 
					    pstRDSData->event_status |= RDS_EVENT_PROGRAMNAME;
					    
					for(indx = 0; indx < 8; indx++){
						pstRDSData->PS_Data.PS[2][indx] = pstRDSData->PS_Data.PS[1][indx];
						pstRDSData->PS_Data.PS[1][indx] = '\0';
						pstRDSData->PS_Data.PS[0][indx] = '\0';
					}
				}else{
					pstRDSData->PS_Data.Addr_Cnt |= 1<<PS_Num;
				}
			}
		}
	}
	else{
        pstRDSData->PS_Data.PS[0][2*PS_Num]=(block_data[3])>>8;
		pstRDSData->PS_Data.PS[0][2*PS_Num+1] = (block_data[3])&0xFF;
		FM_LOG_DBG(D_G0,"RetrieveGroup0 PS, NUM:%x H:%x L:%x\n", 
			      PS_Num, pstRDSData->PS_Data.PS[0][2*PS_Num], pstRDSData->PS_Data.PS[0][2*PS_Num+1]);
    }
    
	if((pstRDSData->event_status)&RDS_EVENT_PROGRAMNAME){
		PS_Num = 0;
		for(num = 0; num < 8;num++){
			if(pstRDSData->PS_Data.PS[2][num] == '\0')
			    PS_Num |= 1<<num;
		}
		if(PS_Num == 0xFF){
			FM_LOG_ERR(D_G0|D_ALL,"RDS PS Canncel event 0x08");
			pstRDSData->event_status &= (~RDS_EVENT_PROGRAMNAME);
	    }
	} 

    return ret;
}

static int MT6620_RDS_RetrieveGroup1(uint16_t *block_data, uint8_t SubType, RDSData_Struct *pstRDSData)
{
	uint8_t variant_code = (block_data[2]&0x7000)>>12;
	int ret = 0;
    
	if(variant_code == 0){	    
		pstRDSData->Extend_Country_Code = (uint8_t)block_data[2]&0xFF;
		FM_LOG_DBG(D_G1,"Extend_Country_Code:%d\n", pstRDSData->Extend_Country_Code);
	}else if(variant_code == 3){
		pstRDSData->Language_Code = block_data[2]&0xFFF;
		FM_LOG_DBG(D_G1,"Language_Code:%d\n", pstRDSData->Language_Code);
	}
				
	pstRDSData->Radio_Page_Code = block_data[1]&0x001F;
	pstRDSData->Program_Item_Number_Code = block_data[3];

    return ret;
}

#ifdef FM_RDS_G2_USE_OLD
static bool PreTextABFlag;

static int MT6620_RDS_RetrieveGroup2(uint16_t *block_data, uint8_t SubType, RDSData_Struct *pstRDSData)
{
    uint8_t TextAddr, indx, indx2, space, byte0, byte1;
	uint16_t addrcnt;
	int i = 0;
    int ret = 0;
    
	TextAddr = (uint8_t)block_data[1]&0x0F;
	
	if(pstRDSData->RDSFlag.Text_AB != ((block_data[1]&0x0010)>>4)){
		pstRDSData->RDSFlag.Text_AB = (block_data[1]&0x0010)>>4;
		pstRDSData->event_status |= RDS_EVENT_FLAGS;
		pstRDSData->RDSFlag.flag_status |= RDS_FLAG_TEXT_AB;
	}
	FM_LOG_DBG(D_G2,"RT RetrieveGroup2 TextABFlag: %x --> %x\n", PreTextABFlag, pstRDSData->RDSFlag.Text_AB);
	
	if(PreTextABFlag != pstRDSData->RDSFlag.Text_AB){ // Text A/B changed, clear old RT, and get new RT
		/*DDB:Some station don't send 0x0D, it just switch TextAB if it want to send next message.*/
		if (pstRDSData->RT_Data.isRTDisplay == 0) {
			FM_LOG_WAR(D_G2,"RT_Data.isRTDisplay == 0, and TextAB changed\n");
			pstRDSData->event_status |= RDS_EVENT_LAST_RADIOTEXT; 
			space = 0;
			for(indx = 0; indx < 64; indx++){
				/*DDB:Why TextData[1][0] NOT TextData[2][0],  Because some station just send a message one time, and then change TextAB, send another message, SUCH As Beijing 90.0*/
				if(pstRDSData->RT_Data.TextData[1][indx] == 0x20)
					space++;
			}	
			if(space == 64)
				pstRDSData->event_status &= (~RDS_EVENT_LAST_RADIOTEXT);

			if (pstRDSData->event_status & RDS_EVENT_LAST_RADIOTEXT){
				/*DDB:Why TextData[1][0] NOT TextData[2][0],  Because some station just send a message one time, and then change TextAB, send another message, SUCH As Beijing 90.0*/
				memcpy(&(pstRDSData->RT_Data.TextData[3][0]), &(pstRDSData->RT_Data.TextData[1][0]), sizeof(pstRDSData->RT_Data.TextData[3]));
				FM_LOG_WAR(D_G2,"RT_Data.isRTDisplay = 1, no 0x0D case.\n");
				pstRDSData->RT_Data.isRTDisplay = 1;
			}
		}

		//to get Radio text length
		pstRDSData->RT_Data.TextLength = 0;
		i = 15;
		while(!(pstRDSData->RT_Data.Addr_Cnt & (1<<i)) && (i > -1)){	
			i--;
		}
        if(!SubType){
			pstRDSData->RT_Data.TextLength = 4*(i+1);
		}else{
			pstRDSData->RT_Data.TextLength = 2*(i+1);
		}
        FM_LOG_INF(D_G2,"RDS RT Get Len: [AddrMap=0x%x] [Length=0x%x]\n", pstRDSData->RT_Data.Addr_Cnt, pstRDSData->RT_Data.TextLength);
			
		/*DDB, end*/
		//clear the buffer because Text A/B changed
	    memset(&(pstRDSData->RT_Data.TextData[0][0]), 0x20, sizeof(pstRDSData->RT_Data.TextData[0]));
		memset(&(pstRDSData->RT_Data.TextData[1][0]), 0x20, sizeof(pstRDSData->RT_Data.TextData[1]));
        memset(&(pstRDSData->RT_Data.TextData[2][0]), 0x20, sizeof(pstRDSData->RT_Data.TextData[2]));		
		PreTextABFlag = pstRDSData->RDSFlag.Text_AB;
		pstRDSData->RT_Data.GetLength = 0;
		pstRDSData->RT_Data.Addr_Cnt = 0;
		//pstRDSData->RT_Data.isRTDisplay = 0;
	}
	
	if(!SubType){
	    //Type A
		//WCN_DBG(FM_ALERT|D_MAIN,"RetrieveGroup2 Type A RT TextAddr: 0x%x Text: 0x%x  0x%x", TextAddr, block_data[2], block_data[3]);
		FM_LOG_NTC(D_G2,"%04x %04x %04x %04x %04x", block_data[0], block_data[1], block_data[2], block_data[3], block_data[4]);
		pstRDSData->RT_Data.isTypeA = 1;
		//to get the 4bytes RadioText
	  	if(block_data[4]&(FM_RDS_GDBK_IND_C|FM_RDS_GDBK_IND_D)){
  			pstRDSData->RT_Data.TextData[0][4*TextAddr] = block_data[2]>>8;
			pstRDSData->RT_Data.TextData[0][4*TextAddr+1] = block_data[2]&0xFF;
  			pstRDSData->RT_Data.TextData[0][4*TextAddr+2] = block_data[3]>>8;
			pstRDSData->RT_Data.TextData[0][4*TextAddr+3] = block_data[3]&0xFF;
			space = 0;
			
            for(indx = 0; indx < 4;indx++){
	  		    byte0 = pstRDSData->RT_Data.TextData[0][4*TextAddr+indx];
			    byte1 = pstRDSData->RT_Data.TextData[1][4*TextAddr+indx];
				if (TextAddr == 0){
					//WCN_DBG(FM_ALERT|D_MAIN,"RT_Data.isRTDisplay = 0\n");
					pstRDSData->RT_Data.isRTDisplay = 0;
				}
				if((!(pstRDSData->event_status&RDS_EVENT_LAST_RADIOTEXT))&&(byte0 == byte1)){
                    //get the same byte 2 times
					space++;
					pstRDSData->RT_Data.TextData[2][4*TextAddr+indx] = byte0;
				}else{
					pstRDSData->RT_Data.TextData[1][4*TextAddr+indx] = byte0;
				}					
			}
			
			if(space == 4){
                addrcnt = pstRDSData->RT_Data.Addr_Cnt;
				pstRDSData->RT_Data.Addr_Cnt |= (1<<TextAddr);
				FM_LOG_DBG(D_G2,"RetrieveGroup2 RT addrcnt:%d, RT_Data.Addr_Cnt:%d\n", addrcnt, pstRDSData->RT_Data.Addr_Cnt);
				
				if(addrcnt == pstRDSData->RT_Data.Addr_Cnt){
				    pstRDSData->RT_Data.BufCnt++;
				}else if(pstRDSData->RT_Data.BufCnt > 0){
					pstRDSData->RT_Data.BufCnt--;
	  	        }
			}
	  	} else {
			FM_LOG_ERR(D_G2|D_ALL,"RT %04x %04x %04x %04x %04x CRC error.", block_data[0], block_data[1], block_data[2], block_data[3], block_data[4]);
	  	}
	  	for(indx = 0; indx < 4; indx++){
	  		if(pstRDSData->RT_Data.TextData[2][4*TextAddr+indx] == 0x0D){
	            pstRDSData->RT_Data.TextLength = 4*TextAddr+indx+1; //Add terminate charater
				pstRDSData->RT_Data.TextData[2][4*TextAddr+indx] = '\0';
				pstRDSData->RT_Data.GetLength = 1;
	        }else if((4*TextAddr+indx) == 63 && pstRDSData->RT_Data.Addr_Cnt == 0xffff){
			    //type A full data. /*add by dongbo, make sure it's TextData[2], Not TextData[1]*/
			    pstRDSData->RT_Data.TextLength = 4*TextAddr+indx+1;  //no terminal character
				pstRDSData->RT_Data.GetLength = 1;
			}
        }
	}else{
	    //FM_LOG_DBG(D_MAIN,"RetrieveGroup2 Type B RT NUM: 0x%x Text: 0x%x", TextAddr, block_data[3]);
		FM_LOG_DBG(D_G2,"RT %04x %04x %04x %04x %04x", block_data[0], block_data[1], block_data[2], block_data[3], block_data[4]);
		pstRDSData->RT_Data.isTypeA = 0;
		if(block_data[4]&FM_RDS_GDBK_IND_D){
            pstRDSData->RT_Data.TextData[0][2*TextAddr] = block_data[3]>>8;
		    pstRDSData->RT_Data.TextData[0][2*TextAddr+1] = block_data[3]&0xFF;
			space = 0;
			
	  	    for(indx = 0; indx < 2; indx++){
	  	        byte0 = pstRDSData->RT_Data.TextData[0][2*TextAddr+indx];
			    byte1 = pstRDSData->RT_Data.TextData[1][2*TextAddr+indx];
				
				if((!((pstRDSData->event_status)&RDS_EVENT_LAST_RADIOTEXT))&&(byte0 == byte1)){
					space++;
					pstRDSData->RT_Data.TextData[2][2*TextAddr+indx] = byte0;
				}else{
					pstRDSData->RT_Data.TextData[1][2*TextAddr+indx] = byte0;
				}
	  		}
			if(space == 2){
			    addrcnt = pstRDSData->RT_Data.Addr_Cnt;
				pstRDSData->RT_Data.Addr_Cnt |= (1<<TextAddr);
				FM_LOG_DBG(D_G2,"RT RetrieveGroup2 RT B addrcnt: 0x%x, RT_Data.Addr_Cnt: 0x%x\n", addrcnt, pstRDSData->RT_Data.Addr_Cnt);
				
                if(addrcnt == pstRDSData->RT_Data.Addr_Cnt){
				    pstRDSData->RT_Data.BufCnt++;
				}else if(pstRDSData->RT_Data.BufCnt > 0){
					pstRDSData->RT_Data.BufCnt--;
				}
            }
		} else {
			FM_LOG_DBG(D_G2,"RT %04x %04x %04x %04x %04x CRC error.", block_data[0], block_data[1], block_data[2], block_data[3], block_data[4]);
		}
		
	 	for(indx = 0; indx < 2; indx++){
	  		if((pstRDSData->RT_Data.TextData[2][2*TextAddr+indx]) == 0x0D){
	  		    //0x0D=end code
	  		    pstRDSData->RT_Data.TextLength = 2*TextAddr+indx+1;  //Add terminate charater
				pstRDSData->RT_Data.TextData[2][2*TextAddr+indx] = '\0';
				pstRDSData->RT_Data.GetLength = 1;
	  		}else if((2*TextAddr+indx) == 31){
			    //full data
			    pstRDSData->RT_Data.TextLength = 2*TextAddr+indx+1;  //Add terminate charater
                pstRDSData->RT_Data.TextData[2][2*TextAddr+indx] = '\0';
				pstRDSData->RT_Data.GetLength = 1;
			}
	  	}		
	}

    //Check if text is fully received
	indx = TextAddr;
	if(pstRDSData->RT_Data.GetLength == 1){
		addrcnt = 0xFFFF>>(0x0F-indx);
	}else if(pstRDSData->RT_Data.BufCnt > 100){
	    pstRDSData->RT_Data.BufCnt = 0;
	    for(indx = 15; indx >= 0; indx--){
	        addrcnt = (pstRDSData->RT_Data.Addr_Cnt)&(1<<indx);
	        if(addrcnt)
	            break;
		}
		
		//get valid radio text length
		if (pstRDSData->RT_Data.isTypeA){
		    for(indx2 = 0; indx2 < 4; indx2++){
		        if(pstRDSData->RT_Data.TextData[2][4*indx+indx2] == 0x0D){
	  			    pstRDSData->RT_Data.TextLength = 4*indx+indx2+1;
					pstRDSData->RT_Data.TextData[2][4*indx+indx2] = '\0';
	            }
		    }
	    }else{
	        for(indx2 = 0; indx2 < 2; indx2++){
		        if(pstRDSData->RT_Data.TextData[2][2*indx+indx2] == 0x0D){
	  			    pstRDSData->RT_Data.TextLength = 2*indx+indx2+1;
					pstRDSData->RT_Data.TextData[2][2*indx+indx2] = '\0';
	            }
		    }
	        
	    }
		addrcnt = 0xFFFF>>(0x0F-indx);
	}else{
		if(pstRDSData->RT_Data.TextLength > 64){
			pstRDSData->RT_Data.TextLength = 64;
		}
		addrcnt = 0xFFFF;
	}
	
    FM_LOG_NTC(D_G2,"RetrieveGroup2 RDS RT: Addr_Cnt: 0x%x Length: 0x%x addrcnt: 0x%x\n", pstRDSData->RT_Data.Addr_Cnt, pstRDSData->RT_Data.TextLength, addrcnt);

	if(((((pstRDSData->RT_Data.Addr_Cnt)&addrcnt) == addrcnt)||((TextAddr == 0x0f) && (pstRDSData->RT_Data.Addr_Cnt == 0xffff)))){		
        //&&(pstRDSData->RT_Data.isRTDisplay == 0))
        pstRDSData->RT_Data.Addr_Cnt = 0;
		//pstRDSData->RT_Data.isRTDisplay = 1;
		pstRDSData->event_status |= RDS_EVENT_LAST_RADIOTEXT; 
		FM_LOG_DBG(D_G2,"RT RetrieveGroup2 isRTDisplay:%d\n", pstRDSData->RT_Data.isRTDisplay);
		space = 0;
		for(indx = 0; indx < 64; indx++){
			if(pstRDSData->RT_Data.TextData[2][indx] == 0x20)
				space++;
	    }	
	    if(space == 64)
            pstRDSData->event_status &= (~RDS_EVENT_LAST_RADIOTEXT);

		memset(&(pstRDSData->RT_Data.TextData[1][0]), 0x20, sizeof(pstRDSData->RT_Data.TextData[1]));
		memset(&(pstRDSData->RT_Data.TextData[0][0]), 0x20, sizeof(pstRDSData->RT_Data.TextData[0]));

		if (pstRDSData->event_status & RDS_EVENT_LAST_RADIOTEXT){
			memcpy(&(pstRDSData->RT_Data.TextData[3][0]), &(pstRDSData->RT_Data.TextData[2][0]), sizeof(pstRDSData->RT_Data.TextData[3]));
			FM_LOG_WAR(D_G2,"RT_Data.isRTDisplay = 1\n");
			pstRDSData->RT_Data.isRTDisplay = 1;
		}
    }

    return ret;
}
#else
static int MT6620_RDS_RetrieveGroup2(uint16_t *source, uint8_t subtype, RDSData_Struct *target)
{
    int ret = 0;
    uint16_t crc, cbc;
    uint16_t blkA, blkB, blkC, blkD;
    uint8_t *fresh, *once, *twice, *display;
    uint16_t *event;
    uint32_t *flag;

    uint8_t rt_addr = 0;
    bool txtAB_change = FALSE;  //text AB flag 0 --> 1 or 1-->0 meas new RT incoming 
    bool txt_end = FALSE;       //0x0D means text end
    bool seg_ok = 0;
    static uint16_t bitmap_twice;
    static int bitmap_cnt;
    int pos = 0;
    int rt_len = 0;
    int bufsize = 0;
    
    FM_ASSERT(source);
    FM_ASSERT(target);
    //source
    blkA = source[0];
    blkB = source[1];
    blkC = source[2];
    blkD = source[3];
    crc = source[4];
    cbc = source[5];
    //target
    fresh = target->RT_Data.TextData[0];
    once = target->RT_Data.TextData[1];
    twice = target->RT_Data.TextData[2];
    display = target->RT_Data.TextData[3];
    event = &target->event_status;
    flag = &target->RDSFlag.flag_status;
    bufsize = sizeof(target->RT_Data.TextData[0]);

    //get basic info: addr, txtAB 
    if(rds_group2_rt_addr_get(blkB, &rt_addr))
        return ret;
    if(rds_group2_txtAB_get(blkB, &target->RDSFlag.Text_AB, &txtAB_change))
        return ret;

    //RT parsing state machine run
    while(1){
        switch(rds_rt_state_get()){
            case RDS_RT_START:
                if(txtAB_change == TRUE){
                    rds_rt_state_set(RDS_RT_DECISION);
                    break;
                }else{
                    if(rds_group2_rt_get(crc, subtype, blkC, blkD, rt_addr, fresh)){
                        rds_rt_state_set(RDS_RT_FINISH); //if CRC error, we should not do parsing
                        break;
                    }
                    rds_group2_rt_cmp(rt_addr, cbc, subtype, fresh, once, twice, 
                                &seg_ok, &txt_end, &rt_len);
                    if(seg_ok == TRUE){
                        rds_group2_rt_addr_bitmap_set(&bitmap_twice, &bitmap_cnt, rt_addr);
                    }
                    rds_rt_state_set(RDS_RT_DECISION);
                    break;
                }
            case RDS_RT_DECISION:
                if(txt_end == TRUE){
                    rds_rt_state_set(RDS_RT_GETLEN);  //find 0x0D, and the lenth has been recorded when do rds_group2_rt_cmp()
                }else if(rds_group2_rt_addr_bitmap_get(bitmap_twice) == 0xFFFF //get max  64 chars
                        || (txtAB_change == TRUE)  //text AB changed,
                        || (bitmap_cnt > RDS_RT_MULTI_REV_TH)){ //repeate many times, but no end char get
                    pos = rds_group2_rt_addr_bitmap_get_pos(bitmap_twice);
                    rds_group2_rt_get_len(subtype, pos, &rt_len);
                    rds_rt_state_set(RDS_RT_GETLEN); 
                }else{
                    rds_rt_state_set(RDS_RT_FINISH);
                }
                break;
            case RDS_RT_GETLEN:
                memcpy(display, twice, bufsize);
                target->RT_Data.TextLength = rt_len;
                rds_event_set(event, RDS_EVENT_LAST_RADIOTEXT); //yes we got a new RT
                FM_LOG_NTC(D_G2,"Yes, get an RT! [len=%d]\n", rt_len);

                rds_group2_rt_addr_bitmap_clear(&bitmap_twice, &bitmap_cnt);
                //clear buf
                memset(fresh, 0x20, bufsize);
                memset(once, 0x20, bufsize);
                memset(twice, 0x20, bufsize);
                if(txtAB_change == TRUE){
                    txtAB_change = FALSE;
                    //we need get new RT after show the old RT to the display
                    rds_rt_state_set(RDS_RT_START);
                }else{
                    rds_rt_state_set(RDS_RT_FINISH);  
                }
                break;
            case RDS_RT_FINISH:
                rds_rt_state_set(RDS_RT_START);
                goto out;
                break;
            default:
                break;          
        }
    }
out:
    return ret;
}
#endif

static int MT6620_RDS_RetrieveGroup4(uint16_t *block_data, uint8_t SubType, RDSData_Struct *pstRDSData)
{
    uint16_t year, month, k=0, D2, minute;
    uint32_t MJD, D1;
    int ret = 0;
	FM_LOG_DBG(D_G4,"RetrieveGroup4 %d\n", SubType);
    if(!SubType){
        //Type A
        if((block_data[4]&FM_RDS_GDBK_IND_C)&&(block_data[4]&FM_RDS_GDBK_IND_D)){            
            MJD = (uint32_t) (((block_data[1]&0x0003)<<15) + ((block_data[2]&0xFFFE)>>1));
            year = (MJD*100 - 1507820)/36525;
			month = (MJD*10000-149561000-3652500*year)/306001;			
		    if((month == 14)||(month == 15))
	            k = 1;	        
	        D1 = (uint32_t)((36525*year)/100);
	        D2 = (uint16_t)((306001*month)/10000);
	        pstRDSData->CT.Year = 1900 + year + k;
	        pstRDSData->CT.Month = month - 1 - k*12;
	        pstRDSData->CT.Day = (uint16_t)(MJD - 14956 - D1 - D2);
	        pstRDSData->CT.Hour = ((block_data[2]&0x0001)<<4)+((block_data[3]&0xF000)>>12);
	        minute = (block_data[3]&0x0FC0)>>6;

	        if(block_data[3]&0x0020){
  	            pstRDSData->CT.Local_Time_offset_signbit = 1; //0=+, 1=-
	        }
	        pstRDSData->CT.Local_Time_offset_half_hour = block_data[3]&0x001F;
	        if(pstRDSData->CT.Minute != minute){
	            pstRDSData->CT.Minute = (block_data[3]&0x0FC0)>>6;
	            pstRDSData->event_status |= RDS_EVENT_UTCDATETIME;
            }
        }
    }

    return ret;
}

static int MT6620_RDS_RetrieveGroup14(uint16_t *block_data, uint8_t SubType, RDSData_Struct *pstRDSData)
{
    uint8_t TP_ON, TA_ON, PI_ON, PS_Num, AF_H, AF_L, indx, indx2, num;
    int ret = 0;
    FM_LOG_DBG(D_G14,"RetrieveGroup14 %d\n", SubType);
	//SubType = (*(block_data+1)&0x0800)>>11;
    PI_ON = block_data[3];
	TP_ON = block_data[1]&0x0010;						   	
	if((!SubType) && (block_data[4]&FM_RDS_GDBK_IND_C)){
	    //Type A
		PS_Num= block_data[1]&0x000F;
		if(PS_Num <4){
			for(indx = 0; indx < 2; indx++){
				pstRDSData->PS_ON[2*PS_Num] = block_data[2]>>8;
				pstRDSData->PS_ON[2*PS_Num+1] = block_data[2]&0xFF;
			}						
		}else if(PS_Num == 4){
			AF_H = (block_data[2]&0xFF00)>>8;
			AF_L = block_data[2]&0x00FF;
			if((AF_H > 223)&&(AF_H < 250)){
			    //Followed AF Number
			    pstRDSData->AFON_Data.isAFNum_Get = 0;
				preAFON_Num = AF_H - 224;
				if(pstRDSData->AFON_Data.AF_Num != preAFON_Num){
					pstRDSData->AFON_Data.AF_Num = preAFON_Num;
				}else
					pstRDSData->AFON_Data.isAFNum_Get= 1;
					
				if(AF_L < 205){
					pstRDSData->AFON_Data.AF[0][0] = AF_L+875;
					if((pstRDSData->AFON_Data.AF[0][0]) != (pstRDSData->AFON_Data.AF[1][0])){
						pstRDSData->AFON_Data.AF[1][0] = pstRDSData->AFON_Data.AF[0][0];
					}else{
						pstRDSData->AFON_Data.isMethod_A = 1;
					}
				}
			}else if((pstRDSData->AFON_Data.isAFNum_Get)&&((pstRDSData->AFON_Data.Addr_Cnt) != 0xFF)){
                //AF Num correct
                num = pstRDSData->AFON_Data.AF_Num;
				num = num>>1;
				//Put AF freq into buffer and check if AF freq is repeat again
				for(indx = 1; indx < (num+1); indx++){
		       		if((AF_H == (pstRDSData->AFON_Data.AF[0][2*indx-1]))&&(AF_L == (pstRDSData->AFON_Data.AF[0][2*indx]))){
					    FM_LOG_NTC(D_G14,"RetrieveGroup14 AFON same as indx:%d\n", indx);
						break;
					}else if(!(pstRDSData->AFON_Data.AF[0][2*indx-1])){
					    //null buffer
						pstRDSData->AFON_Data.AF[0][2*indx-1] = AF_H+875;
						pstRDSData->AFON_Data.AF[0][2*indx] = AF_L+875;
						break;
					}
		       	}
				num = pstRDSData->AFON_Data.AF_Num;
				if(num > 0){
					if((pstRDSData->AFON_Data.AF[0][num-1]) != 0){
						num = num>> 1;
						//arrange frequency from low to high:start
						for(indx = 1; indx < num; indx++){
							for(indx2 = indx+1; indx2 < (num+1); indx2++){
								AF_H = pstRDSData->AFON_Data.AF[0][2*indx-1];
								AF_L = pstRDSData->AFON_Data.AF[0][2*indx];
								if(AF_H > (pstRDSData->AFON_Data.AF[0][2*indx2-1])){
									pstRDSData->AFON_Data.AF[0][2*indx-1] = pstRDSData->AFON_Data.AF[0][2*indx2-1];
									pstRDSData->AFON_Data.AF[0][2*indx] = pstRDSData->AFON_Data.AF[0][2*indx2];
									pstRDSData->AFON_Data.AF[0][2*indx2-1] = AF_H;
									pstRDSData->AFON_Data.AF[0][2*indx2] = AF_L;
								}else if(AF_H == (pstRDSData->AFON_Data.AF[0][2*indx2-1])){
									if(AF_L > (pstRDSData->AFON_Data.AF[0][2*indx2])){			
										pstRDSData->AFON_Data.AF[0][2*indx-1] = pstRDSData->AFON_Data.AF[0][2*indx2-1];
										pstRDSData->AFON_Data.AF[0][2*indx] = pstRDSData->AFON_Data.AF[0][2*indx2];
										pstRDSData->AFON_Data.AF[0][2*indx2-1] = AF_H;
										pstRDSData->AFON_Data.AF[0][2*indx2] = AF_L;
									}
			                    }
							}
						}
						//arrange frequency from low to high:end
						//compare AF buff0 and buff1 data:start
						num = pstRDSData->AFON_Data.AF_Num;
						indx2 = 0;
						for(indx = 0; indx < num; indx++){
							if((pstRDSData->AFON_Data.AF[1][indx]) == (pstRDSData->AFON_Data.AF[0][indx])){
								if(pstRDSData->AFON_Data.AF[1][indx] != 0)
									indx2++;
							}else
								pstRDSData->AFON_Data.AF[1][indx] = pstRDSData->AFON_Data.AF[0][indx];
                        }
						//compare AF buff0 and buff1 data:end						
						if(indx2 == num){
							pstRDSData->AFON_Data.Addr_Cnt = 0xFF;
                            pstRDSData->event_status |= RDS_EVENT_AFON_LIST;				
							for(indx = 0; indx < num; indx++){
								if((pstRDSData->AFON_Data.AF[1][indx]) == 0){
									pstRDSData->AFON_Data.Addr_Cnt = 0x0F;
									pstRDSData->event_status &= (~RDS_EVENT_AFON_LIST);
								}
							}
						}else
							pstRDSData->AFON_Data.Addr_Cnt = 0x0F;
					}
				}
			}
		}		
	}else{
	    //Type B
	    TA_ON = block_data[1]&0x0008;
		FM_LOG_DBG(D_G14,"TA group14 typeB pstRDSData->RDSFlag.TP=%d pstRDSData->RDSFlag.TA=%d TP_ON=%d TA_ON=%d\n", pstRDSData->RDSFlag.TP, pstRDSData->RDSFlag.TA, TP_ON, TA_ON);
        if((!pstRDSData->RDSFlag.TP)&&(pstRDSData->RDSFlag.TA)&&TP_ON&&TA_ON){			
			int TA_num=0;
			for (num=0;num<25;num++){
				if (pstRDSData->AFON_Data.AF[1][num] != 0){
					TA_num++;
				} else {
					break;
				}
			}
			FM_LOG_NTC(D_G14,"TA set RDS_EVENT_TAON");
			if (TA_num == pstRDSData->AFON_Data.AF_Num){
			    pstRDSData->event_status |= RDS_EVENT_TAON;
            }
		}
	}

    return ret;
}

int MT6620_RDS_OnOff(struct fm *fm, bool bFlag)
{
    int ret = 0;
    RDSData_Struct *pstRDSData = fm->pstRDSData;
    
    if(bFlag){
        if((ret = MT6620_RDS_Init_Data(pstRDSData)))
            return ret;
        if((ret = MT6620_RDS_enable()))
            return ret;
    }else {
        if((ret = MT6620_RDS_disable()))
            return ret;
    }
    
    return ret;   
}
 
/*
	Block0: 	PI code(16bits)
	Block1: 	Group type(4bits), B0=version code(1bit), TP=traffic program code(1bit), 
			PTY=program type code(5bits), other(5bits)
	Block2:	16bits
	Block3:	16bits
*/
int MT6620_RDS_Eint_Handler(struct fm *fm, struct rds_rx *rds_raw, int rds_size)
{
    int ret = 0;
    uint16_t block_data[6];
	uint8_t GroupType, SubType = 0;
    int rds_cnt = 0;
    int i = 0;
    bool dirty = FALSE;
    //target to fill the result in
    RDSData_Struct *pstRDSData = fm->pstRDSData;
    uint16_t *event = &pstRDSData->event_status;
    uint32_t *flag = &pstRDSData->RDSFlag.flag_status;

    ret = rds_cnt_get(rds_raw, rds_size, &rds_cnt);
    if(ret){
        FM_LOG_WAR(D_RDS,"get cnt err[ret=%d]\n", ret);
        return ret;
    }
	//pstRDSData->EINT_Flag = 1;
	while(rds_cnt > 0){
        ret = rds_group_get(&block_data[0], rds_raw, i);
        if(ret){
            FM_LOG_WAR(D_RDS,"get group err[ret=%d]\n", ret);
            goto do_next;
	    }
	    
        ret = rds_group_type_get(block_data[4], block_data[1], &GroupType, &SubType);
		if(ret){
            FM_LOG_WAR(D_RDS,"get group type err[ret=%d]\n", ret);
            goto do_next; 
			}			

        ret = rds_group_counter_add(GroupType, SubType, &fm->rds_gc);
        
        ret = rds_group_pi_get(block_data[4], block_data[0], &pstRDSData->PI, &dirty);
        if(ret){
            FM_LOG_WAR(D_RDS,"get group pi err[ret=%d]\n", ret);
            goto do_next;
        }else if(dirty == TRUE){
            ret = rds_event_set(event, RDS_EVENT_PI_CODE); //yes, we got new PI code
		}
		
        ret = rds_group_pty_get(block_data[4], block_data[1], &pstRDSData->PTY, &dirty);
        if(ret){
            FM_LOG_WAR(D_RDS,"get group pty err[ret=%d]\n", ret);
            goto do_next;
        }else if(dirty == TRUE){
            ret = rds_event_set(event, RDS_EVENT_PTY_CODE); // yes, we got new PTY code
		}
		
        ret = rds_group_tp_get(block_data[4], block_data[1], &pstRDSData->RDSFlag.TP, &dirty);
        if(ret){
            FM_LOG_WAR(D_RDS,"get group tp err[ret=%d]\n", ret);
            goto do_next;
        }else if(dirty == TRUE){
            ret = rds_event_set(event, RDS_EVENT_FLAGS); // yes, we got new TP code
            ret = rds_flag_set(flag, RDS_FLAG_IS_TP);
		}
		
		switch(GroupType){
		    case 0:
		   	    if((ret = MT6620_RDS_RetrieveGroup0(&block_data[0], SubType, pstRDSData)))
		   	        goto do_next;
		        break;
            case 1:
		   	    if((ret = MT6620_RDS_RetrieveGroup1(&block_data[0], SubType, pstRDSData)))
		   	        goto do_next;
		   	    break;
            case 2:
		   	    if((ret = MT6620_RDS_RetrieveGroup2(&block_data[0], SubType, pstRDSData)))	
                    goto do_next;
	            break;
            case 4:
		   	    if((ret = MT6620_RDS_RetrieveGroup4(&block_data[0], SubType, pstRDSData)))
                    goto do_next;
			    break;		
		    case 14:
		   	    if((ret = MT6620_RDS_RetrieveGroup14(&block_data[0], SubType, pstRDSData)))	
                    goto do_next;
		   	    break;	
            default:
	          	break;			 
		}

do_next:
        if(ret && (ret != -ERR_RDS_CRC)){
            FM_LOG_ERR(D_RDS,"parsing err[ret=%d]\n", ret);
            return ret;
        }
        rds_cnt--;
        i++;
	}

    return ret;
}

