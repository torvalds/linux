/*
 * util/timehist.h - make histogram of time values.
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

#ifndef UTIL_TIMEHIST_H
#define UTIL_TIMEHIST_H

/** Number of buckets in a histogram */
#define NUM_BUCKETS_HIST 40

/**
 * Bucket of time history information
 */
struct th_buck {
	/** lower bound */
	struct timeval lower;
	/** upper bound */
	struct timeval upper;
	/** number of items */
	size_t count;
};

/**
 * Keep histogram of time values.
 */
struct timehist {
	/** number of buckets */
	size_t num;
	/** bucket array */
	struct th_buck* buckets;
};

/** 
 * Setup a histogram, default
 * @return histogram or NULL on malloc failure.
 */
struct timehist* timehist_setup(void);

/**
 * Delete histogram
 * @param hist: to delete
 */
void timehist_delete(struct timehist* hist);

/**
 * Clear histogram
 * @param hist: to clear all data from
 */
void timehist_clear(struct timehist* hist);

/**
 * Add time value to histogram.
 * @param hist: histogram
 * @param tv: time value
 */
void timehist_insert(struct timehist* hist, struct timeval* tv);

/**
 * Find time value for given quartile, such as 0.25, 0.50, 0.75.
 * The looks up the value for the i-th element in the sorted list of time 
 * values, as approximated using the histogram.
 * @param hist: histogram. Interpolated information is used from it.
 * @param q: quartile, 0.50 results in the median. Must be >0 and <1.
 * @return: the time in seconds for that percentage.
 */
double timehist_quartile(struct timehist* hist, double q);

/**
 * Printout histogram
 * @param hist: histogram
 */
void timehist_print(struct timehist* hist);

/**
 * Log histogram, print it to the logfile.
 * @param hist: histogram
 * @param name: the name of the value column
 */
void timehist_log(struct timehist* hist, const char* name);

/**
 * Export histogram to an array.
 * @param hist: histogram
 * @param array: the array to export to.
 * @param sz: number of items in array.
 */
void timehist_export(struct timehist* hist, long long* array, size_t sz);

/**
 * Import histogram from an array.
 * @param hist: histogram
 * @param array: the array to import from.
 * @param sz: number of items in array.
 */
void timehist_import(struct timehist* hist, long long* array, size_t sz);

#endif /* UTIL_TIMEHIST_H */
