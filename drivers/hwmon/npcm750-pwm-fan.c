// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2014-2018 Nuvoton Technology corporation.

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/thermal.h>

/* NPCM7XX PWM registers */
#define NPCM7XX_PWM_REG_BASE(base, n)    ((base) + ((n) * 0x1000L))

#define NPCM7XX_PWM_REG_PR(base, n)	(NPCM7XX_PWM_REG_BASE(base, n) + 0x00)
#define NPCM7XX_PWM_REG_CSR(base, n)	(NPCM7XX_PWM_REG_BASE(base, n) + 0x04)
#define NPCM7XX_PWM_REG_CR(base, n)	(NPCM7XX_PWM_REG_BASE(base, n) + 0x08)
#define NPCM7XX_PWM_REG_CNRx(base, n, ch) \
			(NPCM7XX_PWM_REG_BASE(base, n) + 0x0C + (12 * (ch)))
#define NPCM7XX_PWM_REG_CMRx(base, n, ch) \
			(NPCM7XX_PWM_REG_BASE(base, n) + 0x10 + (12 * (ch)))
#define NPCM7XX_PWM_REG_PDRx(base, n, ch) \
			(NPCM7XX_PWM_REG_BASE(base, n) + 0x14 + (12 * (ch)))
#define NPCM7XX_PWM_REG_PIER(base, n)	(NPCM7XX_PWM_REG_BASE(base, n) + 0x3C)
#define NPCM7XX_PWM_REG_PIIR(base, n)	(NPCM7XX_PWM_REG_BASE(base, n) + 0x40)

#define NPCM7XX_PWM_CTRL_CH0_MODE_BIT		BIT(3)
#define NPCM7XX_PWM_CTRL_CH1_MODE_BIT		BIT(11)
#define NPCM7XX_PWM_CTRL_CH2_MODE_BIT		BIT(15)
#define NPCM7XX_PWM_CTRL_CH3_MODE_BIT		BIT(19)

#define NPCM7XX_PWM_CTRL_CH0_INV_BIT		BIT(2)
#define NPCM7XX_PWM_CTRL_CH1_INV_BIT		BIT(10)
#define NPCM7XX_PWM_CTRL_CH2_INV_BIT		BIT(14)
#define NPCM7XX_PWM_CTRL_CH3_INV_BIT		BIT(18)

#define NPCM7XX_PWM_CTRL_CH0_EN_BIT		BIT(0)
#define NPCM7XX_PWM_CTRL_CH1_EN_BIT		BIT(8)
#define NPCM7XX_PWM_CTRL_CH2_EN_BIT		BIT(12)
#define NPCM7XX_PWM_CTRL_CH3_EN_BIT		BIT(16)

/* Define the maximum PWM channel number */
#define NPCM7XX_PWM_MAX_CHN_NUM			8
#define NPCM7XX_PWM_MAX_CHN_NUM_IN_A_MODULE	4
#define NPCM7XX_PWM_MAX_MODULES                 2

/* Define the Counter Register, value = 100 for match 100% */
#define NPCM7XX_PWM_COUNTER_DEFAULT_NUM		255
#define NPCM7XX_PWM_CMR_DEFAULT_NUM		255
#define NPCM7XX_PWM_CMR_MAX			255

/* default all PWM channels PRESCALE2 = 1 */
#define NPCM7XX_PWM_PRESCALE2_DEFAULT_CH0	0x4
#define NPCM7XX_PWM_PRESCALE2_DEFAULT_CH1	0x40
#define NPCM7XX_PWM_PRESCALE2_DEFAULT_CH2	0x400
#define NPCM7XX_PWM_PRESCALE2_DEFAULT_CH3	0x4000

#define PWM_OUTPUT_FREQ_25KHZ			25000
#define PWN_CNT_DEFAULT				256
#define MIN_PRESCALE1				2
#define NPCM7XX_PWM_PRESCALE_SHIFT_CH01		8

#define NPCM7XX_PWM_PRESCALE2_DEFAULT	(NPCM7XX_PWM_PRESCALE2_DEFAULT_CH0 | \
					NPCM7XX_PWM_PRESCALE2_DEFAULT_CH1 | \
					NPCM7XX_PWM_PRESCALE2_DEFAULT_CH2 | \
					NPCM7XX_PWM_PRESCALE2_DEFAULT_CH3)

#define NPCM7XX_PWM_CTRL_MODE_DEFAULT	(NPCM7XX_PWM_CTRL_CH0_MODE_BIT | \
					NPCM7XX_PWM_CTRL_CH1_MODE_BIT | \
					NPCM7XX_PWM_CTRL_CH2_MODE_BIT | \
					NPCM7XX_PWM_CTRL_CH3_MODE_BIT)

/* NPCM7XX FAN Tacho registers */
#define NPCM7XX_FAN_REG_BASE(base, n)	((base) + ((n) * 0x1000L))

