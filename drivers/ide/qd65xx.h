/*
 * Copyright (c) 2000	Linus Torvalds & authors
 */

/*
 * Authors:	Petr Soucek <petr@ryston.cz>
 * 		Samuel Thibault <samuel.thibault@ens-lyon.org>
 */

/* truncates a in [b,c] */
#define IDE_IN(a,b,c)   ( ((a)<(b)) ? (b) : ( (a)>(c) ? (c) : (a)) )

#define IDE_IMPLY(a,b)	((!(a)) || (b))

#define QD_TIM1_PORT		(base)
#define QD_CONFIG_PORT		(base+0x01)
#define QD_TIM2_PORT		(base+0x02)
#define QD_CONTROL_PORT		(base+0x03)

#define QD_CONFIG_IDE_BASEPORT	0x01
#define QD_CONFIG_BASEPORT	0x02
#define QD_CONFIG_ID3		0x04
#define QD_CONFIG_DISABLED	0x08
#define QD_CONFIG_QD6500	0xc0
#define QD_CONFIG_QD6580_A	0xa0
#define QD_CONFIG_QD6580_B	0x50

#define QD_CONTR_SEC_DISABLED	0x01

#define QD_ID3			((config & QD_CONFIG_ID3)!=0)

#define QD_CONFIG(hwif)		((hwif)->config_data & 0x00ff)

#define QD_TIMING(drive)	(u8)(((drive)->drive_data) & 0x00ff)
#define QD_TIMREG(drive)	(u8)((((drive)->drive_data) & 0xff00) >> 8)

#define QD6500_DEF_DATA		((QD_TIM1_PORT<<8) | (QD_ID3 ? 0x0c : 0x08))
#define QD6580_DEF_DATA		((QD_TIM1_PORT<<8) | (QD_ID3 ? 0x0a : 0x00))
#define QD6580_DEF_DATA2	((QD_TIM2_PORT<<8) | (QD_ID3 ? 0x0a : 0x00))
#define QD_DEF_CONTR		(0x40 | ((control & 0x02) ? 0x9f : 0x1f))

#define QD_TESTVAL		0x19	/* safe value */

/* Drive specific timing taken from DOS driver v3.7 */

