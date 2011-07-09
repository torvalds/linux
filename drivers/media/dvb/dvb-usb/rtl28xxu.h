/*
 * Realtek RTL28xxU DVB USB driver
 *
 * Copyright (C) 2009 Antti Palosaari <crope@iki.fi>
 * Copyright (C) 2011 Antti Palosaari <crope@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License along
 *    with this program; if not, write to the Free Software Foundation, Inc.,
 *    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef RTL28XXU_H
#define RTL28XXU_H

#define DVB_USB_LOG_PREFIX "rtl28xxu"
#include "dvb-usb.h"

#define deb_info(args...) dprintk(dvb_usb_rtl28xxu_debug, 0x01, args)
#define deb_rc(args...)   dprintk(dvb_usb_rtl28xxu_debug, 0x02, args)
#define deb_xfer(args...) dprintk(dvb_usb_rtl28xxu_debug, 0x04, args)
#define deb_reg(args...)  dprintk(dvb_usb_rtl28xxu_debug, 0x08, args)
#define deb_i2c(args...)  dprintk(dvb_usb_rtl28xxu_debug, 0x10, args)
#define deb_fw(args...)   dprintk(dvb_usb_rtl28xxu_debug, 0x20, args)

#define deb_dump(r, t, v, i, b, l, func) { \
	int loop_; \
	func("%02x %02x %02x %02x %02x %02x %02x %02x", \
		t, r, v & 0xff, v >> 8, i & 0xff, i >> 8, l & 0xff, l >> 8); \
	if (t == (USB_TYPE_VENDOR | USB_DIR_OUT)) \
		func(" >>> "); \
	else \
		func(" <<< "); \
	for (loop_ = 0; loop_ < l; loop_++) \
		func("%02x ", b[loop_]); \
	func("\n");\
}

/*
 * USB commands
 * (usb_control_msg() index parameter)
 */
#define DEMOD (0x00 << 8)
#define USB   (0x01 << 8)
#define SYS   (0x02 << 8)
#define I2C   (0x03 << 8)
#define CMD_WR_FLAG   0x10
#define CMD_DEMOD_RD  (DEMOD)
#define CMD_DEMOD_WR  (DEMOD | CMD_WR_FLAG)
#define CMD_USB_RD    (USB)
#define CMD_USB_WR    (USB | CMD_WR_FLAG)
#define CMD_SYS_RD    (SYS)
#define CMD_SYS_WR    (SYS | CMD_WR_FLAG)
#define CMD_I2C_RD    (I2C)
#define CMD_I2C_WR    (I2C | CMD_WR_FLAG)

struct rtl28xxu_priv {
	u8 chip_id;
	u8 tuner;
};

enum rtl28xxu_chip_id {
	CHIP_ID_NONE = 0,
	CHIP_ID_RTL2831U,
	CHIP_ID_RTL2832U,
};

enum rtl28xxu_tuner {
	TUNER_NONE = 0,
	TUNER_RTL2830_QT1010,
	TUNER_RTL2830_MT2060,
	TUNER_RTL2830_MXL5005S,
};

struct rtl28xxu_req {
	u16 value;
	u16 index;
	u16 size;
	u8 *data;
};

struct rtl28xxu_reg_val {
	u16 reg;
	u8 val;
};

/*
 * memory map
 *
 * 0x0000 DEMOD : demodulator
 * 0x2000 USB   : SIE, USB endpoint, debug, DMA
 * 0x3000 SYS   : system
 * 0xfc00 RC    : remote controller (not RTL2831U)
 */

/*
 * USB registers
 */