#define NPCM7XX_FAN_REG_TCNT1(base, n)    (NPCM7XX_FAN_REG_BASE(base, n) + 0x00)
#define NPCM7XX_FAN_REG_TCRA(base, n)     (NPCM7XX_FAN_REG_BASE(base, n) + 0x02)
#define NPCM7XX_FAN_REG_TCRB(base, n)     (NPCM7XX_FAN_REG_BASE(base, n) + 0x04)
#define NPCM7XX_FAN_REG_TCNT2(base, n)    (NPCM7XX_FAN_REG_BASE(base, n) + 0x06)
#define NPCM7XX_FAN_REG_TPRSC(base, n)    (NPCM7XX_FAN_REG_BASE(base, n) + 0x08)
#define NPCM7XX_FAN_REG_TCKC(base, n)     (NPCM7XX_FAN_REG_BASE(base, n) + 0x0A)
#define NPCM7XX_FAN_REG_TMCTRL(base, n)   (NPCM7XX_FAN_REG_BASE(base, n) + 0x0C)
#define NPCM7XX_FAN_REG_TICTRL(base, n)   (NPCM7XX_FAN_REG_BASE(base, n) + 0x0E)
#define NPCM7XX_FAN_REG_TICLR(base, n)    (NPCM7XX_FAN_REG_BASE(base, n) + 0x10)
#define NPCM7XX_FAN_REG_TIEN(base, n)     (NPCM7XX_FAN_REG_BASE(base, n) + 0x12)
#define NPCM7XX_FAN_REG_TCPA(base, n)     (NPCM7XX_FAN_REG_BASE(base, n) + 0x14)
#define NPCM7XX_FAN_REG_TCPB(base, n)     (NPCM7XX_FAN_REG_BASE(base, n) + 0x16)
#define NPCM7XX_FAN_REG_TCPCFG(base, n)   (NPCM7XX_FAN_REG_BASE(base, n) + 0x18)
#define NPCM7XX_FAN_REG_TINASEL(base, n)  (NPCM7XX_FAN_REG_BASE(base, n) + 0x1A)
#define NPCM7XX_FAN_REG_TINBSEL(base, n)  (NPCM7XX_FAN_REG_BASE(base, n) + 0x1C)

#define NPCM7XX_FAN_TCKC_CLKX_NONE	0
#define NPCM7XX_FAN_TCKC_CLK1_APB	BIT(0)
#define NPCM7XX_FAN_TCKC_CLK2_APB	BIT(3)

#define NPCM7XX_FAN_TMCTRL_TBEN		BIT(6)
#define NPCM7XX_FAN_TMCTRL_TAEN		BIT(5)
#define NPCM7XX_FAN_TMCTRL_TBEDG	BIT(4)
#define NPCM7XX_FAN_TMCTRL_TAEDG	BIT(3)
#define NPCM7XX_FAN_TMCTRL_MODE_5	BIT(2)

#define NPCM7XX_FAN_TICLR_CLEAR_ALL	GENMASK(5, 0)
#define NPCM7XX_FAN_TICLR_TFCLR		BIT(5)
#define NPCM7XX_FAN_TICLR_TECLR		BIT(4)
#define NPCM7XX_FAN_TICLR_TDCLR		BIT(3)
#define NPCM7XX_FAN_TICLR_TCCLR		BIT(2)
#define NPCM7XX_FAN_TICLR_TBCLR		BIT(1)
#define NPCM7XX_FAN_TICLR_TACLR		BIT(0)

#define NPCM7XX_FAN_TIEN_ENABLE_ALL	GENMASK(5, 0)
#define NPCM7XX_FAN_TIEN_TFIEN		BIT(5)
#define NPCM7XX_FAN_TIEN_TEIEN		BIT(4)
#define NPCM7XX_FAN_TIEN_TDIEN		BIT(3)
#define NPCM7XX_FAN_TIEN_TCIEN		BIT(2)
#define NPCM7XX_FAN_TIEN_TBIEN		BIT(1)
#define NPCM7XX_FAN_TIEN_TAIEN		BIT(0)

#define NPCM7XX_FAN_TICTRL_TFPND	BIT(5)
#define NPCM7XX_FAN_TICTRL_TEPND	BIT(4)
#define NPCM7XX_FAN_TICTRL_TDPND	BIT(3)
#define NPCM7XX_FAN_TICTRL_TCPND	BIT(2)
#define NPCM7XX_FAN_TICTRL_TBPND	BIT(1)
#define NPCM7XX_FAN_TICTRL_TAPND	BIT(0)

#define NPCM7XX_FAN_TCPCFG_HIBEN	BIT(7)
#define NPCM7XX_FAN_TCPCFG_EQBEN	BIT(6)
#define NPCM7XX_FAN_TCPCFG_LOBEN	BIT(5)
#define NPCM7XX_FAN_TCPCFG_CPBSEL	BIT(4)
#define NPCM7XX_FAN_TCPCFG_HIAEN	BIT(3)
#define NPCM7XX_FAN_TCPCFG_EQAEN	BIT(2)
#define NPCM7XX_FAN_TCPCFG_LOAEN	BIT(1)
#define NPCM7XX_FAN_TCPCFG_CPASEL	BIT(0)

/* FAN General Definition */
/* Define the maximum FAN channel number */
#define NPCM7XX_FAN_MAX_MODULE			8
#define NPCM7XX_FAN_MAX_CHN_NUM_IN_A_MODULE	2
#define NPCM7XX_FAN_MAX_CHN_NUM			16

/*
 * Get Fan Tach Timeout (base on clock 214843.75Hz, 1 cnt = 4.654us)
 * Timeout 94ms ~= 0x5000
 * (The minimum FAN speed could to support ~640RPM/pulse 1,
 * 320RPM/pulse 2, ...-- 10.6Hz)
 */
