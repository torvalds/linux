/**
******************************************************************************
*
* @file fw_head_check.h
*
* @brief ecrnx usb firmware validity check functions
*
* Copyright (C) ESWIN 2015-2020
*
******************************************************************************
*/

#ifndef _FW_HEAD_CHECK_H_
#define _FW_HEAD_CHECK_H_

#include "core.h"

#define HEAD_SIZE  (64)
#define INFO_SIZE  (48)

typedef struct _bin_head_data {
    unsigned int head_crc32;
    unsigned int crc32;
    unsigned int magic;
    unsigned int UTC_time;
    unsigned char *fw_Info;
}bin_head_data;

typedef struct{
	uint32_t initVal;
	uint32_t POLY;
	uint32_t sub;
	uint8_t  bits;
	char 	 *funcName;
}crc32_Table_TypeDef;


extern unsigned int offset;

void localtime(struct tm *stTm, unsigned int time);
bool fw_check_head(struct eswin *tr);

#endif
