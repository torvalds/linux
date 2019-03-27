/*
 * $Id: duration.h 4341 2011-01-31 15:21:09Z matthijs $
 *
 * Copyright (c) 2009 NLNet Labs. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 *
 * This file is copied from the OpenDNSSEC source repository
 * and only slightly adapted to make it fit.
 */

/**
 *
 * Durations.
 */

#ifndef LDNS_DURATION_H
#define LDNS_DURATION_H

#include <stdint.h>
#include <time.h>

/**
 * Duration.
 *
 */
typedef struct ldns_duration_struct ldns_duration_type;
struct ldns_duration_struct
{
    time_t years;
    time_t months;
    time_t weeks;
    time_t days;
    time_t hours;
    time_t minutes;
    time_t seconds;
};

/**
 * Create a new 'instant' duration.
 * \return ldns_duration_type* created duration
 *
 */
ldns_duration_type* ldns_duration_create(void);

/**
 * Compare durations.
 * \param[in] d1 one duration
 * \param[in] d2 another duration
 * \return int 0 if equal, -1 if d1 < d2, 1 if d2 < d1
 *
 */
int ldns_duration_compare(const ldns_duration_type* d1, const ldns_duration_type* d2);

/**
 * Create a duration from string.
 * \param[in] str string-format duration
 * \return ldns_duration_type* created duration
 *
 */
ldns_duration_type* ldns_duration_create_from_string(const char* str);

/**
 * Convert a duration to a string.
 * \param[in] duration duration to be converted
 * \return char* string-format duration
 *
 */
char* ldns_duration2string(const ldns_duration_type* duration);

/**
 * Convert a duration to a time.
 * \param[in] duration duration to be converted
 * \return time_t time-format duration
 *
 */
time_t ldns_duration2time(const ldns_duration_type* duration);

/**
 * Clean up duration.
 * \param[in] duration duration to be cleaned up
 *
 */
void ldns_duration_cleanup(ldns_duration_type* duration);

#endif /* LDNS_DURATION_H */
