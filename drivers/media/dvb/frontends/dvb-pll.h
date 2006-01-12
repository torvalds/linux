/*
 * descriptions + helper functions for simple dvb plls.
 */

#ifndef __DVB_PLL_H__
#define __DVB_PLL_H__

struct dvb_pll_desc {
	char *name;
	u32  min;
	u32  max;
	void (*setbw)(u8 *buf, u32 freq, int bandwidth);
	int  count;
	struct {
		u32 limit;
		u32 offset;
		u32 stepsize;
		u8  cb1;
		u8  cb2;
	} entries[12];
};

extern struct dvb_pll_desc dvb_pll_thomson_dtt7579;
extern struct dvb_pll_desc dvb_pll_thomson_dtt759x;
extern struct dvb_pll_desc dvb_pll_thomson_dtt7610;
extern struct dvb_pll_desc dvb_pll_lg_z201;
extern struct dvb_pll_desc dvb_pll_microtune_4042;
extern struct dvb_pll_desc dvb_pll_thomson_dtt761x;
extern struct dvb_pll_desc dvb_pll_unknown_1;

extern struct dvb_pll_desc dvb_pll_tua6010xs;
extern struct dvb_pll_desc dvb_pll_env57h1xd5;
extern struct dvb_pll_desc dvb_pll_tua6034;
extern struct dvb_pll_desc dvb_pll_tdvs_tua6034;
extern struct dvb_pll_desc dvb_pll_tda665x;
extern struct dvb_pll_desc dvb_pll_fmd1216me;
extern struct dvb_pll_desc dvb_pll_tded4;

extern struct dvb_pll_desc dvb_pll_tuv1236d;
extern struct dvb_pll_desc dvb_pll_tdhu2;
extern struct dvb_pll_desc dvb_pll_tbmv30111in;
extern struct dvb_pll_desc dvb_pll_philips_sd1878_tda8261;

int dvb_pll_configure(struct dvb_pll_desc *desc, u8 *buf,
		      u32 freq, int bandwidth);

#endif
