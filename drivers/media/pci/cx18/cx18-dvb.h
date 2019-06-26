/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  cx18 functions for DVB support
 *
 *  Copyright (c) 2008 Steven Toth <stoth@linuxtv.org>
 */

#include "cx18-driver.h"

int cx18_dvb_register(struct cx18_stream *stream);
void cx18_dvb_unregister(struct cx18_stream *stream);
