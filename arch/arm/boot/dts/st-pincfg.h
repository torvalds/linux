#ifndef _ST_PINCFG_H_
#define _ST_PINCFG_H_

/* Alternate functions */
#define ALT1	1
#define ALT2	2
#define ALT3	3
#define ALT4	4
#define ALT5	5
#define ALT6	6
#define ALT7	7

/* Output enable */
#define OE			(1 << 27)
/* Pull Up */
#define PU			(1 << 26)
/* Open Drain */
#define OD			(1 << 25)
#define RT			(1 << 23)
#define INVERTCLK		(1 << 22)
#define CLKNOTDATA		(1 << 21)
#define DOUBLE_EDGE		(1 << 20)
#define CLK_A			(0 << 18)
#define CLK_B			(1 << 18)
#define CLK_C			(2 << 18)
#define CLK_D			(3 << 18)

/* User-frendly defines for Pin Direction */
		/* oe = 0, pu = 0, od = 0 */
#define IN			(0)
		/* oe = 0, pu = 1, od = 0 */
#define IN_PU			(PU)
		/* oe = 1, pu = 0, od = 0 */
#define OUT			(OE)
		/* oe = 1, pu = 0, od = 1 */
#define BIDIR			(OE | OD)
		/* oe = 1, pu = 1, od = 1 */
#define BIDIR_PU		(OE | PU | OD)

/* RETIME_TYPE */
/*
 * B Mode
 * Bypass retime with optional delay parameter
 */
#define BYPASS		(0)
/*
 * R0, R1, R0D, R1D modes
 * single-edge data non inverted clock, retime data with clk
 */
#define SE_NICLK_IO	(RT)
/*
 * RIV0, RIV1, RIV0D, RIV1D modes
 * single-edge data inverted clock, retime data with clk
 */
#define SE_ICLK_IO	(RT | INVERTCLK)
/*
 * R0E, R1E, R0ED, R1ED modes
 * double-edge data, retime data with clk
 */
#define DE_IO		(RT | DOUBLE_EDGE)
/*
 * CIV0, CIV1 modes with inverted clock
 * Retiming the clk pins will park clock & reduce the noise within the core.
 */
#define ICLK		(RT | CLKNOTDATA | INVERTCLK)
/*
 * CLK0, CLK1 modes with non-inverted clock
 * Retiming the clk pins will park clock & reduce the noise within the core.
 */
#define NICLK		(RT | CLKNOTDATA)
#endif /* _ST_PINCFG_H_ */
