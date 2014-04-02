#include "usb.h"
#include "scsiglue.h"
#include "transport.h"
/* #include "stdlib.h" */
/* #include "EUCR6SK.h" */
#include "smcommon.h"
#include "smil.h"

/* #include <stdio.h> */
/* #include <stdlib.h> */
/* #include <string.h> */
/* #include <dos.h> */
/* #include "EMCRIOS.h" */

/* CP0-CP5 code table */
static u8 ecctable[256] = {
0x00, 0x55, 0x56, 0x03, 0x59, 0x0C, 0x0F, 0x5A, 0x5A, 0x0F, 0x0C, 0x59, 0x03,
0x56, 0x55, 0x00, 0x65, 0x30, 0x33, 0x66, 0x3C, 0x69, 0x6A, 0x3F, 0x3F, 0x6A,
0x69, 0x3C, 0x66, 0x33, 0x30, 0x65, 0x66, 0x33, 0x30, 0x65, 0x3F, 0x6A, 0x69,
0x3C, 0x3C, 0x69, 0x6A, 0x3F, 0x65, 0x30, 0x33, 0x66, 0x03, 0x56, 0x55, 0x00,
0x5A, 0x0F, 0x0C, 0x59, 0x59, 0x0C, 0x0F, 0x5A, 0x00, 0x55, 0x56, 0x03, 0x69,
0x3C, 0x3F, 0x6A, 0x30, 0x65, 0x66, 0x33, 0x33, 0x66, 0x65, 0x30, 0x6A, 0x3F,
0x3C, 0x69, 0x0C, 0x59, 0x5A, 0x0F, 0x55, 0x00, 0x03, 0x56, 0x56, 0x03, 0x00,
0x55, 0x0F, 0x5A, 0x59, 0x0C, 0x0F, 0x5A, 0x59, 0x0C, 0x56, 0x03, 0x00, 0x55,
0x55, 0x00, 0x03, 0x56, 0x0C, 0x59, 0x5A, 0x0F, 0x6A, 0x3F, 0x3C, 0x69, 0x33,
0x66, 0x65, 0x30, 0x30, 0x65, 0x66, 0x33, 0x69, 0x3C, 0x3F, 0x6A, 0x6A, 0x3F,
0x3C, 0x69, 0x33, 0x66, 0x65, 0x30, 0x30, 0x65, 0x66, 0x33, 0x69, 0x3C, 0x3F,
0x6A, 0x0F, 0x5A, 0x59, 0x0C, 0x56, 0x03, 0x00, 0x55, 0x55, 0x00, 0x03, 0x56,
0x0C, 0x59, 0x5A, 0x0F, 0x0C, 0x59, 0x5A, 0x0F, 0x55, 0x00, 0x03, 0x56, 0x56,
0x03, 0x00, 0x55, 0x0F, 0x5A, 0x59, 0x0C, 0x69, 0x3C, 0x3F, 0x6A, 0x30, 0x65,
0x66, 0x33, 0x33, 0x66, 0x65, 0x30, 0x6A, 0x3F, 0x3C, 0x69, 0x03, 0x56, 0x55,
0x00, 0x5A, 0x0F, 0x0C, 0x59, 0x59, 0x0C, 0x0F, 0x5A, 0x00, 0x55, 0x56, 0x03,
0x66, 0x33, 0x30, 0x65, 0x3F, 0x6A, 0x69, 0x3C, 0x3C, 0x69, 0x6A, 0x3F, 0x65,
0x30, 0x33, 0x66, 0x65, 0x30, 0x33, 0x66, 0x3C, 0x69, 0x6A, 0x3F, 0x3F, 0x6A,
0x69, 0x3C, 0x66, 0x33, 0x30, 0x65, 0x00, 0x55, 0x56, 0x03, 0x59, 0x0C, 0x0F,
0x5A, 0x5A, 0x0F, 0x0C, 0x59, 0x03, 0x56, 0x55, 0x00
};

static void   trans_result(u8,   u8,   u8 *, u8 *);

#define BIT7        0x80
#define BIT6        0x40
#define BIT5        0x20
#define BIT4        0x10
#define BIT3        0x08
#define BIT2        0x04
#define BIT1        0x02
#define BIT0        0x01
#define BIT1BIT0    0x03
#define BIT23       0x00800000L
#define MASK_CPS    0x3f
#define CORRECTABLE 0x00555554L

/*
 * reg2; * LP14,LP12,LP10,...
 * reg3; * LP15,LP13,LP11,...
 * *ecc1; * LP15,LP14,LP13,...
 * *ecc2; * LP07,LP06,LP05,...
 */
static void trans_result(u8 reg2, u8 reg3, u8 *ecc1, u8 *ecc2)
{
	u8 a; /* Working for reg2,reg3 */
	u8 b; /* Working for ecc1,ecc2 */
	u8 i; /* For counting */

	a = BIT7; b = BIT7; /* 80h=10000000b */
	*ecc1 = *ecc2 = 0; /* Clear ecc1,ecc2 */
	for (i = 0; i < 4; ++i) {
		if ((reg3&a) != 0)
			*ecc1 |= b; /* LP15,13,11,9 -> ecc1 */
		b = b>>1; /* Right shift */
		if ((reg2&a) != 0)
			*ecc1 |= b; /* LP14,12,10,8 -> ecc1 */
		b = b>>1; /* Right shift */
		a = a>>1; /* Right shift */
	}

	b = BIT7; /* 80h=10000000b */
	for (i = 0; i < 4; ++i) {
		if ((reg3&a) != 0)
			*ecc2 |= b; /* LP7,5,3,1 -> ecc2 */
		b = b>>1; /* Right shift */
		if ((reg2&a) != 0)
			*ecc2 |= b; /* LP6,4,2,0 -> ecc2 */
		b = b>>1; /* Right shift */
		a = a>>1; /* Right shift */
	}
}

