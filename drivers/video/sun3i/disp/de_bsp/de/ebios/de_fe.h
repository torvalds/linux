//*****************************************************************************
//  All Winner Micro, All Right Reserved. 2006-2010 Copyright (c)
//
//  File name   :        de_scal_bsp.h
//
//  Description :  display engine scaler registers and interface functions define
//                 for aw1620
//  History     :
//                2010/11/09      zchmin       v0.1    Initial version
//******************************************************************************
#ifndef __DE_FE_H__
#define __DE_FE_H__

#include "de_bsp_i.h"

//macro define
#define SCALINITPASELMT (0xfffff)
#define SCALLINEMAX (2048)

//register struct define
typedef struct __DE_SCAL_EN
{
    __u32 en               :   1;  //bit0
    __u32 reserved0        :   31; //bit31~1
}__de_scal_en_t;


typedef struct __DE_SCAL_FRM_CTRL
{
    __u32 reg_rdy_en       :   1;  //bit0
    __u32 coef_rdy_en      :   1;  //bit1
    __u32 wb_en            :   1;  //bit2
    __u32 reserved0        :   1;  //bit3
    __u32 gbl_rdy_en       :   1;  //bit4
    __u32 reserved1        :   3;  //bit7~5
    __u32 out_port_sel     :   2;  //bit9~8
    __u32 reserved2        :   1;  //bit10
    __u32 out_ctrl         :   1;  //bit11
    __u32 reserved3        :   4;  //bit15~12
    __u32 frm_start        :   1;  //bit16
    __u32 reserved4        :   15; //bit31:17
}__de_scal_frm_ctrl_t;


typedef struct __DE_SCAL_BYPASS_EN
{
    __u32 scl_bypass_en    :   1;   //bit0, only for scaler1 valid
    __u32 csc_bypass_en    :   1;   //bit1
    __u32 reserved0        :   30;  //bit31~2
}__de_scal_bypass_en_t;

typedef struct __DE_SCAL_AGTH_SEL
{
    __u32 scl_agth         :   2;   //bit1~0   only for scaler1 valid
    __u32 reserved0        :   6;   //bit7:2
    __u32 linebuf_agth     :   1;   //bit8    only for scaler0 valid
    __u32 reserved1        :   23;  //bit31~9
}__de_scal_agth_sel_t;

typedef struct __DE_SCAL_LINE_INT_CTRL
{
    __u32 trig_line        :   13;  //bit12~0
    __u32 reserved0        :   2;   //bit14~13
    __u32 field_sel        :   1;   //bit15
    __u32 current_line     :   13;  //bit28~16
    __u32 reserved1        :   3;   //bit31~29
}__de_scal_line_int_ctrl_t;


typedef struct __DE_SCAL_FIELD_CTRL
{
    __u32 field_cnt        :   8;  //bit7~0
    __u32 valid_field_cnt  :   3;  //bit10~8
    __u32 reserved0        :   1;  //bit11
    __u32 field_loop_mod   :   1;  //bit12
    __u32 reserved1        :   5; //bit17~bit13
    __u32 sync_edge        :   1; //bit18
    __u32 reserved2        :   13; //bit31~bit19
}__de_scal_field_ctrl_t;

typedef struct __DE_SCAL_MB_OFT
{
    __u32 xoffset0         :   5;  //bit4~0
    __u32 reserved0        :   3;  //bit7~5
    __u32 yoffset0         :   5;  //bit12~8
    __u32 reserved1        :   3;  //bit15~13
    __u32 xoffset1         :   5;  //bit20~16
    __u32 reserved2        :   11; //bit31~21
}__de_scal_mb_oft_t;


typedef struct __DE_SCAL_INPUT_FMT
{
    __u32 data_ps          :   2;  //bit1~0
    __u32 reserved0        :   2;  //bit3~2
    __u32 data_fmt         :   3;  //bit6~4
    __u32 reserved1        :   1;  //bit7
    __u32 data_mod         :   3;  //bit10~8
    __u32 reserved2        :   1;  //bit11
    __u32 scan_mod         :   1;  //bit12
    __u32 reserved3        :   3;  //bit15~13
    __u32 byte_seq         :   1;  //bit16
    __u32 reserved4        :   15; //bit31~17
}__de_scal_input_fmt_t;


