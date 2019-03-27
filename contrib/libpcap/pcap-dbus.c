/*
 * Copyright (c) 2012 Jakub Zawadzki
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <time.h>
#include <sys/time.h>

#include <dbus/dbus.h>

#include "pcap-int.h"
#include "pcap-dbus.h"

/*
 * Private data for capturing on D-Bus.
 */
struct pcap_dbus {
	DBusConnection *conn;
	u_int	packets_read;	/* count of packets read */
};

static int
dbus_read(pcap_t *handle, int max_packets _U_, pcap_handler callback, u_char *user)
{
	struct pcap_dbus *handlep = handle->priv;

	struct pcap_pkthdr pkth;
	DBusMessage *message;

	char *raw_msg;
	int raw_msg_len;

	int count = 0;

	message = dbus_connection_pop_message(handlep->conn);

	while (!message) {
		/* XXX handle->opt.timeout = timeout_ms; */
		if (!dbus_connection_read_write(handlep->conn, 100)) {
			pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE, "Connection closed");
			return -1;
		}

		if (handle->break_loop) {
			handle->break_loop = 0;
			return -2;
		}

		message = dbus_connection_pop_message(handlep->conn);
	}

	if (dbus_message_is_signal(message, DBUS_INTERFACE_LOCAL, "Disconnected")) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE, "Disconnected");
		return -1;
	}

	if (dbus_message_marshal(message, &raw_msg, &raw_msg_len)) {
		pkth.caplen = pkth.len = raw_msg_len;
		/* pkth.caplen = min (payload_len, handle->snapshot); */

		gettimeofday(&pkth.ts, NULL);
		if (handle->fcode.bf_insns == NULL ||
		    bpf_filter(handle->fcode.bf_insns, (u_char *)raw_msg, pkth.len, pkth.caplen)) {
			handlep->packets_read++;
			callback(user, &pkth, (u_char *)raw_msg);
			count++;
		}

		dbus_free(raw_msg);
	}
	return count;
}

static int
dbus_write(pcap_t *handle, const void *buf, size_t size)
{
	/* XXX, not tested */
	struct pcap_dbus *handlep = handle->priv;

	DBusError error = DBUS_ERROR_INIT;
	DBusMessage *msg;

	if (!(msg = dbus_message_demarshal(buf, size, &error))) {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE, "dbus_message_demarshal() failed: %s", error.message);
		dbus_error_free(&error);
		return -1;
	}

	dbus_connection_send(handlep->conn, msg, NULL);
	dbus_connection_flush(handlep->conn);

	dbus_message_unref(msg);
	return 0;
}

static int
dbus_stats(pcap_t *handle, struct pcap_stat *stats)
{
	struct pcap_dbus *handlep = handle->priv;

	stats->ps_recv = handlep->packets_read;
	stats->ps_drop = 0;
	stats->ps_ifdrop = 0;
	return 0;
}

static void
dbus_cleanup(pcap_t *handle)
{
	struct pcap_dbus *handlep = handle->priv;

	dbus_connection_unref(handlep->conn);

	pcap_cleanup_live_common(handle);
}

/*
 * We don't support non-blocking mode.  I'm not sure what we'd
 * do to support it and, given that we don't support select()/
 * poll()/epoll_wait()/kevent() etc., it probably doesn't
 * matter.
 */
static int
dbus_getnonblock(pcap_t *p)
{
	pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
	    "Non-blocking mode isn't supported for capturing on D-Bus");
	return (-1);
}

static int
dbus_setnonblock(pcap_t *p, int nonblock _U_)
{
	pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
	    "Non-blocking mode isn't supported for capturing on D-Bus");
	return (-1);
}

static int
dbus_activate(pcap_t *handle)
{
#define EAVESDROPPING_RULE "eavesdrop=true,"

	static const char *rules[] = {
		EAVESDROPPING_RULE "type='signal'",
		EAVESDROPPING_RULE "type='method_call'",
		EAVESDROPPING_RULE "type='method_return'",
		EAVESDROPPING_RULE "type='error'",
	};

	#define N_RULES sizeof(rules)/sizeof(rules[0])

	struct pcap_dbus *handlep = handle->priv;
	const char *dev = handle->opt.device;

	DBusError error = DBUS_ERROR_INIT;
	u_int i;

	if (strcmp(dev, "dbus-system") == 0) {
		if (!(handlep->conn = dbus_bus_get(DBUS_BUS_SYSTEM, &error))) {
			pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE, "Failed to get system bus: %s", error.message);
			dbus_error_free(&error);
			return PCAP_ERROR;
		}

	} else if (strcmp(dev, "dbus-session") == 0) {
		if (!(handlep->conn = dbus_bus_get(DBUS_BUS_SESSION, &error))) {
			pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE, "Failed to get session bus: %s", error.message);
			dbus_error_free(&error);
			return PCAP_ERROR;
		}

	} else if (strncmp(dev, "dbus://", 7) == 0) {
		const char *addr = dev + 7;

		if (!(handlep->conn = dbus_connection_open(addr, &error))) {
			pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE, "Failed to open connection to: %s: %s", addr, error.message);
			dbus_error_free(&error);
			return PCAP_ERROR;
		}

		if (!dbus_bus_register(handlep->conn, &error)) {
			pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE, "Failed to register bus %s: %s\n", addr, error.message);
			dbus_error_free(&error);
			return PCAP_ERROR;
		}

	} else {
		pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE, "Can't get bus address from %s", handle->opt.device);
		return PCAP_ERROR;
	}

	/* Initialize some components of the pcap structure. */
	handle->bufsize = 0;
	handle->offset = 0;
	handle->linktype = DLT_DBUS;
	handle->read_op = dbus_read;
	handle->inject_op = dbus_write;
	handle->setfilter_op = install_bpf_program; /* XXX, later add support for dbus_bus_add_match() */
	handle->setdirection_op = NULL;
	handle->set_datalink_op = NULL;      /* can't change data link type */
	handle->getnonblock_op = dbus_getnonblock;
	handle->setnonblock_op = dbus_setnonblock;
	handle->stats_op = dbus_stats;
	handle->cleanup_op = dbus_cleanup;

