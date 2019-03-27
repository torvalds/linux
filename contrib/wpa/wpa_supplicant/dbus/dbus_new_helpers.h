/*
 * WPA Supplicant / dbus-based control interface
 * Copyright (c) 2006, Dan Williams <dcbw@redhat.com> and Red Hat, Inc.
 * Copyright (c) 2009, Witold Sowa <witold.sowa@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_DBUS_CTRL_H
#define WPA_DBUS_CTRL_H

#include <dbus/dbus.h>

typedef DBusMessage * (*WPADBusMethodHandler)(DBusMessage *message,
					      void *user_data);
typedef void (*WPADBusArgumentFreeFunction)(void *handler_arg);

struct wpa_dbus_property_desc;
typedef dbus_bool_t (*WPADBusPropertyAccessor)(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data);
#define DECLARE_ACCESSOR(f) \
dbus_bool_t f(const struct wpa_dbus_property_desc *property_desc, \
	      DBusMessageIter *iter, DBusError *error, void *user_data)

struct wpa_dbus_object_desc {
	DBusConnection *connection;
	char *path;

	/* list of methods, properties and signals registered with object */
	const struct wpa_dbus_method_desc *methods;
	const struct wpa_dbus_signal_desc *signals;
	const struct wpa_dbus_property_desc *properties;

	/* property changed flags */
	u8 *prop_changed_flags;

	/* argument for method handlers and properties
	 * getter and setter functions */
	void *user_data;
	/* function used to free above argument */
	WPADBusArgumentFreeFunction user_data_free_func;
};

enum dbus_arg_direction { ARG_IN, ARG_OUT };

struct wpa_dbus_argument {
	char *name;
	char *type;
	enum dbus_arg_direction dir;
};

#define END_ARGS { NULL, NULL, ARG_IN }

/**
 * struct wpa_dbus_method_desc - DBus method description
 */
struct wpa_dbus_method_desc {
	/* method name */
	const char *dbus_method;
	/* method interface */
	const char *dbus_interface;
	/* method handling function */
	WPADBusMethodHandler method_handler;
	/* array of arguments */
	struct wpa_dbus_argument args[4];
};

/**
 * struct wpa_dbus_signal_desc - DBus signal description
 */
struct wpa_dbus_signal_desc {
	/* signal name */
	const char *dbus_signal;
	/* signal interface */
	const char *dbus_interface;
	/* array of arguments */
	struct wpa_dbus_argument args[4];
};

/**
 * struct wpa_dbus_property_desc - DBus property description
 */
struct wpa_dbus_property_desc {
	/* property name */
	const char *dbus_property;
	/* property interface */
	const char *dbus_interface;
	/* property type signature in DBus type notation */
	const char *type;
	/* property getter function */
	WPADBusPropertyAccessor getter;
	/* property setter function */
	WPADBusPropertyAccessor setter;
	/* other data */
	const char *data;
};


#define WPAS_DBUS_OBJECT_PATH_MAX 150
#define WPAS_DBUS_INTERFACE_MAX 150
#define WPAS_DBUS_METHOD_SIGNAL_PROP_MAX 50
#define WPAS_DBUS_AUTH_MODE_MAX 64

#define WPA_DBUS_INTROSPECTION_INTERFACE "org.freedesktop.DBus.Introspectable"
#define WPA_DBUS_INTROSPECTION_METHOD "Introspect"
#define WPA_DBUS_PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"
#define WPA_DBUS_PROPERTIES_GET "Get"
#define WPA_DBUS_PROPERTIES_SET "Set"
#define WPA_DBUS_PROPERTIES_GETALL "GetAll"

void free_dbus_object_desc(struct wpa_dbus_object_desc *obj_dsc);

int wpa_dbus_ctrl_iface_init(struct wpas_dbus_priv *iface, char *dbus_path,
			     char *dbus_service,
			     struct wpa_dbus_object_desc *obj_desc);

int wpa_dbus_register_object_per_iface(
	struct wpas_dbus_priv *ctrl_iface,
	const char *path, const char *ifname,
	struct wpa_dbus_object_desc *obj_desc);

int wpa_dbus_unregister_object_per_iface(
	struct wpas_dbus_priv *ctrl_iface,
	const char *path);

dbus_bool_t wpa_dbus_get_object_properties(struct wpas_dbus_priv *iface,
					   const char *path,
					   const char *interface,
					   DBusMessageIter *iter);


void wpa_dbus_flush_all_changed_properties(DBusConnection *con);

void wpa_dbus_flush_object_changed_properties(DBusConnection *con,
					      const char *path);

void wpa_dbus_mark_property_changed(struct wpas_dbus_priv *iface,
				    const char *path, const char *interface,
				    const char *property);

DBusMessage * wpa_dbus_introspect(DBusMessage *message,
				  struct wpa_dbus_object_desc *obj_dsc);

char * wpas_dbus_new_decompose_object_path(const char *path, const char *sep,
					   char **item);

DBusMessage *wpas_dbus_reply_new_from_error(DBusMessage *message,
					    DBusError *error,
					    const char *fallback_name,
					    const char *fallback_string);

#endif /* WPA_DBUS_CTRL_H */