#define NPCM7XX_FAN_TIMEOUT	0x5000
#define NPCM7XX_FAN_TCNT	0xFFFF
#define NPCM7XX_FAN_TCPA	(NPCM7XX_FAN_TCNT - NPCM7XX_FAN_TIMEOUT)
#define NPCM7XX_FAN_TCPB	(NPCM7XX_FAN_TCNT - NPCM7XX_FAN_TIMEOUT)

#define NPCM7XX_FAN_POLL_TIMER_200MS			200
#define NPCM7XX_FAN_DEFAULT_PULSE_PER_REVOLUTION	2
#define NPCM7XX_FAN_TINASEL_FANIN_DEFAULT		0
#define NPCM7XX_FAN_CLK_PRESCALE			255

#define NPCM7XX_FAN_CMPA				0
#define NPCM7XX_FAN_CMPB				1

/* Obtain the fan number */
#define NPCM7XX_FAN_INPUT(fan, cmp)		(((fan) << 1) + (cmp))

/* fan sample status */
#define FAN_DISABLE				0xFF
#define FAN_INIT				0x00
#define FAN_PREPARE_TO_GET_FIRST_CAPTURE	0x01
#define FAN_ENOUGH_SAMPLE			0x02

struct npcm7xx_fan_dev {
	u8 fan_st_flg;
	u8 fan_pls_per_rev;
	u16 fan_cnt;
	u32 fan_cnt_tmp;
};

struct npcm7xx_cooling_device {
	char name[THERMAL_NAME_LENGTH];
	struct npcm7xx_pwm_fan_data *data;
	struct thermal_cooling_device *tcdev;
	int pwm_port;
	u8 *cooling_levels;
	u8 max_state;
	u8 cur_state;
};

struct npcm7xx_pwm_fan_data {
	void __iomem *pwm_base;
	void __iomem *fan_base;
	unsigned long pwm_clk_freq;
	unsigned long fan_clk_freq;
	struct clk *pwm_clk;
	struct clk *fan_clk;
	struct mutex pwm_lock[NPCM7XX_PWM_MAX_MODULES];
	spinlock_t fan_lock[NPCM7XX_FAN_MAX_MODULE];
	int fan_irq[NPCM7XX_FAN_MAX_MODULE];
	bool pwm_present[NPCM7XX_PWM_MAX_CHN_NUM];
	bool fan_present[NPCM7XX_FAN_MAX_CHN_NUM];
	u32 input_clk_freq;
	struct timer_list fan_timer;
	struct npcm7xx_fan_dev fan_dev[NPCM7XX_FAN_MAX_CHN_NUM];
	struct npcm7xx_cooling_device *cdev[NPCM7XX_PWM_MAX_CHN_NUM];
	u8 fan_select;
};

static int npcm7xx_pwm_config_set(struct npcm7xx_pwm_fan_data *data,
				  int channel, u16 val)
{
	u32 pwm_ch = (channel % NPCM7XX_PWM_MAX_CHN_NUM_IN_A_MODULE);
	u32 module = (channel / NPCM7XX_PWM_MAX_CHN_NUM_IN_A_MODULE);
	u32 tmp_buf, ctrl_en_bit, env_bit;

	/*
	 * Config PWM Comparator register for setting duty cycle
	 */
	mutex_lock(&data->pwm_lock[module]);

	/* write new CMR value  */
	iowrite32(val, NPCM7XX_PWM_REG_CMRx(data->pwm_base, module, pwm_ch));
	tmp_buf = ioread32(NPCM7XX_PWM_REG_CR(data->pwm_base, module));

	switch (pwm_ch) {
	case 0:
		ctrl_en_bit = NPCM7XX_PWM_CTRL_CH0_EN_BIT;
		env_bit = NPCM7XX_PWM_CTRL_CH0_INV_BIT;
		break;
	case 1:
		ctrl_en_bit = NPCM7XX_PWM_CTRL_CH1_EN_BIT;
		env_bit = NPCM7XX_PWM_CTRL_CH1_INV_BIT;
		break;
	case 2:
		ctrl_en_bit = NPCM7XX_PWM_CTRL_CH2_EN_BIT;
		env_bit = NPCM7XX_PWM_CTRL_CH2_INV_BIT;
		break;
	case 3:
		ctrl_en_bit = NPCM7XX_PWM_CTRL_CH3_EN_BIT;
		env_bit = NPCM7XX_PWM_CTRL_CH3_INV_BIT;
		break;
	default:
		mutex_unlock(&data->pwm_lock[module]);
		return -ENODEV;
	}

	if (val == 0) {
		/* Disable PWM */
		tmp_buf &= ~ctrl_en_bit;
		tmp_buf |= env_bit;
	} else {
		/* Enable PWM */
		tmp_buf |= ctrl_en_bit;
		tmp_buf &= ~env_bit;
	}

	iowrite32(tmp_buf, NPCM7XX_PWM_REG_CR(data->pwm_base, module));
	mutex_unlock(&data->pwm_lock[module]);

	return 0;
}

