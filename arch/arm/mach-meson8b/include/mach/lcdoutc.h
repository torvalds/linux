
#ifndef MACH_LCDOUTC_H
#define MACH_LCDOUTC_H

#define CONFIG_LCD_TYPE_MID_VALID
#define CONFIG_LCD_IF_TTL_VALID
#define CONFIG_LCD_IF_LVDS_VALID
#define CONFIG_LCD_IF_MIPI_VALID
/*
// lcd driver global API, special by CPU
*/
//*************************************************************
// For mipi-dsi external driver use
//*************************************************************
//payload struct:
//data_type, command, para_num, parameters...
//data_type=0xff, command=0xff, means ending flag
//data_type=0xff, command<0xff, means delay time(unit ms)
//return:
//command num
extern int dsi_write_cmd(unsigned char* payload);
//*************************************************************

#endif
