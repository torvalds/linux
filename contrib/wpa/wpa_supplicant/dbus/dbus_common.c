/*
 * wpa_supplicant D-Bus control interface - common functionality
 * Copyright (c) 2006, Dan Williams <dcbw@redhat.com> and Red Hat, Inc.
 * Copyright (c) 2009, Witold Sowa <witold.sowa@gmail.com>
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"
#include <dbus/dbus.h>

#include "utils/common.h"
#include "utils/eloop.h"
#include "dbus_common.h"
#include "dbus_common_i.h"
#include "dbus_new.h"
#include "dbus_old.h"
#include "../wpa_supplicant_i.h"


#ifndef SIGPOLL
#ifdef SIGIO
/*
 * If we do not have SIGPOLL, try to use SIGIO instead. This is needed for
 * FreeBSD.
 */
#define SIGPOLL SIGIO
#endif
#endif


static void dispatch_data(DBusConnection *con)
{
	while (dbus_connection_get_dispatch_status(con) ==
	       DBUS_DISPATCH_DATA_REMAINS)
		dbus_connection_dispatch(con);
}


/**
 * dispatch_initial_dbus_messages - Dispatch initial dbus messages after
 *     claiming bus name
 * @eloop_ctx: the DBusConnection to dispatch on
 * @timeout_ctx: unused
 *
 * If clients are quick to notice that service claimed its bus name,
 * there may have been messages that came in before initialization was
 * all finished.  Dispatch those here.
 */
static void dispatch_initial_dbus_messages(void *eloop_ctx, void *timeout_ctx)
{
	DBusConnection *con = eloop_ctx;
	dispatch_data(con);
}


static void process_watch(struct wpas_dbus_priv *priv,
			  DBusWatch *watch, eloop_event_type type)
{
	dbus_connection_ref(priv->con);

	priv->should_dispatch = 0;

	if (type == EVENT_TYPE_READ)
		dbus_watch_handle(watch, DBUS_WATCH_READABLE);
	else if (type == EVENT_TYPE_WRITE)
		dbus_watch_handle(watch, DBUS_WATCH_WRITABLE);
	else if (type == EVENT_TYPE_EXCEPTION)
		dbus_watch_handle(watch, DBUS_WATCH_ERROR);

	if (priv->should_dispatch) {
		dispatch_data(priv->con);
		priv->should_dispatch = 0;
	}

	dbus_connection_unref(priv->con);
}


static void process_watch_exception(int sock, void *eloop_ctx, void *sock_ctx)
{
	process_watch(eloop_ctx, sock_ctx, EVENT_TYPE_EXCEPTION);
}


static void process_watch_read(int sock, void *eloop_ctx, void *sock_ctx)
{
	process_watch(eloop_ctx, sock_ctx, EVENT_TYPE_READ);
}


static void process_watch_write(int sock, void *eloop_ctx, void *sock_ctx)
{
	process_watch(eloop_ctx, sock_ctx, EVENT_TYPE_WRITE);
}


static dbus_bool_t add_watch(DBusWatch *watch, void *data)
{
	struct wpas_dbus_priv *priv = data;
	unsigned int flags;
	int fd;

	if (!dbus_watch_get_enabled(watch))
		return TRUE;

	flags = dbus_watch_get_flags(watch);
	fd = dbus_watch_get_unix_fd(watch);

	eloop_register_sock(fd, EVENT_TYPE_EXCEPTION, process_watch_exception,
			    priv, watch);

	if (flags & DBUS_WATCH_READABLE) {
		eloop_register_sock(fd, EVENT_TYPE_READ, process_watch_read,
				    priv, watch);
	}
	if (flags & DBUS_WATCH_WRITABLE) {
		eloop_register_sock(fd, EVENT_TYPE_WRITE, process_watch_write,
				    priv, watch);
	}

	dbus_watch_set_data(watch, priv, NULL);

	return TRUE;
}


static void remove_watch(DBusWatch *watch, void *data)
{
	unsigned int flags;
	int fd;

	flags = dbus_watch_get_flags(watch);
	fd = dbus_watch_get_unix_fd(watch);

	eloop_unregister_sock(fd, EVENT_TYPE_EXCEPTION);

	if (flags & DBUS_WATCH_READABLE)
		eloop_unregister_sock(fd, EVENT_TYPE_READ);
	if (flags & DBUS_WATCH_WRITABLE)
		eloop_unregister_sock(fd, EVENT_TYPE_WRITE);

	dbus_watch_set_data(watch, NULL, NULL);
}


static void watch_toggled(DBusWatch *watch, void *data)
{
	if (dbus_watch_get_enabled(watch))
		add_watch(watch, data);
	else
		remove_watch(watch, data);
}


static void process_timeout(void *eloop_ctx, void *sock_ctx)
{
	DBusTimeout *timeout = sock_ctx;
	dbus_timeout_handle(timeout);
}


static dbus_bool_t add_timeout(DBusTimeout *timeout, void *data)
{
	struct wpas_dbus_priv *priv = data;

	if (!dbus_timeout_get_enabled(timeout))
		return TRUE;

	eloop_register_timeout(0, dbus_timeout_get_interval(timeout) * 1000,
			       process_timeout, priv, timeout);

	dbus_timeout_set_data(timeout, priv, NULL);

	return TRUE;
}


static void remove_timeout(DBusTimeout *timeout, void *data)
{
	struct wpas_dbus_priv *priv = data;

	eloop_cancel_timeout(process_timeout, priv, timeout);
	dbus_timeout_set_data(timeout, NULL, NULL);
}


static void timeout_toggled(DBusTimeout *timeout, void *data)
{
	if (dbus_timeout_get_enabled(timeout))
		add_timeout(timeout, data);
	else
		remove_timeout(timeout, data);
}