static inline void npcm7xx_fan_start_capture(struct npcm7xx_pwm_fan_data *data,
					     u8 fan, u8 cmp)
{
	u8 fan_id;
	u8 reg_mode;
	u8 reg_int;
	unsigned long flags;

	fan_id = NPCM7XX_FAN_INPUT(fan, cmp);

	/* to check whether any fan tach is enable */
	if (data->fan_dev[fan_id].fan_st_flg != FAN_DISABLE) {
		/* reset status */
		spin_lock_irqsave(&data->fan_lock[fan], flags);

		data->fan_dev[fan_id].fan_st_flg = FAN_INIT;
		reg_int = ioread8(NPCM7XX_FAN_REG_TIEN(data->fan_base, fan));

		/*
		 * the interrupt enable bits do not need to be cleared before
		 * it sets, the interrupt enable bits are cleared only on reset.
		 * the clock unit control register is behaving in the same
		 * manner that the interrupt enable register behave.
		 */
		if (cmp == NPCM7XX_FAN_CMPA) {
			/* enable interrupt */
			iowrite8(reg_int | (NPCM7XX_FAN_TIEN_TAIEN |
					    NPCM7XX_FAN_TIEN_TEIEN),
				 NPCM7XX_FAN_REG_TIEN(data->fan_base, fan));

			reg_mode = NPCM7XX_FAN_TCKC_CLK1_APB
				| ioread8(NPCM7XX_FAN_REG_TCKC(data->fan_base,
							       fan));

			/* start to Capture */
			iowrite8(reg_mode, NPCM7XX_FAN_REG_TCKC(data->fan_base,
								fan));
		} else {
			/* enable interrupt */
			iowrite8(reg_int | (NPCM7XX_FAN_TIEN_TBIEN |
					    NPCM7XX_FAN_TIEN_TFIEN),
				 NPCM7XX_FAN_REG_TIEN(data->fan_base, fan));

			reg_mode =
				NPCM7XX_FAN_TCKC_CLK2_APB
				| ioread8(NPCM7XX_FAN_REG_TCKC(data->fan_base,
							       fan));

			/* start to Capture */
			iowrite8(reg_mode,
				 NPCM7XX_FAN_REG_TCKC(data->fan_base, fan));
		}

		spin_unlock_irqrestore(&data->fan_lock[fan], flags);
	}
}

/*
 * Enable a background timer to poll fan tach value, (200ms * 4)
 * to polling all fan
 */
static void npcm7xx_fan_polling(struct timer_list *t)
{
	struct npcm7xx_pwm_fan_data *data;
	int i;

	data = from_timer(data, t, fan_timer);

	/*
	 * Polling two module per one round,
	 * FAN01 & FAN89 / FAN23 & FAN1011 / FAN45 & FAN1213 / FAN67 & FAN1415
	 */
	for (i = data->fan_select; i < NPCM7XX_FAN_MAX_MODULE;
	      i = i + 4) {
		/* clear the flag and reset the counter (TCNT) */
		iowrite8(NPCM7XX_FAN_TICLR_CLEAR_ALL,
			 NPCM7XX_FAN_REG_TICLR(data->fan_base, i));

		if (data->fan_present[i * 2]) {
			iowrite16(NPCM7XX_FAN_TCNT,
				  NPCM7XX_FAN_REG_TCNT1(data->fan_base, i));
			npcm7xx_fan_start_capture(data, i, NPCM7XX_FAN_CMPA);
		}
		if (data->fan_present[(i * 2) + 1]) {
			iowrite16(NPCM7XX_FAN_TCNT,
				  NPCM7XX_FAN_REG_TCNT2(data->fan_base, i));
			npcm7xx_fan_start_capture(data, i, NPCM7XX_FAN_CMPB);
		}
	}

	data->fan_select++;
	data->fan_select &= 0x3;

	/* reset the timer interval */
	data->fan_timer.expires = jiffies +
		msecs_to_jiffies(NPCM7XX_FAN_POLL_TIMER_200MS);
	add_timer(&data->fan_timer);
}

static inline void npcm7xx_fan_compute(struct npcm7xx_pwm_fan_data *data,
				       u8 fan, u8 cmp, u8 fan_id, u8 flag_int,
				       u8 flag_mode, u8 flag_clear)
{
	u8  reg_int;
	u8  reg_mode;
	u16 fan_cap;

	if (cmp == NPCM7XX_FAN_CMPA)
		fan_cap = ioread16(NPCM7XX_FAN_REG_TCRA(data->fan_base, fan));
	else
		fan_cap = ioread16(NPCM7XX_FAN_REG_TCRB(data->fan_base, fan));

	/* clear capature flag, H/W will auto reset the NPCM7XX_FAN_TCNTx */
	iowrite8(flag_clear, NPCM7XX_FAN_REG_TICLR(data->fan_base, fan));

	if (data->fan_dev[fan_id].fan_st_flg == FAN_INIT) {
		/* First capture, drop it */
		data->fan_dev[fan_id].fan_st_flg =
			FAN_PREPARE_TO_GET_FIRST_CAPTURE;

		/* reset counter */
		data->fan_dev[fan_id].fan_cnt_tmp = 0;
	} else if (data->fan_dev[fan_id].fan_st_flg < FAN_ENOUGH_SAMPLE) {
		/*
		 * collect the enough sample,
		 * (ex: 2 pulse fan need to get 2 sample)
		 */
		data->fan_dev[fan_id].fan_cnt_tmp +=
			(NPCM7XX_FAN_TCNT - fan_cap);

		data->fan_dev[fan_id].fan_st_flg++;
	} else {
		/* get enough sample or fan disable */
		if (data->fan_dev[fan_id].fan_st_flg == FAN_ENOUGH_SAMPLE) {
			data->fan_dev[fan_id].fan_cnt_tmp +=
				(NPCM7XX_FAN_TCNT - fan_cap);

			/* compute finial average cnt per pulse */
			data->fan_dev[fan_id].fan_cnt =
				data->fan_dev[fan_id].fan_cnt_tmp /
				FAN_ENOUGH_SAMPLE;

			data->fan_dev[fan_id].fan_st_flg = FAN_INIT;
		}

		reg_int =  ioread8(NPCM7XX_FAN_REG_TIEN(data->fan_base, fan));

		/* disable interrupt */
		iowrite8((reg_int & ~flag_int),
			 NPCM7XX_FAN_REG_TIEN(data->fan_base, fan));
		reg_mode =  ioread8(NPCM7XX_FAN_REG_TCKC(data->fan_base, fan));

		/* stop capturing */
		iowrite8((reg_mode & ~flag_mode),
			 NPCM7XX_FAN_REG_TCKC(data->fan_base, fan));
	}
}

