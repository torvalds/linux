/**
******************************************************************************
*
* @file fw_head_check.c
*
* @brief ecrnx usb firmware validity check functions
*
* Copyright (C) ESWIN 2015-2020
*
******************************************************************************
*/

#include <linux/firmware.h>
#include <linux/version.h>
#include "core.h"
#include "fw_head_check.h"
#include "ecrnx_defs.h"

extern char *fw_name;

#define MSB_MODE  1
#define LSB_MODE  0
#define DSE_FIRST       (2039)
#define SECONDS_PER_DAY (86400)

bin_head_data head = {0};
unsigned int offset = 0;

static const crc32_Table_TypeDef CRC32_Table[]={
    {0xFFFFFFFF, 0x04C11DB7, 0xFFFFFFFF, LSB_MODE, "CRC32"},
    {0xFFFFFFFF, 0x04C11DB7, 0x00000000, MSB_MODE, "CRC32_MPEG-2"}
};

static const u_int16_t days_since_year[] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
};

static const u_int16_t days_since_leapyear[] = {
    0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335,
};

static const u_int16_t days_since_epoch[] = {
    /* 2039 - 2030 */
    25202, 24837, 24472, 24106, 23741, 23376, 23011, 22645, 22280, 21915,
    /* 2029 - 2020 */
    21550, 21184, 20819, 20454, 20089, 19723, 19358, 18993, 18628, 18262,
    /* 2019 - 2010 */
    17897, 17532, 17167, 16801, 16436, 16071, 15706, 15340, 14975, 14610,
    /* 2009 - 2000 */
    14245, 13879, 13514, 13149, 12784, 12418, 12053, 11688, 11323, 10957,
    /* 1999 - 1990 */
    10592, 10227, 9862, 9496, 9131, 8766, 8401, 8035, 7670, 7305,
    /* 1989 - 1980 */
    6940, 6574, 6209, 5844, 5479, 5113, 4748, 4383, 4018, 3652,
    /* 1979 - 1970 */
    3287, 2922, 2557, 2191, 1826, 1461, 1096, 730, 365, 0,
};

static inline bool is_leap(unsigned int y)
{
    return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
}

void localtime(struct tm *stTm, unsigned int time)
{
    unsigned int year, transition_value, i, days = time / SECONDS_PER_DAY,secs = time % SECONDS_PER_DAY;

    stTm->tm_wday = (4 + days - 1) % 7 + 1;
    stTm->tm_sec = secs % 60;
    transition_value = secs / 60;
    stTm->tm_min = transition_value % 60;
    stTm->tm_hour   = transition_value / 60;

    for (i = 0, year = DSE_FIRST; days_since_epoch[i] > days; ++i, --year);

    days -= days_since_epoch[i];
    stTm->tm_year = DSE_FIRST - i;
    stTm->tm_yday = days;
    if (is_leap(year))
    {
        for (i = ARRAY_SIZE(days_since_leapyear) - 1;i > 0 && days_since_leapyear[i] > days; --i);
        stTm->tm_mday = days - days_since_leapyear[i] + 1;
    }
    else
    {
        for (i = ARRAY_SIZE(days_since_year) - 1;i > 0 && days_since_year[i] > days; --i);
        stTm->tm_mday = days - days_since_year[i] + 1;
    }

    stTm->tm_mon = i + 1;
}

static void _InvertU8(uint8_t *dBuf, uint8_t *srcBuf)
{
    uint8_t tmp[4] = {0};
    uint8_t i=0;
    for(i=0;i<8;i++)
    {
        if(srcBuf[0] & (1<<i))
        {
            tmp[0] |= 1<<(7-i);
        }
    }
    dBuf[0] = tmp[0];
}

static void _InvertU32(uint32_t *dBuf, uint32_t *srcBuf)
{
    uint32_t tmp[4] = {0};
    uint8_t i=0;
    for(i=0;i<32;i++)
    {
        if(srcBuf[0] & (1<<i))
        {
            tmp[0] |= 1<<(31-i);
        }
    }
    dBuf[0] = tmp[0];
}

