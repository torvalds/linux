/* SPDX-License-Identifier: GPL-2.0 */
/*++
Copyright (c) Realtek Semiconductor Corp. All rights reserved.

Module Name:
	rate_adaptive.c

Abstract:
	Implement rate Adaptive functions for common operations.

Major Change History:
	When       Who               What
	---------- ---------------   -------------------------------
	2011-08-12 Page            Create.
	2015-04-23 Wilson		   Fine tune (SD9 Family)

--*/
#include "mp_precomp.h"

#include "../phydm_precomp.h"


#if (RATE_ADAPTIVE_SUPPORT == 1)
/* rate adaptive parameters */


static u8 RETRY_PENALTY[PERENTRY][RETRYSIZE + 1] = {{5, 4, 3, 2, 0, 3}, /* 92 , idx=0 */
	{6, 5, 4, 3, 0, 4}, /* 86 , idx=1 */
	{6, 5, 4, 2, 0, 4}, /* 81 , idx=2 */
	{8, 7, 6, 4, 0, 6}, /* 75 , idx=3 */
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && \
	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
	{10, 9, 7, 6, 0, 8},/*71	, idx=4*/
	{10, 9, 7, 4, 0, 8},/*66	, idx=5*/
#else
	{10, 9, 8, 6, 0, 8}, /* 71	, idx=4 */
	{10, 9, 8, 4, 0, 8}, /* 66	, idx=5 */
#endif
	{10, 9, 8, 2, 0, 8}, /* 62	, idx=6 */
	{10, 9, 8, 0, 0, 8}, /* 59	, idx=7 */
	{18, 17, 16, 8, 0, 16}, /* 53 , idx=8 */
	{26, 25, 24, 16, 0, 24}, /* 50	, idx=9 */
	{34, 33, 32, 24, 0, 32}, /* 47	, idx=0x0a */
	/* {34,33,32,16,0,32}, */ /* 43	, idx=0x0b */
	/* {34,33,32,8,0,32}, */ /* 40 , idx=0x0c */
	/* {34,33,28,8,0,32}, */ /* 37 , idx=0x0d */
	/* {34,33,20,8,0,32}, */ /* 32 , idx=0x0e */
	/* {34,32,24,8,0,32}, */ /* 26 , idx=0x0f */
	/* {49,48,32,16,0,48}, */ /* 20	, idx=0x10 */
	/* {49,48,24,0,0,48}, */ /* 17 , idx=0x11 */
	/* {49,47,16,16,0,48}, */ /* 15	, idx=0x12 */
	/* {49,44,16,16,0,48}, */ /* 12	, idx=0x13 */
	/* {49,40,16,0,0,48}, */ /* 9 , idx=0x14 */
	{34, 31, 28, 20, 0, 32}, /* 43	, idx=0x0b */
	{34, 31, 27, 18, 0, 32}, /* 40 , idx=0x0c */
	{34, 31, 26, 16, 0, 32}, /* 37 , idx=0x0d */
	{34, 30, 22, 16, 0, 32}, /* 32 , idx=0x0e */
	{34, 30, 24, 16, 0, 32}, /* 26 , idx=0x0f */
	{49, 46, 40, 16, 0, 48}, /* 20	, idx=0x10 */
	{49, 45, 32, 0, 0, 48}, /* 17 , idx=0x11 */
	{49, 45, 22, 18, 0, 48}, /* 15	, idx=0x12 */
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	{49, 40, 28, 18, 0, 48}, /* 12 , idx=0x13 */
	{49, 34, 20, 16, 0, 48}, /* 9 , idx=0x14 */
#else
	{49, 40, 24, 16, 0, 48}, /* 12	, idx=0x13 */
	{49, 32, 18, 12, 0, 48}, /* 9 , idx=0x14 */
#endif
	{49, 22, 18, 14, 0, 48}, /* 6 , idx=0x15 */
	{49, 16, 16, 0, 0, 48}
};/* 3 */ /* 3, idx=0x16 */

static u8	RETRY_PENALTY_UP[RETRYSIZE + 1] = {49, 44, 16, 16, 0, 48}; /* 12% for rate up */

static u8 PT_PENALTY[RETRYSIZE + 1] = {34, 31, 30, 24, 0, 32};

#if 0
static u8	RETRY_PENALTY_IDX[2][RATESIZE] = {{
		4, 4, 4, 5, 4, 4, 5, 7, 7, 7, 8, 0x0a,	 /* SS>TH */
		4, 4, 4, 4, 6, 0x0a, 0x0b, 0x0d,
		5, 5, 7, 7, 8, 0x0b, 0x0d, 0x0f
	},	 		   /* 0329 R01 */
	{
		4, 4, 4, 5, 7, 7, 9, 9, 0x0c, 0x0e, 0x10, 0x12,	 /* SS<TH */
		4, 4, 5, 5, 6, 0x0a, 0x11, 0x13,
		9, 9, 9, 9, 0x0c, 0x0e, 0x11, 0x13
	}
};
#endif


#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
static u8	RETRY_PENALTY_IDX[2][RATESIZE] = 	{{
		4, 4, 4, 5, 4, 4, 5, 7, 7, 7, 8, 0x0a,	 /* SS>TH */
#if (DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE)
		4, 4, 4, 4, 0x0d, 0x0d, 0x0f, 0x0f,
#else
		4, 4, 4, 4, 6, 0x0a, 0x0b, 0x0d,
#endif
		5, 5, 7, 7, 8, 0x0b, 0x0d, 0x0f
	},	 		   /* 0329 R01 */
	{
		0x0a, 0x0a, 0x0a, 0x0a, 0x0c, 0x0c, 0x0e, 0x10, 0x11, 0x12, 0x12, 0x13,	 /* SS<TH */
		0x0e, 0x0f, 0x10, 0x10, 0x11, 0x14, 0x14, 0x15,
		9, 9, 9, 9, 0x0c, 0x0e, 0x11, 0x13
	}
};

static u8	RETRY_PENALTY_UP_IDX[RATESIZE] = 	{0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x12, 0x12, 0x12, 0x13, 0x13, 0x14,	 /* SS>TH */
				 0x13, 0x13, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15,
				 0x11, 0x11, 0x12, 0x13, 0x13, 0x13, 0x14, 0x15
					    };

static u8	RSSI_THRESHOLD[RATESIZE] =				{0, 0, 0, 0,
					 0, 0, 0, 0, 0, 0x24, 0x26, 0x2a,
				 0x17, 0x1a, 0x1c, 0x1f, 0x23, 0x28, 0x2a, 0x2c,
					 0, 0, 0, 0x1f, 0x23, 0x28, 0x2a, 0x2c
					};
#else

/* wilson modify */
#if 0
static u8	RETRY_PENALTY_IDX[2][RATESIZE] = {{
		4, 4, 4, 5, 4, 4, 5, 7, 7, 7, 8, 0x0a,	 /*  SS>TH */
		4, 4, 4, 4, 6, 0x0a, 0x0b, 0x0d,
		5, 5, 7, 7, 8, 0x0b, 0x0d, 0x0f
	},	 		   /*  0329 R01 */
	{
		0x0a, 0x0a, 0x0b, 0x0c, 0x0a, 0x0a, 0x0b, 0x0c, 0x0d, 0x10, 0x13, 0x14,	 /*  SS<TH */
		0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x11, 0x13, 0x15,
		9, 9, 9, 9, 0x0c, 0x0e, 0x11, 0x13
	}
};
#endif

