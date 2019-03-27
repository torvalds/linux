/*
 * WPA Supplicant / dbus-based control interface
 * Copyright (c) 2006, Dan Williams <dcbw@redhat.com> and Red Hat, Inc.
 * Copyright (c) 2009, Witold Sowa <witold.sowa@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "dbus_common.h"
#include "dbus_common_i.h"
#include "dbus_new.h"
#include "dbus_new_helpers.h"
#include "dbus_new_handlers.h"
#include "dbus_dict_helpers.h"


static dbus_bool_t fill_dict_with_properties(
	DBusMessageIter *dict_iter,
	const struct wpa_dbus_property_desc *props,
	const char *interface, void *user_data, DBusError *error)
{
	DBusMessageIter entry_iter;
	const struct wpa_dbus_property_desc *dsc;

	for (dsc = props; dsc && dsc->dbus_property; dsc++) {
		/* Only return properties for the requested D-Bus interface */
		if (os_strncmp(dsc->dbus_interface, interface,
			       WPAS_DBUS_INTERFACE_MAX) != 0)
			continue;

		/* Skip write-only properties */
		if (dsc->getter == NULL)
			continue;

		if (!dbus_message_iter_open_container(dict_iter,
						      DBUS_TYPE_DICT_ENTRY,
						      NULL, &entry_iter) ||
		    !dbus_message_iter_append_basic(&entry_iter,
						    DBUS_TYPE_STRING,
						    &dsc->dbus_property))
			goto error;

		/* An error getting a property fails the request entirely */
		if (!dsc->getter(dsc, &entry_iter, error, user_data)) {
			wpa_printf(MSG_INFO,
				   "dbus: %s dbus_interface=%s dbus_property=%s getter failed",
				   __func__, dsc->dbus_interface,
				   dsc->dbus_property);
			return FALSE;
		}

		if (!dbus_message_iter_close_container(dict_iter, &entry_iter))
			goto error;
	}

	return TRUE;

error:
	dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
	return FALSE;
}


/**
 * get_all_properties - Responds for GetAll properties calls on object
 * @message: Message with GetAll call
 * @interface: interface name which properties will be returned
 * @property_dsc: list of object's properties
 * Returns: Message with dict of variants as argument with properties values
 *
 * Iterates over all properties registered with object and execute getters
 * of those, which are readable and which interface matches interface
 * specified as argument. Returned message contains one dict argument
 * with properties names as keys and theirs values as values.
 */
static DBusMessage * get_all_properties(DBusMessage *message, char *interface,
					struct wpa_dbus_object_desc *obj_dsc)
{
	DBusMessage *reply;
	DBusMessageIter iter, dict_iter;
	DBusError error;

	reply = dbus_message_new_method_return(message);
	if (reply == NULL)
		return wpas_dbus_error_no_memory(message);

	dbus_message_iter_init_append(reply, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter)) {
		dbus_message_unref(reply);
		return wpas_dbus_error_no_memory(message);
	}

	dbus_error_init(&error);
	if (!fill_dict_with_properties(&dict_iter, obj_dsc->properties,
				       interface, obj_dsc->user_data, &error)) {
		dbus_message_unref(reply);
		reply = wpas_dbus_reply_new_from_error(
			message, &error, DBUS_ERROR_INVALID_ARGS,
			"No readable properties in this interface");
		dbus_error_free(&error);
		return reply;
	}

	if (!wpa_dbus_dict_close_write(&iter, &dict_iter)) {
		dbus_message_unref(reply);
		return wpas_dbus_error_no_memory(message);
	}

	return reply;
}


static int is_signature_correct(DBusMessage *message,
				const struct wpa_dbus_method_desc *method_dsc)
{
	/* According to DBus documentation max length of signature is 255 */
#define MAX_SIG_LEN 256
	char registered_sig[MAX_SIG_LEN], *pos;
	const char *sig = dbus_message_get_signature(message);
	int ret;
	const struct wpa_dbus_argument *arg;

	pos = registered_sig;
	*pos = '\0';

	for (arg = method_dsc->args; arg && arg->name; arg++) {
		if (arg->dir == ARG_IN) {
			size_t blen = registered_sig + MAX_SIG_LEN - pos;

			ret = os_snprintf(pos, blen, "%s", arg->type);
			if (os_snprintf_error(blen, ret))
				return 0;
			pos += ret;
		}
	}

	return !os_strncmp(registered_sig, sig, MAX_SIG_LEN);
}


