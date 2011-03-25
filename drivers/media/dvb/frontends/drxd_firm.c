/*
 * drxd_firm.c : DRXD firmware tables
 *
 * Copyright (C) 2006-2007 Micronas
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 * Or, point your browser to http://www.gnu.org/copyleft/gpl.html
 */

/* TODO: generate this file with a script from a settings file */

/* Contains A2 firmware version: 1.4.2
 * Contains B1 firmware version: 3.3.33
 * Contains settings from driver 1.4.23
*/

#include "drxd_firm.h"

#define ADDRESS(x)     ((x) & 0xFF), (((x)>>8) & 0xFF), (((x)>>16) & 0xFF), (((x)>>24) & 0xFF)
#define LENGTH(x)      ((x) & 0xFF), (((x)>>8) & 0xFF)

/* Is written via block write, must be little endian */
#define DATA16(x)      ((x) & 0xFF), (((x)>>8) & 0xFF)

#define WRBLOCK(a, l) ADDRESS(a), LENGTH(l)
#define WR16(a, d) ADDRESS(a), LENGTH(1), DATA16(d)

#define END_OF_TABLE      0xFF, 0xFF, 0xFF, 0xFF

/* HI firmware patches */

#define HI_TR_FUNC_ADDR HI_IF_RAM_USR_BEGIN__A
#define HI_TR_FUNC_SIZE 9	/* size of this function in instruction words */

u8 DRXD_InitAtomicRead[] = {
	WRBLOCK(HI_TR_FUNC_ADDR, HI_TR_FUNC_SIZE),
	0x26, 0x00,		/* 0         -> ring.rdy;           */
	0x60, 0x04,		/* r0rami.dt -> ring.xba;           */
	0x61, 0x04,		/* r0rami.dt -> ring.xad;           */
	0xE3, 0x07,		/* HI_RA_RAM_USR_BEGIN -> ring.iad; */
	0x40, 0x00,		/* (long immediate)                 */
	0x64, 0x04,		/* r0rami.dt -> ring.len;           */
	0x65, 0x04,		/* r0rami.dt -> ring.ctl;           */
	0x26, 0x00,		/* 0         -> ring.rdy;           */
	0x38, 0x00,		/* 0         -> jumps.ad;           */
	END_OF_TABLE
};

/* Pins D0 and D1 of the parallel MPEG output can be used
   to set the I2C address of a device. */

#define HI_RST_FUNC_ADDR (HI_IF_RAM_USR_BEGIN__A + HI_TR_FUNC_SIZE)
#define HI_RST_FUNC_SIZE 54	/* size of this function in instruction words */

/* D0 Version */
u8 DRXD_HiI2cPatch_1[] = {
	WRBLOCK(HI_RST_FUNC_ADDR, HI_RST_FUNC_SIZE),
	0xC8, 0x07, 0x01, 0x00,	/* MASK      -> reg0.dt;                        */
	0xE0, 0x07, 0x15, 0x02,	/* (EC__BLK << 6) + EC_OC_REG__BNK -> ring.xba; */
	0xE1, 0x07, 0x12, 0x00,	/* EC_OC_REG_OC_MPG_SIO__A -> ring.xad;         */
	0xA2, 0x00,		/* M_BNK_ID_DAT -> ring.iba;                    */
	0x23, 0x00,		/* &data     -> ring.iad;                       */
	0x24, 0x00,		/* 0         -> ring.len;                       */
	0xA5, 0x02,		/* M_RC_CTR_SWAP | M_RC_CTR_READ -> ring.ctl;   */
	0x26, 0x00,		/* 0         -> ring.rdy;                       */
	0x42, 0x00,		/* &data+1   -> w0ram.ad;                       */
	0xC0, 0x07, 0xFF, 0x0F,	/* -1        -> w0ram.dt;                       */
	0x63, 0x00,		/* &data+1   -> ring.iad;                       */
	0x65, 0x02,		/* M_RC_CTR_SWAP | M_RC_CTR_WRITE -> ring.ctl;  */
	0x26, 0x00,		/* 0         -> ring.rdy;                       */
	0xE1, 0x07, 0x38, 0x00,	/* EC_OC_REG_OCR_MPG_USR_DAT__A -> ring.xad;    */
	0xA5, 0x02,		/* M_RC_CTR_SWAP | M_RC_CTR_READ -> ring.ctl;   */
	0x26, 0x00,		/* 0         -> ring.rdy;                       */
	0xE1, 0x07, 0x12, 0x00,	/* EC_OC_REG_OC_MPG_SIO__A -> ring.xad;         */
	0x23, 0x00,		/* &data     -> ring.iad;                       */
	0x65, 0x02,		/* M_RC_CTR_SWAP | M_RC_CTR_WRITE -> ring.ctl;  */
	0x26, 0x00,		/* 0         -> ring.rdy;                       */
	0x42, 0x00,		/* &data+1   -> w0ram.ad;                       */
	0x0F, 0x04,		/* r0ram.dt  -> and.op;                         */
	0x1C, 0x06,		/* reg0.dt   -> and.tr;                         */
	0xCF, 0x04,		/* and.rs    -> add.op;                         */
	0xD0, 0x07, 0x70, 0x00,	/* DEF_DEV_ID -> add.tr;                        */
	0xD0, 0x04,		/* add.rs    -> add.tr;                         */
	0xC8, 0x04,		/* add.rs    -> reg0.dt;                        */
	0x60, 0x00,		/* reg0.dt   -> w0ram.dt;                       */
	0xC2, 0x07, 0x10, 0x00,	/* SLV0_BASE -> w0rami.ad;                      */
	0x01, 0x00,		/* 0         -> w0rami.dt;                      */
	0x01, 0x06,		/* reg0.dt   -> w0rami.dt;                      */
	0xC2, 0x07, 0x20, 0x00,	/* SLV1_BASE -> w0rami.ad;                      */
	0x01, 0x00,		/* 0         -> w0rami.dt;                      */
	0x01, 0x06,		/* reg0.dt   -> w0rami.dt;                      */
	0xC2, 0x07, 0x30, 0x00,	/* CMD_BASE  -> w0rami.ad;                      */
	0x01, 0x00,		/* 0         -> w0rami.dt;                      */
	0x01, 0x00,		/* 0         -> w0rami.dt;                      */
	0x01, 0x00,		/* 0         -> w0rami.dt;                      */
	0x68, 0x00,		/* M_IC_SEL_PT1 -> i2c.sel;                     */
	0x29, 0x00,		/* M_IC_CMD_RESET -> i2c.cmd;                   */
	0x28, 0x00,		/* M_IC_SEL_PT0 -> i2c.sel;                     */
	0x29, 0x00,		/* M_IC_CMD_RESET -> i2c.cmd;                   */
	0xF8, 0x07, 0x2F, 0x00,	/* 0x2F      -> jumps.ad;                       */

	WR16((B_HI_IF_RAM_TRP_BPT0__AX + ((2 * 0) + 1)),
	     (u16) (HI_RST_FUNC_ADDR & 0x3FF)),
	WR16((B_HI_IF_RAM_TRP_BPT0__AX + ((2 * 1) + 1)),
	     (u16) (HI_RST_FUNC_ADDR & 0x3FF)),
	WR16((B_HI_IF_RAM_TRP_BPT0__AX + ((2 * 2) + 1)),
	     (u16) (HI_RST_FUNC_ADDR & 0x3FF)),
	WR16((B_HI_IF_RAM_TRP_BPT0__AX + ((2 * 3) + 1)),
	     (u16) (HI_RST_FUNC_ADDR & 0x3FF)),

	/* Force quick and dirty reset */
	WR16(B_HI_CT_REG_COMM_STATE__A, 0),
	END_OF_TABLE
};

