/****************************************************************************\
* 
*  File Name      atomfirmwareid.h
*
*  Description    ATOM BIOS command/data table ID definition header file
*
*  Copyright 2016 Advanced Micro Devices, Inc.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
* and associated documentation files (the "Software"), to deal in the Software without restriction,
* including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
* subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all copies or substantial
* portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
* OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
* ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
* OTHER DEALINGS IN THE SOFTWARE.
*
\****************************************************************************/

#ifndef _ATOMFIRMWAREID_H_
#define _ATOMFIRMWAREID_H_

enum atom_master_data_table_id
{
    VBIOS_DATA_TBL_ID__UTILITY_PIPELINE,
    VBIOS_DATA_TBL_ID__MULTIMEDIA_INF,
    VBIOS_DATA_TBL_ID__FIRMWARE_INF,
    VBIOS_DATA_TBL_ID__LCD_INF,
    VBIOS_DATA_TBL_ID__SMU_INF,
    VBIOS_DATA_TBL_ID__VRAM_USAGE_BY_FIRMWARE,
    VBIOS_DATA_TBL_ID__GPIO_PIN_LUT,
    VBIOS_DATA_TBL_ID__GFX_INF,
    VBIOS_DATA_TBL_ID__POWER_PLAY_INF,
    VBIOS_DATA_TBL_ID__DISPLAY_OBJECT_INF,
    VBIOS_DATA_TBL_ID__INDIRECT_IO_ACCESS,
    VBIOS_DATA_TBL_ID__UMC_INF,
    VBIOS_DATA_TBL_ID__DCE_INF,
    VBIOS_DATA_TBL_ID__VRAM_INF,
    VBIOS_DATA_TBL_ID__INTEGRATED_SYS_INF,
    VBIOS_DATA_TBL_ID__ASIC_PROFILING_INF,
    VBIOS_DATA_TBL_ID__VOLTAGE_OBJ_INF,

    VBIOS_DATA_TBL_ID__UNDEFINED,
};

enum atom_master_command_table_id
{
    VBIOS_CMD_TBL_ID__ASIC_INIT,
    VBIOS_CMD_TBL_ID__DIGX_ENCODER_CONTROL,
    VBIOS_CMD_TBL_ID__SET_ENGINE_CLOCK,
    VBIOS_CMD_TBL_ID__SET_MEMORY_CLOCK,
    VBIOS_CMD_TBL_ID__SET_PIXEL_CLOCK,
    VBIOS_CMD_TBL_ID__ENABLE_DISP_POWER_GATING,
    VBIOS_CMD_TBL_ID__BLANK_CRTC,
    VBIOS_CMD_TBL_ID__ENABLE_CRTC,
    VBIOS_CMD_TBL_ID__GET_SMU_CLOCK_INFO,
    VBIOS_CMD_TBL_ID__SELECT_CRTC_SOURCE,
    VBIOS_CMD_TBL_ID__SET_DCE_CLOCK,
    VBIOS_CMD_TBL_ID__GET_MEMORY_CLOCK,
    VBIOS_CMD_TBL_ID__GET_ENGINE_CLOCK,
    VBIOS_CMD_TBL_ID__SET_CRTC_USING_DTD_TIMING,
    VBIOS_CMD_TBL_ID__EXTENAL_ENCODER_CONTROL,
    VBIOS_CMD_TBL_ID__PROCESS_I2C_CHANNEL_TRANSACTION,
    VBIOS_CMD_TBL_ID__COMPUTE_GPU_CLOCK_PARAM,
    VBIOS_CMD_TBL_ID__DYNAMIC_MEMORY_SETTINGS,
    VBIOS_CMD_TBL_ID__MEMORY_TRAINING,
    VBIOS_CMD_TBL_ID__SET_VOLTAGE,
    VBIOS_CMD_TBL_ID__DIG1_TRANSMITTER_CONTROL,
    VBIOS_CMD_TBL_ID__PROCESS_AUX_CHANNEL_TRANSACTION,
    VBIOS_CMD_TBL_ID__GET_VOLTAGE_INF,

    VBIOS_CMD_TBL_ID__UNDEFINED,
};



#endif  /* _ATOMFIRMWAREID_H_  */
/* ### EOF ### */
