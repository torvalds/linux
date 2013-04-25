/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#ifndef __FRQCAL_H__
#define __FRQCAL_H__

/* */
/* The frequency calibration control */
/* */
typedef struct _FREQUENCY_CALIBRATION_CONTROL
{
	BOOLEAN bEnableFrequencyCalibration; /* Enable the frequency calibration algorithm */

	BOOLEAN bSkipFirstFrequencyCalibration; /* Avoid calibrating frequency at the time the STA is just link-up */
	BOOLEAN bApproachFrequency; /* Approach the frequency */
	CHAR AdaptiveFreqOffset; /* Adaptive frequency offset */
	CHAR LatestFreqOffsetOverBeacon; /* Latest frequency offset from the beacon */
	CHAR BeaconPhyMode; /* Latest frequency offset from the beacon */
	
} FREQUENCY_CALIBRATION_CONTROL, *PFREQUENCY_CALIBRATION_CONTROL;

/* */
/* Invalid frequency offset */
/* */
#define INVALID_FREQUENCY_OFFSET			-128

/* */
/* The upperbound/lowerbound of the frequency offset */
/* */
#define UPPERBOUND_OF_FREQUENCY_OFFSET		127
#define LOWERBOUND_OF_FREQUENCY_OFFSET	-127


/*#ifdef RT5390 */
/* */
/* The trigger point of the high/low frequency */
/* */
#define HIGH_FREQUENCY_TRIGGER_POINT_OFDM		20
#define LOW_FREQUENCY_TRIGGER_POINT_OFDM		-20
#define HIGH_FREQUENCY_TRIGGER_POINT_CCK		4
#define LOW_FREQUENCY_TRIGGER_POINT_CCK		-4

/* */
/* The trigger point of decreasng/increasing the frequency offset */
/* */
#define DECREASE_FREQUENCY_OFFSET_OFDM			10
#define INCREASE_FREQUENCY_OFFSET_OFDM			-10
#define DECREASE_FREQUENCY_OFFSET_CCK			2
#define INCREASE_FREQUENCY_OFFSET_CCK			-2
/*#endif // RT5390 */
/* */
/* The trigger point of decreasng/increasing the frequency offset */
/* */
#define DECREASE_FREQUENCY_OFFSET			3
#define INCREASE_FREQUENCY_OFFSET			-3

/* */
/* Frequency calibration period */
/* */

#define FREQUENCY_CALIBRATION_PERIOD		100

#endif /* __FRQCAL_H__ */
