/* driver/input/touchscreen/s5pc210_ts.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *	http://www.samsung.com
 *
 * S5PC210 10.1" Touchscreen driver information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef	_S5PV310_TS_H_
#define	_S5PV310_TS_H_

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <mach/board_rev.h>

#define S5PV310_TS_DEVICE_NAME	"s5pc210_ts"

#define	TOUCH_PRESS             1
#define	TOUCH_RELEASE           0

/* Touch Configuration */

/* Touch Interrupt define */
#ifdef CONFIG_MACH_SMDK4X12

#define TS_ATTB			samsung_board_rev_is_0_0() ? EXYNOS4_GPX1(6) : EXYNOS4212_GPM3(4)
#define S5PV310_TS_IRQ		gpio_to_irq(TS_ATTB)

/* Touch should be reset before using. In order to reset it, the reset pin
   should be set OUTPUT HIGH. The Reset pin is EXYNOS4_GPX1(5) (XEINT 13).
   However, the SMDK4X12 uses this pin for resetting both LCD and touchscreen.
   Therefore, it assumes that LCD driver will reset them by this pin. */

#elif defined (CONFIG_MACH_SMDKV310)

#define S5PV310_TS_IRQ		gpio_to_irq(EXYNOS4_GPX3(5))
#define TS_ATTB			(EXYNOS4_GPX3(5))

#else
#error Unsupported board!
#endif

#define	TS_ABS_MIN_X            0
#define	TS_ABS_MIN_Y            0
#define	TS_ABS_MAX_X		1366
#define	TS_ABS_MAX_Y		768

#define	TS_X_THRESHOLD		1
#define	TS_Y_THRESHOLD		1


/* touch register */
#define	MODULE_CALIBRATION	0x37
#define	MODULE_POWERMODE	0x14
#define	MODULE_INTMODE		0x15
#define	MODULE_INTWIDTH		0x16

#define	PERIOD_10MS		(HZ/100) /* 10ms */
#define	PERIOD_20MS		(HZ/50)	 /* 20ms */
#define	PERIOD_50MS		(HZ/20)	 /* 50ms */

#define	TOUCH_STATE_BOOT	0
#define	TOUCH_STATE_RESUME	1

/* Touch hold event */
#define	SW_TOUCH_HOLD		0x09

#if defined(CONFIG_TOUCHSCREEN_EXYNOS4)
/* multi-touch data process struct */
struct touch_process_data_t {
	unsigned char finger_cnt;
	unsigned int x1;
	unsigned int y1;
	unsigned int x2;
	unsigned int y2;
};
#endif

struct s5pv310_ts_t {
	struct input_dev *driver;

	/* seqlock_t */
	seqlock_t		lock;
	unsigned int		seq;

	/* timer */
	struct  timer_list	penup_timer;

	/* data store */
	unsigned int		status;
	unsigned int		x;
	unsigned int		y;
	unsigned char		rd[10];

	/* sysfs used */
	unsigned char		hold_status;
	unsigned char		sampling_rate;

	/* x data threshold (0-10) : default 3 */
	unsigned char		threshold_x;
	/* y data threshold (0-10) : default 3 */
	unsigned char		threshold_y;
	/* touch sensitivity (0-255) : default 0x14 */
	unsigned char		sensitivity;

#if defined CONFIG_TOUCHSCREEN_EXYNOS4
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct	early_suspend		power;
#endif
#endif
};

extern struct s5pv310_ts_t s5pv310_ts;
#endif