/* D0,D1 Version */
u8 DRXD_HiI2cPatch_3[] = {
	WRBLOCK(HI_RST_FUNC_ADDR, HI_RST_FUNC_SIZE),
	0xC8, 0x07, 0x03, 0x00,	/* MASK      -> reg0.dt;                        */
	0xE0, 0x07, 0x15, 0x02,	/* (EC__BLK << 6) + EC_OC_REG__BNK -> ring.xba; */
	0xE1, 0x07, 0x12, 0x00,	/* EC_OC_REG_OC_MPG_SIO__A -> ring.xad;         */
	0xA2, 0x00,		/* M_BNK_ID_DAT -> ring.iba;                    */
	0x23, 0x00,		/* &data     -> ring.iad;                       */
	0x24, 0x00,		/* 0         -> ring.len;                       */
	0xA5, 0x02,		/* M_RC_CTR_SWAP | M_RC_CTR_READ -> ring.ctl;   */
	0x26, 0x00,		/* 0         -> ring.rdy;                       */
	0x42, 0x00,		/* &data+1   -> w0ram.ad;                       */
	0xC0, 0x07, 0xFF, 0x0F,	/* -1        -> w0ram.dt;                       */
	0x63, 0x00,		/* &data+1   -> ring.iad;                       */
	0x65, 0x02,		/* M_RC_CTR_SWAP | M_RC_CTR_WRITE -> ring.ctl;  */
	0x26, 0x00,		/* 0         -> ring.rdy;                       */
	0xE1, 0x07, 0x38, 0x00,	/* EC_OC_REG_OCR_MPG_USR_DAT__A -> ring.xad;    */
	0xA5, 0x02,		/* M_RC_CTR_SWAP | M_RC_CTR_READ -> ring.ctl;   */
	0x26, 0x00,		/* 0         -> ring.rdy;                       */
	0xE1, 0x07, 0x12, 0x00,	/* EC_OC_REG_OC_MPG_SIO__A -> ring.xad;         */
	0x23, 0x00,		/* &data     -> ring.iad;                       */
	0x65, 0x02,		/* M_RC_CTR_SWAP | M_RC_CTR_WRITE -> ring.ctl;  */
	0x26, 0x00,		/* 0         -> ring.rdy;                       */
	0x42, 0x00,		/* &data+1   -> w0ram.ad;                       */
	0x0F, 0x04,		/* r0ram.dt  -> and.op;                         */
	0x1C, 0x06,		/* reg0.dt   -> and.tr;                         */
	0xCF, 0x04,		/* and.rs    -> add.op;                         */
	0xD0, 0x07, 0x70, 0x00,	/* DEF_DEV_ID -> add.tr;                        */
	0xD0, 0x04,		/* add.rs    -> add.tr;                         */
	0xC8, 0x04,		/* add.rs    -> reg0.dt;                        */
	0x60, 0x00,		/* reg0.dt   -> w0ram.dt;                       */
	0xC2, 0x07, 0x10, 0x00,	/* SLV0_BASE -> w0rami.ad;                      */
	0x01, 0x00,		/* 0         -> w0rami.dt;                      */
	0x01, 0x06,		/* reg0.dt   -> w0rami.dt;                      */
	0xC2, 0x07, 0x20, 0x00,	/* SLV1_BASE -> w0rami.ad;                      */
	0x01, 0x00,		/* 0         -> w0rami.dt;                      */
	0x01, 0x06,		/* reg0.dt   -> w0rami.dt;                      */
	0xC2, 0x07, 0x30, 0x00,	/* CMD_BASE  -> w0rami.ad;                      */
	0x01, 0x00,		/* 0         -> w0rami.dt;                      */
	0x01, 0x00,		/* 0         -> w0rami.dt;                      */
	0x01, 0x00,		/* 0         -> w0rami.dt;                      */
	0x68, 0x00,		/* M_IC_SEL_PT1 -> i2c.sel;                     */
	0x29, 0x00,		/* M_IC_CMD_RESET -> i2c.cmd;                   */
	0x28, 0x00,		/* M_IC_SEL_PT0 -> i2c.sel;                     */
	0x29, 0x00,		/* M_IC_CMD_RESET -> i2c.cmd;                   */
	0xF8, 0x07, 0x2F, 0x00,	/* 0x2F      -> jumps.ad;                       */

	WR16((B_HI_IF_RAM_TRP_BPT0__AX + ((2 * 0) + 1)),
	     (u16) (HI_RST_FUNC_ADDR & 0x3FF)),
	WR16((B_HI_IF_RAM_TRP_BPT0__AX + ((2 * 1) + 1)),
	     (u16) (HI_RST_FUNC_ADDR & 0x3FF)),
	WR16((B_HI_IF_RAM_TRP_BPT0__AX + ((2 * 2) + 1)),
	     (u16) (HI_RST_FUNC_ADDR & 0x3FF)),
	WR16((B_HI_IF_RAM_TRP_BPT0__AX + ((2 * 3) + 1)),
	     (u16) (HI_RST_FUNC_ADDR & 0x3FF)),

	/* Force quick and dirty reset */
	WR16(B_HI_CT_REG_COMM_STATE__A, 0),
	END_OF_TABLE
};