static u8	RETRY_PENALTY_IDX[2][RATESIZE] = {{
		4, 4, 4, 5, 4, 4, 5, 7, 7, 7, 8, 0x0a,	 /* SS>TH */
		4, 4, 4, 4, 6, 0x0a, 0x0b, 0x0d,
		5, 5, 7, 7, 8, 0x0b, 0x0d, 0x0f
	},	 		   /* 0329 R01 */
	{
		0x0a, 0x0a, 0x0b, 0x0c, 0x0a, 0x0a, 0x0b, 0x0c, 0x0d, 0x10, 0x13, 0x13,	 /* SS<TH */
		0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x11, 0x13, 0x13,
		9, 9, 9, 9, 0x0c, 0x0e, 0x11, 0x13
	}
};

static u8	RETRY_PENALTY_UP_IDX[RATESIZE] = {0x0c, 0x0d, 0x0d, 0x0f, 0x0d, 0x0e, 0x0f, 0x0f, 0x10, 0x12, 0x13, 0x14,	 /* SS>TH */
			  0x0f, 0x10, 0x10, 0x12, 0x12, 0x13, 0x14, 0x15,
				  0x11, 0x11, 0x12, 0x13, 0x13, 0x13, 0x14, 0x15
					   };

static u8	RSSI_THRESHOLD[RATESIZE] =			{0, 0, 0, 0,
					 0, 0, 0, 0, 0, 0x24, 0x26, 0x2a,
				 0x18, 0x1a, 0x1d, 0x1f, 0x21, 0x27, 0x29, 0x2a,
					 0, 0, 0, 0x1f, 0x23, 0x28, 0x2a, 0x2c
				       };

#endif

/*static u8	RSSI_THRESHOLD[RATESIZE] = {0,0,0,0,
													0,0,0,0,0,0x24,0x26,0x2a,
													0x1a,0x1c,0x1e,0x21,0x24,0x2a,0x2b,0x2d,
													0,0,0,0x1f,0x23,0x28,0x2a,0x2c};*/
/*static u16	N_THRESHOLD_HIGH[RATESIZE] = {4,4,8,16,
													24,36,48,72,96,144,192,216,
													60,80,100,160,240,400,560,640,
													300,320,480,720,1000,1200,1600,2000};
static u16	N_THRESHOLD_LOW[RATESIZE] = {2,2,4,8,
													12,18,24,36,48,72,96,108,
													30,40,50,80,120,200,280,320,
													150,160,240,360,500,600,800,1000};*/
static u16	N_THRESHOLD_HIGH[RATESIZE] = {4, 4, 8, 16,
					      24, 36, 48, 72, 96, 144, 192, 216,
				      60, 80, 100, 160, 240, 400, 600, 800,
				      300, 320, 480, 720, 1000, 1200, 1600, 2000
					};
static u16	N_THRESHOLD_LOW[RATESIZE] = {2, 2, 4, 8,
					     12, 18, 24, 36, 48, 72, 96, 108,
					     30, 40, 50, 80, 120, 200, 300, 400,
				     150, 160, 240, 360, 500, 600, 800, 1000
				       };
static u8	 TRYING_NECESSARY[RATESIZE] = {2, 2, 2, 2,
					       2, 2, 3, 3, 4, 4, 5, 7,
					       4, 4, 7, 10, 10, 12, 12, 18,
					       5, 7, 7, 8, 11, 18, 36, 60
					};  /* 0329 */ /* 1207 */
#if 0
static u8	 POOL_RETRY_TH[RATESIZE] = {30, 30, 30, 30,
					    30, 30, 25, 25, 20, 15, 15, 10,
					    30, 25, 25, 20, 15, 10, 10, 10,
					    30, 25, 25, 20, 15, 10, 10, 10
				     };
#endif

static u8	DROPING_NECESSARY[RATESIZE] = {1, 1, 1, 1,
					       1, 2, 3, 4, 5, 6, 7, 8,
					       1, 2, 3, 4, 5, 6, 7, 8,
					       5, 6, 7, 8, 9, 10, 11, 12
					};


static u32	INIT_RATE_FALLBACK_TABLE[16] = {0x0f8ff015, /* 0: 40M BGN mode */
					0x0f8ff010,   /* 1: 40M GN mode */
				0x0f8ff005,   /* 2: BN mode/ 40M BGN mode */
						0x0f8ff000,   /* 3: N mode */
						0x00000ff5,   /* 4: BG mode */
						0x00000ff0,   /* 5: G mode */
						0x0000000d,   /* 6: B mode */
						0,			/* 7: */
						0,			/* 8: */
						0,			/* 9: */
						0,			/* 10: */
						0,			/* 11: */
						0,			/* 12: */
						0,			/* 13: */
						0,			/* 14: */
						0,			/* 15: */

					  };
static u8 pending_for_rate_up_fail[5] = {2, 10, 24, 40, 60};
static u16 dynamic_tx_rpt_timing[6] = {0x186a, 0x30d4, 0x493e, 0x61a8, 0x7a12, 0x927c};	/*200ms-1200ms*/

/* End rate adaptive parameters */

#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && \
	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
static int
odm_ra_learn_bounding(
	struct PHY_DM_STRUCT		*p_dm_odm,
	struct _odm_ra_info_	*p_ra_info
)
{
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, (" odm_ra_learn_bounding\n"));
	if (DM_RA_RATE_UP != p_ra_info->rate_direction) {
		/* Check if previous RA adjustment trend as +++--- or ++++----*/
		if (((3 == p_ra_info->rate_up_counter && p_ra_info->bounding_learning_time <= 10)
		     || (4 == p_ra_info->rate_up_counter && p_ra_info->bounding_learning_time <= 16))
		    && (p_ra_info->rate_up_counter == p_ra_info->rate_down_counter)) {
			if (1 != p_ra_info->bounding_type) {
				p_ra_info->bounding_type = 1;
				p_ra_info->bounding_counter = 0;
			}
			p_ra_info->bounding_counter++;
			/* Check if previous RA adjustment trend as ++--*/
		} else if ((2 == p_ra_info->rate_up_counter) && (p_ra_info->bounding_learning_time <= 7)
			&& (p_ra_info->rate_up_counter == p_ra_info->rate_down_counter)) {
			if (2 != p_ra_info->bounding_type) {
				p_ra_info->bounding_type = 2;
				p_ra_info->bounding_counter = 0;
			}
			p_ra_info->bounding_counter++;
			/* Check if previous RA adjustment trend as +++++-----*/
		} else if ((5 == p_ra_info->rate_up_counter) && (p_ra_info->bounding_learning_time <= 17)
			&& (p_ra_info->rate_up_counter == p_ra_info->rate_down_counter)) {
			if (3 != p_ra_info->bounding_type) {
				p_ra_info->bounding_type = 3;
				p_ra_info->bounding_counter = 0;
			}
			p_ra_info->bounding_counter++;
		} else
			p_ra_info->bounding_type = 0;

		p_ra_info->rate_down_counter = 0;
		p_ra_info->rate_up_counter = 0;
		p_ra_info->bounding_learning_time = 1;
	} else if (p_ra_info->bounding_type) {
		/* Check if RA adjustment trend as +++---++(+) or ++++----++(+)*/
		if ((1 == p_ra_info->bounding_type) && (1 == p_ra_info->bounding_counter)
		    && (2 == p_ra_info->rate_up_counter)) {
			p_ra_info->bounding_type = 0;
			if (p_ra_info->bounding_learning_time <= 5)
				return 1;
			/* Check if RA adjustment trend as ++--++--+(+)*/
		} else if ((2 == p_ra_info->bounding_type) && (2 == p_ra_info->bounding_counter)
			   && (1 == p_ra_info->rate_up_counter)) {
			p_ra_info->bounding_type = 0;
			if (p_ra_info->bounding_learning_time <= 2)
				return 1;
			/* Check if RA adjustment trend as +++++-----++(+)*/
		} else if ((3 == p_ra_info->bounding_type) && (1 == p_ra_info->bounding_counter)
			   && (2 == p_ra_info->rate_up_counter)) {
			p_ra_info->bounding_type = 0;
			if (p_ra_info->bounding_learning_time <= 4)
				return 1;
		}
	}

	return 0;
}
#endif