uint32_t calc_crc32(uint8_t Mode, uint8_t *pMsg, uint32_t Len)
{
    if(Mode > sizeof(CRC32_Table)/sizeof(crc32_Table_TypeDef))
        return 0;

    uint32_t CRCin = CRC32_Table[Mode].initVal;
    uint32_t tmp = 0;
    uint32_t i=0;
    uint8_t j=0;

	for(i=0;i<Len;i++)
    {
        tmp = *(pMsg++);
        if(CRC32_Table[Mode].bits == LSB_MODE)
        {
            _InvertU8((uint8_t*)&tmp, (uint8_t*)&tmp);
        }
        CRCin ^= (tmp <<24);
        for(j=0;j<8;j++)
        {
            if(CRCin & 0x80000000)
            {
                CRCin = (CRCin << 1) ^ CRC32_Table[Mode].POLY;
            }
            else
            {
                CRCin <<= 1;
            }
        }
    }
	if(CRC32_Table[Mode].bits == LSB_MODE)
    {
    	_InvertU32(&CRCin, &CRCin);
    }
	return (CRCin ^ CRC32_Table[Mode].sub);
}


unsigned long long parse_data(struct eswin *tr,unsigned char size)
{
    unsigned long long value = 0;
    if(size == 1)
    {
        value = * (unsigned char*)(tr->fw->data + offset);
    }
    else if(size == 2)
    {
        value = * (unsigned short*)(tr->fw->data + offset);
    }
    else if(size == 4)
    {
        value = * (unsigned int*)(tr->fw->data + offset);
    }
    else if(size == 8)
    {
        value = * (unsigned long long*)(tr->fw->data + offset);
    }
    else
    {
        return value;
    }
    offset += size;
    return value;
}

uint8_t parse_fw_info(struct eswin *tr, bin_head_data *phead)
{
    unsigned int len = INFO_SIZE;

    phead->fw_Info = vmalloc(len+1);
    memcpy(phead->fw_Info,(unsigned char*)(tr->fw->data + offset),len);

    offset += len;
    return len;
}

bool fw_crc_check(struct eswin *tr,bin_head_data head)
{
    unsigned int now_crc32 = 0;
    unsigned char crc_shift = ((unsigned char)((void *)(&head.head_crc32) - (void *)(&head))) + sizeof(unsigned int);
    now_crc32 = calc_crc32(LSB_MODE, (unsigned char *)(tr->fw->data + crc_shift), HEAD_SIZE - crc_shift);
    if(now_crc32 == head.head_crc32)
    {
        return true;
    }
    else
    {
        ECRNX_PRINT("%s,firmware CRC check error , (%s) ,crc total size(%d)\n", __func__, fw_name,HEAD_SIZE - crc_shift);
        print_hex_dump(KERN_ERR, DBG_PREFIX_CRC_CHECK, DUMP_PREFIX_ADDRESS, 32, 1, &(head.crc32), 4, false);
        print_hex_dump(KERN_ERR, DBG_PREFIX_CRC_CHECK, DUMP_PREFIX_ADDRESS, 32, 1, &now_crc32, 4, false);
        return false;
    }
}
bool fw_magic_check(struct eswin *tr,bin_head_data head)
{
    if(head.magic == 0xEF40)
    {
        return true;
    }
    else if(head.magic == 0xCE56)
    {
        return true;
    }
    else
    {
        ECRNX_PRINT("%s,firmware magic check error , (%s) ,magic(%x)\n", __func__, fw_name,head.magic);
        return false;
    }
}

bool fw_check_head(struct eswin *tr)
{
    struct tm timenow = {0};

    head.head_crc32 = parse_data(tr, sizeof(unsigned int));
    head.crc32 = parse_data(tr, sizeof(unsigned int));
    head.magic = parse_data(tr, sizeof(unsigned int));
    head.UTC_time = parse_data(tr, sizeof(unsigned int));

    if(fw_magic_check(tr,head) == false)
    {
        return false;
    }
    if(fw_crc_check(tr,head) == false)
    {
        return false;
    }

    parse_fw_info(tr, &head);

    localtime(&timenow, head.UTC_time);
    ECRNX_PRINT("%s,firmware build time: %04d-%02d-%02d %02d:%02d:%02d\n", __func__,(int)timenow.tm_year,timenow.tm_mon,timenow.tm_mday,timenow.tm_hour,timenow.tm_min,timenow.tm_sec);
    if(head.fw_Info != NULL)
    {
        ECRNX_PRINT("%s,firmware information: (%s)\n", __func__, head.fw_Info);
    }
    return true;
}