u8 DRXD_ResetCEFR[] = {
	WRBLOCK(CE_REG_FR_TREAL00__A, 57),
	0x52, 0x00,		/* CE_REG_FR_TREAL00__A */
	0x00, 0x00,		/* CE_REG_FR_TIMAG00__A */
	0x52, 0x00,		/* CE_REG_FR_TREAL01__A */
	0x00, 0x00,		/* CE_REG_FR_TIMAG01__A */
	0x52, 0x00,		/* CE_REG_FR_TREAL02__A */
	0x00, 0x00,		/* CE_REG_FR_TIMAG02__A */
	0x52, 0x00,		/* CE_REG_FR_TREAL03__A */
	0x00, 0x00,		/* CE_REG_FR_TIMAG03__A */
	0x52, 0x00,		/* CE_REG_FR_TREAL04__A */
	0x00, 0x00,		/* CE_REG_FR_TIMAG04__A */
	0x52, 0x00,		/* CE_REG_FR_TREAL05__A */
	0x00, 0x00,		/* CE_REG_FR_TIMAG05__A */
	0x52, 0x00,		/* CE_REG_FR_TREAL06__A */
	0x00, 0x00,		/* CE_REG_FR_TIMAG06__A */
	0x52, 0x00,		/* CE_REG_FR_TREAL07__A */
	0x00, 0x00,		/* CE_REG_FR_TIMAG07__A */
	0x52, 0x00,		/* CE_REG_FR_TREAL08__A */
	0x00, 0x00,		/* CE_REG_FR_TIMAG08__A */
	0x52, 0x00,		/* CE_REG_FR_TREAL09__A */
	0x00, 0x00,		/* CE_REG_FR_TIMAG09__A */
	0x52, 0x00,		/* CE_REG_FR_TREAL10__A */
	0x00, 0x00,		/* CE_REG_FR_TIMAG10__A */
	0x52, 0x00,		/* CE_REG_FR_TREAL11__A */
	0x00, 0x00,		/* CE_REG_FR_TIMAG11__A */

	0x52, 0x00,		/* CE_REG_FR_MID_TAP__A */

	0x0B, 0x00,		/* CE_REG_FR_SQS_G00__A */
	0x0B, 0x00,		/* CE_REG_FR_SQS_G01__A */
	0x0B, 0x00,		/* CE_REG_FR_SQS_G02__A */
	0x0B, 0x00,		/* CE_REG_FR_SQS_G03__A */
	0x0B, 0x00,		/* CE_REG_FR_SQS_G04__A */
	0x0B, 0x00,		/* CE_REG_FR_SQS_G05__A */
	0x0B, 0x00,		/* CE_REG_FR_SQS_G06__A */
	0x0B, 0x00,		/* CE_REG_FR_SQS_G07__A */
	0x0B, 0x00,		/* CE_REG_FR_SQS_G08__A */
	0x0B, 0x00,		/* CE_REG_FR_SQS_G09__A */
	0x0B, 0x00,		/* CE_REG_FR_SQS_G10__A */
	0x0B, 0x00,		/* CE_REG_FR_SQS_G11__A */
	0x0B, 0x00,		/* CE_REG_FR_SQS_G12__A */

	0xFF, 0x01,		/* CE_REG_FR_RIO_G00__A */
	0x90, 0x01,		/* CE_REG_FR_RIO_G01__A */
	0x0B, 0x01,		/* CE_REG_FR_RIO_G02__A */
	0xC8, 0x00,		/* CE_REG_FR_RIO_G03__A */
	0xA0, 0x00,		/* CE_REG_FR_RIO_G04__A */
	0x85, 0x00,		/* CE_REG_FR_RIO_G05__A */
	0x72, 0x00,		/* CE_REG_FR_RIO_G06__A */
	0x64, 0x00,		/* CE_REG_FR_RIO_G07__A */
	0x59, 0x00,		/* CE_REG_FR_RIO_G08__A */
	0x50, 0x00,		/* CE_REG_FR_RIO_G09__A */
	0x49, 0x00,		/* CE_REG_FR_RIO_G10__A */

	0x10, 0x00,		/* CE_REG_FR_MODE__A     */
	0x78, 0x00,		/* CE_REG_FR_SQS_TRH__A  */
	0x00, 0x00,		/* CE_REG_FR_RIO_GAIN__A */
	0x00, 0x02,		/* CE_REG_FR_BYPASS__A   */
	0x0D, 0x00,		/* CE_REG_FR_PM_SET__A   */
	0x07, 0x00,		/* CE_REG_FR_ERR_SH__A   */
	0x04, 0x00,		/* CE_REG_FR_MAN_SH__A   */
	0x06, 0x00,		/* CE_REG_FR_TAP_SH__A   */

	END_OF_TABLE
};

u8 DRXD_InitFEA2_1[] = {
	WRBLOCK(FE_AD_REG_PD__A, 3),
	0x00, 0x00,		/* FE_AD_REG_PD__A          */
	0x01, 0x00,		/* FE_AD_REG_INVEXT__A      */
	0x00, 0x00,		/* FE_AD_REG_CLKNEG__A      */

	WRBLOCK(FE_AG_REG_DCE_AUR_CNT__A, 2),
	0x10, 0x00,		/* FE_AG_REG_DCE_AUR_CNT__A */
	0x10, 0x00,		/* FE_AG_REG_DCE_RUR_CNT__A */

	WRBLOCK(FE_AG_REG_ACE_AUR_CNT__A, 2),
	0x0E, 0x00,		/* FE_AG_REG_ACE_AUR_CNT__A */
	0x00, 0x00,		/* FE_AG_REG_ACE_RUR_CNT__A */

	WRBLOCK(FE_AG_REG_EGC_FLA_RGN__A, 5),
	0x04, 0x00,		/* FE_AG_REG_EGC_FLA_RGN__A */
	0x1F, 0x00,		/* FE_AG_REG_EGC_SLO_RGN__A */
	0x00, 0x00,		/* FE_AG_REG_EGC_JMP_PSN__A */
	0x00, 0x00,		/* FE_AG_REG_EGC_FLA_INC__A */
	0x00, 0x00,		/* FE_AG_REG_EGC_FLA_DEC__A */

	WRBLOCK(FE_AG_REG_GC1_AGC_MAX__A, 2),
	0xFF, 0x01,		/* FE_AG_REG_GC1_AGC_MAX__A */
	0x00, 0xFE,		/* FE_AG_REG_GC1_AGC_MIN__A */

	WRBLOCK(FE_AG_REG_IND_WIN__A, 29),
	0x00, 0x00,		/* FE_AG_REG_IND_WIN__A     */
	0x05, 0x00,		/* FE_AG_REG_IND_THD_LOL__A */
	0x0F, 0x00,		/* FE_AG_REG_IND_THD_HIL__A */
	0x00, 0x00,		/* FE_AG_REG_IND_DEL__A     don't care */
	0x1E, 0x00,		/* FE_AG_REG_IND_PD1_WRI__A */
	0x0C, 0x00,		/* FE_AG_REG_PDA_AUR_CNT__A */
	0x00, 0x00,		/* FE_AG_REG_PDA_RUR_CNT__A */
	0x00, 0x00,		/* FE_AG_REG_PDA_AVE_DAT__A don't care  */
	0x00, 0x00,		/* FE_AG_REG_PDC_RUR_CNT__A */
	0x01, 0x00,		/* FE_AG_REG_PDC_SET_LVL__A */
	0x02, 0x00,		/* FE_AG_REG_PDC_FLA_RGN__A */
	0x00, 0x00,		/* FE_AG_REG_PDC_JMP_PSN__A don't care  */
	0xFF, 0xFF,		/* FE_AG_REG_PDC_FLA_STP__A */
	0xFF, 0xFF,		/* FE_AG_REG_PDC_SLO_STP__A */
	0x00, 0x1F,		/* FE_AG_REG_PDC_PD2_WRI__A don't care  */
	0x00, 0x00,		/* FE_AG_REG_PDC_MAP_DAT__A don't care  */
	0x02, 0x00,		/* FE_AG_REG_PDC_MAX__A     */
	0x0C, 0x00,		/* FE_AG_REG_TGA_AUR_CNT__A */
	0x00, 0x00,		/* FE_AG_REG_TGA_RUR_CNT__A */
	0x00, 0x00,		/* FE_AG_REG_TGA_AVE_DAT__A don't care  */
	0x00, 0x00,		/* FE_AG_REG_TGC_RUR_CNT__A */
	0x22, 0x00,		/* FE_AG_REG_TGC_SET_LVL__A */
	0x15, 0x00,		/* FE_AG_REG_TGC_FLA_RGN__A */
	0x00, 0x00,		/* FE_AG_REG_TGC_JMP_PSN__A don't care  */
	0x01, 0x00,		/* FE_AG_REG_TGC_FLA_STP__A */
	0x0A, 0x00,		/* FE_AG_REG_TGC_SLO_STP__A */
	0x00, 0x00,		/* FE_AG_REG_TGC_MAP_DAT__A don't care  */
	0x10, 0x00,		/* FE_AG_REG_FGA_AUR_CNT__A */
	0x10, 0x00,		/* FE_AG_REG_FGA_RUR_CNT__A */

	WRBLOCK(FE_AG_REG_BGC_FGC_WRI__A, 2),
	0x00, 0x00,		/* FE_AG_REG_BGC_FGC_WRI__A */
	0x00, 0x00,		/* FE_AG_REG_BGC_CGC_WRI__A */

	WRBLOCK(FE_FD_REG_SCL__A, 3),
	0x05, 0x00,		/* FE_FD_REG_SCL__A         */
	0x03, 0x00,		/* FE_FD_REG_MAX_LEV__A     */
	0x05, 0x00,		/* FE_FD_REG_NR__A          */

	WRBLOCK(FE_CF_REG_SCL__A, 5),
	0x16, 0x00,		/* FE_CF_REG_SCL__A         */
	0x04, 0x00,		/* FE_CF_REG_MAX_LEV__A     */
	0x06, 0x00,		/* FE_CF_REG_NR__A          */
	0x00, 0x00,		/* FE_CF_REG_IMP_VAL__A     */
	0x01, 0x00,		/* FE_CF_REG_MEAS_VAL__A    */

	WRBLOCK(FE_CU_REG_FRM_CNT_RST__A, 2),
	0x00, 0x08,		/* FE_CU_REG_FRM_CNT_RST__A */
	0x00, 0x00,		/* FE_CU_REG_FRM_CNT_STR__A */

	END_OF_TABLE
};

   /* with PGA */