static DBusMessage * properties_get_all(DBusMessage *message, char *interface,
					struct wpa_dbus_object_desc *obj_dsc)
{
	if (os_strcmp(dbus_message_get_signature(message), "s") != 0)
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      NULL);

	return get_all_properties(message, interface, obj_dsc);
}


static DBusMessage * properties_get(DBusMessage *message,
				    const struct wpa_dbus_property_desc *dsc,
				    void *user_data)
{
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusError error;

	if (os_strcmp(dbus_message_get_signature(message), "ss")) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      NULL);
	}

	if (dsc->getter == NULL) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      "Property is write-only");
	}

	reply = dbus_message_new_method_return(message);
	dbus_message_iter_init_append(reply, &iter);

	dbus_error_init(&error);
	if (dsc->getter(dsc, &iter, &error, user_data) == FALSE) {
		dbus_message_unref(reply);
		reply = wpas_dbus_reply_new_from_error(
			message, &error, DBUS_ERROR_FAILED,
			"Failed to read property");
		dbus_error_free(&error);
	}

	return reply;
}


static DBusMessage * properties_set(DBusMessage *message,
				    const struct wpa_dbus_property_desc *dsc,
				    void *user_data)
{
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusError error;

	if (os_strcmp(dbus_message_get_signature(message), "ssv")) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      NULL);
	}

	if (dsc->setter == NULL) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      "Property is read-only");
	}

	dbus_message_iter_init(message, &iter);
	/* Skip the interface name and the property name */
	dbus_message_iter_next(&iter);
	dbus_message_iter_next(&iter);

	/* Iter will now point to the property's new value */
	dbus_error_init(&error);
	if (dsc->setter(dsc, &iter, &error, user_data) == TRUE) {
		/* Success */
		reply = dbus_message_new_method_return(message);
	} else {
		reply = wpas_dbus_reply_new_from_error(
			message, &error, DBUS_ERROR_FAILED,
			"Failed to set property");
		dbus_error_free(&error);
	}

	return reply;
}


static DBusMessage *
properties_get_or_set(DBusMessage *message, DBusMessageIter *iter,
		      char *interface,
		      struct wpa_dbus_object_desc *obj_dsc)
{
	const struct wpa_dbus_property_desc *property_dsc;
	char *property;
	const char *method;

	method = dbus_message_get_member(message);
	property_dsc = obj_dsc->properties;

	/* Second argument: property name (DBUS_TYPE_STRING) */
	if (!dbus_message_iter_next(iter) ||
	    dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_STRING) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      NULL);
	}
	dbus_message_iter_get_basic(iter, &property);

	while (property_dsc && property_dsc->dbus_property) {
		/* compare property names and
		 * interfaces */
		if (!os_strncmp(property_dsc->dbus_property, property,
				WPAS_DBUS_METHOD_SIGNAL_PROP_MAX) &&
		    !os_strncmp(property_dsc->dbus_interface, interface,
				WPAS_DBUS_INTERFACE_MAX))
			break;

		property_dsc++;
	}
	if (property_dsc == NULL || property_dsc->dbus_property == NULL) {
		wpa_printf(MSG_DEBUG, "no property handler for %s.%s on %s",
			   interface, property,
			   dbus_message_get_path(message));
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      "No such property");
	}

	if (os_strncmp(WPA_DBUS_PROPERTIES_GET, method,
		       WPAS_DBUS_METHOD_SIGNAL_PROP_MAX) == 0) {
		wpa_printf(MSG_MSGDUMP, "%s: Get(%s)", __func__, property);
		return properties_get(message, property_dsc,
				      obj_dsc->user_data);
	}

	wpa_printf(MSG_MSGDUMP, "%s: Set(%s)", __func__, property);
	return properties_set(message, property_dsc, obj_dsc->user_data);
}


static DBusMessage * properties_handler(DBusMessage *message,
					struct wpa_dbus_object_desc *obj_dsc)
{
	DBusMessageIter iter;
	char *interface;
	const char *method;

	method = dbus_message_get_member(message);
	dbus_message_iter_init(message, &iter);

