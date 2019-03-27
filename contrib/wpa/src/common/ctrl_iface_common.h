/*
 * Common hostapd/wpa_supplicant ctrl iface code.
 * Copyright (c) 2002-2013, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2015, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */
#ifndef CONTROL_IFACE_COMMON_H
#define CONTROL_IFACE_COMMON_H

#include "utils/list.h"

/* Events enable bits (wpa_ctrl_dst::events) */
#define WPA_EVENT_RX_PROBE_REQUEST BIT(0)

/**
 * struct wpa_ctrl_dst - Data structure of control interface monitors
 *
 * This structure is used to store information about registered control
 * interface monitors into struct wpa_supplicant.
 */
struct wpa_ctrl_dst {
	struct dl_list list;
	struct sockaddr_storage addr;
	socklen_t addrlen;
	int debug_level;
	int errors;
	u32 events; /* WPA_EVENT_* bitmap */
};

void sockaddr_print(int level, const char *msg, struct sockaddr_storage *sock,
		    socklen_t socklen);

int ctrl_iface_attach(struct dl_list *ctrl_dst, struct sockaddr_storage *from,
		       socklen_t fromlen, const char *input);
int ctrl_iface_detach(struct dl_list *ctrl_dst, struct sockaddr_storage *from,
		      socklen_t fromlen);
int ctrl_iface_level(struct dl_list *ctrl_dst, struct sockaddr_storage *from,
		     socklen_t fromlen, const char *level);

#endif /* CONTROL_IFACE_COMMON_H */
