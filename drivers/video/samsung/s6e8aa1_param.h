/* linux/drivers/video/samsung/s6e8aa1_param.h
 *
 * MIPI-DSI based s6e8aa1 lcd panel driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/
//---------------------------------------------------------------------------------------------------
#ifndef __S6E8AA1_PARAM__
#define __S6E8AA1_PARAM__

const unsigned char APPLY_LEVEL_2[] = { 
    0xf1, 
    0x5a, 0x5a 
};

const unsigned char SEQ_SLEEP_OUT[] = {
	0x11,
	0x00, 0x00
};

const unsigned char PANEL_CONTROL[] = {
    0xF8,
    0x3d, 0x35, 0x00, 0x00, 0x00, 0x94, 0x00, 0x3c, 0x7d, 0x08,
    0x27, 0x08, 0x7d, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08, 0x7d,
    0x00, 0x00, 0x00, 0x02, 0x08, 0x08, 0x23, 0x66, 0xc0, 0xc1,
    0x01, 0xb1, 0xc1, 0x00, 0xc1, 0xf6, 0xf6, 0xc1
};

const unsigned char DISPAY_CONDITION_SET[] = {
    0xf2,
    0x80, 0x03, 0x0d
};

const unsigned char GAMMA_CONDITION_SET[] = {
    0xfa,
    0x01, 0x40, 0x32, 0x49, 0xc5, 0xbb, 0xb6, 0xbc, 0xc5, 0xc0,
    0xc9, 0xc9, 0xc5, 0xa0, 0x9f, 0x98, 0xb3, 0xb3, 0xaf, 0x00,
    0xd0, 0x00, 0xcf, 0x00, 0xe7
};

const unsigned char GAMMA_SET_UPDATE[] = {
    0xf7,
    0x03
};

const unsigned char SOURCE_CONTROL[] = {
    0xf6,
    0x00, 0x02, 0x00
};

const unsigned char PENTILE_CONTROL[] = {
    0xb6,
    0x0c, 0x02, 0x03, 0x32, 0xff, 0x44, 0x44, 0xc0, 0x10
};

const unsigned char ELVSS_CONTROL[] = {
    0xb1,
    0x08, 0x95, 0x41, 0xc4
};

const unsigned char NVM_SETTING[] = {
    0xd9,
    0x14, 0x40, 0x0c, 0xcb, 0xce, 0x6e, 0xc4, 0x07, 0x40
};

static const unsigned char SEQ_DISPLAY_ON[] = {
	0x29,
	0x00, 0x00
};

static const unsigned char SEQ_DISPLAY_OFF[] = {
	0x28,
	0x00, 0x00
};

//---------------------------------------------------------------------------------------------------
MODULE_DESCRIPTION("MIPI-DSI s6e8aa1 (720x1280) Panel Driver");
MODULE_LICENSE("GPL");

//---------------------------------------------------------------------------------------------------
#endif  //#ifndef __S6E8AA1_PARAM__

//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