	if (!os_strncmp(WPA_DBUS_PROPERTIES_GET, method,
			WPAS_DBUS_METHOD_SIGNAL_PROP_MAX) ||
	    !os_strncmp(WPA_DBUS_PROPERTIES_SET, method,
			WPAS_DBUS_METHOD_SIGNAL_PROP_MAX) ||
	    !os_strncmp(WPA_DBUS_PROPERTIES_GETALL, method,
			WPAS_DBUS_METHOD_SIGNAL_PROP_MAX)) {
		/* First argument: interface name (DBUS_TYPE_STRING) */
		if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING) {
			return dbus_message_new_error(message,
						      DBUS_ERROR_INVALID_ARGS,
						      NULL);
		}

		dbus_message_iter_get_basic(&iter, &interface);

		if (!os_strncmp(WPA_DBUS_PROPERTIES_GETALL, method,
				WPAS_DBUS_METHOD_SIGNAL_PROP_MAX)) {
			/* GetAll */
			return properties_get_all(message, interface, obj_dsc);
		}
		/* Get or Set */
		return properties_get_or_set(message, &iter, interface,
					     obj_dsc);
	}
	return dbus_message_new_error(message, DBUS_ERROR_UNKNOWN_METHOD,
				      NULL);
}


static DBusMessage * msg_method_handler(DBusMessage *message,
					struct wpa_dbus_object_desc *obj_dsc)
{
	const struct wpa_dbus_method_desc *method_dsc = obj_dsc->methods;
	const char *method;
	const char *msg_interface;

	method = dbus_message_get_member(message);
	msg_interface = dbus_message_get_interface(message);

	/* try match call to any registered method */
	while (method_dsc && method_dsc->dbus_method) {
		/* compare method names and interfaces */
		if (!os_strncmp(method_dsc->dbus_method, method,
				WPAS_DBUS_METHOD_SIGNAL_PROP_MAX) &&
		    !os_strncmp(method_dsc->dbus_interface, msg_interface,
				WPAS_DBUS_INTERFACE_MAX))
			break;

		method_dsc++;
	}
	if (method_dsc == NULL || method_dsc->dbus_method == NULL) {
		wpa_printf(MSG_DEBUG, "no method handler for %s.%s on %s",
			   msg_interface, method,
			   dbus_message_get_path(message));
		return dbus_message_new_error(message,
					      DBUS_ERROR_UNKNOWN_METHOD, NULL);
	}

	if (!is_signature_correct(message, method_dsc)) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      NULL);
	}

	return method_dsc->method_handler(message, obj_dsc->user_data);
}


/**
 * message_handler - Handles incoming DBus messages
 * @connection: DBus connection on which message was received
 * @message: Received message
 * @user_data: pointer to description of object to which message was sent
 * Returns: Returns information whether message was handled or not
 *
 * Reads message interface and method name, then checks if they matches one
 * of the special cases i.e. introspection call or properties get/getall/set
 * methods and handles it. Else it iterates over registered methods list
 * and tries to match method's name and interface to those read from message
 * If appropriate method was found its handler function is called and
 * response is sent. Otherwise, the DBUS_ERROR_UNKNOWN_METHOD error message
 * will be sent.
 */
static DBusHandlerResult message_handler(DBusConnection *connection,
					 DBusMessage *message, void *user_data)
{
	struct wpa_dbus_object_desc *obj_dsc = user_data;
	const char *method;
	const char *path;
	const char *msg_interface;
	DBusMessage *reply;

	/* get method, interface and path the message is addressed to */
	method = dbus_message_get_member(message);
	path = dbus_message_get_path(message);
	msg_interface = dbus_message_get_interface(message);
	if (!method || !path || !msg_interface)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	wpa_printf(MSG_MSGDUMP, "dbus: %s.%s (%s) [%s]",
		   msg_interface, method, path,
		   dbus_message_get_signature(message));

	/* if message is introspection method call */
	if (!os_strncmp(WPA_DBUS_INTROSPECTION_METHOD, method,
			WPAS_DBUS_METHOD_SIGNAL_PROP_MAX) &&
	    !os_strncmp(WPA_DBUS_INTROSPECTION_INTERFACE, msg_interface,
			WPAS_DBUS_INTERFACE_MAX)) {
#ifdef CONFIG_CTRL_IFACE_DBUS_INTRO
		reply = wpa_dbus_introspect(message, obj_dsc);
#else /* CONFIG_CTRL_IFACE_DBUS_INTRO */
		reply = dbus_message_new_error(
			message, DBUS_ERROR_UNKNOWN_METHOD,
			"wpa_supplicant was compiled without introspection support.");
#endif /* CONFIG_CTRL_IFACE_DBUS_INTRO */
	} else if (!os_strncmp(WPA_DBUS_PROPERTIES_INTERFACE, msg_interface,
			     WPAS_DBUS_INTERFACE_MAX)) {
		/* if message is properties method call */
		reply = properties_handler(message, obj_dsc);
	} else {
		reply = msg_method_handler(message, obj_dsc);
	}

	/* If handler succeed returning NULL, reply empty message */
	if (!reply)
		reply = dbus_message_new_method_return(message);
	if (reply) {
		if (!dbus_message_get_no_reply(message))
			dbus_connection_send(connection, reply, NULL);
		dbus_message_unref(reply);
	}

	wpa_dbus_flush_all_changed_properties(connection);

	return DBUS_HANDLER_RESULT_HANDLED;
}


