/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Windfarm PowerMac thermal control. Generic PID helpers
 *
 * (c) Copyright 2005 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 *
 * This is a pair of generic PID helpers that can be used by
 * control loops. One is the basic PID implementation, the
 * other one is more specifically tailored to the loops used
 * for CPU control with 2 input sample types (temp and power)
 */

/*
 * *** Simple PID ***
 */

#define WF_PID_MAX_HISTORY	32

/* This parameter array is passed to the PID algorithm. Currently,
 * we don't support changing parameters on the fly as it's not needed
 * but could be implemented (with necessary adjustment of the history
 * buffer
 */
struct wf_pid_param {
	int	interval;	/* Interval between samples in seconds */
	int	history_len;	/* Size of history buffer */
	int	additive;	/* 1: target relative to previous value */
	s32	gd, gp, gr;	/* PID gains */
	s32	itarget;	/* PID input target */
	s32	min,max;	/* min and max target values */
};

struct wf_pid_state {
	int	first;				/* first run of the loop */
	int	index; 				/* index of current sample */
	s32	target;				/* current target value */
	s32	samples[WF_PID_MAX_HISTORY];	/* samples history buffer */
	s32	errors[WF_PID_MAX_HISTORY];	/* error history buffer */

	struct wf_pid_param param;
};

extern void wf_pid_init(struct wf_pid_state *st, struct wf_pid_param *param);
extern s32 wf_pid_run(struct wf_pid_state *st, s32 sample);


/*
 * *** CPU PID ***
 */

#define WF_CPU_PID_MAX_HISTORY	32

/* This parameter array is passed to the CPU PID algorithm. Currently,
 * we don't support changing parameters on the fly as it's not needed
 * but could be implemented (with necessary adjustment of the history
 * buffer
 */
struct wf_cpu_pid_param {
	int	interval;	/* Interval between samples in seconds */
	int	history_len;	/* Size of history buffer */
	s32	gd, gp, gr;	/* PID gains */
	s32	pmaxadj;	/* PID max power adjust */
	s32	ttarget;	/* PID input target */
	s32	tmax;		/* PID input max */
	s32	min,max;	/* min and max target values */
};

struct wf_cpu_pid_state {
	int	first;				/* first run of the loop */
	int	index; 				/* index of current power */
	int	tindex; 			/* index of current temp */
	s32	target;				/* current target value */
	s32	last_delta;			/* last Tactual - Ttarget */
	s32	powers[WF_PID_MAX_HISTORY];	/* power history buffer */
	s32	errors[WF_PID_MAX_HISTORY];	/* error history buffer */
	s32	temps[2];			/* temp. history buffer */

	struct wf_cpu_pid_param param;
};

extern void wf_cpu_pid_init(struct wf_cpu_pid_state *st,
			    struct wf_cpu_pid_param *param);
extern s32 wf_cpu_pid_run(struct wf_cpu_pid_state *st, s32 power, s32 temp);