static void
odm_set_tx_rpt_timing_8188e(
	struct PHY_DM_STRUCT		*p_dm_odm,
	struct _odm_ra_info_	*p_ra_info,
	u8				extend
)
{
	u8 idx = 0;

	for (idx = 0; idx < 5; idx++)
		if (dynamic_tx_rpt_timing[idx] == p_ra_info->rpt_time)
			break;

	if (extend == 0) /* back to default timing */
		idx = 0; /* 200ms */
	else if (extend == 1) { /* increase the timing */
		idx += 1;
		if (idx > 5)
			idx = 5;
	} else if (extend == 2) { /* decrease the timing */
		if (idx != 0)
			idx -= 1;
	}
	p_ra_info->rpt_time = dynamic_tx_rpt_timing[idx];

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("p_ra_info->rpt_time=0x%x\n", p_ra_info->rpt_time));
}

static int
odm_rate_down_8188e(
	struct PHY_DM_STRUCT		*p_dm_odm,
	struct _odm_ra_info_  *p_ra_info
)
{
	u8 rate_id, lowest_rate, highest_rate;
	s8 i;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("=====>odm_rate_down_8188e()\n"));
	if (NULL == p_ra_info) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("odm_rate_down_8188e(): p_ra_info is NULL\n"));
		return -1;
	}
	rate_id = p_ra_info->pre_rate;
	lowest_rate = p_ra_info->lowest_rate;
	highest_rate = p_ra_info->highest_rate;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
		(" rate_id=%d lowest_rate=%d highest_rate=%d rate_sgi=%d\n",
		      rate_id, lowest_rate, highest_rate, p_ra_info->rate_sgi));
	if (rate_id > highest_rate)
		rate_id = highest_rate;
	else if (p_ra_info->rate_sgi)
		p_ra_info->rate_sgi = 0;
	else if (rate_id > lowest_rate) {
		if (rate_id > 0) {
			for (i = rate_id - 1; i >= lowest_rate; i--) {
				if (p_ra_info->ra_use_rate & BIT(i)) {
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && \
	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
					p_ra_info->rate_down_counter++;
					p_ra_info->rate_direction = DM_RA_RATE_DOWN;

					/* Learning +(0)-(-)(-)+ and ++(0)--(-)(-)(0)+ after the persistence of learned TX rate expire*/
					if (0xFF == p_ra_info->rate_down_start_time) {
						if ((0 == p_ra_info->rate_up_counter) || (p_ra_info->rate_up_counter + 2 < p_ra_info->bounding_learning_time))
							p_ra_info->rate_down_start_time = 0;
						else
							p_ra_info->rate_down_start_time = p_ra_info->bounding_learning_time;
					}
#endif
					rate_id = i;
					goto rate_down_finish;

				}
			}
		}
	} else if (rate_id <= lowest_rate)
		rate_id = lowest_rate;
rate_down_finish:
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && \
	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
	/*if (p_ra_info->RTY[2] >= 100) {
		p_ra_info->ra_waiting_counter = 2;
		p_ra_info->ra_pending_counter += 1;
	} else */if ((0 != p_ra_info->rate_down_start_time) && (0xFF != p_ra_info->rate_down_start_time)) {
		/* Learning +(0)-(-)(-)+ and ++(0)--(-)(-)(0)+ after the persistence of learned TX rate expire*/
		if (p_ra_info->rate_down_counter < p_ra_info->rate_up_counter) {

		} else if (p_ra_info->rate_down_counter == p_ra_info->rate_up_counter) {
			p_ra_info->ra_waiting_counter = 2;
			p_ra_info->ra_pending_counter += 1;
		} else if (p_ra_info->rate_down_counter <= p_ra_info->rate_up_counter + 2)
			rate_id = p_ra_info->pre_rate;
		else {
			p_ra_info->ra_waiting_counter = 0;
			p_ra_info->ra_pending_counter = 0;
			p_ra_info->rate_down_start_time = 0;
		}
	} else
#endif
		if (p_ra_info->ra_waiting_counter == 1) {
			p_ra_info->ra_waiting_counter += 1;
			p_ra_info->ra_pending_counter += 1;
		} else if (p_ra_info->ra_waiting_counter == 0) {
		} else {
			p_ra_info->ra_waiting_counter = 0;
			p_ra_info->ra_pending_counter = 0;
		}

	if (p_ra_info->ra_pending_counter >= 4)
		p_ra_info->ra_pending_counter = 4;
	p_ra_info->ra_drop_after_down = 1;
	p_ra_info->decision_rate = rate_id;
	odm_set_tx_rpt_timing_8188e(p_dm_odm, p_ra_info, 2);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("rate down, Decrease RPT Timing\n"));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("ra_waiting_counter %d, ra_pending_counter %d RADrop %d", p_ra_info->ra_waiting_counter, p_ra_info->ra_pending_counter, p_ra_info->ra_drop_after_down));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("rate down to rate_id %d rate_sgi %d\n", rate_id, p_ra_info->rate_sgi));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("<=====odm_rate_down_8188e()\n"));
	return 0;
}

static int
odm_rate_up_8188e(
	struct PHY_DM_STRUCT		*p_dm_odm,
	struct _odm_ra_info_  *p_ra_info
)
{
	u8 rate_id, highest_rate;
	u8 i;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("=====>odm_rate_up_8188e()\n"));
	if (NULL == p_ra_info) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("odm_rate_up_8188e(): p_ra_info is NULL\n"));
		return -1;
	}
	rate_id = p_ra_info->pre_rate;
	highest_rate = p_ra_info->highest_rate;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
		     (" rate_id=%d highest_rate=%d\n",
		      rate_id, highest_rate));
	if (p_ra_info->ra_waiting_counter == 1) {
		p_ra_info->ra_waiting_counter = 0;
		p_ra_info->ra_pending_counter = 0;
	} else if (p_ra_info->ra_waiting_counter > 1) {
		p_ra_info->pre_rssi_sta_ra = p_ra_info->rssi_sta_ra;
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && \
	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
		p_ra_info->rate_down_start_time = 0;
#endif
		goto rate_up_finish;
	}
	odm_set_tx_rpt_timing_8188e(p_dm_odm, p_ra_info, 0);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("odm_rate_up_8188e(): default RPT Timing\n"));

	if (rate_id < highest_rate) {
		for (i = rate_id + 1; i <= highest_rate; i++) {
			if (p_ra_info->ra_use_rate & BIT(i)) {
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && \
	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
				if (odm_ra_learn_bounding(p_dm_odm, p_ra_info)) {
					p_ra_info->ra_waiting_counter = 2;
					p_ra_info->ra_pending_counter = 1;
					goto rate_up_finish;
				}
				p_ra_info->rate_up_counter++;
				p_ra_info->rate_direction = DM_RA_RATE_UP;
#endif
				rate_id = i;
				goto rate_up_finish;
			}
		}
	} else if (rate_id == highest_rate) {
		if (p_ra_info->sgi_enable && (p_ra_info->rate_sgi != 1))
			p_ra_info->rate_sgi = 1;
		else if ((p_ra_info->sgi_enable) != 1)
			p_ra_info->rate_sgi = 0;
	} else /* if((sta_info_ra->Decision_rate) > (sta_info_ra->Highest_rate)) */
		rate_id = highest_rate;

