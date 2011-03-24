#ifndef _TSLIB_H_
#define _TSLIB_H_
/*
 *  tslib/tslib.h
 *
 *  Copyright (C) 2001 Russell King.
 *
 * This file is placed under the LGPL.
 *
 *
 * Touch screen library interface definitions.
 */

#define NR_SAMPHISTLEN	4

#define VARIANCE_DELTA   10
#define DEJITTER_DELTA   100

struct ts_sample {
	int		x;
	int		y;
	unsigned int	pressure;
};

struct tslib_variance {
	int delta;
    struct ts_sample last;
    struct ts_sample noise;
	unsigned int flags;
};

struct ts_hist {
	int x;
	int y;
	unsigned int p;
};

struct tslib_dejitter {
	int delta;
	int x;
	int y;
	int down;
	int nr;
	int head;
	struct ts_hist hist[NR_SAMPHISTLEN];
};

struct tslib_info {
	int (*raw_read)(struct tslib_info *info, struct ts_sample *samp, int nr);
	struct tslib_variance *var;
	struct tslib_dejitter *djt;
};

int sqr(int x);

int tslib_init(struct tslib_info *info, void *raw_read);
void variance_clear(struct tslib_info *info);
int variance_read(struct tslib_info *info, struct ts_sample *samp, int nr);
int dejitter_read(struct tslib_info *info, struct ts_sample *samp, int nr);


#endif /* _TSLIB_H_ */