typedef struct __DE_SCAL_OUTPUT_FMT
{
    __u32 data_fmt         :   3;  //bit2~0
    __u32 reserved0        :   1;  //bit3
    __u32 scan_mod         :   1;  //bit4
    __u32 reserved1        :   3;  //bit7~5
    __u32 byte_seq         :   1;  //bit8
    __u32 reserved2        :   23; //bit31~9
}__de_scal_output_fmt_t;


typedef struct __DE_SCAL_INT_EN
{
    __u32 reserved0        :   7;  //bit6~0
    __u32 wb_en            :   1;  //bit7
    __u32 reserved1        :   1;  //bit8
    __u32 line_en          :   1;  //bit9
    __u32 load_en          :   1;  //bit10
    __u32 reserved2        :   21; //bit31~11
}__de_scal_int_en_t;

typedef struct __DE_SCAL_INT_STATUS
{
    __u32 reserved0        :   7;  //bit6~0
    __u32 wb_sts           :   1;  //bit7
    __u32 reserved1        :   1;  //bit8
    __u32 line_sts         :   1;  //bit9
    __u32 load_sts         :   1;  //bit10
    __u32 reserved2        :   21; //bit31~11
}__de_scal_int_status_t;

typedef struct __DE_SCAL_STATUS
{
    __u32 frm_busy         :   1;  //bit0
    __u32 wb_sts           :   1;  //bit1
    __u32 cfg_pending      :   1;  //bit2
    __u32 reserved0        :   1;  //bit3
    __u32 dram_sts         :   1;  //bit4
    __u32 lcd_field        :   1;  //bit5
    __u32 reserved1        :   10; //bit15~6
    __u32 line_syn         :   13; //bit28~16
    __u32 reserved2        :   3;  //bit31~29
}__de_scal_status_t;


typedef struct __DE_SCAL_CSC_COEF
{
    __u32 coef             :   13;  //bit12~0
    __u32 reserved         :   19;  //bit31~13
}__de_scal_csc_coef_t;

typedef struct __DE_SCAL_CSC_CONT
{
    __u32 cont             :   14;  //bit13~0
    __u32 reserved         :   18;  //bit31~14
}__de_scal_csc_cont_t;

//deinterlacing only for scaler0 valid
typedef struct __DE_SCAL_DI_CTRL
{
    __u32 en               :   1;   //bit0
    __u32 reserved0        :   15;  //bit15~1
    __u32 mod              :   2;   //bit17~16
    __u32 reserved1        :   6;   //bit23~18
    __u32 diagintp_en      :   1;   //bit24
    __u32 tempdiff_en      :   1;   //bit25
    __u32 reserved2        :   6;   //bit31~26
}__de_scal_di_ctrl_t;

typedef struct __DE_SCAL_DI_DIAGINTP_TH
{
    __u32 th0              :   7;   //bit6~0
    __u32 reserved0        :   1;   //bit7
    __u32 th1              :   7;   //bit14~8
    __u32 reserved1        :   1;   //bit15
    __u32 th2              :   8;   //bit23~16
    __u32 th3              :   8;   //bit31~24
}__de_scal_di_diagintp_th_t;

typedef struct __DE_SCAL_DI_TEMPDIFF_TH
{
    __u32 reserved0        :   8;   //bit7~0
    __u32 th               :   5;   //bit12~8
    __u32 reserved1        :   19;  //bit31~13
}__de_scal_di_tempdiff_th_t;

typedef struct __DE_SCAL_DI_SAWTOOTH_TH
{
    __u32 th1              :   8;   //bit7~0
    __u32 th2              :   8;   //bit15~8
    __u32 reserved         :   16;  //bit31~16
}__de_scal_di_sawtooth_th_t;

typedef struct __DE_SCAL_DI_SPATIAL_TH
{
    __u32 th0              :   9;   //bit8~0
    __u32 reserved0        :   7;   //bit15~9
    __u32 th1              :   9;   //bit24~16
    __u32 reserved1        :   7;   //bit31~25
}__de_scal_di_spatial_th_t;


