#ifndef __TDA8261_H
#define __TDA8261_H

enum tda8261_step {
	TDA8261_STEP_2000 = 0,	/* 2000 kHz */
	TDA8261_STEP_1000,	/* 1000 kHz */
	TDA8261_STEP_500,	/*  500 kHz */
	TDA8261_STEP_250,	/*  250 kHz */
	TDA8261_STEP_125	/*  125 kHz */
};

struct tda8261_config {
//	u8			buf[16];
	u8			addr;
	enum tda8261_step	step_size;
};

/* move out from here! */
static const struct tda8261_config sd1878c_config = {
//	.name		= "SD1878C",
	.addr		= 0x60,
	.step_size	= TDA8261_STEP_1000 /* kHz */
};

#endif// __TDA8261_H