rate_up_finish:
	/* if(p_ra_info->ra_waiting_counter==10) */
	if (p_ra_info->ra_waiting_counter == (4 + pending_for_rate_up_fail[p_ra_info->ra_pending_counter])) {
		p_ra_info->ra_waiting_counter = 0;
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && \
	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
		/* Mark persistence expiration state*/
		p_ra_info->rate_down_start_time = 0xFF;
		/* Clear state to avoid wrong bounding check*/
		p_ra_info->rate_down_counter = 0;
		p_ra_info->rate_up_counter = 0;
		p_ra_info->rate_direction = 0;
#endif
	} else
		p_ra_info->ra_waiting_counter++;

	p_ra_info->decision_rate = rate_id;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("rate up to rate_id %d\n", rate_id));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("ra_waiting_counter %d, ra_pending_counter %d", p_ra_info->ra_waiting_counter, p_ra_info->ra_pending_counter));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("<=====odm_rate_up_8188e()\n"));
	return 0;
}

static void odm_reset_ra_counter_8188e(struct _odm_ra_info_  *p_ra_info)
{
	u8 rate_id;
	rate_id = p_ra_info->decision_rate;
	p_ra_info->nsc_up = (N_THRESHOLD_HIGH[rate_id] + N_THRESHOLD_LOW[rate_id]) >> 1;
	p_ra_info->nsc_down = (N_THRESHOLD_HIGH[rate_id] + N_THRESHOLD_LOW[rate_id]) >> 1;
}

static void
odm_rate_decision_8188e(
	struct PHY_DM_STRUCT		*p_dm_odm,
	struct _odm_ra_info_  *p_ra_info
)
{
	u8 rate_id = 0, rty_pt_id = 0, penalty_id1 = 0, penalty_id2 = 0;
	/* u32 pool_retry; */
	static u8 dynamic_tx_rpt_timing_counter = 0;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("=====>odm_rate_decision_8188e()\n"));

	if (p_ra_info->active && (p_ra_info->TOTAL > 0)) { /* STA used and data packet exits */
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && \
	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
		if (((p_ra_info->rssi_sta_ra <= 17) && (p_ra_info->rssi_sta_ra > p_ra_info->pre_rssi_sta_ra))
		    || ((p_ra_info->pre_rssi_sta_ra <= 17) && (p_ra_info->pre_rssi_sta_ra > p_ra_info->rssi_sta_ra))) {
			/* don't reset state in low signal due to the power different between CCK and MCS is large.*/
		} else
#endif
			if (p_ra_info->ra_drop_after_down) {
				p_ra_info->ra_drop_after_down--;
				odm_reset_ra_counter_8188e(p_ra_info);
				return;
			}
		if ((p_ra_info->rssi_sta_ra < (p_ra_info->pre_rssi_sta_ra - 3)) || (p_ra_info->rssi_sta_ra > (p_ra_info->pre_rssi_sta_ra + 3))) {
			p_ra_info->pre_rssi_sta_ra = p_ra_info->rssi_sta_ra;
			p_ra_info->ra_waiting_counter = 0;
			p_ra_info->ra_pending_counter = 0;
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && \
	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
			p_ra_info->bounding_type = 0;
#endif
		}

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
		if (0xff != p_dm_odm->priv->pshare->rf_ft_var.txforce) {
			p_ra_info->pre_rate = p_dm_odm->priv->pshare->rf_ft_var.txforce;
			odm_reset_ra_counter_8188e(p_ra_info);
		}
#endif

		/* Start RA decision */
		if (p_ra_info->pre_rate > p_ra_info->highest_rate)
			rate_id = p_ra_info->highest_rate;
		else
			rate_id = p_ra_info->pre_rate;
		if (p_ra_info->rssi_sta_ra > RSSI_THRESHOLD[rate_id])
			rty_pt_id = 0;
		else
			rty_pt_id = 1;
		penalty_id1 = RETRY_PENALTY_IDX[rty_pt_id][rate_id]; /* TODO by page */

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
			     (" nsc_down init is %d\n", p_ra_info->nsc_down));
		/* pool_retry=p_ra_info->RTY[2]+p_ra_info->RTY[3]+p_ra_info->RTY[4]+p_ra_info->DROP; */
		p_ra_info->nsc_down += p_ra_info->RTY[0] * RETRY_PENALTY[penalty_id1][0];
		p_ra_info->nsc_down += p_ra_info->RTY[1] * RETRY_PENALTY[penalty_id1][1];
		p_ra_info->nsc_down += p_ra_info->RTY[2] * RETRY_PENALTY[penalty_id1][2];
		p_ra_info->nsc_down += p_ra_info->RTY[3] * RETRY_PENALTY[penalty_id1][3];
		p_ra_info->nsc_down += p_ra_info->RTY[4] * RETRY_PENALTY[penalty_id1][4];
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
			     (" nsc_down is %d, total*penalty[5] is %d\n",
			p_ra_info->nsc_down, (p_ra_info->TOTAL * RETRY_PENALTY[penalty_id1][5])));
		if (p_ra_info->nsc_down > (p_ra_info->TOTAL * RETRY_PENALTY[penalty_id1][5]))
			p_ra_info->nsc_down -= p_ra_info->TOTAL * RETRY_PENALTY[penalty_id1][5];
		else
			p_ra_info->nsc_down = 0;

		/* rate up */
		penalty_id2 = RETRY_PENALTY_UP_IDX[rate_id];
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
			     (" nsc_up init is %d\n", p_ra_info->nsc_up));
		p_ra_info->nsc_up += p_ra_info->RTY[0] * RETRY_PENALTY[penalty_id2][0];
		p_ra_info->nsc_up += p_ra_info->RTY[1] * RETRY_PENALTY[penalty_id2][1];
		p_ra_info->nsc_up += p_ra_info->RTY[2] * RETRY_PENALTY[penalty_id2][2];
		p_ra_info->nsc_up += p_ra_info->RTY[3] * RETRY_PENALTY[penalty_id2][3];
		p_ra_info->nsc_up += p_ra_info->RTY[4] * RETRY_PENALTY[penalty_id2][4];
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
			     ("nsc_up is %d, total*up[5] is %d\n",
			p_ra_info->nsc_up, (p_ra_info->TOTAL * RETRY_PENALTY[penalty_id2][5])));
		if (p_ra_info->nsc_up > (p_ra_info->TOTAL * RETRY_PENALTY[penalty_id2][5]))
			p_ra_info->nsc_up -= p_ra_info->TOTAL * RETRY_PENALTY[penalty_id2][5];
		else
			p_ra_info->nsc_up = 0;

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE | ODM_COMP_INIT, ODM_DBG_LOUD,
			(" RssiStaRa= %d rty_pt_id=%d penalty_id1=0x%x  penalty_id2=0x%x rate_id=%d nsc_down=%d nsc_up=%d SGI=%d\n",
			p_ra_info->rssi_sta_ra, rty_pt_id, penalty_id1, penalty_id2, rate_id, p_ra_info->nsc_down, p_ra_info->nsc_up, p_ra_info->rate_sgi));
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && \
	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
		if (0xFF != p_ra_info->bounding_learning_time)
			p_ra_info->bounding_learning_time++;
#endif
		if ((p_ra_info->nsc_down < N_THRESHOLD_LOW[rate_id]) || (p_ra_info->DROP > DROPING_NECESSARY[rate_id]))
			odm_rate_down_8188e(p_dm_odm, p_ra_info);
		/* else if ((p_ra_info->nsc_up > N_THRESHOLD_HIGH[rate_id])&&(pool_retry<POOL_RETRY_TH[rate_id])) */
		else if (p_ra_info->nsc_up > N_THRESHOLD_HIGH[rate_id])
			odm_rate_up_8188e(p_dm_odm, p_ra_info);
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && \
	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
		else if ((p_ra_info->RTY[2] >= 100) && (ODM_BW20M == *p_dm_odm->p_band_width))
			odm_rate_down_8188e(p_dm_odm, p_ra_info);