/**
 * free_dbus_object_desc - Frees object description data structure
 * @connection: DBus connection
 * @obj_dsc: Object description to free
 *
 * Frees each of properties, methods and signals description lists and
 * the object description structure itself.
 */
void free_dbus_object_desc(struct wpa_dbus_object_desc *obj_dsc)
{
	if (!obj_dsc)
		return;

	/* free handler's argument */
	if (obj_dsc->user_data_free_func)
		obj_dsc->user_data_free_func(obj_dsc->user_data);

	os_free(obj_dsc->path);
	os_free(obj_dsc->prop_changed_flags);
	os_free(obj_dsc);
}


static void free_dbus_object_desc_cb(DBusConnection *connection, void *obj_dsc)
{
	free_dbus_object_desc(obj_dsc);
}


/**
 * wpa_dbus_ctrl_iface_init - Initialize dbus control interface
 * @application_data: Pointer to application specific data structure
 * @dbus_path: DBus path to interface object
 * @dbus_service: DBus service name to register with
 * @messageHandler: a pointer to function which will handle dbus messages
 * coming on interface
 * Returns: 0 on success, -1 on failure
 *
 * Initialize the dbus control interface and start receiving commands from
 * external programs over the bus.
 */
int wpa_dbus_ctrl_iface_init(struct wpas_dbus_priv *iface,
			     char *dbus_path, char *dbus_service,
			     struct wpa_dbus_object_desc *obj_desc)
{
	DBusError error;
	int ret = -1;
	DBusObjectPathVTable wpa_vtable = {
		&free_dbus_object_desc_cb, &message_handler,
		NULL, NULL, NULL, NULL
	};

	obj_desc->connection = iface->con;
	obj_desc->path = os_strdup(dbus_path);

	/* Register the message handler for the global dbus interface */
	if (!dbus_connection_register_object_path(iface->con, dbus_path,
						  &wpa_vtable, obj_desc)) {
		wpa_printf(MSG_ERROR, "dbus: Could not set up message handler");
		return -1;
	}

	/* Register our service with the message bus */
	dbus_error_init(&error);
	switch (dbus_bus_request_name(iface->con, dbus_service, 0, &error)) {
	case DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER:
		ret = 0;
		break;
	case DBUS_REQUEST_NAME_REPLY_EXISTS:
	case DBUS_REQUEST_NAME_REPLY_IN_QUEUE:
	case DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER:
		wpa_printf(MSG_ERROR,
			   "dbus: Could not request service name: already registered");
		break;
	default:
		wpa_printf(MSG_ERROR,
			   "dbus: Could not request service name: %s %s",
			   error.name, error.message);
		break;
	}
	dbus_error_free(&error);

	if (ret != 0)
		return -1;

	wpa_printf(MSG_DEBUG, "Providing DBus service '%s'.", dbus_service);

	return 0;
}


/**
 * wpa_dbus_register_object_per_iface - Register a new object with dbus
 * @ctrl_iface: pointer to dbus private data
 * @path: DBus path to object
 * @ifname: interface name
 * @obj_desc: description of object's methods, signals and properties
 * Returns: 0 on success, -1 on error
 *
 * Registers a new interface with dbus and assigns it a dbus object path.
 */
