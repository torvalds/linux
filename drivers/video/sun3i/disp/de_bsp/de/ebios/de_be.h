#ifndef __DE_BE_H__
#define __DE_BE_H__

#include "de_bsp_i.h"

typedef struct
{
    __u32 en                :1; //bit0
    __u32 reset_start       :1; //bit1
    __u32 r0                :2; //bit3~2
    __u32 ready_en          :1; //bit4
    __u32 ready_ctl         :1; //bit5
    __u32 global_ready_en   :1; //bit6
    __u32 global_ready_ctl  :1; //bit7
    __u32 r1                :24;//bit31~8
}__image_control_t;

typedef struct
{
    __u32 in_progress   :1; //bit0
    __u32 error         :1; //bit1
    __u32 r0            :2; //bit3~2
    __u32 ready         :1; //bit4
    __u32 r1            :1; //bit5
    __u32 global_ready  :1; //bit6
    __u32 r2            :25;//bit31~7
}__image_status_t;

typedef struct
{
    __u32 blue      :8; //bit7~0
    __u32 green     :8; //bit15~8
    __u32 red       :8; //bit23~16
    __u32 r0        :8; //bit31~24
}__image_backcolor_t;

typedef struct
{
    __u32 addr   :32; //bit31~0
}__image_layer_address_t;

typedef struct
{
    __u32 line_width   :32; //bit31~0
}__image_layer_line_width_t;

typedef struct
{
    __u32 width     :11; //bit10~0
    __u32 r0        :5; //bit15~11
    __u32 height    :11; //bit26~16
    __u32 r1        :5; //bit31~27
}__image_layer_size_t;

typedef struct
{
    __u32 x :16; //bit15~0
    __u32 y :16; //bit31~16
}__image_layer_coordinate_t;

typedef struct
{
    __u32 en            :1; //bit0
    __u32 pipe          :1; //bit1
    __u32 priority      :1; //bit2
    __u32 video_ch      :1; //bit3
    __u32 alpha_en      :1; //bit4
    __u32 r0            :3; //bit7~5
    __u32 data_fmt      :4; //bit11~8
    __u32 pixel_seq     :1; //bit12
    __u32 r1            :11; //bit23~13
    __u32 alpha_value   :8; //bit31~24
}__image_layer_attribute_t;

typedef struct
{
    __image_control_t           control;                //0x00
    __image_status_t            status;                 //0x04
    __image_backcolor_t         back_color;             //0x08
    __u32                       r0;                     //0x0c
    __image_layer_address_t     layer_addr[2];          //0x14,0x10
    __u32                       r1[2];                  //0x1c,0x18
    __image_layer_line_width_t  layer_line_width[2];    //0x24,0x20
    __u32                       r2[2];                  //0x2c,0x28
    __image_layer_size_t        layer_size[2];          //0x34,0x30
    __u32                       r3[2];                  //0x3c,0x38
    __image_layer_coordinate_t  layer_coord[2];         //0x44,0x40
    __u32                       r4[2];                  //0x4c,0x48
    __image_layer_attribute_t   layer_attri[2];         //0x54,0x50
}__image_reg_t;

extern volatile __image_reg_t * image1_reg;

#endif