#endif

		if ((p_ra_info->decision_rate) == (p_ra_info->pre_rate))
			dynamic_tx_rpt_timing_counter += 1;
		else
			dynamic_tx_rpt_timing_counter = 0;

		if (dynamic_tx_rpt_timing_counter >= 4) {
			odm_set_tx_rpt_timing_8188e(p_dm_odm, p_ra_info, 1);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("<=====rate don't change 4 times, Extend RPT Timing\n"));
			dynamic_tx_rpt_timing_counter = 0;
		}

		p_ra_info->pre_rate = p_ra_info->decision_rate;  /* YJ,add,120120 */

		odm_reset_ra_counter_8188e(p_ra_info);
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, ("<=====odm_rate_decision_8188e()\n"));
}

static int
odm_arfb_refresh_8188e(
	struct PHY_DM_STRUCT		*p_dm_odm,
	struct _odm_ra_info_  *p_ra_info
)
{
	/* Wilson 2011/10/26 */
	u32 mask_from_reg;
	u8 i;

	switch (p_ra_info->rate_id) {
	case RATR_INX_WIRELESS_NGB:
		p_ra_info->ra_use_rate = (p_ra_info->rate_mask) & 0x0f8fe00f;
		break;
	case RATR_INX_WIRELESS_NG:
		p_ra_info->ra_use_rate = (p_ra_info->rate_mask) & 0x0f8ff010;
		break;
	case RATR_INX_WIRELESS_NB:
		p_ra_info->ra_use_rate = (p_ra_info->rate_mask) & 0x0f8fe00f;
		break;
	case RATR_INX_WIRELESS_N:
		p_ra_info->ra_use_rate = (p_ra_info->rate_mask) & 0x0f8ff000;
		break;
	case RATR_INX_WIRELESS_GB:
		p_ra_info->ra_use_rate = (p_ra_info->rate_mask) & 0x00000fef;
		break;
	case RATR_INX_WIRELESS_G:
		p_ra_info->ra_use_rate = (p_ra_info->rate_mask) & 0x00000ff0;
		break;
	case RATR_INX_WIRELESS_B:
		p_ra_info->ra_use_rate = (p_ra_info->rate_mask) & 0x0000000d;
		break;
	case 12:
		mask_from_reg = odm_read_4byte(p_dm_odm, REG_ARFR0);
		p_ra_info->ra_use_rate = (p_ra_info->rate_mask) & mask_from_reg;
		break;
	case 13:
		mask_from_reg = odm_read_4byte(p_dm_odm, REG_ARFR1);
		p_ra_info->ra_use_rate = (p_ra_info->rate_mask) & mask_from_reg;
		break;
	case 14:
		mask_from_reg = odm_read_4byte(p_dm_odm, REG_ARFR2);
		p_ra_info->ra_use_rate = (p_ra_info->rate_mask) & mask_from_reg;
		break;
	case 15:
		mask_from_reg = odm_read_4byte(p_dm_odm, REG_ARFR3);
		p_ra_info->ra_use_rate = (p_ra_info->rate_mask) & mask_from_reg;
		break;

	default:
		p_ra_info->ra_use_rate = (p_ra_info->rate_mask);
		break;
	}
	/* Highest rate */
	if (p_ra_info->ra_use_rate)
		for (i = RATESIZE ; i >= 0 ; i--) {
			if ((p_ra_info->ra_use_rate) & BIT(i)) {
				p_ra_info->highest_rate = i;
				break;
			}
		}
	else
		p_ra_info->highest_rate = 0;
	/* Lowest rate */
	if (p_ra_info->ra_use_rate)
		for (i = 0; i < RATESIZE; i++) {
			if ((p_ra_info->ra_use_rate) & BIT(i)) {
				p_ra_info->lowest_rate = i;
				break;
			}
		}
	else
		p_ra_info->lowest_rate = 0;

#if POWER_TRAINING_ACTIVE == 1
	if (p_ra_info->highest_rate > 0x13)
		p_ra_info->pt_mode_ss = 3;
	else if (p_ra_info->highest_rate > 0x0b)
		p_ra_info->pt_mode_ss = 2;
	else if (p_ra_info->highest_rate > 0x0b)
		p_ra_info->pt_mode_ss = 1;
	else
		p_ra_info->pt_mode_ss = 0;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
		("ODM_ARFBRefresh_8188E(): pt_mode_ss=%d\n", p_ra_info->pt_mode_ss));

#endif
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
		("ODM_ARFBRefresh_8188E(): rate_id=%d rate_mask=%8.8x ra_use_rate=%8.8x highest_rate=%d\n",
		p_ra_info->rate_id, p_ra_info->rate_mask, p_ra_info->ra_use_rate, p_ra_info->highest_rate));
	return 0;
}

#if POWER_TRAINING_ACTIVE == 1
static void
odm_pt_try_state_8188e(
	struct PHY_DM_STRUCT		*p_dm_odm,
	struct _odm_ra_info_	*p_ra_info
)
{
	p_ra_info->pt_try_state = 0;
	switch (p_ra_info->pt_mode_ss) {
	case 3:
		if (p_ra_info->decision_rate >= 0x19)
			p_ra_info->pt_try_state = 1;
		break;
	case 2:
		if (p_ra_info->decision_rate >= 0x11)
			p_ra_info->pt_try_state = 1;
		break;
	case 1:
		if (p_ra_info->decision_rate >= 0x0a)
			p_ra_info->pt_try_state = 1;
		break;
	case 0:
		if (p_ra_info->decision_rate >= 0x03)
			p_ra_info->pt_try_state = 1;
		break;
	default:
		p_ra_info->pt_try_state = 0;
	}

	if (p_ra_info->rssi_sta_ra < 48)
		p_ra_info->pt_stage = 0;
	else if (p_ra_info->pt_try_state == 1) {
		if ((p_ra_info->pt_stop_count >= 10) || (p_ra_info->pt_pre_rssi > p_ra_info->rssi_sta_ra + 5)
		    || (p_ra_info->pt_pre_rssi < p_ra_info->rssi_sta_ra - 5) || (p_ra_info->decision_rate != p_ra_info->pt_pre_rate)) {
			if (p_ra_info->pt_stage == 0)
				p_ra_info->pt_stage = 1;
			else if (p_ra_info->pt_stage == 1)
				p_ra_info->pt_stage = 3;
			else
				p_ra_info->pt_stage = 5;

			p_ra_info->pt_pre_rssi = p_ra_info->rssi_sta_ra;
			p_ra_info->pt_stop_count = 0;

		} else {
			p_ra_info->ra_stage = 0;
			p_ra_info->pt_stop_count++;
		}
	} else {
		p_ra_info->pt_stage = 0;
		p_ra_info->ra_stage = 0;
	}
	p_ra_info->pt_pre_rate = p_ra_info->decision_rate;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	/* Disable power training when noisy environment */
	if (p_dm_odm->is_disable_power_training) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("odm_pt_try_state_8188e(): Disable power training when noisy environment\n"));
		p_ra_info->pt_stage = 0;
		p_ra_info->ra_stage = 0;
		p_ra_info->pt_stop_count = 0;
	}
#endif
}

