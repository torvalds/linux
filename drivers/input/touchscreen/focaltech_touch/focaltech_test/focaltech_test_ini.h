/*
 *
 * FocalTech TouchScreen driver.
 *
 * Copyright (c) 2012-2018, FocalTech Systems, Ltd., all rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _INI_H
#define _INI_H
/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define MAX_KEYWORD_NUM                         (500)
#define MAX_KEYWORD_NAME_LEN                    (50)
#define MAX_KEYWORD_VALUE_LEN                   (512)
#define MAX_KEYWORD_VALUE_ONE_LEN               (16)
#define MAX_INI_LINE_LEN        (MAX_KEYWORD_NAME_LEN + MAX_KEYWORD_VALUE_LEN)
#define MAX_INI_SECTION_NUM                     (20)
#define MAX_IC_NAME_LEN                         (512)	//The original value is 32
#define MAX_TEST_ITEM                           (20)
#define IC_CODE_OFFSET                          (16)

/*****************************************************************************
* enumerations, structures and unions
*****************************************************************************/
struct ini_ic_type {
    char ic_name[MAX_IC_NAME_LEN];
    u32 ic_type;
};

enum line_type {
    LINE_SECTION = 1,
    LINE_KEYWORD = 2 ,
    LINE_OTHER = 3,
};

struct ini_keyword {
    char name[MAX_KEYWORD_NAME_LEN];
    char value[MAX_KEYWORD_VALUE_LEN];
};

struct ini_section {
    char name[MAX_KEYWORD_NAME_LEN];
    int keyword_num;
    /* point to ini.tmp, don't need free */
    struct ini_keyword *keyword;
};

struct ini_data {
    char *data;
    int length;
    int keyword_num_total;
    int section_num;
    struct ini_section section[MAX_INI_SECTION_NUM];
    struct ini_keyword *tmp;
    char ic_name[MAX_IC_NAME_LEN];
    u32 ic_code;
};

#define TEST_ITEM_INCELL            { \
    "SHORT_CIRCUIT_TEST", \
    "OPEN_TEST", \
    "CB_TEST", \
    "RAWDATA_TEST", \
    "LCD_NOISE_TEST", \
    "KEY_SHORT_TEST", \
}

#define BASIC_THRESHOLD_INCELL      { \
    "ShortCircuit_ResMin", "ShortCircuit_VkResMin", \
    "OpenTest_CBMin", "OpenTest_Check_K1", "OpenTest_K1Threshold", "OpenTest_Check_K2", "OpenTest_K2Threshold", \
    "CBTest_Min", "CBTest_Max", \
    "CBTest_VKey_Check", "CBTest_Min_Vkey", "CBTest_Max_Vkey", \
    "RawDataTest_Min", "RawDataTest_Max", \
    "RawDataTest_VKey_Check", "RawDataTest_Min_VKey", "RawDataTest_Max_VKey", \
    "LCD_NoiseTest_Frame", "LCD_NoiseTest_Coefficient", "LCD_NoiseTest_Coefficient_key", \
}


#define TEST_ITEM_MC_SC             { \
    "RAWDATA_TEST", \
    "UNIFORMITY_TEST", \
    "SCAP_CB_TEST", \
    "SCAP_RAWDATA_TEST", \
    "WEAK_SHORT_CIRCUIT_TEST", \
    "PANEL_DIFFER_TEST", \
}

#define BASIC_THRESHOLD_MC_SC       { \
    "RawDataTest_High_Min", "RawDataTest_High_Max", "RawDataTest_HighFreq", \
    "RawDataTest_Low_Min", "RawDataTest_Low_Max", "RawDataTest_LowFreq", \
    "UniformityTest_Check_Tx", "UniformityTest_Check_Rx","UniformityTest_Check_MinMax", \
    "UniformityTest_Tx_Hole", "UniformityTest_Rx_Hole", "UniformityTest_MinMax_Hole", \
    "SCapCbTest_OFF_Min", "SCapCbTest_OFF_Max", "ScapCBTest_SetWaterproof_OFF", \
    "SCapCbTest_ON_Min", "SCapCbTest_ON_Max", "ScapCBTest_SetWaterproof_ON", \
    "SCapRawDataTest_OFF_Min", "SCapRawDataTest_OFF_Max", "SCapRawDataTest_SetWaterproof_OFF", \
    "SCapRawDataTest_ON_Min", "SCapRawDataTest_ON_Max", "SCapRawDataTest_SetWaterproof_ON", \
    "WeakShortTest_CG", "WeakShortTest_CC", \
    "PanelDifferTest_Min", "PanelDifferTest_Max", \
}

#define TEST_ITEM_SC                { \
    "CB_TEST", \
    "DELTA_CB_TEST", \
    "RAWDATA_TEST", \
}

#define BASIC_THRESHOLD_SC          { \
    "RawDataTest_Min", "RawDataTest_Max", \
    "CbTest_Min", "CbTest_Max", \
    "DeltaCbTest_Base", "DeltaCbTest_Differ_Max", \
    "DeltaCbTest_Include_Key_Test", "DeltaCbTest_Key_Differ_Max", \
    "DeltaCbTest_Deviation_S1", "DeltaCbTest_Deviation_S2", "DeltaCbTest_Deviation_S3", \
    "DeltaCbTest_Deviation_S4", "DeltaCbTest_Deviation_S5", "DeltaCbTest_Deviation_S6", \
    "DeltaCbTest_Set_Critical", "DeltaCbTest_Critical_S1", "DeltaCbTest_Critical_S2", \
    "DeltaCbTest_Critical_S3", "DeltaCbTest_Critical_S4", \
    "DeltaCbTest_Critical_S5", "DeltaCbTest_Critical_S6", \
}

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/
int fts_test_get_testparam_from_ini(char *config_name);
int get_keyword_value(char *section, char *name, int *value);

#define get_value_interface(name, value) \
    get_keyword_value("Interface", name, value)
#define get_value_basic(name, value) \
    get_keyword_value("Basic_Threshold", name, value)
#define get_value_detail(name, value) \
    get_keyword_value("SpecialSet", name, value)
#define get_value_testitem(name, value) \
    get_keyword_value("TestItem", name, value)
#endif /* _INI_H */