#ifndef _WIN32
	/*
	 * Unfortunately, trying to do a select()/poll()/epoll_wait()/
	 * kevent()/etc. on a D-Bus connection isn't a simple
	 * case of "give me an FD on which to wait".
	 *
	 * Apparently, you have to register "add watch", "remove watch",
	 * and "toggle watch" functions with
	 * dbus_connection_set_watch_functions(),
	 * keep a *set* of FDs, add to that set in the "add watch"
	 * function, subtract from it in the "remove watch" function,
	 * and either add to or subtract from that set in the "toggle
	 * watch" function, and do the wait on *all* of the FDs in the
	 * set.  (Yes, you need the "toggle watch" function, so that
	 * the main loop doesn't itself need to check for whether
	 * a given watch is enabled or disabled - most libpcap programs
	 * know nothing about D-Bus and shouldn't *have* to know anything
	 * about D-Bus other than how to decode D-Bus messages.)
	 *
	 * Implementing that would require considerable changes in
	 * the way libpcap exports "selectable FDs" to its client.
	 * Until that's done, we just say "you can't do that".
	 */
	handle->selectable_fd = handle->fd = -1;
#endif

	if (handle->opt.rfmon) {
		/*
		 * Monitor mode doesn't apply to dbus connections.
		 */
		dbus_cleanup(handle);
		return PCAP_ERROR_RFMON_NOTSUP;
	}

	/*
	 * Turn a negative snapshot value (invalid), a snapshot value of
	 * 0 (unspecified), or a value bigger than the normal maximum
	 * value, into the maximum message length for D-Bus (128MB).
	 */
	if (handle->snapshot <= 0 || handle->snapshot > 134217728)
		handle->snapshot = 134217728;

	/* dbus_connection_set_max_message_size(handlep->conn, handle->snapshot); */
	if (handle->opt.buffer_size != 0)
		dbus_connection_set_max_received_size(handlep->conn, handle->opt.buffer_size);

	for (i = 0; i < N_RULES; i++) {
		dbus_bus_add_match(handlep->conn, rules[i], &error);
		if (dbus_error_is_set(&error)) {
			dbus_error_free(&error);

			/* try without eavesdrop */
			dbus_bus_add_match(handlep->conn, rules[i] + strlen(EAVESDROPPING_RULE), &error);
			if (dbus_error_is_set(&error)) {
				pcap_snprintf(handle->errbuf, PCAP_ERRBUF_SIZE, "Failed to add bus match: %s\n", error.message);
				dbus_error_free(&error);
				dbus_cleanup(handle);
				return PCAP_ERROR;
			}
		}
	}

	return 0;
}

pcap_t *
dbus_create(const char *device, char *ebuf, int *is_ours)
{
	pcap_t *p;

	if (strcmp(device, "dbus-system") &&
		strcmp(device, "dbus-session") &&
		strncmp(device, "dbus://", 7))
	{
		*is_ours = 0;
		return NULL;
	}

	*is_ours = 1;
	p = pcap_create_common(ebuf, sizeof (struct pcap_dbus));
	if (p == NULL)
		return (NULL);

	p->activate_op = dbus_activate;
	/*
	 * Set these up front, so that, even if our client tries
	 * to set non-blocking mode before we're activated, or
	 * query the state of non-blocking mode, they get an error,
	 * rather than having the non-blocking mode option set
	 * for use later.
	 */
	p->getnonblock_op = dbus_getnonblock;
	p->setnonblock_op = dbus_setnonblock;
	return (p);
}

int
dbus_findalldevs(pcap_if_list_t *devlistp, char *err_str)
{
	/*
	 * The notion of "connected" vs. "disconnected" doesn't apply.
	 * XXX - what about the notions of "up" and "running"?
	 */
	if (add_dev(devlistp, "dbus-system",
	    PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE, "D-Bus system bus",
	    err_str) == NULL)
		return -1;
	if (add_dev(devlistp, "dbus-session",
	    PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE, "D-Bus session bus",
	    err_str) == NULL)
		return -1;
	return 0;
}