static inline void npcm7xx_check_cmp(struct npcm7xx_pwm_fan_data *data,
				     u8 fan, u8 cmp, u8 flag)
{
	u8 reg_int;
	u8 reg_mode;
	u8 flag_timeout;
	u8 flag_cap;
	u8 flag_clear;
	u8 flag_int;
	u8 flag_mode;
	u8 fan_id;

	fan_id = NPCM7XX_FAN_INPUT(fan, cmp);

	if (cmp == NPCM7XX_FAN_CMPA) {
		flag_cap = NPCM7XX_FAN_TICTRL_TAPND;
		flag_timeout = NPCM7XX_FAN_TICTRL_TEPND;
		flag_int = NPCM7XX_FAN_TIEN_TAIEN | NPCM7XX_FAN_TIEN_TEIEN;
		flag_mode = NPCM7XX_FAN_TCKC_CLK1_APB;
		flag_clear = NPCM7XX_FAN_TICLR_TACLR | NPCM7XX_FAN_TICLR_TECLR;
	} else {
		flag_cap = NPCM7XX_FAN_TICTRL_TBPND;
		flag_timeout = NPCM7XX_FAN_TICTRL_TFPND;
		flag_int = NPCM7XX_FAN_TIEN_TBIEN | NPCM7XX_FAN_TIEN_TFIEN;
		flag_mode = NPCM7XX_FAN_TCKC_CLK2_APB;
		flag_clear = NPCM7XX_FAN_TICLR_TBCLR | NPCM7XX_FAN_TICLR_TFCLR;
	}

	if (flag & flag_timeout) {
		reg_int =  ioread8(NPCM7XX_FAN_REG_TIEN(data->fan_base, fan));

		/* disable interrupt */
		iowrite8((reg_int & ~flag_int),
			 NPCM7XX_FAN_REG_TIEN(data->fan_base, fan));

		/* clear interrupt flag */
		iowrite8(flag_clear,
			 NPCM7XX_FAN_REG_TICLR(data->fan_base, fan));

		reg_mode =  ioread8(NPCM7XX_FAN_REG_TCKC(data->fan_base, fan));

		/* stop capturing */
		iowrite8((reg_mode & ~flag_mode),
			 NPCM7XX_FAN_REG_TCKC(data->fan_base, fan));

		/*
		 *  If timeout occurs (NPCM7XX_FAN_TIMEOUT), the fan doesn't
		 *  connect or speed is lower than 10.6Hz (320RPM/pulse2).
		 *  In these situation, the RPM output should be zero.
		 */
		data->fan_dev[fan_id].fan_cnt = 0;
	} else {
	    /* input capture is occurred */
		if (flag & flag_cap)
			npcm7xx_fan_compute(data, fan, cmp, fan_id, flag_int,
					    flag_mode, flag_clear);
	}
}

static irqreturn_t npcm7xx_fan_isr(int irq, void *dev_id)
{
	struct npcm7xx_pwm_fan_data *data = dev_id;
	unsigned long flags;
	int module;
	u8 flag;

	module = irq - data->fan_irq[0];
	spin_lock_irqsave(&data->fan_lock[module], flags);

	flag = ioread8(NPCM7XX_FAN_REG_TICTRL(data->fan_base, module));
	if (flag > 0) {
		npcm7xx_check_cmp(data, module, NPCM7XX_FAN_CMPA, flag);
		npcm7xx_check_cmp(data, module, NPCM7XX_FAN_CMPB, flag);
		spin_unlock_irqrestore(&data->fan_lock[module], flags);
		return IRQ_HANDLED;
	}

	spin_unlock_irqrestore(&data->fan_lock[module], flags);

	return IRQ_NONE;
}