/*static void calculate_ecc(table,data,ecc1,ecc2,ecc3) */
/*
 * *table; * CP0-CP5 code table
 * *data; * DATA
 * *ecc1; * LP15,LP14,LP13,...
 * *ecc2; * LP07,LP06,LP05,...
 * *ecc3; * CP5,CP4,CP3,...,"1","1"
 */
void calculate_ecc(u8 *table, u8 *data, u8 *ecc1, u8 *ecc2, u8 *ecc3)
{
	u32  i;    /* For counting */
	u8 a;    /* Working for table */
	u8 reg1; /* D-all,CP5,CP4,CP3,... */
	u8 reg2; /* LP14,LP12,L10,... */
	u8 reg3; /* LP15,LP13,L11,... */

	reg1 = reg2 = reg3 = 0;   /* Clear parameter */
	for (i = 0; i < 256; ++i) {
		a = table[data[i]]; /* Get CP0-CP5 code from table */
		reg1 ^= (a&MASK_CPS); /* XOR with a */
		if ((a&BIT6) != 0) { /* If D_all(all bit XOR) = 1 */
			reg3 ^= (u8)i; /* XOR with counter */
			reg2 ^= ~((u8)i); /* XOR with inv. of counter */
		}
	}

	/* Trans LP14,12,10,... & LP15,13,11,... ->
						LP15,14,13,... & LP7,6,5,.. */
	trans_result(reg2, reg3, ecc1, ecc2);
	*ecc1 = ~(*ecc1); *ecc2 = ~(*ecc2); /* Inv. ecc2 & ecc3 */
	*ecc3 = ((~reg1)<<2)|BIT1BIT0; /* Make TEL format */
}

/*
 * *data; * DATA
 * *eccdata; * ECC DATA
 * ecc1; * LP15,LP14,LP13,...
 * ecc2; * LP07,LP06,LP05,...
 * ecc3; * CP5,CP4,CP3,...,"1","1"
 */
u8 correct_data(u8 *data, u8 *eccdata, u8 ecc1, u8 ecc2, u8 ecc3)
{
	u32 l; /* Working to check d */
	u32 d; /* Result of comparison */
	u32 i; /* For counting */
	u8 d1, d2, d3; /* Result of comparison */
	u8 a; /* Working for add */
	u8 add; /* Byte address of cor. DATA */
	u8 b; /* Working for bit */
	u8 bit; /* Bit address of cor. DATA */

	d1 = ecc1^eccdata[1]; d2 = ecc2^eccdata[0]; /* Compare LP's */
	d3 = ecc3^eccdata[2]; /* Compare CP's */
	d = ((u32)d1<<16) /* Result of comparison */
	+((u32)d2<<8)
	+(u32)d3;

	if (d == 0)
		return 0; /* If No error, return */

	if (((d^(d>>1))&CORRECTABLE) == CORRECTABLE) { /* If correctable */
		l = BIT23;
		add = 0; /* Clear parameter */
		a = BIT7;

		for (i = 0; i < 8; ++i) { /* Checking 8 bit */
			if ((d&l) != 0)
				add |= a; /* Make byte address from LP's */
			l >>= 2; a >>= 1; /* Right Shift */
		}

		bit = 0; /* Clear parameter */
		b = BIT2;
		for (i = 0; i < 3; ++i) { /* Checking 3 bit */
			if ((d&l) != 0)
				bit |= b; /* Make bit address from CP's */
			l >>= 2; b >>= 1; /* Right shift */
		}

		b = BIT0;
		data[add] ^= (b<<bit); /* Put corrected data */
		return 1;
	}

	i = 0; /* Clear count */
	d &= 0x00ffffffL; /* Masking */

	while (d) { /* If d=0 finish counting */
		if (d&BIT0)
			++i; /* Count number of 1 bit */
		d >>= 1; /* Right shift */
	}

	if (i == 1) { /* If ECC error */
		eccdata[1] = ecc1; eccdata[0] = ecc2; /* Put right ECC code */
		eccdata[2] = ecc3;
		return 2;
	}
	return 3; /* Uncorrectable error */
}

int _Correct_D_SwECC(u8 *buf, u8 *redundant_ecc, u8 *calculate_ecc)
{
	u32 err;

	err = correct_data(buf, redundant_ecc, *(calculate_ecc + 1),
			   *(calculate_ecc), *(calculate_ecc + 2));
	if (err == 1)
		memcpy(calculate_ecc, redundant_ecc, 3);

	if (err == 0 || err == 1 || err == 2)
		return 0;

	return -1;
}

void _Calculate_D_SwECC(u8 *buf, u8 *ecc)
{
	calculate_ecc(ecctable, buf, ecc+1, ecc+0, ecc+2);
}