static void process_wakeup_main(int sig, void *signal_ctx)
{
	struct wpas_dbus_priv *priv = signal_ctx;

	if (sig != SIGPOLL || !priv->con)
		return;

	if (dbus_connection_get_dispatch_status(priv->con) !=
	    DBUS_DISPATCH_DATA_REMAINS)
		return;

	/* Only dispatch once - we do not want to starve other events */
	dbus_connection_ref(priv->con);
	dbus_connection_dispatch(priv->con);
	dbus_connection_unref(priv->con);
}


/**
 * wakeup_main - Attempt to wake our mainloop up
 * @data: dbus control interface private data
 *
 * Try to wake up the main eloop so it will process
 * dbus events that may have happened.
 */
static void wakeup_main(void *data)
{
	struct wpas_dbus_priv *priv = data;

	/* Use SIGPOLL to break out of the eloop select() */
	raise(SIGPOLL);
	priv->should_dispatch = 1;
}


/**
 * integrate_with_eloop - Register our mainloop integration with dbus
 * @connection: connection to the system message bus
 * @priv: a dbus control interface data structure
 * Returns: 0 on success, -1 on failure
 */
static int integrate_with_eloop(struct wpas_dbus_priv *priv)
{
	if (!dbus_connection_set_watch_functions(priv->con, add_watch,
						 remove_watch, watch_toggled,
						 priv, NULL) ||
	    !dbus_connection_set_timeout_functions(priv->con, add_timeout,
						   remove_timeout,
						   timeout_toggled, priv,
						   NULL)) {
		wpa_printf(MSG_ERROR, "dbus: Failed to set callback functions");
		return -1;
	}

	if (eloop_register_signal(SIGPOLL, process_wakeup_main, priv))
		return -1;
	dbus_connection_set_wakeup_main_function(priv->con, wakeup_main,
						 priv, NULL);

	return 0;
}


static DBusHandlerResult disconnect_filter(DBusConnection *conn,
					   DBusMessage *message, void *data)
{
	struct wpas_dbus_priv *priv = data;

	if (dbus_message_is_signal(message, DBUS_INTERFACE_LOCAL,
				   "Disconnected")) {
		wpa_printf(MSG_DEBUG, "dbus: bus disconnected, terminating");
		dbus_connection_set_exit_on_disconnect(conn, FALSE);
		wpa_supplicant_terminate_proc(priv->global);
		return DBUS_HANDLER_RESULT_HANDLED;
	} else
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


static int wpas_dbus_init_common(struct wpas_dbus_priv *priv)
{
	DBusError error;
	int ret = 0;

	/* Get a reference to the system bus */
	dbus_error_init(&error);
	priv->con = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
	if (priv->con) {
		dbus_connection_add_filter(priv->con, disconnect_filter, priv,
					   NULL);
	} else {
		wpa_printf(MSG_ERROR,
			   "dbus: Could not acquire the system bus: %s - %s",
			   error.name, error.message);
		ret = -1;
	}
	dbus_error_free(&error);

	return ret;
}


static int wpas_dbus_init_common_finish(struct wpas_dbus_priv *priv)
{
	/* Tell dbus about our mainloop integration functions */
	integrate_with_eloop(priv);

	/*
	 * Dispatch initial DBus messages that may have come in since the bus
	 * name was claimed above. Happens when clients are quick to notice the
	 * service.
	 *
	 * FIXME: is there a better solution to this problem?
	 */
	eloop_register_timeout(0, 50, dispatch_initial_dbus_messages,
			       priv->con, NULL);

	return 0;
}


static void wpas_dbus_deinit_common(struct wpas_dbus_priv *priv)
{
	if (priv->con) {
		eloop_cancel_timeout(dispatch_initial_dbus_messages,
				     priv->con, NULL);
		eloop_cancel_timeout(process_timeout, priv, ELOOP_ALL_CTX);

		dbus_connection_set_watch_functions(priv->con, NULL, NULL,
						    NULL, NULL, NULL);
		dbus_connection_set_timeout_functions(priv->con, NULL, NULL,
						      NULL, NULL, NULL);
		dbus_connection_remove_filter(priv->con, disconnect_filter,
					      priv);

		dbus_connection_unref(priv->con);
	}

	os_free(priv);
}


struct wpas_dbus_priv * wpas_dbus_init(struct wpa_global *global)
{
	struct wpas_dbus_priv *priv;

	priv = os_zalloc(sizeof(*priv));
	if (priv == NULL)
		return NULL;
	priv->global = global;

	if (wpas_dbus_init_common(priv) < 0 ||
#ifdef CONFIG_CTRL_IFACE_DBUS_NEW
	    wpas_dbus_ctrl_iface_init(priv) < 0 ||
#endif /* CONFIG_CTRL_IFACE_DBUS_NEW */
#ifdef CONFIG_CTRL_IFACE_DBUS
	    wpa_supplicant_dbus_ctrl_iface_init(priv) < 0 ||
#endif /* CONFIG_CTRL_IFACE_DBUS */
	    wpas_dbus_init_common_finish(priv) < 0) {
		wpas_dbus_deinit(priv);
		return NULL;
	}

	return priv;
}


void wpas_dbus_deinit(struct wpas_dbus_priv *priv)
{
	if (priv == NULL)
		return;

#ifdef CONFIG_CTRL_IFACE_DBUS_NEW
	wpas_dbus_ctrl_iface_deinit(priv);
#endif /* CONFIG_CTRL_IFACE_DBUS_NEW */

#ifdef CONFIG_CTRL_IFACE_DBUS
	/* TODO: is any deinit needed? */
#endif /* CONFIG_CTRL_IFACE_DBUS */

	wpas_dbus_deinit_common(priv);
}