/* SIE Control Registers */
#define USB_SYSCTL         0x2000 /* USB system control */
#define USB_SYSCTL_0       0x2000 /* USB system control */
#define USB_SYSCTL_1       0x2001 /* USB system control */
#define USB_SYSCTL_2       0x2002 /* USB system control */
#define USB_SYSCTL_3       0x2003 /* USB system control */
#define USB_IRQSTAT        0x2008 /* SIE interrupt status */
#define USB_IRQEN          0x200C /* SIE interrupt enable */
#define USB_CTRL           0x2010 /* USB control */
#define USB_STAT           0x2014 /* USB status */
#define USB_DEVADDR        0x2018 /* USB device address */
#define USB_TEST           0x201C /* USB test mode */
#define USB_FRAME_NUMBER   0x2020 /* frame number */
#define USB_FIFO_ADDR      0x2028 /* address of SIE FIFO RAM */
#define USB_FIFO_CMD       0x202A /* SIE FIFO RAM access command */
#define USB_FIFO_DATA      0x2030 /* SIE FIFO RAM data */
/* Endpoint Registers */
#define EP0_SETUPA         0x20F8 /* EP 0 setup packet lower byte */
#define EP0_SETUPB         0x20FC /* EP 0 setup packet higher byte */
#define USB_EP0_CFG        0x2104 /* EP 0 configure */
#define USB_EP0_CTL        0x2108 /* EP 0 control */
#define USB_EP0_STAT       0x210C /* EP 0 status */
#define USB_EP0_IRQSTAT    0x2110 /* EP 0 interrupt status */
#define USB_EP0_IRQEN      0x2114 /* EP 0 interrupt enable */
#define USB_EP0_MAXPKT     0x2118 /* EP 0 max packet size */
#define USB_EP0_BC         0x2120 /* EP 0 FIFO byte counter */
#define USB_EPA_CFG        0x2144 /* EP A configure */
#define USB_EPA_CFG_0      0x2144 /* EP A configure */
#define USB_EPA_CFG_1      0x2145 /* EP A configure */
#define USB_EPA_CFG_2      0x2146 /* EP A configure */
#define USB_EPA_CFG_3      0x2147 /* EP A configure */
#define USB_EPA_CTL        0x2148 /* EP A control */
#define USB_EPA_CTL_0      0x2148 /* EP A control */
#define USB_EPA_CTL_1      0x2149 /* EP A control */
#define USB_EPA_CTL_2      0x214A /* EP A control */
#define USB_EPA_CTL_3      0x214B /* EP A control */
#define USB_EPA_STAT       0x214C /* EP A status */
#define USB_EPA_IRQSTAT    0x2150 /* EP A interrupt status */
#define USB_EPA_IRQEN      0x2154 /* EP A interrupt enable */
#define USB_EPA_MAXPKT     0x2158 /* EP A max packet size */
#define USB_EPA_MAXPKT_0   0x2158 /* EP A max packet size */
#define USB_EPA_MAXPKT_1   0x2159 /* EP A max packet size */
#define USB_EPA_MAXPKT_2   0x215A /* EP A max packet size */
#define USB_EPA_MAXPKT_3   0x215B /* EP A max packet size */
#define USB_EPA_FIFO_CFG   0x2160 /* EP A FIFO configure */
#define USB_EPA_FIFO_CFG_0 0x2160 /* EP A FIFO configure */
#define USB_EPA_FIFO_CFG_1 0x2161 /* EP A FIFO configure */
#define USB_EPA_FIFO_CFG_2 0x2162 /* EP A FIFO configure */
#define USB_EPA_FIFO_CFG_3 0x2163 /* EP A FIFO configure */
/* Debug Registers */
#define USB_PHYTSTDIS      0x2F04 /* PHY test disable */
#define USB_TOUT_VAL       0x2F08 /* USB time-out time */
#define USB_VDRCTRL        0x2F10 /* UTMI vendor signal control */
#define USB_VSTAIN         0x2F14 /* UTMI vendor signal status in */
#define USB_VLOADM         0x2F18 /* UTMI load vendor signal status in */
#define USB_VSTAOUT        0x2F1C /* UTMI vendor signal status out */
#define USB_UTMI_TST       0x2F80 /* UTMI test */
#define USB_UTMI_STATUS    0x2F84 /* UTMI status */
#define USB_TSTCTL         0x2F88 /* test control */
#define USB_TSTCTL2        0x2F8C /* test control 2 */
#define USB_PID_FORCE      0x2F90 /* force PID */
#define USB_PKTERR_CNT     0x2F94 /* packet error counter */
#define USB_RXERR_CNT      0x2F98 /* RX error counter */
#define USB_MEM_BIST       0x2F9C /* MEM BIST test */
#define USB_SLBBIST        0x2FA0 /* self-loop-back BIST */
#define USB_CNTTEST        0x2FA4 /* counter test */
#define USB_PHYTST         0x2FC0 /* USB PHY test */
#define USB_DBGIDX         0x2FF0 /* select individual block debug signal */
#define USB_DBGMUX         0x2FF4 /* debug signal module mux */

/*
 * SYS registers
 */
/* demod control registers */
#define SYS_SYS0           0x3000 /* include DEMOD_CTL, GPO, GPI, GPOE */
#define SYS_DEMOD_CTL      0x3000 /* control register for DVB-T demodulator */
/* GPIO registers */
#define SYS_GPIO_OUT_VAL   0x3001 /* output value of GPIO */
#define SYS_GPIO_IN_VAL    0x3002 /* input value of GPIO */
#define SYS_GPIO_OUT_EN    0x3003 /* output enable of GPIO */
#define SYS_SYS1           0x3004 /* include GPD, SYSINTE, SYSINTS, GP_CFG0 */
#define SYS_GPIO_DIR       0x3004 /* direction control for GPIO */
#define SYS_SYSINTE        0x3005 /* system interrupt enable */
#define SYS_SYSINTS        0x3006 /* system interrupt status */
#define SYS_GPIO_CFG0      0x3007 /* PAD configuration for GPIO0-GPIO3 */
#define SYS_SYS2           0x3008 /* include GP_CFG1 and 3 reserved bytes */
#define SYS_GPIO_CFG1      0x3008 /* PAD configuration for GPIO4 */
/* IrDA registers */
#define SYS_IRRC_PSR       0x3020 /* IR protocol selection */
#define SYS_IRRC_PER       0x3024 /* IR protocol extension */
#define SYS_IRRC_SF        0x3028 /* IR sampling frequency */
#define SYS_IRRC_DPIR      0x302C /* IR data package interval */
#define SYS_IRRC_CR        0x3030 /* IR control */
#define SYS_IRRC_RP        0x3034 /* IR read port */
#define SYS_IRRC_SR        0x3038 /* IR status */
/* I2C master registers */
#define SYS_I2CCR          0x3040 /* I2C clock */
#define SYS_I2CMCR         0x3044 /* I2C master control */
#define SYS_I2CMSTR        0x3048 /* I2C master SCL timing */
#define SYS_I2CMSR         0x304C /* I2C master status */
#define SYS_I2CMFR         0x3050 /* I2C master FIFO */

#endif
