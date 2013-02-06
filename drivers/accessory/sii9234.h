/*
 * Silicon Image MHL(Mobile HD Link) Transmitter device driver
 *
 * Copyright (c) by Dongsoo Kim <dongsoo45.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#include <linux/types.h>

#define SII9234_I2C_CHECK_RETRY	50
#define SII9234_SEC(a)	((a*HZ/10) * 10)

#define TRUE    1
#define FALSE   0

/*
 * regset item mode ID
 */
#define SII_W	0x01
#define SII_R	0x02
#define SII_M	0x03
#define SII_D	0x04

/*
 * Local register define
 */
#define TX_SLAVE_ADDR	(0x72 >> 1)
#define SII9234_ES_P1	(0x7A >> 1)
#define SII9234_ES_P2	(0x92 >> 1)
#define CBUS_SLAVE_ADDR	(0xC8 >> 1)

/* BIT MASK */
#define BIT_0	0x01
#define BIT_1	0x02
#define BIT_2	0x04
#define BIT_3	0x08
#define BIT_4	0x10
#define BIT_5	0x20
#define BIT_6	0x40
#define BIT_7	0x80

/* power state */
#define TX_POWER_STATE_D0	0x00
#define TX_POWER_STATE_D1	0x01
#define TX_POWER_STATE_D2	0x02
#define TX_POWER_STATE_D3	0x03

/*
 * @sii9234_regset
 * mode : 0x01(write), 0x02(read), 0x03(mask)
 * addr : TX_SLAVE_ADDR, SII9234_ES_P1,
 *	SII9234_ES_P2, CBUS_SLAVE_ADDR
 * reg : address of register
 * val : expecting value
 */
struct sii9234_regset {
	unsigned char mode;
	unsigned char addr;
	unsigned char reg;
	unsigned char val;
};

/* TPI System Control Register ================= */

#define TPI_SYSTEM_CONTROL_DATA_REG			(0x1A)

#define LINK_INTEGRITY_MODE_MASK			(BIT_6)
#define LINK_INTEGRITY_STATIC				(0x00)
#define LINK_INTEGRITY_DYNAMIC				(0x40)

#define TMDS_OUTPUT_CONTROL_MASK			(BIT_4)
#define TMDS_OUTPUT_CONTROL_ACTIVE			(0x00)
#define TMDS_OUTPUT_CONTROL_POWER_DOWN		(0x10)

#define AV_MUTE_MASK						(BIT_3)
#define AV_MUTE_NORMAL						(0x00)
#define AV_MUTE_MUTED						(0x08)

#define DDC_BUS_REQUEST_MASK				(BIT_2)
#define DDC_BUS_REQUEST_NOT_USING			(0x00)
#define DDC_BUS_REQUEST_REQUESTED			(0x04)

#define DDC_BUS_GRANT_MASK					(BIT_1)
#define DDC_BUS_GRANT_NOT_AVAILABLE			(0x00)
#define DDC_BUS_GRANT_GRANTED				(0x02)

#define OUTPUT_MODE_MASK					(BIT_0)
#define OUTPUT_MODE_DVI						(0x00)
#define OUTPUT_MODE_HDMI					(0x01)

/* Interrupt Enable Register =================== */

#define TPI_INTERRUPT_ENABLE_REG			(0x3C)

#define HDCP_AUTH_STATUS_CHANGE_EN_MASK		(BIT_7)
#define HDCP_AUTH_STATUS_CHANGE_DISABLE		(0x00)
#define HDCP_AUTH_STATUS_CHANGE_ENABLE		(0x80)

#define HDCP_VPRIME_VALUE_READY_EN_MASK		(BIT_6)
#define HDCP_VPRIME_VALUE_READY_DISABLE		(0x00)
#define HDCP_VPRIME_VALUE_READY_ENABLE		(0x40)

