/******************************************************************************
 *
 * Name:	ski2c.h
 * Project:	Gigabit Ethernet Adapters, TWSI-Module
 * Version:	$Revision: 1.35 $
 * Date:	$Date: 2003/10/20 09:06:30 $
 * Purpose:	Defines to access Voltage and Temperature Sensor
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect.
 *	(C)Copyright 2002-2003 Marvell.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/*
 * SKI2C.H	contains all I2C specific defines
 */

#ifndef _SKI2C_H_
#define _SKI2C_H_

typedef struct  s_Sensor SK_SENSOR;

#include "h/skgei2c.h"

/*
 * Define the I2C events.
 */
#define SK_I2CEV_IRQ	1	/* IRQ happened Event */
#define SK_I2CEV_TIM	2	/* Timeout event */
#define SK_I2CEV_CLEAR	3	/* Clear MIB Values */

/*
 * Define READ and WRITE Constants.
 */
#define I2C_READ	0
#define I2C_WRITE	1
#define I2C_BURST	1
#define I2C_SINGLE	0

#define SKERR_I2C_E001		(SK_ERRBASE_I2C+0)
#define SKERR_I2C_E001MSG	"Sensor index unknown"
#define SKERR_I2C_E002		(SKERR_I2C_E001+1)
#define SKERR_I2C_E002MSG	"TWSI: transfer does not complete"
#define SKERR_I2C_E003		(SKERR_I2C_E002+1)
#define SKERR_I2C_E003MSG	"LM80: NAK on device send"
#define SKERR_I2C_E004		(SKERR_I2C_E003+1)
#define SKERR_I2C_E004MSG	"LM80: NAK on register send"
#define SKERR_I2C_E005		(SKERR_I2C_E004+1)
#define SKERR_I2C_E005MSG	"LM80: NAK on device (2) send"
#define SKERR_I2C_E006		(SKERR_I2C_E005+1)
#define SKERR_I2C_E006MSG	"Unknown event"
#define SKERR_I2C_E007		(SKERR_I2C_E006+1)
#define SKERR_I2C_E007MSG	"LM80 read out of state"
#define SKERR_I2C_E008		(SKERR_I2C_E007+1)
#define SKERR_I2C_E008MSG	"Unexpected sensor read completed"
#define SKERR_I2C_E009		(SKERR_I2C_E008+1)
#define SKERR_I2C_E009MSG	"WARNING: temperature sensor out of range"
#define SKERR_I2C_E010		(SKERR_I2C_E009+1)
#define SKERR_I2C_E010MSG	"WARNING: voltage sensor out of range"
#define SKERR_I2C_E011		(SKERR_I2C_E010+1)
#define SKERR_I2C_E011MSG	"ERROR: temperature sensor out of range"
#define SKERR_I2C_E012		(SKERR_I2C_E011+1)
#define SKERR_I2C_E012MSG	"ERROR: voltage sensor out of range"
#define SKERR_I2C_E013		(SKERR_I2C_E012+1)
#define SKERR_I2C_E013MSG	"ERROR: couldn't init sensor"
#define SKERR_I2C_E014		(SKERR_I2C_E013+1)
#define SKERR_I2C_E014MSG	"WARNING: fan sensor out of range"
#define SKERR_I2C_E015		(SKERR_I2C_E014+1)
#define SKERR_I2C_E015MSG	"ERROR: fan sensor out of range"
#define SKERR_I2C_E016		(SKERR_I2C_E015+1)
#define SKERR_I2C_E016MSG	"TWSI: active transfer does not complete"

/*
 * Define Timeout values
 */
#define SK_I2C_TIM_LONG		2000000L	/* 2 seconds */
#define SK_I2C_TIM_SHORT	 100000L	/* 100 milliseconds */
#define SK_I2C_TIM_WATCH	1000000L	/* 1 second */

/*
 * Define trap and error log hold times
 */
#ifndef	SK_SEN_ERR_TR_HOLD
#define SK_SEN_ERR_TR_HOLD		(4*SK_TICKS_PER_SEC)
#endif
#ifndef	SK_SEN_ERR_LOG_HOLD
#define SK_SEN_ERR_LOG_HOLD		(60*SK_TICKS_PER_SEC)
#endif
#ifndef	SK_SEN_WARN_TR_HOLD
#define SK_SEN_WARN_TR_HOLD		(15*SK_TICKS_PER_SEC)
#endif
#ifndef	SK_SEN_WARN_LOG_HOLD
#define SK_SEN_WARN_LOG_HOLD	(15*60*SK_TICKS_PER_SEC)
#endif