/*   WR16COND( DRXD_WITH_PGA, FE_AG_REG_AG_PGA_MODE__A   , 0x0004), */
   /* without PGA */
/*   WR16COND( DRXD_WITHOUT_PGA, FE_AG_REG_AG_PGA_MODE__A   , 0x0001), */
/*   WR16(FE_AG_REG_AG_AGC_SIO__A,  (extAttr -> FeAgRegAgAgcSio), 0x0000 );*/
/*   WR16(FE_AG_REG_AG_PWD__A        ,(extAttr -> FeAgRegAgPwd), 0x0000 );*/

u8 DRXD_InitFEA2_2[] = {
	WR16(FE_AG_REG_CDR_RUR_CNT__A, 0x0010),
	WR16(FE_AG_REG_FGM_WRI__A, 48),
	/* Activate measurement, activate scale */
	WR16(FE_FD_REG_MEAS_VAL__A, 0x0001),

	WR16(FE_CU_REG_COMM_EXEC__A, 0x0001),
	WR16(FE_CF_REG_COMM_EXEC__A, 0x0001),
	WR16(FE_IF_REG_COMM_EXEC__A, 0x0001),
	WR16(FE_FD_REG_COMM_EXEC__A, 0x0001),
	WR16(FE_FS_REG_COMM_EXEC__A, 0x0001),
	WR16(FE_AD_REG_COMM_EXEC__A, 0x0001),
	WR16(FE_AG_REG_COMM_EXEC__A, 0x0001),
	WR16(FE_AG_REG_AG_MODE_LOP__A, 0x895E),

	END_OF_TABLE
};

u8 DRXD_InitFEB1_1[] = {
	WR16(B_FE_AD_REG_PD__A, 0x0000),
	WR16(B_FE_AD_REG_CLKNEG__A, 0x0000),
	WR16(B_FE_AG_REG_BGC_FGC_WRI__A, 0x0000),
	WR16(B_FE_AG_REG_BGC_CGC_WRI__A, 0x0000),
	WR16(B_FE_AG_REG_AG_MODE_LOP__A, 0x000a),
	WR16(B_FE_AG_REG_IND_PD1_WRI__A, 35),
	WR16(B_FE_AG_REG_IND_WIN__A, 0),
	WR16(B_FE_AG_REG_IND_THD_LOL__A, 8),
	WR16(B_FE_AG_REG_IND_THD_HIL__A, 8),
	WR16(B_FE_CF_REG_IMP_VAL__A, 1),
	WR16(B_FE_AG_REG_EGC_FLA_RGN__A, 7),
	END_OF_TABLE
};

	/* with PGA */
/*      WR16(B_FE_AG_REG_AG_PGA_MODE__A   , 0x0000, 0x0000); */
       /* without PGA */
/*      WR16(B_FE_AG_REG_AG_PGA_MODE__A   ,
	     B_FE_AG_REG_AG_PGA_MODE_PFN_PCN_AFY_REN, 0x0000);*/
									     /*   WR16(B_FE_AG_REG_AG_AGC_SIO__A,(extAttr -> FeAgRegAgAgcSio), 0x0000 );*//*added HS 23-05-2005 */
/*   WR16(B_FE_AG_REG_AG_PWD__A    ,(extAttr -> FeAgRegAgPwd), 0x0000 );*/

u8 DRXD_InitFEB1_2[] = {
	WR16(B_FE_COMM_EXEC__A, 0x0001),

	/* RF-AGC setup */
	WR16(B_FE_AG_REG_PDA_AUR_CNT__A, 0x0C),
	WR16(B_FE_AG_REG_PDC_SET_LVL__A, 0x01),
	WR16(B_FE_AG_REG_PDC_FLA_RGN__A, 0x02),
	WR16(B_FE_AG_REG_PDC_FLA_STP__A, 0xFFFF),
	WR16(B_FE_AG_REG_PDC_SLO_STP__A, 0xFFFF),
	WR16(B_FE_AG_REG_PDC_MAX__A, 0x02),
	WR16(B_FE_AG_REG_TGA_AUR_CNT__A, 0x0C),
	WR16(B_FE_AG_REG_TGC_SET_LVL__A, 0x22),
	WR16(B_FE_AG_REG_TGC_FLA_RGN__A, 0x15),
	WR16(B_FE_AG_REG_TGC_FLA_STP__A, 0x01),
	WR16(B_FE_AG_REG_TGC_SLO_STP__A, 0x0A),

	WR16(B_FE_CU_REG_DIV_NFC_CLP__A, 0),
	WR16(B_FE_CU_REG_CTR_NFC_OCR__A, 25000),
	WR16(B_FE_CU_REG_CTR_NFC_ICR__A, 1),
	END_OF_TABLE
};

u8 DRXD_InitCPA2[] = {
	WRBLOCK(CP_REG_BR_SPL_OFFSET__A, 2),
	0x07, 0x00,		/* CP_REG_BR_SPL_OFFSET__A  */
	0x0A, 0x00,		/* CP_REG_BR_STR_DEL__A     */

	WRBLOCK(CP_REG_RT_ANG_INC0__A, 4),
	0x00, 0x00,		/* CP_REG_RT_ANG_INC0__A    */
	0x00, 0x00,		/* CP_REG_RT_ANG_INC1__A    */
	0x03, 0x00,		/* CP_REG_RT_DETECT_ENA__A  */
	0x03, 0x00,		/* CP_REG_RT_DETECT_TRH__A  */

	WRBLOCK(CP_REG_AC_NEXP_OFFS__A, 5),
	0x32, 0x00,		/* CP_REG_AC_NEXP_OFFS__A   */
	0x62, 0x00,		/* CP_REG_AC_AVER_POW__A    */
	0x82, 0x00,		/* CP_REG_AC_MAX_POW__A     */
	0x26, 0x00,		/* CP_REG_AC_WEIGHT_MAN__A  */
	0x0F, 0x00,		/* CP_REG_AC_WEIGHT_EXP__A  */

	WRBLOCK(CP_REG_AC_AMP_MODE__A, 2),
	0x02, 0x00,		/* CP_REG_AC_AMP_MODE__A    */
	0x01, 0x00,		/* CP_REG_AC_AMP_FIX__A     */

	WR16(CP_REG_INTERVAL__A, 0x0005),
	WR16(CP_REG_RT_EXP_MARG__A, 0x0004),
	WR16(CP_REG_AC_ANG_MODE__A, 0x0003),

	WR16(CP_REG_COMM_EXEC__A, 0x0001),
	END_OF_TABLE
};