static void
odm_pt_decision_8188e(
	struct _odm_ra_info_	*p_ra_info
)
{
	u8 stage_BUF;
	u8 j;
	u8 temp_stage;
	u32 numsc;
	u32 num_total;
	u8 stage_id;

	stage_BUF = p_ra_info->pt_stage;
	numsc  = 0;
	num_total = p_ra_info->TOTAL * PT_PENALTY[5];
	for (j = 0; j <= 4; j++) {
		numsc += p_ra_info->RTY[j] * PT_PENALTY[j];
		if (numsc > num_total)
			break;
	}

	j = j >> 1;
	temp_stage = (p_ra_info->pt_stage + 1) >> 1;
	if (temp_stage > j)
		stage_id = temp_stage - j;
	else
		stage_id = 0;

	p_ra_info->pt_smooth_factor = (p_ra_info->pt_smooth_factor >> 1) + (p_ra_info->pt_smooth_factor >> 2) + stage_id * 16 + 2;
	if (p_ra_info->pt_smooth_factor > 192)
		p_ra_info->pt_smooth_factor = 192;
	stage_id = p_ra_info->pt_smooth_factor >> 6;
	temp_stage = stage_id * 2;
	if (temp_stage != 0)
		temp_stage -= 1;
	if (p_ra_info->DROP > 3)
		temp_stage = 0;
	p_ra_info->pt_stage = temp_stage;

}
#endif

static void
odm_ra_tx_rpt_timer_setting(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u16			min_rpt_time
)
{
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, (" =====>odm_ra_tx_rpt_timer_setting()\n"));


	if (p_dm_odm->currmin_rpt_time != min_rpt_time) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
			(" currmin_rpt_time =0x%04x min_rpt_time=0x%04x\n", p_dm_odm->currmin_rpt_time, min_rpt_time));
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_AP))
		odm_ra_set_tx_rpt_time(p_dm_odm, min_rpt_time);
#else
		rtw_rpt_timer_cfg_cmd(p_dm_odm->adapter, min_rpt_time);
#endif
		p_dm_odm->currmin_rpt_time = min_rpt_time;
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE, (" <=====odm_ra_tx_rpt_timer_setting()\n"));
}


void
odm_ra_support_init(
	struct PHY_DM_STRUCT	*p_dm_odm
)
{
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("=====>odm_ra_support_init()\n"));

	/* 2012/02/14 MH Be noticed, the init must be after IC type is recognized!!!!! */
	if (p_dm_odm->support_ic_type == ODM_RTL8188E)
		p_dm_odm->ra_support88e = true;

}



int
odm_ra_info_init(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		mac_id
)
{
	struct _odm_ra_info_ *p_ra_info = &p_dm_odm->ra_info[mac_id];
#if 0
	u8 wireless_mode = 0xFF; /* invalid value */
	u8 max_rate_idx = 0x13; /* MCS7 */
	if (p_dm_odm->p_wireless_mode != NULL)
		wireless_mode = *(p_dm_odm->p_wireless_mode);

	if (wireless_mode != 0xFF) {
		if (wireless_mode & ODM_WM_N24G)
			max_rate_idx = 0x13;
		else if (wireless_mode & ODM_WM_G)
			max_rate_idx = 0x0b;
		else if (wireless_mode & ODM_WM_B)
			max_rate_idx = 0x03;
	}

	/* printk("%s ==>wireless_mode:0x%08x,max_raid_idx:0x%02x\n ",__FUNCTION__,wireless_mode,max_rate_idx); */
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
		("odm_ra_info_init(): wireless_mode:0x%08x,max_raid_idx:0x%02x\n",
		      wireless_mode, max_rate_idx));

	p_ra_info->decision_rate = max_rate_idx;
	p_ra_info->pre_rate = max_rate_idx;
	p_ra_info->highest_rate = max_rate_idx;
#else
	p_ra_info->decision_rate = 0x13;
	p_ra_info->pre_rate = 0x13;
	p_ra_info->highest_rate = 0x13;
#endif
	p_ra_info->lowest_rate = 0;
	p_ra_info->rate_id = 0;
	p_ra_info->rate_mask = 0xffffffff;
	p_ra_info->rssi_sta_ra = 0;
	p_ra_info->pre_rssi_sta_ra = 0;
	p_ra_info->sgi_enable = 0;
	p_ra_info->ra_use_rate = 0xffffffff;
	p_ra_info->nsc_down = (N_THRESHOLD_HIGH[0x13] + N_THRESHOLD_LOW[0x13]) / 2;
	p_ra_info->nsc_up = (N_THRESHOLD_HIGH[0x13] + N_THRESHOLD_LOW[0x13]) / 2;
	p_ra_info->rate_sgi = 0;
	p_ra_info->active = 1;	/* active is not used at present. by page, 110819 */
	p_ra_info->rpt_time = 0x927c;
	p_ra_info->DROP = 0;
	p_ra_info->RTY[0] = 0;
	p_ra_info->RTY[1] = 0;
	p_ra_info->RTY[2] = 0;
	p_ra_info->RTY[3] = 0;
	p_ra_info->RTY[4] = 0;
	p_ra_info->TOTAL = 0;
	p_ra_info->ra_waiting_counter = 0;
	p_ra_info->ra_pending_counter = 0;
	p_ra_info->ra_drop_after_down = 0;
#if POWER_TRAINING_ACTIVE == 1
	p_ra_info->pt_active = 1; /* active when this STA is use */
	p_ra_info->pt_try_state = 0;
	p_ra_info->pt_stage = 5; /* Need to fill into HW_PWR_STATUS */
	p_ra_info->pt_smooth_factor = 192;
	p_ra_info->pt_stop_count = 0;
	p_ra_info->pt_pre_rate = 0;
	p_ra_info->pt_pre_rssi = 0;
	p_ra_info->pt_mode_ss = 0;
	p_ra_info->ra_stage = 0;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && \
	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
	p_ra_info->rate_down_counter = 0;
	p_ra_info->rate_up_counter = 0;
	p_ra_info->rate_direction = 0;
	p_ra_info->bounding_type = 0;
	p_ra_info->bounding_counter = 0;
	p_ra_info->bounding_learning_time = 0;
	p_ra_info->rate_down_start_time = 0;
#endif
	return 0;
}

