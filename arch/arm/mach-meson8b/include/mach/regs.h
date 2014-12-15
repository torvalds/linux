/*
 *
 *
 *  Copyright (C) 2010 AMLOGIC, INC.
 *
 * License terms: GNU General Public License (GPL) version 2
 * Basic register address definitions in physical memory and
 * some block defintions for core devices like the timer.
 * copy from linux kernel
 */

/*
 * Temp solution file for M6 define
*/
#ifndef __MACH_MESSON_FIRM_REGS_H
#define __MACH_MESSON_FIRM_REGS_H

#define IO_CBUS_BASE2  0xc1100000

#define NAND_CMD  ((0xc1108600-IO_CBUS_BASE2)>>2)
#define NAND_CFG  ((0xc1108604-IO_CBUS_BASE2)>>2)
#define NAND_DADR ((0xc1108608-IO_CBUS_BASE2)>>2)
#define NAND_IADR ((0xc110860c-IO_CBUS_BASE2)>>2)
#define NAND_BUF  ((0xc1108610-IO_CBUS_BASE2)>>2)
#define NAND_INFO ((0xc1108614-IO_CBUS_BASE2)>>2)
#define NAND_DC   ((0xc1108618-IO_CBUS_BASE2)>>2)
#define NAND_ADR  ((0xc110861c-IO_CBUS_BASE2)>>2)
#define NAND_DL   ((0xc1108620-IO_CBUS_BASE2)>>2)
#define NAND_DH   ((0xc1108624-IO_CBUS_BASE2)>>2)
#define NAND_CADR ((0xc1108628-IO_CBUS_BASE2)>>2)
#define NAND_SADR ((0xc110862c-IO_CBUS_BASE2)>>2)

#define P_NAND_CMD                                CBUS_REG_ADDR(NAND_CMD)
#define P_NAND_CFG                                CBUS_REG_ADDR(NAND_CFG)
#define P_NAND_DADR                               CBUS_REG_ADDR(NAND_DADR)
#define P_NAND_IADR                               CBUS_REG_ADDR(NAND_IADR)
#define P_NAND_BUF                                CBUS_REG_ADDR(NAND_BUF)
#define P_NAND_INFO                               CBUS_REG_ADDR(NAND_INFO)
#define P_NAND_DC                                 CBUS_REG_ADDR(NAND_DC)
#define P_NAND_ADR                                CBUS_REG_ADDR(NAND_ADR)
#define P_NAND_DL                                 CBUS_REG_ADDR(NAND_DL)
#define P_NAND_DH                                 CBUS_REG_ADDR(NAND_DH)
#define P_NAND_CADR                               CBUS_REG_ADDR(NAND_CADR)
#define P_NAND_SADR                               CBUS_REG_ADDR(NAND_SADR)

#define VPP_OSD2_PREBLEND           (1 << 17)
#define VPP_OSD1_PREBLEND           (1 << 16)
#define VPP_VD2_PREBLEND            (1 << 15)
#define VPP_VD1_PREBLEND            (1 << 14)
#define VPP_OSD2_POSTBLEND          (1 << 13)
#define VPP_OSD1_POSTBLEND          (1 << 12)
#define VPP_VD2_POSTBLEND           (1 << 11)
#define VPP_VD1_POSTBLEND           (1 << 10)
#define VPP_POSTBLEND_EN			(1 << 7)
#define VPP_PRE_FG_OSD2             (1 << 5)
#define VPP_PREBLEND_EN             (1 << 6)
#define VPP_POST_FG_OSD2            (1 << 4)

#define I2SIN_DIR       0    // I2S CLK and LRCLK direction. 0 : input 1 : output.
#define I2SIN_CLK_SEL    1    // I2S clk selection : 0 : from pad input. 1 : from AIU.
#define I2SIN_LRCLK_SEL 2
#define I2SIN_POS_SYNC  3
#define I2SIN_LRCLK_SKEW 4    // 6:4
#define I2SIN_LRCLK_INVT 7
#define I2SIN_SIZE       8    //9:8 : 0 16 bit. 1 : 18 bits 2 : 20 bits 3 : 24bits.
#define I2SIN_CHAN_EN   10    //13:10. 
#define I2SIN_EN        15