u8 DRXD_InitCPB1[] = {
	WR16(B_CP_REG_BR_SPL_OFFSET__A, 0x0008),
	WR16(B_CP_COMM_EXEC__A, 0x0001),
	END_OF_TABLE
};

u8 DRXD_InitCEA2[] = {
	WRBLOCK(CE_REG_AVG_POW__A, 4),
	0x62, 0x00,		/* CE_REG_AVG_POW__A        */
	0x78, 0x00,		/* CE_REG_MAX_POW__A        */
	0x62, 0x00,		/* CE_REG_ATT__A            */
	0x17, 0x00,		/* CE_REG_NRED__A           */

	WRBLOCK(CE_REG_NE_ERR_SELECT__A, 2),
	0x07, 0x00,		/* CE_REG_NE_ERR_SELECT__A  */
	0xEB, 0xFF,		/* CE_REG_NE_TD_CAL__A      */

	WRBLOCK(CE_REG_NE_MIXAVG__A, 2),
	0x06, 0x00,		/* CE_REG_NE_MIXAVG__A      */
	0x00, 0x00,		/* CE_REG_NE_NUPD_OFS__A    */

	WRBLOCK(CE_REG_PE_NEXP_OFFS__A, 2),
	0x00, 0x00,		/* CE_REG_PE_NEXP_OFFS__A   */
	0x00, 0x00,		/* CE_REG_PE_TIMESHIFT__A   */

	WRBLOCK(CE_REG_TP_A0_TAP_NEW__A, 3),
	0x00, 0x01,		/* CE_REG_TP_A0_TAP_NEW__A       */
	0x01, 0x00,		/* CE_REG_TP_A0_TAP_NEW_VALID__A */
	0x0E, 0x00,		/* CE_REG_TP_A0_MU_LMS_STEP__A   */

	WRBLOCK(CE_REG_TP_A1_TAP_NEW__A, 3),
	0x00, 0x00,		/* CE_REG_TP_A1_TAP_NEW__A        */
	0x01, 0x00,		/* CE_REG_TP_A1_TAP_NEW_VALID__A  */
	0x0A, 0x00,		/* CE_REG_TP_A1_MU_LMS_STEP__A    */

	WRBLOCK(CE_REG_FI_SHT_INCR__A, 2),
	0x12, 0x00,		/* CE_REG_FI_SHT_INCR__A          */
	0x0C, 0x00,		/* CE_REG_FI_EXP_NORM__A          */

	WRBLOCK(CE_REG_IR_INPUTSEL__A, 3),
	0x00, 0x00,		/* CE_REG_IR_INPUTSEL__A          */
	0x00, 0x00,		/* CE_REG_IR_STARTPOS__A          */
	0xFF, 0x00,		/* CE_REG_IR_NEXP_THRES__A        */

	WR16(CE_REG_TI_NEXP_OFFS__A, 0x0000),

	END_OF_TABLE
};

u8 DRXD_InitCEB1[] = {
	WR16(B_CE_REG_TI_PHN_ENABLE__A, 0x0001),
	WR16(B_CE_REG_FR_PM_SET__A, 0x000D),

	END_OF_TABLE
};

u8 DRXD_InitEQA2[] = {
	WRBLOCK(EQ_REG_OT_QNT_THRES0__A, 4),
	0x1E, 0x00,		/* EQ_REG_OT_QNT_THRES0__A        */
	0x1F, 0x00,		/* EQ_REG_OT_QNT_THRES1__A        */
	0x06, 0x00,		/* EQ_REG_OT_CSI_STEP__A          */
	0x02, 0x00,		/* EQ_REG_OT_CSI_OFFSET__A        */

	WR16(EQ_REG_TD_REQ_SMB_CNT__A, 0x0200),
	WR16(EQ_REG_IS_CLIP_EXP__A, 0x001F),
	WR16(EQ_REG_SN_OFFSET__A, (u16) (-7)),
	WR16(EQ_REG_RC_SEL_CAR__A, 0x0002),
	WR16(EQ_REG_COMM_EXEC__A, 0x0001),
	END_OF_TABLE
};

u8 DRXD_InitEQB1[] = {
	WR16(B_EQ_REG_COMM_EXEC__A, 0x0001),
	END_OF_TABLE
};