int wpa_dbus_register_object_per_iface(struct wpas_dbus_priv *ctrl_iface,
				       const char *path, const char *ifname,
				       struct wpa_dbus_object_desc *obj_desc)
{
	DBusConnection *con;
	DBusError error;
	DBusObjectPathVTable vtable = {
		&free_dbus_object_desc_cb, &message_handler,
		NULL, NULL, NULL, NULL
	};

	/* Do nothing if the control interface is not turned on */
	if (ctrl_iface == NULL)
		return 0;

	con = ctrl_iface->con;
	obj_desc->connection = con;
	obj_desc->path = os_strdup(path);

	dbus_error_init(&error);
	/* Register the message handler for the interface functions */
	if (!dbus_connection_try_register_object_path(con, path, &vtable,
						      obj_desc, &error)) {
		if (os_strcmp(error.name, DBUS_ERROR_OBJECT_PATH_IN_USE) == 0) {
			wpa_printf(MSG_DEBUG, "dbus: %s", error.message);
		} else {
			wpa_printf(MSG_ERROR,
				   "dbus: Could not set up message handler for interface %s object %s (error: %s message: %s)",
				   ifname, path, error.name, error.message);
		}
		dbus_error_free(&error);
		return -1;
	}

	dbus_error_free(&error);
	return 0;
}


static void flush_object_timeout_handler(void *eloop_ctx, void *timeout_ctx);


/**
 * wpa_dbus_unregister_object_per_iface - Unregisters DBus object
 * @ctrl_iface: Pointer to dbus private data
 * @path: DBus path to object which will be unregistered
 * Returns: Zero on success and -1 on failure
 *
 * Unregisters DBus object given by its path
 */
int wpa_dbus_unregister_object_per_iface(
	struct wpas_dbus_priv *ctrl_iface, const char *path)
{
	DBusConnection *con = ctrl_iface->con;
	struct wpa_dbus_object_desc *obj_desc = NULL;

	dbus_connection_get_object_path_data(con, path, (void **) &obj_desc);
	if (!obj_desc) {
		wpa_printf(MSG_ERROR,
			   "dbus: %s: Could not obtain object's private data: %s",
			   __func__, path);
		return 0;
	}

	eloop_cancel_timeout(flush_object_timeout_handler, con, obj_desc);

	if (!dbus_connection_unregister_object_path(con, path))
		return -1;

	return 0;
}


static dbus_bool_t put_changed_properties(
	const struct wpa_dbus_object_desc *obj_dsc, const char *interface,
	DBusMessageIter *dict_iter, int clear_changed)
{
	DBusMessageIter entry_iter;
	const struct wpa_dbus_property_desc *dsc;
	int i;
	DBusError error;

	for (dsc = obj_dsc->properties, i = 0; dsc && dsc->dbus_property;
	     dsc++, i++) {
		if (obj_dsc->prop_changed_flags == NULL ||
		    !obj_dsc->prop_changed_flags[i])
			continue;
		if (os_strcmp(dsc->dbus_interface, interface) != 0)
			continue;
		if (clear_changed)
			obj_dsc->prop_changed_flags[i] = 0;

		if (!dbus_message_iter_open_container(dict_iter,
						      DBUS_TYPE_DICT_ENTRY,
						      NULL, &entry_iter) ||
		    !dbus_message_iter_append_basic(&entry_iter,
						    DBUS_TYPE_STRING,
						    &dsc->dbus_property))
			return FALSE;

		dbus_error_init(&error);
		if (!dsc->getter(dsc, &entry_iter, &error, obj_dsc->user_data))
		{
			if (dbus_error_is_set(&error)) {
				wpa_printf(MSG_ERROR,
					   "dbus: %s: Cannot get new value of property %s: (%s) %s",
					   __func__, dsc->dbus_property,
					   error.name, error.message);
			} else {
				wpa_printf(MSG_ERROR,
					   "dbus: %s: Cannot get new value of property %s",
					   __func__, dsc->dbus_property);
			}
			dbus_error_free(&error);
			return FALSE;
		}

		if (!dbus_message_iter_close_container(dict_iter, &entry_iter))
			return FALSE;
	}

	return TRUE;
}


