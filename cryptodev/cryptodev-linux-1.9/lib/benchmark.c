/*
 * Copyright (C) 2011 Free Software Foundation, Inc.
 *
 * This file is part of GnuTLS.
 *
 * GnuTLS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * GnuTLS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include "benchmark.h"

int benchmark_must_finish = 0;

static void
alarm_handler (int signo)
{
  benchmark_must_finish = 1;
}

int start_benchmark(struct benchmark_st * st)
{
  int ret;
  struct itimerval timer;

  memset(st, 0, sizeof(*st));

  st->old_handler = signal (SIGPROF, alarm_handler);

  ret = gettimeofday (&st->start, NULL);
  if (ret < 0) {
    perror("gettimeofday");
    return -1;
  }

  benchmark_must_finish = 0;

  memset(&timer, 0, sizeof(timer));
  timer.it_value.tv_sec = 0;
  timer.it_value.tv_usec = 100*1000;

  ret = setitimer(ITIMER_PROF, &timer, NULL);
  if (ret < 0) {
    perror("setitimer");
    return -1;
  }

  return 0;
}

/* Returns -1 on error or 0 on success.
 * elapsed: the elapsed time in milliseconds
 */
int stop_benchmark(struct benchmark_st * st, unsigned long * elapsed)
{
  unsigned long msecs;
  struct timeval stop;
  int ret;

  signal(SIGPROF, st->old_handler);

  ret = gettimeofday (&stop, NULL);
  if (ret < 0)
    return -1;

  msecs = (stop.tv_sec * 1000 + stop.tv_usec / 1000 -
          (st->start.tv_sec * 1000 + st->start.tv_usec / (1000)));

  if (elapsed) *elapsed = msecs;

  return 0;
}

