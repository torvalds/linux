/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef CALIB_H
#define CALIB_H

#include "hw.h"

#define AR_PHY_CCA_FILTERWINDOW_LENGTH          5

/* Internal noise floor can vary by about 6db depending on the frequency */
#define ATH9K_NF_CAL_NOISE_THRESH		6

#define NUM_NF_READINGS       6
#define ATH9K_NF_CAL_HIST_MAX 5

struct ar5416IniArray {
	u32 *ia_array;
	u32 ia_rows;
	u32 ia_columns;
};

#define INIT_INI_ARRAY(iniarray, array, rows, columns) do {	\
		(iniarray)->ia_array = (u32 *)(array);		\
		(iniarray)->ia_rows = (rows);			\
		(iniarray)->ia_columns = (columns);		\
	} while (0)

#define INI_RA(iniarray, row, column) \
	(((iniarray)->ia_array)[(row) *	((iniarray)->ia_columns) + (column)])

#define INIT_CAL(_perCal) do {				\
		(_perCal)->calState = CAL_WAITING;	\
		(_perCal)->calNext = NULL;		\
	} while (0)

#define INSERT_CAL(_ahp, _perCal)					\
	do {								\
		if ((_ahp)->cal_list_last == NULL) {			\
			(_ahp)->cal_list =				\
				(_ahp)->cal_list_last = (_perCal);	\
			((_ahp)->cal_list_last)->calNext = (_perCal); \
		} else {						\
			((_ahp)->cal_list_last)->calNext = (_perCal); \
			(_ahp)->cal_list_last = (_perCal);		\
			(_perCal)->calNext = (_ahp)->cal_list;	\
		}							\
	} while (0)

enum ath9k_cal_state {
	CAL_INACTIVE,
	CAL_WAITING,
	CAL_RUNNING,
	CAL_DONE
};

#define MIN_CAL_SAMPLES     1
#define MAX_CAL_SAMPLES    64
#define INIT_LOG_COUNT      5
#define PER_MIN_LOG_COUNT   2
#define PER_MAX_LOG_COUNT  10

struct ath9k_percal_data {
	u32 calType;
	u32 calNumSamples;
	u32 calCountMax;
	void (*calCollect) (struct ath_hw *);
	void (*calPostProc) (struct ath_hw *, u8);
};

struct ath9k_cal_list {
	const struct ath9k_percal_data *calData;
	enum ath9k_cal_state calState;
	struct ath9k_cal_list *calNext;
};

struct ath9k_nfcal_hist {
	int16_t nfCalBuffer[ATH9K_NF_CAL_HIST_MAX];
	u8 currIndex;
	int16_t privNF;
	u8 invalidNFcount;
};

#define MAX_PACAL_SKIPCOUNT 8
struct ath9k_pacal_info{
	int32_t prev_offset;	/* Previous value of PA offset value */
	int8_t max_skipcount;	/* Max No. of times PACAL can be skipped */
	int8_t skipcount;	/* No. of times the PACAL to be skipped */
};

bool ath9k_hw_reset_calvalid(struct ath_hw *ah);
void ath9k_hw_start_nfcal(struct ath_hw *ah, bool update);
void ath9k_hw_loadnf(struct ath_hw *ah, struct ath9k_channel *chan);
bool ath9k_hw_getnf(struct ath_hw *ah, struct ath9k_channel *chan);
void ath9k_init_nfcal_hist_buffer(struct ath_hw *ah,
				  struct ath9k_channel *chan);
void ath9k_hw_bstuck_nfcal(struct ath_hw *ah);
void ath9k_hw_reset_calibration(struct ath_hw *ah,
				struct ath9k_cal_list *currCal);
s16 ath9k_hw_getchan_noise(struct ath_hw *ah, struct ath9k_channel *chan);


#endif /* CALIB_H */