static void do_send_prop_changed_signal(
	DBusConnection *con, const char *path, const char *interface,
	const struct wpa_dbus_object_desc *obj_dsc)
{
	DBusMessage *msg;
	DBusMessageIter signal_iter, dict_iter;

	msg = dbus_message_new_signal(path, DBUS_INTERFACE_PROPERTIES,
				      "PropertiesChanged");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &signal_iter);

	if (!dbus_message_iter_append_basic(&signal_iter, DBUS_TYPE_STRING,
					    &interface) ||
	    /* Changed properties dict */
	    !dbus_message_iter_open_container(&signal_iter, DBUS_TYPE_ARRAY,
					      "{sv}", &dict_iter) ||
	    !put_changed_properties(obj_dsc, interface, &dict_iter, 0) ||
	    !dbus_message_iter_close_container(&signal_iter, &dict_iter) ||
	    /* Invalidated properties array (empty) */
	    !dbus_message_iter_open_container(&signal_iter, DBUS_TYPE_ARRAY,
					      "s", &dict_iter) ||
	    !dbus_message_iter_close_container(&signal_iter, &dict_iter)) {
		wpa_printf(MSG_DEBUG, "dbus: %s: Failed to construct signal",
			   __func__);
	} else {
		dbus_connection_send(con, msg, NULL);
	}

	dbus_message_unref(msg);
}


static void do_send_deprecated_prop_changed_signal(
	DBusConnection *con, const char *path, const char *interface,
	const struct wpa_dbus_object_desc *obj_dsc)
{
	DBusMessage *msg;
	DBusMessageIter signal_iter, dict_iter;

	msg = dbus_message_new_signal(path, interface, "PropertiesChanged");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &signal_iter);

	if (!dbus_message_iter_open_container(&signal_iter, DBUS_TYPE_ARRAY,
					      "{sv}", &dict_iter) ||
	    !put_changed_properties(obj_dsc, interface, &dict_iter, 1) ||
	    !dbus_message_iter_close_container(&signal_iter, &dict_iter)) {
		wpa_printf(MSG_DEBUG, "dbus: %s: Failed to construct signal",
			   __func__);
	} else {
		dbus_connection_send(con, msg, NULL);
	}

	dbus_message_unref(msg);
}


static void send_prop_changed_signal(
	DBusConnection *con, const char *path, const char *interface,
	const struct wpa_dbus_object_desc *obj_dsc)
{
	/*
	 * First, send property change notification on the standardized
	 * org.freedesktop.DBus.Properties interface. This call will not
	 * clear the property change bits, so that they are preserved for
	 * the call that follows.
	 */
	do_send_prop_changed_signal(con, path, interface, obj_dsc);

	/*
	 * Now send PropertiesChanged on our own interface for backwards
	 * compatibility. This is deprecated and will be removed in a future
	 * release.
	 */
	do_send_deprecated_prop_changed_signal(con, path, interface, obj_dsc);

	/* Property change bits have now been cleared. */
}


static void flush_object_timeout_handler(void *eloop_ctx, void *timeout_ctx)
{
	DBusConnection *con = eloop_ctx;
	struct wpa_dbus_object_desc *obj_desc = timeout_ctx;

	wpa_printf(MSG_DEBUG,
		   "dbus: %s: Timeout - sending changed properties of object %s",
		   __func__, obj_desc->path);
	wpa_dbus_flush_object_changed_properties(con, obj_desc->path);
}


static void recursive_flush_changed_properties(DBusConnection *con,
					       const char *path)
{
	char **objects = NULL;
	char subobj_path[WPAS_DBUS_OBJECT_PATH_MAX];
	int i;

	wpa_dbus_flush_object_changed_properties(con, path);

	if (!dbus_connection_list_registered(con, path, &objects))
		goto out;

	for (i = 0; objects[i]; i++) {
		os_snprintf(subobj_path, WPAS_DBUS_OBJECT_PATH_MAX,
			    "%s/%s", path, objects[i]);
		recursive_flush_changed_properties(con, subobj_path);
	}

out:
	dbus_free_string_array(objects);
}


/**
 * wpa_dbus_flush_all_changed_properties - Send all PropertiesChanged signals
 * @con: DBus connection
 *
 * Traverses through all registered objects and sends PropertiesChanged for
 * each properties.
 */
void wpa_dbus_flush_all_changed_properties(DBusConnection *con)
{
	recursive_flush_changed_properties(con, WPAS_DBUS_NEW_PATH);
}