typedef struct __DE_SCAL_DI_BURST_LEN
{
    __u32 luma             :   6;   //bit5~0
    __u32 reserved0        :   2;   //bit7~6
    __u32 chroma           :   6;   //bit13~8
    __u32 reserved1        :   18;  //bit31~14
}__de_scal_di_burst_len_t;


typedef struct __DE_SCAL_INPUT_SIZE
{
    __u32 width            :   13;   //bit12~0
    __u32 reserved0        :   3;    //bit15~13
    __u32 height           :   13;   //bit28~16
    __u32 reserved1        :   3;    //bit31~29
}__de_scal_input_size_t;


typedef struct __DE_SCAL_OUTPUT_SIZE
{
    __u32 width            :   13;   //bit12~0  , for scaler0, the maxium is 8192, and for scaler1 the maxium is 2048
    __u32 reserved0        :   3;    //bit15~13
    __u32 height           :   13;   //bit28~16
    __u32 reserved1        :   3;    //bit31~29
}__de_scal_output_size_t;


typedef struct __DE_SCAL_SCAL_FACTOR
{
    __u32 factor           :   24;   //bit12~0
    __u32 reserved0        :   8;    //bit31~24
}__de_scal_scal_factor_t;

//initphase and tape offset only for scaler0 valid
typedef struct __DE_SCAL_INIT_PHASE
{
    __u32 phase            :   20;   //bit19~0
    __u32 reserved0        :   12;    //bit31~20
}__de_scal_init_phase_t;


typedef struct __DE_SCAL_TAPE_OFFSET
{
    __u32 tape0            :   7;   //bit6~0
    __u32 reserved0        :   1;   //bit7
    __u32 tape1            :   7;   //bit14~8
    __u32 reserved1        :   1;   //bit15
    __u32 tape2            :   7;   //bit22~16
    __u32 reserved2        :   1;   //bit23
    __u32 tape3            :   7;   //bit30~24
    __u32 reserved3        :   1;   //bit31
}__de_scal_tape_offset_t;

typedef struct __DE_SCAL_FIR_COEF
{
    __u32 tape0            :   8;  //bit7~0
    __u32 tape1            :   8;  //bit15~8
    __u32 tape2            :   8;  //bit23~16
    __u32 tape3            :   8;  //bit31~24
}__de_scal_fir_coef_t;