int
odm_ra_info_init_all(
	struct PHY_DM_STRUCT		*p_dm_odm
)
{
	u32 mac_id = 0;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("=====>\n"));
	p_dm_odm->currmin_rpt_time = 0;

	for (mac_id = 0; mac_id < ODM_ASSOCIATE_ENTRY_NUM; mac_id++)
		odm_ra_info_init(p_dm_odm, mac_id);

	/* Redifine arrays for I-cut NIC */
	if (p_dm_odm->cut_version == ODM_CUT_I) {
#if !(DM_ODM_SUPPORT_TYPE & ODM_AP)

		u8 i;
		u8 RETRY_PENALTY_IDX_S[2][RATESIZE] = {{
				4, 4, 4, 5,
				4, 4, 5, 7, 7, 7, 8, 0x0a,	 /* SS>TH */
				4, 4, 4, 4, 6, 0x0a, 0x0b, 0x0d,
				5, 5, 7, 7, 8, 0x0b, 0x0d, 0x0f
			},	 		   /* 0329 R01 */
			{
				0x0a, 0x0a, 0x0b, 0x0c,
				0x0a, 0x0a, 0x0b, 0x0c, 0x0d, 0x10, 0x13, 0x13,	 /* SS<TH */
				0x06, 0x07, 0x08, 0x0d, 0x0e, 0x11, 0x11, 0x11,
				9, 9, 9, 9, 0x0c, 0x0e, 0x11, 0x13
			}
		};

		u8 RETRY_PENALTY_UP_IDX_S[RATESIZE] = {0x0c, 0x0d, 0x0d, 0x0f,
			0x0d, 0x0e, 0x0f, 0x0f, 0x10, 0x12, 0x13, 0x14,	 /* SS>TH */
			       0x0b, 0x0b, 0x11, 0x11, 0x12, 0x12, 0x12, 0x12,
			       0x11, 0x11, 0x12, 0x13, 0x13, 0x13, 0x14, 0x15
						      };

		for (i = 0; i < RATESIZE; i++) {
			RETRY_PENALTY_IDX[0][i] = RETRY_PENALTY_IDX_S[0][i];
			RETRY_PENALTY_IDX[1][i] = RETRY_PENALTY_IDX_S[1][i];

			RETRY_PENALTY_UP_IDX[i] = RETRY_PENALTY_UP_IDX_S[i];
		}
		return 0;
#endif
	}


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)/* This is for non-I-cut */
	{
		struct _ADAPTER	*adapter = p_dm_odm->adapter;

		/* dbg_print("adapter->mgnt_info.reg_ra_lvl = %d\n", adapter->mgnt_info.reg_ra_lvl); */

		/*  */
		/* 2012/09/14 MH Add for different Ra pattern init. For TPLINK case, we */
		/* need to to adjust different RA pattern for middle range RA. 20-30dB degarde */
		/* 88E rate adptve will raise too slow. */
		/*  */
		if (adapter->MgntInfo.RegRALvl == 0) {
			RETRY_PENALTY_UP_IDX[11] = 0x14;

			RETRY_PENALTY_UP_IDX[17] = 0x13;
			RETRY_PENALTY_UP_IDX[18] = 0x14;
			RETRY_PENALTY_UP_IDX[19] = 0x15;

			RETRY_PENALTY_UP_IDX[23] = 0x13;
			RETRY_PENALTY_UP_IDX[24] = 0x13;
			RETRY_PENALTY_UP_IDX[25] = 0x13;
			RETRY_PENALTY_UP_IDX[26] = 0x14;
			RETRY_PENALTY_UP_IDX[27] = 0x15;
		} else if (adapter->MgntInfo.RegRALvl == 1) {
			RETRY_PENALTY_UP_IDX[17] = 0x13;
			RETRY_PENALTY_UP_IDX[18] = 0x13;
			RETRY_PENALTY_UP_IDX[19] = 0x14;

			RETRY_PENALTY_UP_IDX[23] = 0x12;
			RETRY_PENALTY_UP_IDX[24] = 0x13;
			RETRY_PENALTY_UP_IDX[25] = 0x13;
			RETRY_PENALTY_UP_IDX[26] = 0x13;
			RETRY_PENALTY_UP_IDX[27] = 0x14;
		} else if (adapter->MgntInfo.RegRALvl == 2) {
			/* Compile flag default is lvl2, we need not to update. */
		} else if (adapter->MgntInfo.RegRALvl >= 0x80) {
			u8	index = 0, offset = adapter->MgntInfo.RegRALvl - 0x80;

			/* Reset to default rate adaptive value. */
			RETRY_PENALTY_UP_IDX[11] = 0x14;

			RETRY_PENALTY_UP_IDX[17] = 0x13;
			RETRY_PENALTY_UP_IDX[18] = 0x14;
			RETRY_PENALTY_UP_IDX[19] = 0x15;

			RETRY_PENALTY_UP_IDX[23] = 0x13;
			RETRY_PENALTY_UP_IDX[24] = 0x13;
			RETRY_PENALTY_UP_IDX[25] = 0x13;
			RETRY_PENALTY_UP_IDX[26] = 0x14;
			RETRY_PENALTY_UP_IDX[27] = 0x15;

			if (adapter->MgntInfo.RegRALvl >= 0x90) {
				offset = adapter->MgntInfo.RegRALvl - 0x90;
				/* Lazy mode. */
				for (index = 0; index < 28; index++)
					RETRY_PENALTY_UP_IDX[index] += (offset);
			} else {
				/* Aggrasive side. */
				for (index = 0; index < 28; index++)
					RETRY_PENALTY_UP_IDX[index] -= (offset);
			}

		}
	}
#endif
	return 0;
}


u8
odm_ra_get_sgi_8188e(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u8		mac_id
)
{
	if ((NULL == p_dm_odm) || (mac_id >= ASSOCIATE_ENTRY_NUM))
		return 0;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
		("mac_id=%d SGI=%d\n", mac_id, p_dm_odm->ra_info[mac_id].rate_sgi));
	return p_dm_odm->ra_info[mac_id].rate_sgi;
}

u8
odm_ra_get_decision_rate_8188e(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u8		mac_id
)
{
	u8 decision_rate = 0;
	struct _rate_adaptive_table_	*p_ra_table = &p_dm_odm->dm_ra_table;
	u8	cmd_buf[3];

	if ((NULL == p_dm_odm) || (mac_id >= ASSOCIATE_ENTRY_NUM))
		return 0;
	decision_rate = (p_dm_odm->ra_info[mac_id].decision_rate);

	if (decision_rate != p_ra_table->link_tx_rate[mac_id]) {

		cmd_buf[1] = mac_id;
		cmd_buf[0] = decision_rate;
		phydm_c2h_ra_report_handler(p_dm_odm, &(cmd_buf[0]), 3);
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
		(" mac_id=%d decision_rate=0x%x\n", mac_id, decision_rate));
	return decision_rate;
}

u8
odm_ra_get_hw_pwr_status_8188e(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u8		mac_id
)
{
	u8 pt_stage = 5;
	if ((NULL == p_dm_odm) || (mac_id >= ASSOCIATE_ENTRY_NUM))
		return 0;
	pt_stage = (p_dm_odm->ra_info[mac_id].pt_stage);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
		     ("mac_id=%d pt_stage=0x%x\n", mac_id, pt_stage));
	return pt_stage;
}

void
odm_ra_update_rate_info_8188e(
	struct PHY_DM_STRUCT *p_dm_odm,
	u8 mac_id,
	u8 rate_id,
	u32 rate_mask,
	u8 sgi_enable
)
{
	struct _odm_ra_info_ *p_ra_info = NULL;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
		     ("mac_id=%d rate_id=0x%x rate_mask=0x%x sgi_enable=%d\n",
		      mac_id, rate_id, rate_mask, sgi_enable));
	if ((NULL == p_dm_odm) || (mac_id >= ASSOCIATE_ENTRY_NUM))
		return;

	p_ra_info = &(p_dm_odm->ra_info[mac_id]);
	p_ra_info->rate_id = rate_id;
	p_ra_info->rate_mask = rate_mask;
	p_ra_info->sgi_enable = sgi_enable;
	odm_arfb_refresh_8188e(p_dm_odm, p_ra_info);
}

void
odm_ra_set_rssi_8188e(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u8			mac_id,
	u8			rssi
)
{
	struct _odm_ra_info_ *p_ra_info = NULL;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_TRACE,
		     (" mac_id=%d rssi=%d\n", mac_id, rssi));
	if ((NULL == p_dm_odm) || (mac_id >= ASSOCIATE_ENTRY_NUM))
		return;

	p_ra_info = &(p_dm_odm->ra_info[mac_id]);
	p_ra_info->rssi_sta_ra = rssi;
}

void
odm_ra_set_tx_rpt_time(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u16			min_rpt_time
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	if (min_rpt_time != 0xffff) {
#if defined(CONFIG_PCI_HCI)
		odm_write_2byte(p_dm_odm, REG_TX_RPT_TIME, min_rpt_time);
#elif defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)
		notify_tx_report_interval_change(p_dm_odm->priv, min_rpt_time);
#endif
	}
#else
	odm_write_2byte(p_dm_odm, REG_TX_RPT_TIME, min_rpt_time);
#endif

}


