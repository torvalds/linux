/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_TSENS_MTC_H__
#define __QCOM_TSENS_MTC_H__

#define TSENS_NUM_MTC_ZONES_SUPPORT 3
#define TSENS_TM_MTC_ZONE0_SW_MASK_ADDR(n)	  ((n) + 0x140)
#define TSENS_TM_MTC_ZONE0_LOG(n)			   ((n) + 0x150)
#define TSENS_TM_MTC_ZONE0_HISTORY(n)		   ((n) + 0x160)
#define TSENS_TM_MTC_ZONE0_SW_MASK_ADDR_V14(n)  ((n) + 0xC0)
#define TSENS_TM_MTC_ZONE0_LOG_V14(n)		   ((n) + 0xD0)

#define TSENS_SN_ADDR_OFFSET			 0x4
#define TSENS_RESET_HISTORY_MASK		0x4
#define TSENS_ZONEMASK_PARAMS		   3
#define TSENS_MTC_ZONE_LOG_SIZE		 6
#define TSENS_MTC_ZONE_HISTORY_SIZE	 3

#define TSENS_TH1_MTC_IN_EFFECT			   BIT(0)
#define TSENS_TH2_MTC_IN_EFFECT			   BIT(1)
#define TSENS_MTC_IN_EFFECT					 0x3
#define TSENS_MTC_DISABLE					   0x0

#define TSENS_LOGS_VALID_MASK	  0x40000000
#define TSENS_LOGS_VALID_SHIFT	 30
#define TSENS_LOGS_LATEST_MASK	0x0000001f
#define TSENS_LOGS_LOG1_MASK	  0x000003e0
#define TSENS_LOGS_LOG2_MASK	  0x00007c00
#define TSENS_LOGS_LOG3_MASK	  0x000f8000
#define TSENS_LOGS_LOG4_MASK	  0x01f00000
#define TSENS_LOGS_LOG5_MASK	  0x3e000000
#define TSENS_LOGS_LOG1_SHIFT	 5
#define TSENS_LOGS_LOG2_SHIFT	 10
#define TSENS_LOGS_LOG3_SHIFT	 15
#define TSENS_LOGS_LOG4_SHIFT	 20
#define TSENS_LOGS_LOG5_SHIFT	 25

#define TSENS_PS_RED_CMD_MASK   0x3ff00000
#define TSENS_PS_YELLOW_CMD_MASK		0x000ffc00
#define TSENS_PS_COOL_CMD_MASK  0x000003ff
#define TSENS_PS_YELLOW_CMD_SHIFT	   0xa
#define TSENS_PS_RED_CMD_SHIFT  0x14

#define TSENS_RESET_HISTORY_SHIFT	   2

#define TSENS_ZONEMASK_PARAMS		   3
#define TSENS_MTC_ZONE_LOG_SIZE		 6
#define TSENS_MTC_ZONE_HISTORY_SIZE	 3

extern int tsens_get_mtc_zone_history(unsigned int zone, void *zone_hist);
extern struct tsens_device *tsens_controller_is_present(void);
extern int tsens_set_mtc_zone_sw_mask(unsigned int zone,
			unsigned int th1_enable, unsigned int th2_enable);
extern int tsens_get_mtc_zone_log(unsigned int zone, void *zone_log);

#endif /* __QCOM_TSENS_MTC_H__ */
