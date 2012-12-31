/* linux/drivers/video/samsung/lg4591_param.h
 *
 * MIPI-DSI based lg4591 lcd panel driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/
//---------------------------------------------------------------------------------------------------
#ifndef __LG4591_PARAM__
#define __LG4591_PARAM__

const unsigned char DSI_CONFIG[] = {
    0xE0, 
    0x43, 0x00, 0x80, 0x00, 0x00
}; 
const unsigned char DISPLAY_MODE1[] = {
    0xB5, 
    0x29, 0x20, 0x40, 0x00, 0x00
};
const unsigned char DISPLAY_MODE2[] = {
    0xB6, 
    0x01, 0x14, 0x0F, 0x16, 0x13
};
const unsigned char P_GAMMA_R_SETTING[] = {
    0xD0, 
    0x00, 0x11, 0x77, 0x23, 0x16, 0x06, 0x62, 0x41, 0x03
};
const unsigned char N_GAMMA_R_SETTING[] = {
    0xD1, 
    0x00, 0x14, 0x63, 0x23, 0x08, 0x06, 0x41, 0x33, 0x04
};
const unsigned char P_GAMMA_G_SETTING[] = {
    0xD2, 
    0x00, 0x11, 0x77, 0x23, 0x16, 0x06, 0x62, 0x41, 0x03
};
const unsigned char N_GAMMA_G_SETTING[] = {
    0xD3, 
    0x00, 0x14, 0x63, 0x23, 0x08, 0x06, 0x41, 0x33, 0x04
};
const unsigned char P_GAMMA_B_SETTING[] = {
    0xD4, 
    0x00, 0x11, 0x77, 0x23, 0x16, 0x06, 0x62, 0x41, 0x03
};
const unsigned char N_GAMMA_B_SETTING[] = {
    0xD5, 
    0x00, 0x14, 0x63, 0x23, 0x08, 0x06, 0x41, 0x33, 0x04
};
const unsigned char OSC_SETTING[] = {
    0xC0, 
    0x01, 0x04
};
const unsigned char POWER_SETTING3[] = {
    0xC3, 
    0x00, 0x09, 0x10, 0x12, 0x00, 0x66, 0x20, 0x31,0x00
};
const unsigned char POWER_SETTING4[] = {
    0xC4, 
    0x22, 0x24, 0x18, 0x18, 0x47
};
const unsigned char POWER_SETTING7[] = {
    0xC7, 
    0x10, 0x00, 0x14
};
const unsigned char OTP2_SETTING[] = {
    0XF9, 
    0x00
};
const unsigned char DEEP_STANDBY_0[] = {
    0xC1, 
    0x00
};
const unsigned char POWER_SETTING2_1[] = {
    0xC2, 
    0x02
};
const unsigned char POWER_SETTING2_2[] = {
    0xC2, 
    0x06
};
const unsigned char POWER_SETTING2_3[] = {
    0xC2, 
    0x4E
};
const unsigned char EXIT_SLEEP[] =  {
    0x11,
    0x00
};
const unsigned char OTP2_SETTING2[] = {
    0XF9, 
    0x80
};
const unsigned char DISPLAY_ON[] = {
    0x29,
    0x00
};

//---------------------------------------------------------------------------------------------------
MODULE_DESCRIPTION("MIPI-DSI lg4591 (720x1280) Panel Driver");
MODULE_LICENSE("GPL");

//---------------------------------------------------------------------------------------------------
#endif  //#ifndef __LG4591_PARAM__

//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
