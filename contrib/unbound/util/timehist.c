/*
 * util/timehist.c - make histogram of time values.
 *
 * Copyright (c) 2007, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 *
 * This file contains functions to make a histogram of time values.
 */
#include "config.h"
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#include <sys/time.h>
#include <sys/types.h>
#include "util/timehist.h"
#include "util/log.h"

/** special timestwo operation for time values in histogram setup */
static void
timestwo(struct timeval* v)
{
#ifndef S_SPLINT_S
	if(v->tv_sec == 0 && v->tv_usec == 0) {
		v->tv_usec = 1;
		return;
	}
	v->tv_sec *= 2;
	v->tv_usec *= 2;
	if(v->tv_usec == 1024*1024) {
		/* nice values and easy to compute */
		v->tv_sec = 1;
		v->tv_usec = 0;
	}
#endif
}

/** do setup exponentially */
static void
dosetup(struct timehist* hist)
{
	struct timeval last;
	size_t i;
	memset(&last, 0, sizeof(last));
	for(i=0; i<hist->num; i++) {
		hist->buckets[i].lower = last;
		timestwo(&last);
		hist->buckets[i].upper = last;
		hist->buckets[i].count = 0;
	}
}

struct timehist* timehist_setup(void)
{
	struct timehist* hist = (struct timehist*)calloc(1, 
		sizeof(struct timehist));
	if(!hist)
		return NULL;
	hist->num = NUM_BUCKETS_HIST;
	hist->buckets = (struct th_buck*)calloc(hist->num, 
		sizeof(struct th_buck));
	if(!hist->buckets) {
		free(hist);
		return NULL;
	}
	/* setup the buckets */
	dosetup(hist);
	return hist;
}

void timehist_delete(struct timehist* hist)
{
	if(!hist)
		return;
	free(hist->buckets);
	free(hist);
}

void timehist_clear(struct timehist* hist)
{
	size_t i;
	for(i=0; i<hist->num; i++)
		hist->buckets[i].count = 0;
}

/** histogram compare of time values */
static int
timeval_smaller(const struct timeval* x, const struct timeval* y)
{
#ifndef S_SPLINT_S
	if(x->tv_sec < y->tv_sec)
		return 1;
	else if(x->tv_sec == y->tv_sec) {
		if(x->tv_usec <= y->tv_usec)
			return 1;
		else	return 0;
	}
	else	return 0;
#endif
}


void timehist_insert(struct timehist* hist, struct timeval* tv)
{
	size_t i;
	for(i=0; i<hist->num; i++) {
		if(timeval_smaller(tv, &hist->buckets[i].upper)) {
			hist->buckets[i].count++;
			return;
		}
	}
	/* dump in last bucket */
	hist->buckets[hist->num-1].count++;
}

void timehist_print(struct timehist* hist)
{
#ifndef S_SPLINT_S
	size_t i;
	for(i=0; i<hist->num; i++) {
		if(hist->buckets[i].count != 0) {
			printf("%4d.%6.6d %4d.%6.6d %u\n",
				(int)hist->buckets[i].lower.tv_sec,
				(int)hist->buckets[i].lower.tv_usec,
				(int)hist->buckets[i].upper.tv_sec,
				(int)hist->buckets[i].upper.tv_usec,
				(unsigned)hist->buckets[i].count);
		}
	}
#endif
}

void timehist_log(struct timehist* hist, const char* name)
{
#ifndef S_SPLINT_S
	size_t i;
	log_info("[25%%]=%g median[50%%]=%g [75%%]=%g",
		timehist_quartile(hist, 0.25),
		timehist_quartile(hist, 0.50),
		timehist_quartile(hist, 0.75));
	/*        0000.000000 0000.000000 0 */
	log_info("lower(secs) upper(secs) %s", name);
	for(i=0; i<hist->num; i++) {
		if(hist->buckets[i].count != 0) {
			log_info("%4d.%6.6d %4d.%6.6d %u",
				(int)hist->buckets[i].lower.tv_sec,
				(int)hist->buckets[i].lower.tv_usec,
				(int)hist->buckets[i].upper.tv_sec,
				(int)hist->buckets[i].upper.tv_usec,
				(unsigned)hist->buckets[i].count);
		}
	}
#endif
}

/** total number in histogram */
static size_t
timehist_count(struct timehist* hist)
{
	size_t i, res = 0;
	for(i=0; i<hist->num; i++)
		res += hist->buckets[i].count;
	return res;
}

double 
timehist_quartile(struct timehist* hist, double q)
{
	double lookfor, passed, res;
	double low = 0, up = 0;
	size_t i;
	if(!hist || hist->num == 0)
		return 0.;
	/* look for i'th element, interpolated */
	lookfor = (double)timehist_count(hist);
	if(lookfor < 4)
		return 0.; /* not enough elements for a good estimate */
	lookfor *= q;
	passed = 0;
	i = 0;
	while(i+1 < hist->num && 
		passed+(double)hist->buckets[i].count < lookfor) {
		passed += (double)hist->buckets[i++].count;
	}
	/* got the right bucket */
#ifndef S_SPLINT_S
	low = (double)hist->buckets[i].lower.tv_sec + 
		(double)hist->buckets[i].lower.tv_usec/1000000.;
	up = (double)hist->buckets[i].upper.tv_sec + 
		(double)hist->buckets[i].upper.tv_usec/1000000.;
#endif
	res = (lookfor - passed)*(up-low)/((double)hist->buckets[i].count);
	return low+res;
}

void 
timehist_export(struct timehist* hist, long long* array, size_t sz)
{
	size_t i;
	if(!hist) return;
	if(sz > hist->num)
		sz = hist->num;
	for(i=0; i<sz; i++)
		array[i] = (long long)hist->buckets[i].count;
}

void 
timehist_import(struct timehist* hist, long long* array, size_t sz)
{
	size_t i;
	if(!hist) return;
	if(sz > hist->num)
		sz = hist->num;
	for(i=0; i<sz; i++)
		hist->buckets[i].count = (size_t)array[i];
}
