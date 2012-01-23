/*
**********************************************************************************************************************
*											        eGon
*						                     the Embedded System
*									       script parser sub-system
*
*						  Copyright(C), 2006-2010, SoftWinners Microelectronic Co., Ltd.
*                                           All Rights Reserved
*
* File    : script.c
*
* By      : Jerry
*
* Version : V2.00
*
* Date	  :
*
* Descript:
**********************************************************************************************************************
*/
#ifndef  _SCRIPT_I_H_
#define  _SCRIPT_I_H_

typedef struct
{
	int  main_key_count;
	int  version[3];
}
script_head_t;

typedef struct
{
	char main_name[32];
	int  lenth;
	int  offset;
}
script_main_key_t;

typedef struct
{
	char sub_name[32];
	int  offset;
	int  pattern;
}
script_sub_key_t;


#endif  // _SCRIPT_I_H_


