/*
 * Windfarm PowerMac thermal control. Generic PID helpers
 *
 * (c) Copyright 2005 Benjamin Herrenschmidt, IBM Corp.
 *                    <benh@kernel.crashing.org>
 *
 * Released under the term of the GNU GPL v2.
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/module.h>

#include "windfarm_pid.h"

#undef DEBUG

#ifdef DEBUG
#define DBG(args...)	printk(args)
#else
#define DBG(args...)	do { } while(0)
#endif

void wf_pid_init(struct wf_pid_state *st, struct wf_pid_param *param)
{
	memset(st, 0, sizeof(struct wf_pid_state));
	st->param = *param;
	st->first = 1;
}
EXPORT_SYMBOL_GPL(wf_pid_init);

s32 wf_pid_run(struct wf_pid_state *st, s32 new_sample)
{
	s64	error, integ, deriv;
	s32	target;
	int	i, hlen = st->param.history_len;

	/* Calculate error term */
	error = new_sample - st->param.itarget;

	/* Get samples into our history buffer */
	if (st->first) {
		for (i = 0; i < hlen; i++) {
			st->samples[i] = new_sample;
			st->errors[i] = error;
		}
		st->first = 0;
		st->index = 0;
	} else {
		st->index = (st->index + 1) % hlen;
		st->samples[st->index] = new_sample;
		st->errors[st->index] = error;
	}

	/* Calculate integral term */
	for (i = 0, integ = 0; i < hlen; i++)
		integ += st->errors[(st->index + hlen - i) % hlen];
	integ *= st->param.interval;

	/* Calculate derivative term */
	deriv = st->errors[st->index] -
		st->errors[(st->index + hlen - 1) % hlen];
	deriv /= st->param.interval;

	/* Calculate target */
	target = (s32)((integ * (s64)st->param.gr + deriv * (s64)st->param.gd +
		  error * (s64)st->param.gp) >> 36);
	if (st->param.additive)
		target += st->target;
	target = max(target, st->param.min);
	target = min(target, st->param.max);
	st->target = target;

	return st->target;
}
EXPORT_SYMBOL_GPL(wf_pid_run);

void wf_cpu_pid_init(struct wf_cpu_pid_state *st,
		     struct wf_cpu_pid_param *param)
{
	memset(st, 0, sizeof(struct wf_cpu_pid_state));
	st->param = *param;
	st->first = 1;
}
EXPORT_SYMBOL_GPL(wf_cpu_pid_init);

s32 wf_cpu_pid_run(struct wf_cpu_pid_state *st, s32 new_power, s32 new_temp)
{
	s64	integ, deriv, prop;
	s32	error, target, sval, adj;
	int	i, hlen = st->param.history_len;

	/* Calculate error term */
	error = st->param.pmaxadj - new_power;

	/* Get samples into our history buffer */
	if (st->first) {
		for (i = 0; i < hlen; i++) {
			st->powers[i] = new_power;
			st->errors[i] = error;
		}
		st->temps[0] = st->temps[1] = new_temp;
		st->first = 0;
		st->index = st->tindex = 0;
	} else {
		st->index = (st->index + 1) % hlen;
		st->powers[st->index] = new_power;
		st->errors[st->index] = error;
		st->tindex = (st->tindex + 1) % 2;
		st->temps[st->tindex] = new_temp;
	}

	/* Calculate integral term */
	for (i = 0, integ = 0; i < hlen; i++)
		integ += st->errors[(st->index + hlen - i) % hlen];
	integ *= st->param.interval;
	integ *= st->param.gr;
	sval = st->param.tmax - (s32)(integ >> 20);
	adj = min(st->param.ttarget, sval);

	DBG("integ: %lx, sval: %lx, adj: %lx\n", integ, sval, adj);

	/* Calculate derivative term */
	deriv = st->temps[st->tindex] -
		st->temps[(st->tindex + 2 - 1) % 2];
	deriv /= st->param.interval;
	deriv *= st->param.gd;

	/* Calculate proportional term */
	prop = st->last_delta = (new_temp - adj);
	prop *= st->param.gp;

	DBG("deriv: %lx, prop: %lx\n", deriv, prop);

	/* Calculate target */
	target = st->target + (s32)((deriv + prop) >> 36);
	target = max(target, st->param.min);
	target = min(target, st->param.max);
	st->target = target;

	return st->target;
}
EXPORT_SYMBOL_GPL(wf_cpu_pid_run);

MODULE_AUTHOR("Benjamin Herrenschmidt <benh@kernel.crashing.org>");
MODULE_DESCRIPTION("PID algorithm for PowerMacs thermal control");
MODULE_LICENSE("GPL");
