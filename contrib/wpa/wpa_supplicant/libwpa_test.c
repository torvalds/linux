/*
 * libwpa_test - Test program for libwpa_client.* library linking
 * Copyright (c) 2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common/wpa_ctrl.h"

int main(int argc, char *argv[])
{
	struct wpa_ctrl *ctrl;

	ctrl = wpa_ctrl_open("foo");
	if (!ctrl)
		return -1;
	if (wpa_ctrl_attach(ctrl) == 0)
		wpa_ctrl_detach(ctrl);
	if (wpa_ctrl_pending(ctrl)) {
		char buf[10];
		size_t len;

		len = sizeof(buf);
		wpa_ctrl_recv(ctrl, buf, &len);
	}
	wpa_ctrl_close(ctrl);

	return 0;
}