u8 DRXD_ResetECRAM[] = {
	/* Reset packet sync bytes in EC_VD ram */
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (0 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (1 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (2 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (3 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (4 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (5 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (6 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (7 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (8 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (9 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (10 * 17), 0x0000),

	/* Reset packet sync bytes in EC_RS ram */
	WR16(EC_RS_EC_RAM__A, 0x0000),
	WR16(EC_RS_EC_RAM__A + 204, 0x0000),
	END_OF_TABLE
};

u8 DRXD_InitECA2[] = {
	WRBLOCK(EC_SB_REG_CSI_HI__A, 6),
	0x1F, 0x00,		/* EC_SB_REG_CSI_HI__A            */
	0x1E, 0x00,		/* EC_SB_REG_CSI_LO__A            */
	0x01, 0x00,		/* EC_SB_REG_SMB_TGL__A           */
	0x7F, 0x00,		/* EC_SB_REG_SNR_HI__A            */
	0x7F, 0x00,		/* EC_SB_REG_SNR_MID__A           */
	0x7F, 0x00,		/* EC_SB_REG_SNR_LO__A            */

	WRBLOCK(EC_RS_REG_REQ_PCK_CNT__A, 2),
	0x00, 0x10,		/* EC_RS_REG_REQ_PCK_CNT__A       */
	DATA16(EC_RS_REG_VAL_PCK),	/* EC_RS_REG_VAL__A               */

	WRBLOCK(EC_OC_REG_TMD_TOP_MODE__A, 5),
	0x03, 0x00,		/* EC_OC_REG_TMD_TOP_MODE__A      */
	0xF4, 0x01,		/* EC_OC_REG_TMD_TOP_CNT__A       */
	0xC0, 0x03,		/* EC_OC_REG_TMD_HIL_MAR__A       */
	0x40, 0x00,		/* EC_OC_REG_TMD_LOL_MAR__A       */
	0x03, 0x00,		/* EC_OC_REG_TMD_CUR_CNT__A       */

	WRBLOCK(EC_OC_REG_AVR_ASH_CNT__A, 2),
	0x06, 0x00,		/* EC_OC_REG_AVR_ASH_CNT__A       */
	0x02, 0x00,		/* EC_OC_REG_AVR_BSH_CNT__A       */

	WRBLOCK(EC_OC_REG_RCN_MODE__A, 7),
	0x07, 0x00,		/* EC_OC_REG_RCN_MODE__A          */
	0x00, 0x00,		/* EC_OC_REG_RCN_CRA_LOP__A       */
	0xc0, 0x00,		/* EC_OC_REG_RCN_CRA_HIP__A       */
	0x00, 0x10,		/* EC_OC_REG_RCN_CST_LOP__A       */
	0x00, 0x00,		/* EC_OC_REG_RCN_CST_HIP__A       */
	0xFF, 0x01,		/* EC_OC_REG_RCN_SET_LVL__A       */
	0x0D, 0x00,		/* EC_OC_REG_RCN_GAI_LVL__A       */

	WRBLOCK(EC_OC_REG_RCN_CLP_LOP__A, 2),
	0x00, 0x00,		/* EC_OC_REG_RCN_CLP_LOP__A       */
	0xC0, 0x00,		/* EC_OC_REG_RCN_CLP_HIP__A       */

	WR16(EC_SB_REG_CSI_OFS__A, 0x0001),
	WR16(EC_VD_REG_FORCE__A, 0x0002),
	WR16(EC_VD_REG_REQ_SMB_CNT__A, 0x0001),
	WR16(EC_VD_REG_RLK_ENA__A, 0x0001),
	WR16(EC_OD_REG_SYNC__A, 0x0664),
	WR16(EC_OC_REG_OC_MON_SIO__A, 0x0000),
	WR16(EC_OC_REG_SNC_ISC_LVL__A, 0x0D0C),
	/* Output zero on monitorbus pads, power saving */
	WR16(EC_OC_REG_OCR_MON_UOS__A,
	     (EC_OC_REG_OCR_MON_UOS_DAT_0_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_1_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_2_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_3_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_4_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_5_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_6_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_7_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_8_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_9_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_VAL_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_CLK_ENABLE)),
	WR16(EC_OC_REG_OCR_MON_WRI__A,
	     EC_OC_REG_OCR_MON_WRI_INIT),

/*   CHK_ERROR(ResetECRAM(demod)); */
	/* Reset packet sync bytes in EC_VD ram */
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (0 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (1 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (2 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (3 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (4 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (5 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (6 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (7 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (8 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (9 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (10 * 17), 0x0000),

	/* Reset packet sync bytes in EC_RS ram */
	WR16(EC_RS_EC_RAM__A, 0x0000),
	WR16(EC_RS_EC_RAM__A + 204, 0x0000),

	WR16(EC_SB_REG_COMM_EXEC__A, 0x0001),
	WR16(EC_VD_REG_COMM_EXEC__A, 0x0001),
	WR16(EC_OD_REG_COMM_EXEC__A, 0x0001),
	WR16(EC_RS_REG_COMM_EXEC__A, 0x0001),
	END_OF_TABLE
};

u8 DRXD_InitECB1[] = {
	WR16(B_EC_SB_REG_CSI_OFS0__A, 0x0001),
	WR16(B_EC_SB_REG_CSI_OFS1__A, 0x0001),
	WR16(B_EC_SB_REG_CSI_OFS2__A, 0x0001),
	WR16(B_EC_SB_REG_CSI_LO__A, 0x000c),
	WR16(B_EC_SB_REG_CSI_HI__A, 0x0018),
	WR16(B_EC_SB_REG_SNR_HI__A, 0x007f),
	WR16(B_EC_SB_REG_SNR_MID__A, 0x007f),
	WR16(B_EC_SB_REG_SNR_LO__A, 0x007f),

	WR16(B_EC_OC_REG_DTO_CLKMODE__A, 0x0002),
	WR16(B_EC_OC_REG_DTO_PER__A, 0x0006),
	WR16(B_EC_OC_REG_DTO_BUR__A, 0x0001),
	WR16(B_EC_OC_REG_RCR_CLKMODE__A, 0x0000),
	WR16(B_EC_OC_REG_RCN_GAI_LVL__A, 0x000D),
	WR16(B_EC_OC_REG_OC_MPG_SIO__A, 0x0000),

	/* Needed because shadow registers do not have correct default value */
	WR16(B_EC_OC_REG_RCN_CST_LOP__A, 0x1000),
	WR16(B_EC_OC_REG_RCN_CST_HIP__A, 0x0000),
	WR16(B_EC_OC_REG_RCN_CRA_LOP__A, 0x0000),
	WR16(B_EC_OC_REG_RCN_CRA_HIP__A, 0x00C0),
	WR16(B_EC_OC_REG_RCN_CLP_LOP__A, 0x0000),
	WR16(B_EC_OC_REG_RCN_CLP_HIP__A, 0x00C0),
	WR16(B_EC_OC_REG_DTO_INC_LOP__A, 0x0000),
	WR16(B_EC_OC_REG_DTO_INC_HIP__A, 0x00C0),

	WR16(B_EC_OD_REG_SYNC__A, 0x0664),
	WR16(B_EC_RS_REG_REQ_PCK_CNT__A, 0x1000),

/*   CHK_ERROR(ResetECRAM(demod)); */
	/* Reset packet sync bytes in EC_VD ram */
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (0 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (1 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (2 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (3 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (4 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (5 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (6 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (7 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (8 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (9 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (10 * 17), 0x0000),

	/* Reset packet sync bytes in EC_RS ram */
	WR16(EC_RS_EC_RAM__A, 0x0000),
	WR16(EC_RS_EC_RAM__A + 204, 0x0000),

	WR16(B_EC_SB_REG_COMM_EXEC__A, 0x0001),
	WR16(B_EC_VD_REG_COMM_EXEC__A, 0x0001),
	WR16(B_EC_OD_REG_COMM_EXEC__A, 0x0001),
	WR16(B_EC_RS_REG_COMM_EXEC__A, 0x0001),
	END_OF_TABLE
};

u8 DRXD_ResetECA2[] = {

	WR16(EC_OC_REG_COMM_EXEC__A, 0x0000),
	WR16(EC_OD_REG_COMM_EXEC__A, 0x0000),

	WRBLOCK(EC_OC_REG_TMD_TOP_MODE__A, 5),
	0x03, 0x00,		/* EC_OC_REG_TMD_TOP_MODE__A      */
	0xF4, 0x01,		/* EC_OC_REG_TMD_TOP_CNT__A       */
	0xC0, 0x03,		/* EC_OC_REG_TMD_HIL_MAR__A       */
	0x40, 0x00,		/* EC_OC_REG_TMD_LOL_MAR__A       */
	0x03, 0x00,		/* EC_OC_REG_TMD_CUR_CNT__A       */

	WRBLOCK(EC_OC_REG_AVR_ASH_CNT__A, 2),
	0x06, 0x00,		/* EC_OC_REG_AVR_ASH_CNT__A       */
	0x02, 0x00,		/* EC_OC_REG_AVR_BSH_CNT__A       */

	WRBLOCK(EC_OC_REG_RCN_MODE__A, 7),
	0x07, 0x00,		/* EC_OC_REG_RCN_MODE__A          */
	0x00, 0x00,		/* EC_OC_REG_RCN_CRA_LOP__A       */
	0xc0, 0x00,		/* EC_OC_REG_RCN_CRA_HIP__A       */
	0x00, 0x10,		/* EC_OC_REG_RCN_CST_LOP__A       */
	0x00, 0x00,		/* EC_OC_REG_RCN_CST_HIP__A       */
	0xFF, 0x01,		/* EC_OC_REG_RCN_SET_LVL__A       */
	0x0D, 0x00,		/* EC_OC_REG_RCN_GAI_LVL__A       */

	WRBLOCK(EC_OC_REG_RCN_CLP_LOP__A, 2),
	0x00, 0x00,		/* EC_OC_REG_RCN_CLP_LOP__A       */
	0xC0, 0x00,		/* EC_OC_REG_RCN_CLP_HIP__A       */

	WR16(EC_OD_REG_SYNC__A, 0x0664),
	WR16(EC_OC_REG_OC_MON_SIO__A, 0x0000),
	WR16(EC_OC_REG_SNC_ISC_LVL__A, 0x0D0C),
	/* Output zero on monitorbus pads, power saving */
	WR16(EC_OC_REG_OCR_MON_UOS__A,
	     (EC_OC_REG_OCR_MON_UOS_DAT_0_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_1_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_2_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_3_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_4_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_5_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_6_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_7_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_8_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_DAT_9_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_VAL_ENABLE |
	      EC_OC_REG_OCR_MON_UOS_CLK_ENABLE)),
	WR16(EC_OC_REG_OCR_MON_WRI__A,
	     EC_OC_REG_OCR_MON_WRI_INIT),

/*   CHK_ERROR(ResetECRAM(demod)); */
	/* Reset packet sync bytes in EC_VD ram */
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (0 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (1 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (2 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (3 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (4 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (5 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (6 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (7 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (8 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (9 * 17), 0x0000),
	WR16(EC_OD_DEINT_RAM__A + 0x3b7 + (10 * 17), 0x0000),

	/* Reset packet sync bytes in EC_RS ram */
	WR16(EC_RS_EC_RAM__A, 0x0000),
	WR16(EC_RS_EC_RAM__A + 204, 0x0000),

	WR16(EC_OD_REG_COMM_EXEC__A, 0x0001),
	END_OF_TABLE
};

u8 DRXD_InitSC[] = {
	WR16(SC_COMM_EXEC__A, 0),
	WR16(SC_COMM_STATE__A, 0),

#ifdef COMPILE_FOR_QT
	WR16(SC_RA_RAM_BE_OPT_DELAY__A, 0x100),
#endif

	/* SC is not started, this is done in SetChannels() */
	END_OF_TABLE
};

/* Diversity settings */

u8 DRXD_InitDiversityFront[] = {
	/* Start demod ********* RF in , diversity out **************************** */
	WR16(B_SC_RA_RAM_CONFIG__A, B_SC_RA_RAM_CONFIG_FR_ENABLE__M |
	     B_SC_RA_RAM_CONFIG_FREQSCAN__M),

	WR16(B_SC_RA_RAM_LC_ABS_2K__A, 0x7),
	WR16(B_SC_RA_RAM_LC_ABS_8K__A, 0x7),
	WR16(B_SC_RA_RAM_IR_COARSE_8K_LENGTH__A, IRLEN_COARSE_8K),
	WR16(B_SC_RA_RAM_IR_COARSE_8K_FREQINC__A, 1 << (11 - IRLEN_COARSE_8K)),
	WR16(B_SC_RA_RAM_IR_COARSE_8K_KAISINC__A, 1 << (17 - IRLEN_COARSE_8K)),
	WR16(B_SC_RA_RAM_IR_FINE_8K_LENGTH__A, IRLEN_FINE_8K),
	WR16(B_SC_RA_RAM_IR_FINE_8K_FREQINC__A, 1 << (11 - IRLEN_FINE_8K)),
	WR16(B_SC_RA_RAM_IR_FINE_8K_KAISINC__A, 1 << (17 - IRLEN_FINE_8K)),

	WR16(B_SC_RA_RAM_IR_COARSE_2K_LENGTH__A, IRLEN_COARSE_2K),
	WR16(B_SC_RA_RAM_IR_COARSE_2K_FREQINC__A, 1 << (11 - IRLEN_COARSE_2K)),
	WR16(B_SC_RA_RAM_IR_COARSE_2K_KAISINC__A, 1 << (17 - IRLEN_COARSE_2K)),
	WR16(B_SC_RA_RAM_IR_FINE_2K_LENGTH__A, IRLEN_FINE_2K),
	WR16(B_SC_RA_RAM_IR_FINE_2K_FREQINC__A, 1 << (11 - IRLEN_FINE_2K)),
	WR16(B_SC_RA_RAM_IR_FINE_2K_KAISINC__A, 1 << (17 - IRLEN_FINE_2K)),

	WR16(B_LC_RA_RAM_FILTER_CRMM_A__A, 7),
	WR16(B_LC_RA_RAM_FILTER_CRMM_B__A, 4),
	WR16(B_LC_RA_RAM_FILTER_SRMM_A__A, 7),
	WR16(B_LC_RA_RAM_FILTER_SRMM_B__A, 4),
	WR16(B_LC_RA_RAM_FILTER_SYM_SET__A, 500),

	WR16(B_CC_REG_DIVERSITY__A, 0x0001),
	WR16(B_EC_OC_REG_OC_MODE_HIP__A, 0x0010),
	WR16(B_EQ_REG_RC_SEL_CAR__A, B_EQ_REG_RC_SEL_CAR_PASS_B_CE |
	     B_EQ_REG_RC_SEL_CAR_LOCAL_B_CE | B_EQ_REG_RC_SEL_CAR_MEAS_B_CE),

	/*    0x2a ), *//* CE to PASS mux */

	END_OF_TABLE
};

u8 DRXD_InitDiversityEnd[] = {
	/* End demod *********** combining RF in and diversity in, MPEG TS out **** */
	/* disable near/far; switch on timing slave mode */
	WR16(B_SC_RA_RAM_CONFIG__A, B_SC_RA_RAM_CONFIG_FR_ENABLE__M |
	     B_SC_RA_RAM_CONFIG_FREQSCAN__M |
	     B_SC_RA_RAM_CONFIG_DIV_ECHO_ENABLE__M |
	     B_SC_RA_RAM_CONFIG_SLAVE__M |
	     B_SC_RA_RAM_CONFIG_DIV_BLANK_ENABLE__M
/* MV from CtrlDiversity */
	    ),
#ifdef DRXDDIV_SRMM_SLAVING
	WR16(SC_RA_RAM_LC_ABS_2K__A, 0x3c7),
	WR16(SC_RA_RAM_LC_ABS_8K__A, 0x3c7),
#else
	WR16(SC_RA_RAM_LC_ABS_2K__A, 0x7),
	WR16(SC_RA_RAM_LC_ABS_8K__A, 0x7),
#endif

	WR16(B_SC_RA_RAM_IR_COARSE_8K_LENGTH__A, IRLEN_COARSE_8K),
	WR16(B_SC_RA_RAM_IR_COARSE_8K_FREQINC__A, 1 << (11 - IRLEN_COARSE_8K)),
	WR16(B_SC_RA_RAM_IR_COARSE_8K_KAISINC__A, 1 << (17 - IRLEN_COARSE_8K)),
	WR16(B_SC_RA_RAM_IR_FINE_8K_LENGTH__A, IRLEN_FINE_8K),
	WR16(B_SC_RA_RAM_IR_FINE_8K_FREQINC__A, 1 << (11 - IRLEN_FINE_8K)),
	WR16(B_SC_RA_RAM_IR_FINE_8K_KAISINC__A, 1 << (17 - IRLEN_FINE_8K)),

	WR16(B_SC_RA_RAM_IR_COARSE_2K_LENGTH__A, IRLEN_COARSE_2K),
	WR16(B_SC_RA_RAM_IR_COARSE_2K_FREQINC__A, 1 << (11 - IRLEN_COARSE_2K)),
	WR16(B_SC_RA_RAM_IR_COARSE_2K_KAISINC__A, 1 << (17 - IRLEN_COARSE_2K)),
	WR16(B_SC_RA_RAM_IR_FINE_2K_LENGTH__A, IRLEN_FINE_2K),
	WR16(B_SC_RA_RAM_IR_FINE_2K_FREQINC__A, 1 << (11 - IRLEN_FINE_2K)),
	WR16(B_SC_RA_RAM_IR_FINE_2K_KAISINC__A, 1 << (17 - IRLEN_FINE_2K)),

	WR16(B_LC_RA_RAM_FILTER_CRMM_A__A, 7),
	WR16(B_LC_RA_RAM_FILTER_CRMM_B__A, 4),
	WR16(B_LC_RA_RAM_FILTER_SRMM_A__A, 7),
	WR16(B_LC_RA_RAM_FILTER_SRMM_B__A, 4),
	WR16(B_LC_RA_RAM_FILTER_SYM_SET__A, 500),

	WR16(B_CC_REG_DIVERSITY__A, 0x0001),
	END_OF_TABLE
};

u8 DRXD_DisableDiversity[] = {
	WR16(B_SC_RA_RAM_LC_ABS_2K__A, B_SC_RA_RAM_LC_ABS_2K__PRE),
	WR16(B_SC_RA_RAM_LC_ABS_8K__A, B_SC_RA_RAM_LC_ABS_8K__PRE),
	WR16(B_SC_RA_RAM_IR_COARSE_8K_LENGTH__A,
	     B_SC_RA_RAM_IR_COARSE_8K_LENGTH__PRE),
	WR16(B_SC_RA_RAM_IR_COARSE_8K_FREQINC__A,
	     B_SC_RA_RAM_IR_COARSE_8K_FREQINC__PRE),
	WR16(B_SC_RA_RAM_IR_COARSE_8K_KAISINC__A,
	     B_SC_RA_RAM_IR_COARSE_8K_KAISINC__PRE),
	WR16(B_SC_RA_RAM_IR_FINE_8K_LENGTH__A,
	     B_SC_RA_RAM_IR_FINE_8K_LENGTH__PRE),
	WR16(B_SC_RA_RAM_IR_FINE_8K_FREQINC__A,
	     B_SC_RA_RAM_IR_FINE_8K_FREQINC__PRE),
	WR16(B_SC_RA_RAM_IR_FINE_8K_KAISINC__A,
	     B_SC_RA_RAM_IR_FINE_8K_KAISINC__PRE),

	WR16(B_SC_RA_RAM_IR_COARSE_2K_LENGTH__A,
	     B_SC_RA_RAM_IR_COARSE_2K_LENGTH__PRE),
	WR16(B_SC_RA_RAM_IR_COARSE_2K_FREQINC__A,
	     B_SC_RA_RAM_IR_COARSE_2K_FREQINC__PRE),
	WR16(B_SC_RA_RAM_IR_COARSE_2K_KAISINC__A,
	     B_SC_RA_RAM_IR_COARSE_2K_KAISINC__PRE),
	WR16(B_SC_RA_RAM_IR_FINE_2K_LENGTH__A,
	     B_SC_RA_RAM_IR_FINE_2K_LENGTH__PRE),
	WR16(B_SC_RA_RAM_IR_FINE_2K_FREQINC__A,
	     B_SC_RA_RAM_IR_FINE_2K_FREQINC__PRE),
	WR16(B_SC_RA_RAM_IR_FINE_2K_KAISINC__A,
	     B_SC_RA_RAM_IR_FINE_2K_KAISINC__PRE),

	WR16(B_LC_RA_RAM_FILTER_CRMM_A__A, B_LC_RA_RAM_FILTER_CRMM_A__PRE),
	WR16(B_LC_RA_RAM_FILTER_CRMM_B__A, B_LC_RA_RAM_FILTER_CRMM_B__PRE),
	WR16(B_LC_RA_RAM_FILTER_SRMM_A__A, B_LC_RA_RAM_FILTER_SRMM_A__PRE),
	WR16(B_LC_RA_RAM_FILTER_SRMM_B__A, B_LC_RA_RAM_FILTER_SRMM_B__PRE),
	WR16(B_LC_RA_RAM_FILTER_SYM_SET__A, B_LC_RA_RAM_FILTER_SYM_SET__PRE),

	WR16(B_CC_REG_DIVERSITY__A, 0x0000),
	WR16(B_EQ_REG_RC_SEL_CAR__A, B_EQ_REG_RC_SEL_CAR_INIT),	/* combining disabled */

	END_OF_TABLE
};

u8 DRXD_StartDiversityFront[] = {
	/* Start demod, RF in and diversity out, no combining */
	WR16(B_FE_CF_REG_IMP_VAL__A, 0x0),
	WR16(B_FE_AD_REG_FDB_IN__A, 0x0),
	WR16(B_FE_AD_REG_INVEXT__A, 0x0),
	WR16(B_EQ_REG_COMM_MB__A, 0x12),	/* EQ to MB out */
	WR16(B_EQ_REG_RC_SEL_CAR__A, B_EQ_REG_RC_SEL_CAR_PASS_B_CE |	/* CE to PASS mux */
	     B_EQ_REG_RC_SEL_CAR_LOCAL_B_CE | B_EQ_REG_RC_SEL_CAR_MEAS_B_CE),

	WR16(SC_RA_RAM_ECHO_SHIFT_LIM__A, 2),

	END_OF_TABLE
};

u8 DRXD_StartDiversityEnd[] = {
	/* End demod, combining RF in and diversity in, MPEG TS out */
	WR16(B_FE_CF_REG_IMP_VAL__A, 0x0),	/* disable impulse noise cruncher */
	WR16(B_FE_AD_REG_INVEXT__A, 0x0),	/* clock inversion (for sohard board) */
	WR16(B_CP_REG_BR_STR_DEL__A, 10),	/* apperently no mb delay matching is best */

	WR16(B_EQ_REG_RC_SEL_CAR__A, B_EQ_REG_RC_SEL_CAR_DIV_ON |	/* org = 0x81 combining enabled */
	     B_EQ_REG_RC_SEL_CAR_MEAS_A_CC |
	     B_EQ_REG_RC_SEL_CAR_PASS_A_CC | B_EQ_REG_RC_SEL_CAR_LOCAL_A_CC),

	END_OF_TABLE
};

u8 DRXD_DiversityDelay8MHZ[] = {
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_2K_32__A, 1150 - 50),
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_2K_16__A, 1100 - 50),
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_2K_8__A, 1000 - 50),
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_2K_4__A, 800 - 50),
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_8K_32__A, 5420 - 50),
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_8K_16__A, 5200 - 50),
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_8K_8__A, 4800 - 50),
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_8K_4__A, 4000 - 50),
	END_OF_TABLE
};

u8 DRXD_DiversityDelay6MHZ[] =	/* also used ok for 7 MHz */
{
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_2K_32__A, 1100 - 50),
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_2K_16__A, 1000 - 50),
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_2K_8__A, 900 - 50),
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_2K_4__A, 600 - 50),
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_8K_32__A, 5300 - 50),
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_8K_16__A, 5000 - 50),
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_8K_8__A, 4500 - 50),
	WR16(B_SC_RA_RAM_DIVERSITY_DELAY_8K_4__A, 3500 - 50),
	END_OF_TABLE
};