/**
 * wpa_dbus_flush_object_changed_properties - Send PropertiesChanged for object
 * @con: DBus connection
 * @path: path to a DBus object for which PropertiesChanged will be sent.
 *
 * Iterates over all properties registered with object and for each interface
 * containing properties marked as changed, sends a PropertiesChanged signal
 * containing names and new values of properties that have changed.
 *
 * You need to call this function after wpa_dbus_mark_property_changed()
 * if you want to send PropertiesChanged signal immediately (i.e., without
 * waiting timeout to expire). PropertiesChanged signal for an object is sent
 * automatically short time after first marking property as changed. All
 * PropertiesChanged signals are sent automatically after responding on DBus
 * message, so if you marked a property changed as a result of DBus call
 * (e.g., param setter), you usually do not need to call this function.
 */
void wpa_dbus_flush_object_changed_properties(DBusConnection *con,
					      const char *path)
{
	struct wpa_dbus_object_desc *obj_desc = NULL;
	const struct wpa_dbus_property_desc *dsc;
	int i;

	dbus_connection_get_object_path_data(con, path, (void **) &obj_desc);
	if (!obj_desc)
		return;
	eloop_cancel_timeout(flush_object_timeout_handler, con, obj_desc);

	for (dsc = obj_desc->properties, i = 0; dsc && dsc->dbus_property;
	     dsc++, i++) {
		if (obj_desc->prop_changed_flags == NULL ||
		    !obj_desc->prop_changed_flags[i])
			continue;
		send_prop_changed_signal(con, path, dsc->dbus_interface,
					 obj_desc);
	}
}


#define WPA_DBUS_SEND_PROP_CHANGED_TIMEOUT 5000


/**
 * wpa_dbus_mark_property_changed - Mark a property as changed and
 * @iface: dbus priv struct
 * @path: path to DBus object which property has changed
 * @interface: interface containing changed property
 * @property: property name which has changed
 *
 * Iterates over all properties registered with an object and marks the one
 * given in parameters as changed. All parameters registered for an object
 * within a single interface will be aggregated together and sent in one
 * PropertiesChanged signal when function
 * wpa_dbus_flush_object_changed_properties() is called.
 */
void wpa_dbus_mark_property_changed(struct wpas_dbus_priv *iface,
				    const char *path, const char *interface,
				    const char *property)
{
	struct wpa_dbus_object_desc *obj_desc = NULL;
	const struct wpa_dbus_property_desc *dsc;
	int i = 0;

	if (iface == NULL)
		return;

	dbus_connection_get_object_path_data(iface->con, path,
					     (void **) &obj_desc);
	if (!obj_desc) {
		wpa_printf(MSG_ERROR,
			   "dbus: wpa_dbus_property_changed: could not obtain object's private data: %s",
			   path);
		return;
	}

	for (dsc = obj_desc->properties; dsc && dsc->dbus_property; dsc++, i++)
		if (os_strcmp(property, dsc->dbus_property) == 0 &&
		    os_strcmp(interface, dsc->dbus_interface) == 0) {
			if (obj_desc->prop_changed_flags)
				obj_desc->prop_changed_flags[i] = 1;
			break;
		}

	if (!dsc || !dsc->dbus_property) {
		wpa_printf(MSG_ERROR,
			   "dbus: wpa_dbus_property_changed: no property %s in object %s",
			   property, path);
		return;
	}

	if (!eloop_is_timeout_registered(flush_object_timeout_handler,
					 iface->con, obj_desc)) {
		eloop_register_timeout(0, WPA_DBUS_SEND_PROP_CHANGED_TIMEOUT,
				       flush_object_timeout_handler,
				       iface->con, obj_desc);
	}
}


/**
 * wpa_dbus_get_object_properties - Put object's properties into dictionary
 * @iface: dbus priv struct
 * @path: path to DBus object which properties will be obtained
 * @interface: interface name which properties will be obtained
 * @iter: DBus message iter at which to append property dictionary.
 *
 * Iterates over all properties registered with object and execute getters
 * of those, which are readable and which interface matches interface
 * specified as argument. Obtained properties values are stored in
 * dict_iter dictionary.
 */