#define AUDIN_FIFO0_EN       0
#define AUDIN_FIFO0_LOAD     2    //write 1 to load address to AUDIN_FIFO0.
         
#define AUDIN_FIFO0_DIN_SEL  3
            // 0     spdifIN
            // 1     i2Sin
            // 2     PCMIN
            // 3     HDMI in
            // 4     DEMODULATOR IN
#define AUDIN_FIFO0_ENDIAN   8    //10:8   data endian control.
#define AUDIN_FIFO0_CHAN     11    //14:11   channel number.  in M1 suppose there's only 1 channel and 2 channel.
#define AUDIN_FIFO0_UG       15    // urgent request enable.


/*BT656 MACRO */
//#define BT_CTRL 0x2240 	///../ucode/register.h
#define BT_SYSCLOCK_RESET    30      //Sync fifo soft  reset_n at system clock domain.     Level reset. 0 = reset. 1 : normal mode.
#define BT_656CLOCK_RESET    29      //Sync fifo soft reset_n at bt656 clock domain.   Level reset.  0 = reset.  1 : normal mode.
//    #define BT_VSYNC_SEL              25      //25:26 VDIN VS selection.   00 :  SOF.  01: EOF.   10: vbi start point.  11 : vbi end point.
//    #define BT_HSYNC_SEL              23      //24:23 VDIN HS selection.  00 : EAV.  01: SAV.    10:  EOL.  11: SOL
#define BT_CAMERA_MODE        22      // Camera_mode
#define BT_CLOCK_ENABLE        7	// 1: enable bt656 clock. 0: disable bt656 clock.

//#define BT_PORT_CTRL 0x2249 	///../ucode/register.h
//    #define BT_VSYNC_MODE      23  //1: use  vsync  as the VBI start point. 0: use the regular vref.
//    #define BT_HSYNC_MODE      22  //1: use hsync as the active video start point.  0. Use regular sav and eav. 
#define BT_SOFT_RESET           31	// Soft reset
//    #define BT_JPEG_START           30
//    #define BT_JPEG_IGNORE_BYTES    18	//20:18
//    #define BT_JPEG_IGNORE_LAST     17
#define BT_UPDATE_ST_SEL        16
#define BT_COLOR_REPEAT         15
//    #define BT_VIDEO_MODE           13	// 14:13
#define BT_AUTO_FMT             12
#define BT_PROG_MODE            11
//    #define BT_JPEG_MODE            10
#define BT_XCLK27_EN_BIT        9	// 1 : xclk27 is input.     0 : xclk27 is output.
#define BT_FID_EN_BIT           8	// 1 : enable use FID port.
#define BT_CLK27_SEL_BIT        7	// 1 : external xclk27      0 : internal clk27.
//    #define BT_CLK27_PHASE_BIT      6	// 1 : no inverted          0 : inverted.
//    #define BT_ACE_MODE_BIT         5	// 1 : auto cover error by hardware.
#define BT_SLICE_MODE_BIT       4	// 1 : no ancillay flag     0 : with ancillay flag.
#define BT_FMT_MODE_BIT         3	// 1 : ntsc                 0 : pal.
#define BT_REF_MODE_BIT         2	// 1 : from bit stream.     0 : from ports.
#define BT_MODE_BIT             1	// 1 : BT656 model          0 : SAA7118 mode.
#define BT_EN_BIT               0	// 1 : enable.
#define BT_VSYNC_PHASE      0
#define BT_HSYNC_PHASE      1
//    #define BT_VSYNC_PULSE      2
//    #define BT_HSYNC_PULSE      3
//    #define BT_FID_PHASE        4
#define BT_FID_HSVS         5
#define BT_IDQ_EN           6
#define BT_IDQ_PHASE        7
#define BT_D8B              8
//    #define BT_10BTO8B          9
#define BT_FID_DELAY       10	//12:10
#define BT_VSYNC_DELAY     13	//
#define BT_HSYNC_DELAY     16



#endif