static struct qd65xx_timing_s {
	s8	offset;   /* ofset from the beginning of Model Number" */
	char	model[4];    /* 4 chars from Model number, no conversion */
	s16	active;   /* active time */
	s16	recovery; /* recovery time */
} qd65xx_timing [] = {
	{ 30, "2040", 110, 225 },  /* Conner CP30204			*/
	{ 30, "2045", 135, 225 },  /* Conner CP30254			*/
	{ 30, "1040", 155, 325 },  /* Conner CP30104			*/
	{ 30, "1047", 135, 265 },  /* Conner CP30174			*/
	{ 30, "5344", 135, 225 },  /* Conner CP3544			*/
	{ 30, "01 4", 175, 405 },  /* Conner CP-3104			*/
	{ 27, "C030", 175, 375 },  /* Conner CP3000			*/
	{  8, "PL42", 110, 295 },  /* Quantum LP240			*/
	{  8, "PL21", 110, 315 },  /* Quantum LP120			*/
	{  8, "PL25", 175, 385 },  /* Quantum LP52			*/
	{  4, "PA24", 110, 285 },  /* WD Piranha SP4200			*/
	{  6, "2200", 110, 260 },  /* WD Caviar AC2200			*/
	{  6, "3204", 110, 235 },  /* WD Caviar AC2340			*/
	{  6, "1202", 110, 265 },  /* WD Caviar AC2120			*/
	{  0, "DS3-", 135, 315 },  /* Teac SD340			*/
	{  8, "KM32", 175, 355 },  /* Toshiba MK234			*/
	{  2, "53A1", 175, 355 },  /* Seagate ST351A			*/
	{  2, "4108", 175, 295 },  /* Seagate ST1480A			*/
	{  2, "1344", 175, 335 },  /* Seagate ST3144A			*/
	{  6, "7 12", 110, 225 },  /* Maxtor 7213A			*/
	{ 30, "02F4", 145, 295 },  /* Conner 3204F			*/
	{  2, "1302", 175, 335 },  /* Seagate ST3120A			*/
	{  2, "2334", 145, 265 },  /* Seagate ST3243A			*/
	{  2, "2338", 145, 275 },  /* Seagate ST3283A			*/
	{  2, "3309", 145, 275 },  /* Seagate ST3390A			*/
	{  2, "5305", 145, 275 },  /* Seagate ST3550A			*/
	{  2, "4100", 175, 295 },  /* Seagate ST1400A			*/
	{  2, "4110", 175, 295 },  /* Seagate ST1401A			*/
	{  2, "6300", 135, 265 },  /* Seagate ST3600A			*/
	{  2, "5300", 135, 265 },  /* Seagate ST3500A			*/
	{  6, "7 31", 135, 225 },  /* Maxtor 7131 AT			*/
	{  6, "7 43", 115, 265 },  /* Maxtor 7345 AT			*/
	{  6, "7 42", 110, 255 },  /* Maxtor 7245 AT			*/
	{  6, "3 04", 135, 265 },  /* Maxtor 340 AT			*/
	{  6, "61 0", 135, 285 },  /* WD AC160				*/
	{  6, "1107", 135, 235 },  /* WD AC1170				*/
	{  6, "2101", 110, 220 },  /* WD AC1210				*/
	{  6, "4202", 135, 245 },  /* WD AC2420				*/
	{  6, "41 0", 175, 355 },  /* WD Caviar 140			*/
	{  6, "82 0", 175, 355 },  /* WD Caviar 280			*/
	{  8, "PL01", 175, 375 },  /* Quantum LP105			*/
	{  8, "PL25", 110, 295 },  /* Quantum LP525			*/
	{ 10, "4S 2", 175, 385 },  /* Quantum ELS42			*/
	{ 10, "8S 5", 175, 385 },  /* Quantum ELS85			*/
	{ 10, "1S72", 175, 385 },  /* Quantum ELS127			*/
	{ 10, "1S07", 175, 385 },  /* Quantum ELS170			*/
	{  8, "ZE42", 135, 295 },  /* Quantum EZ240			*/
	{  8, "ZE21", 175, 385 },  /* Quantum EZ127			*/
	{  8, "ZE58", 175, 385 },  /* Quantum EZ85			*/
	{  8, "ZE24", 175, 385 },  /* Quantum EZ42			*/
	{ 27, "C036", 155, 325 },  /* Conner CP30064			*/
	{ 27, "C038", 155, 325 },  /* Conner CP30084			*/
	{  6, "2205", 110, 255 },  /* WDC AC2250			*/
	{  2, " CHA", 140, 415 },  /* WDC AH series; WDC AH260, WDC	*/
	{  2, " CLA", 140, 415 },  /* WDC AL series: WDC AL2120, 2170,	*/
	{  4, "UC41", 140, 415 },  /* WDC CU140				*/
	{  6, "1207", 130, 275 },  /* WDC AC2170			*/
	{  6, "2107", 130, 275 },  /* WDC AC1270			*/
	{  6, "5204", 130, 275 },  /* WDC AC2540			*/
	{ 30, "3004", 110, 235 },  /* Conner CP30340			*/
	{ 30, "0345", 135, 255 },  /* Conner CP30544			*/
	{ 12, "12A3", 175, 320 },  /* MAXTOR LXT-213A			*/
	{ 12, "43A0", 145, 240 },  /* MAXTOR LXT-340A			*/
	{  6, "7 21", 180, 290 },  /* Maxtor 7120 AT			*/
	{  6, "7 71", 135, 240 },  /* Maxtor 7170 AT			*/
	{ 12, "45\0000", 110, 205 },   /* MAXTOR MXT-540		*/
	{  8, "PL11", 180, 290 },  /* QUANTUM LP110A			*/
	{  8, "OG21", 150, 275 },  /* QUANTUM GO120			*/
	{ 12, "42A5", 175, 320 },  /* MAXTOR LXT-245A			*/
	{  2, "2309", 175, 295 },  /* ST3290A				*/
	{  2, "3358", 180, 310 },  /* ST3385A				*/
	{  2, "6355", 180, 310 },  /* ST3655A				*/
	{  2, "1900", 175, 270 },  /* ST9100A				*/
	{  2, "1954", 175, 270 },  /* ST9145A				*/
	{  2, "1909", 175, 270 },  /* ST9190AG				*/
	{  2, "2953", 175, 270 },  /* ST9235A				*/
	{  2, "1359", 175, 270 },  /* ST3195A				*/
	{ 24, "3R11", 175, 290 },  /* ALPS ELECTRIC Co.,LTD, DR311C	*/
	{  0, "2M26", 175, 215 },  /* M262XT-0Ah			*/
	{  4, "2253", 175, 300 },  /* HP C2235A				*/
	{  4, "-32A", 145, 245 },  /* H3133-A2				*/
	{ 30, "0326", 150, 270 },  /* Samsung Electronics 120MB		*/
	{ 30, "3044", 110, 195 },  /* Conner CFA340A			*/
	{ 30, "43A0", 110, 195 },  /* Conner CFA340A			*/
	{ -1, "    ", 175, 415 }   /* unknown disk name			*/
};