static int npcm7xx_read_pwm(struct device *dev, u32 attr, int channel,
			    long *val)
{
	struct npcm7xx_pwm_fan_data *data = dev_get_drvdata(dev);
	u32 pmw_ch = (channel % NPCM7XX_PWM_MAX_CHN_NUM_IN_A_MODULE);
	u32 module = (channel / NPCM7XX_PWM_MAX_CHN_NUM_IN_A_MODULE);

	switch (attr) {
	case hwmon_pwm_input:
		*val = ioread32
			(NPCM7XX_PWM_REG_CMRx(data->pwm_base, module, pmw_ch));
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int npcm7xx_write_pwm(struct device *dev, u32 attr, int channel,
			     long val)
{
	struct npcm7xx_pwm_fan_data *data = dev_get_drvdata(dev);
	int err;

	switch (attr) {
	case hwmon_pwm_input:
		if (val < 0 || val > NPCM7XX_PWM_CMR_MAX)
			return -EINVAL;
		err = npcm7xx_pwm_config_set(data, channel, (u16)val);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static umode_t npcm7xx_pwm_is_visible(const void *_data, u32 attr, int channel)
{
	const struct npcm7xx_pwm_fan_data *data = _data;

	if (!data->pwm_present[channel])
		return 0;

	switch (attr) {
	case hwmon_pwm_input:
		return 0644;
	default:
		return 0;
	}
}

static int npcm7xx_read_fan(struct device *dev, u32 attr, int channel,
			    long *val)
{
	struct npcm7xx_pwm_fan_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_fan_input:
		*val = 0;
		if (data->fan_dev[channel].fan_cnt <= 0)
			return data->fan_dev[channel].fan_cnt;

		/* Convert the raw reading to RPM */
		if (data->fan_dev[channel].fan_cnt > 0 &&
		    data->fan_dev[channel].fan_pls_per_rev > 0)
			*val = ((data->input_clk_freq * 60) /
				(data->fan_dev[channel].fan_cnt *
				 data->fan_dev[channel].fan_pls_per_rev));
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t npcm7xx_fan_is_visible(const void *_data, u32 attr, int channel)
{
	const struct npcm7xx_pwm_fan_data *data = _data;

	if (!data->fan_present[channel])
		return 0;

	switch (attr) {
	case hwmon_fan_input:
		return 0444;
	default:
		return 0;
	}
}

static int npcm7xx_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_pwm:
		return npcm7xx_read_pwm(dev, attr, channel, val);
	case hwmon_fan:
		return npcm7xx_read_fan(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int npcm7xx_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_pwm:
		return npcm7xx_write_pwm(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t npcm7xx_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_pwm:
		return npcm7xx_pwm_is_visible(data, attr, channel);
	case hwmon_fan:
		return npcm7xx_fan_is_visible(data, attr, channel);
	default:
		return 0;
	}
}

static const struct hwmon_channel_info *npcm7xx_info[] = {
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT,
			   HWMON_F_INPUT),
	NULL
};

static const struct hwmon_ops npcm7xx_hwmon_ops = {
	.is_visible = npcm7xx_is_visible,
	.read = npcm7xx_read,
	.write = npcm7xx_write,
};

static const struct hwmon_chip_info npcm7xx_chip_info = {
	.ops = &npcm7xx_hwmon_ops,
	.info = npcm7xx_info,
};

static u32 npcm7xx_pwm_init(struct npcm7xx_pwm_fan_data *data)
{
	int m, ch;
	u32 prescale_val, output_freq;

	data->pwm_clk_freq = clk_get_rate(data->pwm_clk);

	/* Adjust NPCM7xx PWMs output frequency to ~25Khz */
	output_freq = data->pwm_clk_freq / PWN_CNT_DEFAULT;
	prescale_val = DIV_ROUND_CLOSEST(output_freq, PWM_OUTPUT_FREQ_25KHZ);

	/* If prescale_val = 0, then the prescale output clock is stopped */
	if (prescale_val < MIN_PRESCALE1)
		prescale_val = MIN_PRESCALE1;
	/*
	 * prescale_val need to decrement in one because in the PWM Prescale
	 * register the Prescale value increment by one
	 */
	prescale_val--;

	/* Setting PWM Prescale Register value register to both modules */
	prescale_val |= (prescale_val << NPCM7XX_PWM_PRESCALE_SHIFT_CH01);

	for (m = 0; m < NPCM7XX_PWM_MAX_MODULES  ; m++) {
		iowrite32(prescale_val, NPCM7XX_PWM_REG_PR(data->pwm_base, m));
		iowrite32(NPCM7XX_PWM_PRESCALE2_DEFAULT,
			  NPCM7XX_PWM_REG_CSR(data->pwm_base, m));
		iowrite32(NPCM7XX_PWM_CTRL_MODE_DEFAULT,
			  NPCM7XX_PWM_REG_CR(data->pwm_base, m));

		for (ch = 0; ch < NPCM7XX_PWM_MAX_CHN_NUM_IN_A_MODULE; ch++) {
			iowrite32(NPCM7XX_PWM_COUNTER_DEFAULT_NUM,
				  NPCM7XX_PWM_REG_CNRx(data->pwm_base, m, ch));
		}
	}

	return output_freq / ((prescale_val & 0xf) + 1);
}

static void npcm7xx_fan_init(struct npcm7xx_pwm_fan_data *data)
{
	int md;
	int ch;
	int i;
	u32 apb_clk_freq;

	for (md = 0; md < NPCM7XX_FAN_MAX_MODULE; md++) {
		/* stop FAN0~7 clock */
		iowrite8(NPCM7XX_FAN_TCKC_CLKX_NONE,
			 NPCM7XX_FAN_REG_TCKC(data->fan_base, md));

		/* disable all interrupt */
		iowrite8(0x00, NPCM7XX_FAN_REG_TIEN(data->fan_base, md));

		/* clear all interrupt */
		iowrite8(NPCM7XX_FAN_TICLR_CLEAR_ALL,
			 NPCM7XX_FAN_REG_TICLR(data->fan_base, md));

		/* set FAN0~7 clock prescaler */
		iowrite8(NPCM7XX_FAN_CLK_PRESCALE,
			 NPCM7XX_FAN_REG_TPRSC(data->fan_base, md));

		/* set FAN0~7 mode (high-to-low transition) */
		iowrite8((NPCM7XX_FAN_TMCTRL_MODE_5 | NPCM7XX_FAN_TMCTRL_TBEN |
			  NPCM7XX_FAN_TMCTRL_TAEN),
			 NPCM7XX_FAN_REG_TMCTRL(data->fan_base, md));

		/* set FAN0~7 Initial Count/Cap */
		iowrite16(NPCM7XX_FAN_TCNT,
			  NPCM7XX_FAN_REG_TCNT1(data->fan_base, md));
		iowrite16(NPCM7XX_FAN_TCNT,
			  NPCM7XX_FAN_REG_TCNT2(data->fan_base, md));

		/* set FAN0~7 compare (equal to count) */
		iowrite8((NPCM7XX_FAN_TCPCFG_EQAEN | NPCM7XX_FAN_TCPCFG_EQBEN),
			 NPCM7XX_FAN_REG_TCPCFG(data->fan_base, md));

		/* set FAN0~7 compare value */
		iowrite16(NPCM7XX_FAN_TCPA,
			  NPCM7XX_FAN_REG_TCPA(data->fan_base, md));
		iowrite16(NPCM7XX_FAN_TCPB,
			  NPCM7XX_FAN_REG_TCPB(data->fan_base, md));

		/* set FAN0~7 fan input FANIN 0~15 */
		iowrite8(NPCM7XX_FAN_TINASEL_FANIN_DEFAULT,
			 NPCM7XX_FAN_REG_TINASEL(data->fan_base, md));
		iowrite8(NPCM7XX_FAN_TINASEL_FANIN_DEFAULT,
			 NPCM7XX_FAN_REG_TINBSEL(data->fan_base, md));

		for (i = 0; i < NPCM7XX_FAN_MAX_CHN_NUM_IN_A_MODULE; i++) {
			ch = md * NPCM7XX_FAN_MAX_CHN_NUM_IN_A_MODULE + i;
			data->fan_dev[ch].fan_st_flg = FAN_DISABLE;
			data->fan_dev[ch].fan_pls_per_rev =
				NPCM7XX_FAN_DEFAULT_PULSE_PER_REVOLUTION;
			data->fan_dev[ch].fan_cnt = 0;
		}
	}

	apb_clk_freq = clk_get_rate(data->fan_clk);

	/* Fan tach input clock = APB clock / prescalar, default is 255. */
	data->input_clk_freq = apb_clk_freq / (NPCM7XX_FAN_CLK_PRESCALE + 1);
}

static int
npcm7xx_pwm_cz_get_max_state(struct thermal_cooling_device *tcdev,
			     unsigned long *state)
{
	struct npcm7xx_cooling_device *cdev = tcdev->devdata;

	*state = cdev->max_state;

	return 0;
}

static int
npcm7xx_pwm_cz_get_cur_state(struct thermal_cooling_device *tcdev,
			     unsigned long *state)
{
	struct npcm7xx_cooling_device *cdev = tcdev->devdata;

	*state = cdev->cur_state;

	return 0;
}

static int
npcm7xx_pwm_cz_set_cur_state(struct thermal_cooling_device *tcdev,
			     unsigned long state)
{
	struct npcm7xx_cooling_device *cdev = tcdev->devdata;
	int ret;

	if (state > cdev->max_state)
		return -EINVAL;

	cdev->cur_state = state;
	ret = npcm7xx_pwm_config_set(cdev->data, cdev->pwm_port,
				     cdev->cooling_levels[cdev->cur_state]);

	return ret;
}

static const struct thermal_cooling_device_ops npcm7xx_pwm_cool_ops = {
	.get_max_state = npcm7xx_pwm_cz_get_max_state,
	.get_cur_state = npcm7xx_pwm_cz_get_cur_state,
	.set_cur_state = npcm7xx_pwm_cz_set_cur_state,
};

static int npcm7xx_create_pwm_cooling(struct device *dev,
				      struct device_node *child,
				      struct npcm7xx_pwm_fan_data *data,
				      u32 pwm_port, u8 num_levels)
{
	int ret;
	struct npcm7xx_cooling_device *cdev;

	cdev = devm_kzalloc(dev, sizeof(*cdev), GFP_KERNEL);
	if (!cdev)
		return -ENOMEM;

	cdev->cooling_levels = devm_kzalloc(dev, num_levels, GFP_KERNEL);
	if (!cdev->cooling_levels)
		return -ENOMEM;

	cdev->max_state = num_levels - 1;
	ret = of_property_read_u8_array(child, "cooling-levels",
					cdev->cooling_levels,
					num_levels);
	if (ret) {
		dev_err(dev, "Property 'cooling-levels' cannot be read.\n");
		return ret;
	}
	snprintf(cdev->name, THERMAL_NAME_LENGTH, "%pOFn%d", child,
		 pwm_port);

	cdev->tcdev = devm_thermal_of_cooling_device_register(dev, child,
				cdev->name, cdev, &npcm7xx_pwm_cool_ops);
	if (IS_ERR(cdev->tcdev))
		return PTR_ERR(cdev->tcdev);

	cdev->data = data;
	cdev->pwm_port = pwm_port;

	data->cdev[pwm_port] = cdev;

	return 0;
}

static int npcm7xx_en_pwm_fan(struct device *dev,
			      struct device_node *child,
			      struct npcm7xx_pwm_fan_data *data)
{
	u8 *fan_ch;
	u32 pwm_port;
	int ret, fan_cnt;
	u8 index, ch;

	ret = of_property_read_u32(child, "reg", &pwm_port);
	if (ret)
		return ret;

	data->pwm_present[pwm_port] = true;
	ret = npcm7xx_pwm_config_set(data, pwm_port,
				     NPCM7XX_PWM_CMR_DEFAULT_NUM);

	ret = of_property_count_u8_elems(child, "cooling-levels");
	if (ret > 0) {
		ret = npcm7xx_create_pwm_cooling(dev, child, data, pwm_port,
						 ret);
		if (ret)
			return ret;
	}

	fan_cnt = of_property_count_u8_elems(child, "fan-tach-ch");
	if (fan_cnt < 1)
		return -EINVAL;

	fan_ch = devm_kcalloc(dev, fan_cnt, sizeof(*fan_ch), GFP_KERNEL);
	if (!fan_ch)
		return -ENOMEM;

	ret = of_property_read_u8_array(child, "fan-tach-ch", fan_ch, fan_cnt);
	if (ret)
		return ret;

	for (ch = 0; ch < fan_cnt; ch++) {
		index = fan_ch[ch];
		data->fan_present[index] = true;
		data->fan_dev[index].fan_st_flg = FAN_INIT;
	}

	return 0;
}

static int npcm7xx_pwm_fan_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np, *child;
	struct npcm7xx_pwm_fan_data *data;
	struct resource *res;
	struct device *hwmon;
	char name[20];
	int ret, cnt;
	u32 output_freq;
	u32 i;

	np = dev->of_node;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "pwm");
	if (!res) {
		dev_err(dev, "pwm resource not found\n");
		return -ENODEV;
	}

	data->pwm_base = devm_ioremap_resource(dev, res);
	dev_dbg(dev, "pwm base resource is %pR\n", res);
	if (IS_ERR(data->pwm_base))
		return PTR_ERR(data->pwm_base);

	data->pwm_clk = devm_clk_get(dev, "pwm");
	if (IS_ERR(data->pwm_clk)) {
		dev_err(dev, "couldn't get pwm clock\n");
		return PTR_ERR(data->pwm_clk);
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "fan");
	if (!res) {
		dev_err(dev, "fan resource not found\n");
		return -ENODEV;
	}

	data->fan_base = devm_ioremap_resource(dev, res);
	dev_dbg(dev, "fan base resource is %pR\n", res);
	if (IS_ERR(data->fan_base))
		return PTR_ERR(data->fan_base);

	data->fan_clk = devm_clk_get(dev, "fan");
	if (IS_ERR(data->fan_clk)) {
		dev_err(dev, "couldn't get fan clock\n");
		return PTR_ERR(data->fan_clk);
	}

	output_freq = npcm7xx_pwm_init(data);
	npcm7xx_fan_init(data);

	for (cnt = 0; cnt < NPCM7XX_PWM_MAX_MODULES  ; cnt++)
		mutex_init(&data->pwm_lock[cnt]);

	for (i = 0; i < NPCM7XX_FAN_MAX_MODULE; i++) {
		spin_lock_init(&data->fan_lock[i]);

		data->fan_irq[i] = platform_get_irq(pdev, i);
		if (data->fan_irq[i] < 0) {
			dev_err(dev, "get IRQ fan%d failed\n", i);
			return data->fan_irq[i];
		}

		sprintf(name, "NPCM7XX-FAN-MD%d", i);
		ret = devm_request_irq(dev, data->fan_irq[i], npcm7xx_fan_isr,
				       0, name, (void *)data);
		if (ret) {
			dev_err(dev, "register IRQ fan%d failed\n", i);
			return ret;
		}
	}

	for_each_child_of_node(np, child) {
		ret = npcm7xx_en_pwm_fan(dev, child, data);
		if (ret) {
			dev_err(dev, "enable pwm and fan failed\n");
			of_node_put(child);
			return ret;
		}
	}

	hwmon = devm_hwmon_device_register_with_info(dev, "npcm7xx_pwm_fan",
						     data, &npcm7xx_chip_info,
						     NULL);
	if (IS_ERR(hwmon)) {
		dev_err(dev, "unable to register hwmon device\n");
		return PTR_ERR(hwmon);
	}

	for (i = 0; i < NPCM7XX_FAN_MAX_CHN_NUM; i++) {
		if (data->fan_present[i]) {
			/* fan timer initialization */
			data->fan_timer.expires = jiffies +
				msecs_to_jiffies(NPCM7XX_FAN_POLL_TIMER_200MS);
			timer_setup(&data->fan_timer,
				    npcm7xx_fan_polling, 0);
			add_timer(&data->fan_timer);
			break;
		}
	}

	pr_info("NPCM7XX PWM-FAN Driver probed, output Freq %dHz[PWM], input Freq %dHz[FAN]\n",
		output_freq, data->input_clk_freq);

	return 0;
}

static const struct of_device_id of_pwm_fan_match_table[] = {
	{ .compatible = "nuvoton,npcm750-pwm-fan", },
	{},
};
MODULE_DEVICE_TABLE(of, of_pwm_fan_match_table);

static struct platform_driver npcm7xx_pwm_fan_driver = {
	.probe		= npcm7xx_pwm_fan_probe,
	.driver		= {
		.name	= "npcm7xx_pwm_fan",
		.of_match_table = of_pwm_fan_match_table,
	},
};

module_platform_driver(npcm7xx_pwm_fan_driver);

MODULE_DESCRIPTION("Nuvoton NPCM7XX PWM and Fan Tacho driver");
MODULE_AUTHOR("Tomer Maimon <tomer.maimon@nuvoton.com>");
MODULE_LICENSE("GPL v2");
