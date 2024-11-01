// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include "../include/HalPwrSeqCmd.h"

#define PWR_CMD_WRITE			0x01
     /*  offset: the read register offset */
     /*  msk: the mask of the write bits */
     /*  value: write value */
     /*  note: driver shall implement this cmd by read & msk after write */

#define PWR_CMD_POLLING			0x02
     /*  offset: the read register offset */
     /*  msk: the mask of the polled value */
     /*  value: the value to be polled, masked by the msd field. */
     /*  note: driver shall implement this cmd by */
     /*  do{ */
     /*  if ( (Read(offset) & msk) == (value & msk) ) */
     /*  break; */
     /*  } while (not timeout); */

#define PWR_CMD_DELAY			0x03
     /*  offset: the value to delay (in us) */
     /*  msk: N/A */
     /*  value: N/A */

struct wl_pwr_cfg {
	u16 offset;
	u8 cmd:4;
	u8 msk;
	u8 value;
};

#define GET_PWR_CFG_OFFSET(__PWR_CMD)		__PWR_CMD.offset
#define GET_PWR_CFG_CMD(__PWR_CMD)		__PWR_CMD.cmd
#define GET_PWR_CFG_MASK(__PWR_CMD)		__PWR_CMD.msk
#define GET_PWR_CFG_VALUE(__PWR_CMD)		__PWR_CMD.value

static struct wl_pwr_cfg rtl8188E_power_on_flow[] = {
	{ 0x0006, PWR_CMD_POLLING, BIT(1), BIT(1) },
	{ 0x0002, PWR_CMD_WRITE, BIT(0) | BIT(1), 0 }, /* reset BB */
	{ 0x0026, PWR_CMD_WRITE, BIT(7), BIT(7) }, /* schmitt trigger */
	{ 0x0005, PWR_CMD_WRITE, BIT(7), 0 }, /* disable HWPDN (control by DRV)*/
	{ 0x0005, PWR_CMD_WRITE, BIT(4) | BIT(3), 0 }, /* disable WL suspend*/
	{ 0x0005, PWR_CMD_WRITE, BIT(0), BIT(0) },
	{ 0x0005, PWR_CMD_POLLING, BIT(0), 0 },
	{ 0x0023, PWR_CMD_WRITE, BIT(4), 0 },
};

static struct wl_pwr_cfg rtl8188E_card_disable_flow[] = {
	{ 0x001F, PWR_CMD_WRITE, 0xFF, 0 }, /* turn off RF */
	{ 0x0023, PWR_CMD_WRITE, BIT(4), BIT(4) }, /* LDO Sleep mode */
	{ 0x0005, PWR_CMD_WRITE, BIT(1), BIT(1) }, /* turn off MAC by HW state machine */
	{ 0x0005, PWR_CMD_POLLING, BIT(1), 0 },
	{ 0x0026, PWR_CMD_WRITE, BIT(7), BIT(7) }, /* schmitt trigger */
	{ 0x0005, PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3) }, /* enable WL suspend */
	{ 0x0007, PWR_CMD_WRITE, 0xFF, 0 }, /* enable bandgap mbias in suspend */
	{ 0x0041, PWR_CMD_WRITE, BIT(4), 0 }, /* Clear SIC_EN register */
	{ 0xfe10, PWR_CMD_WRITE, BIT(4), BIT(4) }, /* Set USB suspend enable local register */
};

/* This is used by driver for LPSRadioOff Procedure, not for FW LPS Step */
static struct wl_pwr_cfg rtl8188E_enter_lps_flow[] = {
	{ 0x0522, PWR_CMD_WRITE, 0xFF, 0x7F },/* Tx Pause */
	{ 0x05F8, PWR_CMD_POLLING, 0xFF, 0 }, /* Should be zero if no packet is transmitted */
	{ 0x05F9, PWR_CMD_POLLING, 0xFF, 0 }, /* Should be zero if no packet is transmitted */
	{ 0x05FA, PWR_CMD_POLLING, 0xFF, 0 }, /* Should be zero if no packet is transmitted */
	{ 0x05FB, PWR_CMD_POLLING, 0xFF, 0 }, /* Should be zero if no packet is transmitted */
	{ 0x0002, PWR_CMD_WRITE, BIT(0), 0 }, /* CCK and OFDM are disabled, clocks are gated */
	{ 0x0002, PWR_CMD_DELAY, 0, 0 },
	{ 0x0100, PWR_CMD_WRITE, 0xFF, 0x3F }, /* Reset MAC TRX */
	{ 0x0101, PWR_CMD_WRITE, BIT(1), 0 }, /* check if removed later */
	{ 0x0553, PWR_CMD_WRITE, BIT(5), BIT(5) }, /* Respond TxOK to scheduler */
};

u8 HalPwrSeqCmdParsing(struct adapter *padapter, enum r8188eu_pwr_seq seq)
{
	struct wl_pwr_cfg pwrcfgcmd = {0};
	struct wl_pwr_cfg *pwrseqcmd;
	u8 poll_bit = false;
	u8 idx, num_steps;
	u8 value = 0;
	u32 offset = 0;
	u32 poll_count = 0; /*  polling autoload done. */
	u32 max_poll_count = 5000;
	int res;

	switch (seq) {
	case PWR_ON_FLOW:
		pwrseqcmd = rtl8188E_power_on_flow;
		num_steps = ARRAY_SIZE(rtl8188E_power_on_flow);
		break;
	case DISABLE_FLOW:
		pwrseqcmd = rtl8188E_card_disable_flow;
		num_steps = ARRAY_SIZE(rtl8188E_card_disable_flow);
		break;
	case LPS_ENTER_FLOW:
		pwrseqcmd = rtl8188E_enter_lps_flow;
		num_steps = ARRAY_SIZE(rtl8188E_enter_lps_flow);
		break;
	default:
		return false;
	}

	for (idx = 0; idx < num_steps; idx++) {
		pwrcfgcmd = pwrseqcmd[idx];

		switch (GET_PWR_CFG_CMD(pwrcfgcmd)) {
		case PWR_CMD_WRITE:
			offset = GET_PWR_CFG_OFFSET(pwrcfgcmd);

			/*  Read the value from system register */
			res = rtw_read8(padapter, offset, &value);
			if (res)
				return false;

			value &= ~(GET_PWR_CFG_MASK(pwrcfgcmd));
			value |= (GET_PWR_CFG_VALUE(pwrcfgcmd) & GET_PWR_CFG_MASK(pwrcfgcmd));

			/*  Write the value back to system register */
			rtw_write8(padapter, offset, value);
			break;
		case PWR_CMD_POLLING:
			poll_bit = false;
			offset = GET_PWR_CFG_OFFSET(pwrcfgcmd);
			do {
				res = rtw_read8(padapter, offset, &value);
				if (res)
					return false;

				value &= GET_PWR_CFG_MASK(pwrcfgcmd);
				if (value == (GET_PWR_CFG_VALUE(pwrcfgcmd) & GET_PWR_CFG_MASK(pwrcfgcmd)))
					poll_bit = true;
				else
					udelay(10);

				if (poll_count++ > max_poll_count)
					return false;
			} while (!poll_bit);
			break;
		case PWR_CMD_DELAY:
			udelay(GET_PWR_CFG_OFFSET(pwrcfgcmd));
			break;
		default:
			break;
		}
	}
	return true;
}