#define HDCP_SECURITY_CHANGE_EN_MASK		(BIT_5)
#define HDCP_SECURITY_CHANGE_DISABLE		(0x00)
#define HDCP_SECURITY_CHANGE_ENABLE			(0x20)

#define AUDIO_ERROR_EVENT_EN_MASK			(BIT_4)
#define AUDIO_ERROR_EVENT_DISABLE			(0x00)
#define AUDIO_ERROR_EVENT_ENABLE			(0x10)

#define CPI_EVENT_NO_RX_SENSE_MASK			(BIT_3)
#define CPI_EVENT_NO_RX_SENSE_DISABLE		(0x00)
#define CPI_EVENT_NO_RX_SENSE_ENABLE		(0x08)

#define RECEIVER_SENSE_EVENT_EN_MASK		(BIT_1)
#define RECEIVER_SENSE_EVENT_DISABLE		(0x00)
#define RECEIVER_SENSE_EVENT_ENABLE			(0x02)

#define HOT_PLUG_EVENT_EN_MASK				(BIT_0)
#define HOT_PLUG_EVENT_DISABLE				(0x00)
#define HOT_PLUG_EVENT_ENABLE				(0x01)

/* Interrupt status register ==================== */
#define TPI_INTERRUPT_STATUS_REG		(0x3D)

#define HDCP_AUTH_STATUS_CHANGE_EVENT_MASK	(BIT_7)
#define HDCP_AUTH_STATUS_CHANGE_EVENT_NO	(0x00)
#define HDCP_AUTH_STATUS_CHANGE_EVENT_YES	(0x80)

#define HDCP_VPRIME_VALUE_READY_EVENT_MASK	(BIT_6)
#define HDCP_VPRIME_VALUE_READY_EVENT_NO	(0x00)
#define HDCP_VPRIME_VALUE_READY_EVENT_YES	(0x40)

#define HDCP_SECURITY_CHANGE_EVENT_MASK		(BIT_5)
#define HDCP_SECURITY_CHANGE_EVENT_NO		(0x00)
#define HDCP_SECURITY_CHANGE_EVENT_YES		(0x20)

#define AUDIO_ERROR_EVENT_MASK			(BIT_4)
#define AUDIO_ERROR_EVENT_NO			(0x00)
#define AUDIO_ERROR_EVENT_YES			(0x10)

#define CPI_EVENT_MASK				(BIT_3)
#define CPI_EVENT_NO				(0x00)
#define CPI_EVENT_YES				(0x08)
/* This bit is dual purpose depending on the value of 0x3C[3] */
#define RX_SENSE_MASK				(BIT_3)
#define RX_SENSE_NOT_ATTACHED			(0x00)
#define RX_SENSE_ATTACHED			(0x08)

#define HOT_PLUG_PIN_STATE_MASK			(BIT_2)
#define HOT_PLUG_PIN_STATE_LOW			(0x00)
#define HOT_PLUG_PIN_STATE_HIGH			(0x04)

#define RECEIVER_SENSE_EVENT_MASK		(BIT_1)
#define RECEIVER_SENSE_EVENT_NO			(0x00)
#define RECEIVER_SENSE_EVENT_YES		(0x02)

#define HOT_PLUG_EVENT_MASK			(BIT_0)
#define HOT_PLUG_EVENT_NO			(0x00)
#define HOT_PLUG_EVENT_YES			(0x01)

/* ===================================================== */

extern void sii9234_tpi_init(void);
extern void MHD_HW_Reset(void);
extern void MHD_HW_Off(void);
extern int MHD_HW_IsOn(void);
extern int MHD_Read_deviceID(void);
extern void MHD_INT_clear(void);
extern void MHD_OUT_EN(void);

/*I2C driver add 20100614  kyungrok */
extern struct i2c_driver SII9234_i2c_driver;
extern struct i2c_driver SII9234A_i2c_driver;
extern struct i2c_driver SII9234B_i2c_driver;
extern struct i2c_driver SII9234C_i2c_driver;