/*
 * Defines for SenType
 */
#define SK_SEN_UNKNOWN	0
#define SK_SEN_TEMP		1
#define SK_SEN_VOLT		2
#define SK_SEN_FAN		3

/*
 * Define for the SenErrorFlag
 */
#define SK_SEN_ERR_NOT_PRESENT	0	/* Error Flag: Sensor not present */
#define SK_SEN_ERR_OK			1	/* Error Flag: O.K. */
#define SK_SEN_ERR_WARN			2	/* Error Flag: Warning */
#define SK_SEN_ERR_ERR			3	/* Error Flag: Error */
#define SK_SEN_ERR_FAULTY		4	/* Error Flag: Faulty */

/*
 * Define the Sensor struct
 */
struct	s_Sensor {
	char	*SenDesc;			/* Description */
	int		SenType;			/* Voltage or Temperature */
	SK_I32	SenValue;			/* Current value of the sensor */
	SK_I32	SenThreErrHigh;		/* High error Threshhold of this sensor */
	SK_I32	SenThreWarnHigh;	/* High warning Threshhold of this sensor */
	SK_I32	SenThreErrLow;		/* Lower error Threshold of the sensor */
	SK_I32	SenThreWarnLow;		/* Lower warning Threshold of the sensor */
	int		SenErrFlag;			/* Sensor indicated an error */
	SK_BOOL	SenInit;			/* Is sensor initialized ? */
	SK_U64	SenErrCts;			/* Error trap counter */
	SK_U64	SenWarnCts;			/* Warning trap counter */
	SK_U64	SenBegErrTS;		/* Begin error timestamp */
	SK_U64	SenBegWarnTS;		/* Begin warning timestamp */
	SK_U64	SenLastErrTrapTS;	/* Last error trap timestamp */
	SK_U64	SenLastErrLogTS;	/* Last error log timestamp */
	SK_U64	SenLastWarnTrapTS;	/* Last warning trap timestamp */
	SK_U64	SenLastWarnLogTS;	/* Last warning log timestamp */
	int		SenState;			/* Sensor State (see HW specific include) */
	int		(*SenRead)(SK_AC *pAC, SK_IOC IoC, struct s_Sensor *pSen);
								/* Sensors read function */
	SK_U16	SenReg;				/* Register Address for this sensor */
	SK_U8	SenDev;				/* Device Selection for this sensor */
};

typedef	struct	s_I2c {
	SK_SENSOR	SenTable[SK_MAX_SENSORS];	/* Sensor Table */
	int			CurrSens;	/* Which sensor is currently queried */
	int			MaxSens;	/* Max. number of sensors */
	int			TimerMode;	/* Use the timer also to watch the state machine */
	int			InitLevel;	/* Initialized Level */
#ifndef SK_DIAG
	int			DummyReads;	/* Number of non-checked dummy reads */
	SK_TIMER	SenTimer;	/* Sensors timer */
#endif /* !SK_DIAG */
} SK_I2C;

extern int SkI2cInit(SK_AC *pAC, SK_IOC IoC, int Level);
extern int SkI2cWrite(SK_AC *pAC, SK_IOC IoC, SK_U32 Data, int Dev, int Size,
					   int Reg, int Burst);
extern int SkI2cReadSensor(SK_AC *pAC, SK_IOC IoC, SK_SENSOR *pSen);
#ifdef SK_DIAG
extern	SK_U32 SkI2cRead(SK_AC *pAC, SK_IOC IoC, int Dev, int Size, int Reg,
						 int Burst);
#else /* !SK_DIAG */
extern int SkI2cEvent(SK_AC *pAC, SK_IOC IoC, SK_U32 Event, SK_EVPARA Para);
extern void SkI2cWaitIrq(SK_AC *pAC, SK_IOC IoC);
extern void SkI2cIsr(SK_AC *pAC, SK_IOC IoC);
#endif /* !SK_DIAG */
#endif /* n_SKI2C_H */

