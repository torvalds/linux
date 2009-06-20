#ifndef _SJA2M16C_H
#define _SJA2M16C_H

#include "cpc.h"

#define BAUDRATE_TOLERANCE_PERCENT	1
#define SAMPLEPOINT_TOLERANCE_PERCENT	5
#define SAMPLEPOINT_UPPER_LIMIT		88

/* M16C parameters */
struct FIELD_C0CONR {
	unsigned int brp:4;
	unsigned int sam:1;
	unsigned int pr:3;
	unsigned int dummy:8;
};
struct FIELD_C1CONR {
	unsigned int ph1:3;
	unsigned int ph2:3;
	unsigned int sjw:2;
	unsigned int dummy:8;
};
typedef union C0CONR {
	unsigned char c0con;
	struct FIELD_C0CONR bc0con;
} C0CONR_T;
typedef union C1CONR {
	unsigned char c1con;
	struct FIELD_C1CONR bc1con;
} C1CONR_T;

#define SJA_TSEG1	((pParams->btr1 & 0x0f)+1)
#define SJA_TSEG2	(((pParams->btr1 & 0x70)>>4)+1)
#define SJA_BRP		((pParams->btr0 & 0x3f)+1)
#define SJA_SJW		((pParams->btr0 & 0xc0)>>6)
#define SJA_SAM		((pParams->btr1 & 0x80)>>7)
int baudrate_m16c(int clk, int brp, int pr, int ph1, int ph2);
int samplepoint_m16c(int brp, int pr, int ph1, int ph2);
int SJA1000_TO_M16C_BASIC_Params(CPC_MSG_T *pMsg);

#endif