dbus_bool_t wpa_dbus_get_object_properties(struct wpas_dbus_priv *iface,
					   const char *path,
					   const char *interface,
					   DBusMessageIter *iter)
{
	struct wpa_dbus_object_desc *obj_desc = NULL;
	DBusMessageIter dict_iter;
	DBusError error;

	dbus_connection_get_object_path_data(iface->con, path,
					     (void **) &obj_desc);
	if (!obj_desc) {
		wpa_printf(MSG_ERROR,
			   "dbus: %s: could not obtain object's private data: %s",
			   __func__, path);
		return FALSE;
	}

	if (!wpa_dbus_dict_open_write(iter, &dict_iter)) {
		wpa_printf(MSG_ERROR, "dbus: %s: failed to open message dict",
			   __func__);
		return FALSE;
	}

	dbus_error_init(&error);
	if (!fill_dict_with_properties(&dict_iter, obj_desc->properties,
				       interface, obj_desc->user_data,
				       &error)) {
		wpa_printf(MSG_ERROR,
			   "dbus: %s: failed to get object properties: (%s) %s",
			   __func__,
			   dbus_error_is_set(&error) ? error.name : "none",
			   dbus_error_is_set(&error) ? error.message : "none");
		dbus_error_free(&error);
		return FALSE;
	}

	return wpa_dbus_dict_close_write(iter, &dict_iter);
}

/**
 * wpas_dbus_new_decompose_object_path - Decompose an interface object path into parts
 * @path: The dbus object path
 * @sep: Separating part (e.g., "Networks" or "PersistentGroups")
 * @item: (out) The part following the specified separator, if any
 * Returns: The object path of the interface this path refers to
 *
 * For a given object path, decomposes the object path into object id and
 * requested part, if those parts exist. The caller is responsible for freeing
 * the returned value. The *item pointer points to that allocated value and must
 * not be freed separately.
 *
 * As an example, path = "/fi/w1/wpa_supplicant1/Interfaces/1/Networks/0" and
 * sep = "Networks" would result in "/fi/w1/wpa_supplicant1/Interfaces/1"
 * getting returned and *items set to point to "0".
 */
char * wpas_dbus_new_decompose_object_path(const char *path, const char *sep,
					   char **item)
{
	const unsigned int dev_path_prefix_len =
		os_strlen(WPAS_DBUS_NEW_PATH_INTERFACES "/");
	char *obj_path_only;
	char *pos;
	size_t sep_len;

	*item = NULL;

	/* Verify that this starts with our interface prefix */
	if (os_strncmp(path, WPAS_DBUS_NEW_PATH_INTERFACES "/",
		       dev_path_prefix_len) != 0)
		return NULL; /* not our path */

	/* Ensure there's something at the end of the path */
	if ((path + dev_path_prefix_len)[0] == '\0')
		return NULL;

	obj_path_only = os_strdup(path);
	if (obj_path_only == NULL)
		return NULL;

	pos = obj_path_only + dev_path_prefix_len;
	pos = os_strchr(pos, '/');
	if (pos == NULL)
		return obj_path_only; /* no next item on the path */

	 /* Separate network interface prefix from the path */
	*pos++ = '\0';

	sep_len = os_strlen(sep);
	if (os_strncmp(pos, sep, sep_len) != 0 || pos[sep_len] != '/')
		return obj_path_only; /* no match */

	 /* return a pointer to the requested item */
	*item = pos + sep_len + 1;
	return obj_path_only;
}


/**
 * wpas_dbus_reply_new_from_error - Create a new D-Bus error message from a
 *   dbus error structure
 * @message: The original request message for which the error is a reply
 * @error: The error containing a name and a descriptive error cause
 * @fallback_name: A generic error name if @error was not set
 * @fallback_string: A generic error string if @error was not set
 * Returns: A new D-Bus error message
 *
 * Given a DBusMessage structure, creates a new D-Bus error message using
 * the error name and string contained in that structure.
 */
DBusMessage * wpas_dbus_reply_new_from_error(DBusMessage *message,
					     DBusError *error,
					     const char *fallback_name,
					     const char *fallback_string)
{
	if (error && error->name && error->message) {
		return dbus_message_new_error(message, error->name,
					      error->message);
	}
	if (fallback_name && fallback_string) {
		return dbus_message_new_error(message, fallback_name,
					      fallback_string);
	}
	return NULL;
}