void
odm_ra_tx_rpt2_handle_8188e(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u8			*tx_rpt_buf,
	u16			tx_rpt_len,
	u32			mac_id_valid_entry0,
	u32			mac_id_valid_entry1
)
{
	struct _odm_ra_info_ *p_ra_info = NULL;
	u8			mac_id = 0;
	u8			*p_buffer = NULL;
	u32			valid = 0, item_num = 0;
	u16			min_rpt_time = 0x927c;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("=====>odm_ra_tx_rpt2_handle_8188e(): valid0=%d valid1=%d BufferLength=%d\n",
			mac_id_valid_entry0, mac_id_valid_entry1, tx_rpt_len));

	item_num = tx_rpt_len >> 3;
	p_buffer = tx_rpt_buf;

	do {
		valid = 0;
		if (mac_id < 32)
			valid = (1 << mac_id) & mac_id_valid_entry0;
		else if (mac_id < 64)
			valid = (1 << (mac_id - 32)) & mac_id_valid_entry1;

		p_ra_info = &(p_dm_odm->ra_info[mac_id]);
		if (valid) {


			p_ra_info->RTY[0] = (u16)GET_TX_REPORT_TYPE1_RERTY_0(p_buffer);
			p_ra_info->RTY[1] = (u16)GET_TX_REPORT_TYPE1_RERTY_1(p_buffer);
			p_ra_info->RTY[2] = (u16)GET_TX_REPORT_TYPE1_RERTY_2(p_buffer);
			p_ra_info->RTY[3] = (u16)GET_TX_REPORT_TYPE1_RERTY_3(p_buffer);
			p_ra_info->RTY[4] = (u16)GET_TX_REPORT_TYPE1_RERTY_4(p_buffer);
			p_ra_info->DROP = (u16)GET_TX_REPORT_TYPE1_DROP_0(p_buffer);

			p_ra_info->TOTAL = p_ra_info->RTY[0] + \
					   p_ra_info->RTY[1] + \
					   p_ra_info->RTY[2] + \
					   p_ra_info->RTY[3] + \
					   p_ra_info->RTY[4] + \
					   p_ra_info->DROP;
#if defined(TXRETRY_CNT)
			extern struct stat_info *get_macidinfo(struct rtl8192cd_priv *priv, unsigned int aid);

			{
				struct stat_info *pstat = get_macidinfo(p_dm_odm->priv, mac_id);
				if (pstat) {
					pstat->cur_tx_ok += p_ra_info->RTY[0];
					pstat->cur_tx_retry_pkts += p_ra_info->RTY[1] + p_ra_info->RTY[2] + p_ra_info->RTY[3] + p_ra_info->RTY[4];
					pstat->cur_tx_retry_cnt += p_ra_info->RTY[1] + p_ra_info->RTY[2] * 2 + p_ra_info->RTY[3] * 3 + p_ra_info->RTY[4] * 4;
					pstat->total_tx_retry_cnt += pstat->cur_tx_retry_cnt;
					pstat->total_tx_retry_pkts += pstat->cur_tx_retry_pkts;
					pstat->cur_tx_fail += p_ra_info->DROP;
				}
			}
#endif
			if (p_ra_info->TOTAL != 0) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD,
					("macid=%d Total=%d R0=%d R1=%d R2=%d R3=%d R4=%d D0=%d valid0=%x valid1=%x\n",
					      mac_id,
					      p_ra_info->TOTAL,
					      p_ra_info->RTY[0],
					      p_ra_info->RTY[1],
					      p_ra_info->RTY[2],
					      p_ra_info->RTY[3],
					      p_ra_info->RTY[4],
					      p_ra_info->DROP,
					      mac_id_valid_entry0,
					      mac_id_valid_entry1));
#if POWER_TRAINING_ACTIVE == 1
				if (p_ra_info->pt_active) {
					if (p_ra_info->ra_stage < 5)
						odm_rate_decision_8188e(p_dm_odm, p_ra_info);
					else if (p_ra_info->ra_stage == 5) /* Power training try state */
						odm_pt_try_state_8188e(p_dm_odm, p_ra_info);
					else  /* ra_stage==6 */
						odm_pt_decision_8188e(p_ra_info);

					/* Stage_RA counter */
					if (p_ra_info->ra_stage <= 5)
						p_ra_info->ra_stage++;
					else
						p_ra_info->ra_stage = 0;
				} else
					odm_rate_decision_8188e(p_dm_odm, p_ra_info);
#else
				odm_rate_decision_8188e(p_dm_odm, p_ra_info);
#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
				extern void rtl8188e_set_station_tx_rate_info(struct PHY_DM_STRUCT *, struct _odm_ra_info_ *, int);
				rtl8188e_set_station_tx_rate_info(p_dm_odm, p_ra_info, mac_id);
#ifdef DETECT_STA_EXISTANCE
				void rtl8188e_detect_sta_existance(struct PHY_DM_STRUCT	*p_dm_odm, struct _odm_ra_info_ *p_ra_info, int mac_id);
				rtl8188e_detect_sta_existance(p_dm_odm, p_ra_info, mac_id);
#endif
#endif

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD,
					("macid=%d R0=%d R1=%d R2=%d R3=%d R4=%d drop=%d valid0=%x rate_id=%d SGI=%d\n",
					      mac_id,
					      p_ra_info->RTY[0],
					      p_ra_info->RTY[1],
					      p_ra_info->RTY[2],
					      p_ra_info->RTY[3],
					      p_ra_info->RTY[4],
					      p_ra_info->DROP,
					      mac_id_valid_entry0,
					      p_ra_info->decision_rate,
					      p_ra_info->rate_sgi));
			} else
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, (" TOTAL=0!!!!\n"));
		}

		if (min_rpt_time > p_ra_info->rpt_time)
			min_rpt_time = p_ra_info->rpt_time;

		p_buffer += TX_RPT2_ITEM_SIZE;
		mac_id++;
	} while (mac_id < item_num);

	odm_ra_tx_rpt_timer_setting(p_dm_odm, min_rpt_time);


	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("<===== odm_ra_tx_rpt2_handle_8188e()\n"));
}

#else

static void
odm_ra_tx_rpt_timer_setting(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u16			min_rpt_time
)
{
	return;
}


void
odm_ra_support_init(
	struct PHY_DM_STRUCT	*p_dm_odm
)
{
	return;
}

int
odm_ra_info_init(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u32		mac_id
)
{
	return 0;
}

int
odm_ra_info_init_all(
	struct PHY_DM_STRUCT		*p_dm_odm
)
{
	return 0;
}

u8
odm_ra_get_sgi_8188e(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u8		mac_id
)
{
	return 0;
}

u8
odm_ra_get_decision_rate_8188e(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u8		mac_id
)
{
	return 0;
}
u8
odm_ra_get_hw_pwr_status_8188e(
	struct PHY_DM_STRUCT	*p_dm_odm,
	u8		mac_id
)
{
	return 0;
}

void
odm_ra_update_rate_info_8188e(
	struct PHY_DM_STRUCT *p_dm_odm,
	u8 mac_id,
	u8 rate_id,
	u32 rate_mask,
	u8 sgi_enable
)
{
	return;
}

void
odm_ra_set_rssi_8188e(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u8			mac_id,
	u8			rssi
)
{
	return;
}

void
odm_ra_set_tx_rpt_time(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u16			min_rpt_time
)
{
	return;
}

void
odm_ra_tx_rpt2_handle_8188e(
	struct PHY_DM_STRUCT		*p_dm_odm,
	u8			*tx_rpt_buf,
	u16			tx_rpt_len,
	u32			mac_id_valid_entry0,
	u32			mac_id_valid_entry1
)
{
	return;
}


#endif
