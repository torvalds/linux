// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Intel Corporation
 * Author: Johannes Berg <johannes@sipsolutions.net>
 */
#include <stdbool.h>
#include <os.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>
#include <kern_util.h>
#include <sys/select.h>
#include <stdio.h>
#include <sys/timerfd.h>
#include "rtc.h"

static int uml_rtc_irq_fds[2];

void uml_rtc_send_timetravel_alarm(void)
{
	unsigned long long c = 1;

	CATCH_EINTR(write(uml_rtc_irq_fds[1], &c, sizeof(c)));
}

int uml_rtc_start(bool timetravel)
{
	int err;

	if (timetravel) {
		int err = os_pipe(uml_rtc_irq_fds, 1, 1);
		if (err)
			goto fail;
	} else {
		uml_rtc_irq_fds[0] = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC);
		if (uml_rtc_irq_fds[0] < 0) {
			err = -errno;
			goto fail;
		}

		/* apparently timerfd won't send SIGIO, use workaround */
		sigio_broken(uml_rtc_irq_fds[0]);
		err = add_sigio_fd(uml_rtc_irq_fds[0]);
		if (err < 0) {
			close(uml_rtc_irq_fds[0]);
			goto fail;
		}
	}

	return uml_rtc_irq_fds[0];
fail:
	uml_rtc_stop(timetravel);
	return err;
}

int uml_rtc_enable_alarm(unsigned long long delta_seconds)
{
	struct itimerspec it = {
		.it_value = {
			.tv_sec = delta_seconds,
		},
	};

	if (timerfd_settime(uml_rtc_irq_fds[0], 0, &it, NULL))
		return -errno;
	return 0;
}

void uml_rtc_disable_alarm(void)
{
	uml_rtc_enable_alarm(0);
}

void uml_rtc_stop(bool timetravel)
{
	if (timetravel)
		os_close_file(uml_rtc_irq_fds[1]);
	else
		ignore_sigio_fd(uml_rtc_irq_fds[0]);
	os_close_file(uml_rtc_irq_fds[0]);
}