typedef struct __DE_SCAL_DEV
{
    __de_scal_en_t                  modl_en;               //0x000
    __de_scal_frm_ctrl_t            frm_ctrl;              //0x004
    __de_scal_bypass_en_t           bypass;                //0x008
    __de_scal_agth_sel_t            agth_sel;              //0x00c
    __de_scal_line_int_ctrl_t       line_int_ctrl;         //0x010
    __u32                           reserved0[3];          //0x014~0x01c
    __u32                           buf_addr[3];           //0x020
    __de_scal_field_ctrl_t          field_ctrl;            //0x02c
    __de_scal_mb_oft_t              mb_off[3];             //0x030~38
    __u32                           reserved1;             //0x03c
    __u32                           stride[3];             //0x040~48
    __de_scal_input_fmt_t           input_fmt;             //0x04c
    __u32                           wb_addr[3];            //0x050
    __de_scal_output_fmt_t          output_fmt;            //0x05c
    __de_scal_int_en_t              int_en;                //0x60
    __de_scal_int_status_t          int_status;            //0x064
    __de_scal_status_t              status;                //0x068
    __u32                           reserved2;             //0x6c
    __u32                           csc_coef[12];          //0x70~0x9c
    /*__DE_SCAL_CSC_COEF            ch0_csc_coef[3];       //0x070~78
    __DE_SCAL_CSC_CONT              ch0_csc_cont;          //0x07c
    __DE_SCAL_CSC_COEF              ch1_csc_coef[3];       //0x080~88
    __DE_SCAL_CSC_CONT              ch1_csc_cont;          //0x08c
    __DE_SCAL_CSC_COEF              ch2_csc_coef[3];       //0x090~98
    __DE_SCAL_CSC_CONT              ch2_csc_cont;          //0x09c
	*/
    __de_scal_di_ctrl_t             di_ctrl;               //0x0a0   //only for scaler0
    __de_scal_di_diagintp_th_t      di_diagintp_th;        //0x0a4
    __de_scal_di_tempdiff_th_t      di_tempdiff_th;        //0x0a8
    __de_scal_di_sawtooth_th_t      di_sawtooth_th;        //0x0ac
    __de_scal_di_spatial_th_t       di_spatial_th;         //0x0b0
    __de_scal_di_burst_len_t        di_burst_len;          //0x0b4
    __u32                           di_preluma_buf;        //0x0b8
    __u32                           di_mafflag_buf;        //0x0bc
    __u32                           di_flag_linestride;    //0x0c0  //only for scaler0
    __u32                           reserved3[15];         //0xff~c4
    __de_scal_input_size_t          ch0_in_size;           //0x100
    __de_scal_output_size_t         ch0_out_size;          //0x104
    __de_scal_scal_factor_t         ch0_h_factor;          //0x108
    __de_scal_scal_factor_t         ch0_v_factor;          //0x10c
    __de_scal_init_phase_t          ch0_h_init_phase;      //0x110  //only for scaler0
    __de_scal_init_phase_t          ch0_v_init_phase0;     //0x114
    __de_scal_init_phase_t          ch0_v_init_phase1;     //0x118
    __u32                           ch0_reserved4;         //0x11c
    __de_scal_tape_offset_t         ch0_h_tape_offset;     //0x120
    __u32                           ch0_reverved5;         //0x124
    __de_scal_tape_offset_t         ch0_v_tape_offset;     //0x128
    __u32                           ch0_reserved6[53];     //0x12c~1ff
    __de_scal_input_size_t          ch12_in_size;          //0x200
    __de_scal_output_size_t         ch12_out_size;         //0x204
    __de_scal_scal_factor_t         ch12_h_factor;         //0x208
    __de_scal_scal_factor_t         ch12_v_factor;         //0x20c
    __de_scal_init_phase_t          ch12_h_init_phase;     //0x210
    __de_scal_init_phase_t          ch12_v_init_phase0;    //0x214
    __de_scal_init_phase_t          ch12_v_init_phase1;    //0x218
    __u32                           ch12_reserved7;        //0x21c
    __de_scal_tape_offset_t         ch12_h_tape_offset;    //0x220
    __u32                           ch12_reverved8;        //0x224
    __de_scal_tape_offset_t         ch12_v_tape_offset;    //0x228       //only for scaler0
    __u32                           ch12_reserved9[53];    //0x22c~2ff
    __u32                           reserved10[64];        //0x300~?3ff
    __u32                           ch0_h_fir_coef[32];    //0x400~47f
    __u32                           reserved11[32];        //0x480~4ff
    __u32                           ch0_v_fir_coef[32];    //0x500~57f
    __u32                           reserved12[32];        //0x580~5ff
    __u32                           ch12_h_fir_coef[32];   //0x600~67f
    __u32                           reserved13[32];        //0x680~6ff
    __u32                           ch12_v_fir_coef[32];   //0x700~77f
    __u32                           reserved14[32];        //0x780~7ff
    __u32                           reserved15[512];       //0x800~fff
}__de_scal_dev_t;


typedef struct __SCAL_MATRIX4X4
{
	__s32 x00;
	__s32 x01;
	__s32 x02;
	__s32 x03;
	__s32 x10;
	__s32 x11;
	__s32 x12;
	__s32 x13;
	__s32 x20;
	__s32 x21;
	__s32 x22;
	__s32 x23;
	__s32 x30;
	__s32 x31;
	__s32 x32;
	__s32 x33;
}__scal_matrix4x4;

extern __s32 iDE_SCAL_Matrix_Mul(__scal_matrix4x4 in1, __scal_matrix4x4 in2, __scal_matrix4x4 *result);
extern __s32 iDE_SCAL_Csc_Lmt(__s32 *value, __s32 min, __s32 max, __s32 shift, __s32 validbit);

#endif
