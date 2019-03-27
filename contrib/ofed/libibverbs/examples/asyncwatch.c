/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#define _GNU_SOURCE
#include <config.h>

#include <stdio.h>
#include <infiniband/endian.h>
#include <getopt.h>
#include <string.h>

#include <infiniband/verbs.h>

static const char *event_name_str(enum ibv_event_type event_type)
{
	switch (event_type) {
	case IBV_EVENT_DEVICE_FATAL:
		return "IBV_EVENT_DEVICE_FATAL";
	case IBV_EVENT_PORT_ACTIVE:
		return "IBV_EVENT_PORT_ACTIVE";
	case IBV_EVENT_PORT_ERR:
		return "IBV_EVENT_PORT_ERR";
	case IBV_EVENT_LID_CHANGE:
		return "IBV_EVENT_LID_CHANGE";
	case IBV_EVENT_PKEY_CHANGE:
		return "IBV_EVENT_PKEY_CHANGE";
	case IBV_EVENT_SM_CHANGE:
		return "IBV_EVENT_SM_CHANGE";
	case IBV_EVENT_CLIENT_REREGISTER:
		return "IBV_EVENT_CLIENT_REREGISTER";
	case IBV_EVENT_GID_CHANGE:
		return "IBV_EVENT_GID_CHANGE";

	case IBV_EVENT_CQ_ERR:
	case IBV_EVENT_QP_FATAL:
	case IBV_EVENT_QP_REQ_ERR:
	case IBV_EVENT_QP_ACCESS_ERR:
	case IBV_EVENT_COMM_EST:
	case IBV_EVENT_SQ_DRAINED:
	case IBV_EVENT_PATH_MIG:
	case IBV_EVENT_PATH_MIG_ERR:
	case IBV_EVENT_SRQ_ERR:
	case IBV_EVENT_SRQ_LIMIT_REACHED:
	case IBV_EVENT_QP_LAST_WQE_REACHED:
	default:
		return "unexpected";
	}
}

static void usage(const char *argv0)
{
	printf("Usage:\n");
	printf("  %s            start an asyncwatch process\n", argv0);
	printf("\n");
	printf("Options:\n");
	printf("  -d, --ib-dev=<dev>     use IB device <dev> (default first device found)\n");
	printf("  -h, --help             print a help text and exit\n");
}

int main(int argc, char *argv[])
{
	struct ibv_device **dev_list;
	struct ibv_context *context;
	struct ibv_async_event event;
	char   *ib_devname = NULL;
	int i = 0;

	/* Force line-buffering in case stdout is redirected */
	setvbuf(stdout, NULL, _IOLBF, 0);

	while (1) {
		int ret = 1;
		int c;
		static struct option long_options[] = {
			{ .name = "ib-dev",    .has_arg = 1, .val = 'd' },
			{ .name = "help",      .has_arg = 0, .val = 'h' },
			{}
		};

		c = getopt_long(argc, argv, "d:h", long_options, NULL);
		if (c == -1)
			break;
		switch (c) {
		case 'd':
			ib_devname = strdupa(optarg);
			break;
		case 'h':
			ret = 0;
			SWITCH_FALLTHROUGH;
		default:
			usage(argv[0]);
			return ret;
		}
	}
	dev_list = ibv_get_device_list(NULL);
	if (!dev_list) {
		perror("Failed to get IB devices list");
		return 1;
	}
	if (ib_devname) {
		for (; dev_list[i]; ++i) {
			if (!strcmp(ibv_get_device_name(dev_list[i]), ib_devname))
				break;
		}
	}

	if (!dev_list[i]) {
		fprintf(stderr, "IB device %s not found\n",
			ib_devname ? ib_devname : "");
		return 1;
	}

	context = ibv_open_device(dev_list[i]);
	if (!context) {
		fprintf(stderr, "Couldn't get context for %s\n",
			ibv_get_device_name(dev_list[i]));
		return 1;
	}

	printf("%s: async event FD %d\n",
	       ibv_get_device_name(dev_list[i]), context->async_fd);

	while (1) {
		if (ibv_get_async_event(context, &event))
			return 1;

		printf("  event_type %s (%d), port %d\n",
		       event_name_str(event.event_type),
		       event.event_type, event.element.port_num);

		ibv_ack_async_event(&event);
	}

	return 0;
}
