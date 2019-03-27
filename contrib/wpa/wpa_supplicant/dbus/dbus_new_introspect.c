/*
 * wpa_supplicant - D-Bus introspection
 * Copyright (c) 2006, Dan Williams <dcbw@redhat.com> and Red Hat, Inc.
 * Copyright (c) 2009, Witold Sowa <witold.sowa@gmail.com>
 * Copyright (c) 2010, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/list.h"
#include "utils/wpabuf.h"
#include "dbus_common_i.h"
#include "dbus_new_helpers.h"


struct interfaces {
	struct dl_list list;
	char *dbus_interface;
	struct wpabuf *xml;
};


static struct interfaces * add_interface(struct dl_list *list,
					 const char *dbus_interface)
{
	struct interfaces *iface;

	dl_list_for_each(iface, list, struct interfaces, list) {
		if (os_strcmp(iface->dbus_interface, dbus_interface) == 0)
			return iface; /* already in the list */
	}

	iface = os_zalloc(sizeof(struct interfaces));
	if (!iface)
		return NULL;
	iface->dbus_interface = os_strdup(dbus_interface);
	iface->xml = wpabuf_alloc(15000);
	if (iface->dbus_interface == NULL || iface->xml == NULL) {
		os_free(iface->dbus_interface);
		wpabuf_free(iface->xml);
		os_free(iface);
		return NULL;
	}
	wpabuf_printf(iface->xml, "<interface name=\"%s\">", dbus_interface);
	dl_list_add_tail(list, &iface->list);
	return iface;
}


static void add_arg(struct wpabuf *xml, const char *name, const char *type,
		    const char *direction)
{
	wpabuf_printf(xml, "<arg name=\"%s\"", name);
	if (type)
		wpabuf_printf(xml, " type=\"%s\"", type);
	if (direction)
		wpabuf_printf(xml, " direction=\"%s\"", direction);
	wpabuf_put_str(xml, "/>");
}


static void add_entry(struct wpabuf *xml, const char *type, const char *name,
		      const struct wpa_dbus_argument *args, int include_dir)
{
	const struct wpa_dbus_argument *arg;

	if (args == NULL || args->name == NULL) {
		wpabuf_printf(xml, "<%s name=\"%s\"/>", type, name);
		return;
	}
	wpabuf_printf(xml, "<%s name=\"%s\">", type, name);
	for (arg = args; arg && arg->name; arg++) {
		add_arg(xml, arg->name, arg->type,
			include_dir ? (arg->dir == ARG_IN ? "in" : "out") :
			NULL);
	}
	wpabuf_printf(xml, "</%s>", type);
}


static void add_property(struct wpabuf *xml,
			 const struct wpa_dbus_property_desc *dsc)
{
	wpabuf_printf(xml, "<property name=\"%s\" type=\"%s\" "
		      "access=\"%s%s\"/>",
		      dsc->dbus_property, dsc->type,
		      dsc->getter ? "read" : "",
		      dsc->setter ? "write" : "");
}


static void extract_interfaces_methods(
	struct dl_list *list, const struct wpa_dbus_method_desc *methods)
{
	const struct wpa_dbus_method_desc *dsc;
	struct interfaces *iface;

	for (dsc = methods; dsc && dsc->dbus_method; dsc++) {
		iface = add_interface(list, dsc->dbus_interface);
		if (iface)
			add_entry(iface->xml, "method", dsc->dbus_method,
				  dsc->args, 1);
	}
}


static void extract_interfaces_signals(
	struct dl_list *list, const struct wpa_dbus_signal_desc *signals)
{
	const struct wpa_dbus_signal_desc *dsc;
	struct interfaces *iface;

	for (dsc = signals; dsc && dsc->dbus_signal; dsc++) {
		iface = add_interface(list, dsc->dbus_interface);
		if (iface)
			add_entry(iface->xml, "signal", dsc->dbus_signal,
				  dsc->args, 0);
	}
}


static void extract_interfaces_properties(
	struct dl_list *list, const struct wpa_dbus_property_desc *properties)
{
	const struct wpa_dbus_property_desc *dsc;
	struct interfaces *iface;

	for (dsc = properties; dsc && dsc->dbus_property; dsc++) {
		iface = add_interface(list, dsc->dbus_interface);
		if (iface)
			add_property(iface->xml, dsc);
	}
}


/**
 * extract_interfaces - Extract interfaces from methods, signals and props
 * @list: Interface list to be filled
 * @obj_dsc: Description of object from which interfaces will be extracted
 *
 * Iterates over all methods, signals, and properties registered with an
 * object and collects all declared DBus interfaces and create interfaces'
 * node in XML root node for each. Returned list elements contain interface
 * name and XML node of corresponding interface.
 */
static void extract_interfaces(struct dl_list *list,
			       struct wpa_dbus_object_desc *obj_dsc)
{
	extract_interfaces_methods(list, obj_dsc->methods);
	extract_interfaces_signals(list, obj_dsc->signals);
	extract_interfaces_properties(list, obj_dsc->properties);
}


static void add_interfaces(struct dl_list *list, struct wpabuf *xml)
{
	struct interfaces *iface, *n;

	dl_list_for_each_safe(iface, n, list, struct interfaces, list) {
		if (wpabuf_len(iface->xml) + 20 < wpabuf_tailroom(xml)) {
			wpabuf_put_buf(xml, iface->xml);
			wpabuf_put_str(xml, "</interface>");
		} else {
			wpa_printf(MSG_DEBUG,
				   "dbus: Not enough room for add_interfaces inspect data: tailroom %u, add %u",
				   (unsigned int) wpabuf_tailroom(xml),
				   (unsigned int) wpabuf_len(iface->xml));
		}
		dl_list_del(&iface->list);
		wpabuf_free(iface->xml);
		os_free(iface->dbus_interface);
		os_free(iface);
	}
}


static void add_child_nodes(struct wpabuf *xml, DBusConnection *con,
			    const char *path)
{
	char **children;
	int i;

	/* add child nodes to introspection tree */
	dbus_connection_list_registered(con, path, &children);
	for (i = 0; children[i]; i++)
		wpabuf_printf(xml, "<node name=\"%s\"/>", children[i]);
	dbus_free_string_array(children);
}


static void add_introspectable_interface(struct wpabuf *xml)
{
	wpabuf_printf(xml, "<interface name=\"%s\">"
		      "<method name=\"%s\">"
		      "<arg name=\"data\" type=\"s\" direction=\"out\"/>"
		      "</method>"
		      "</interface>",
		      WPA_DBUS_INTROSPECTION_INTERFACE,
		      WPA_DBUS_INTROSPECTION_METHOD);
}


static void add_properties_interface(struct wpabuf *xml)
{
	wpabuf_printf(xml, "<interface name=\"%s\">",
		      WPA_DBUS_PROPERTIES_INTERFACE);

	wpabuf_printf(xml, "<method name=\"%s\">", WPA_DBUS_PROPERTIES_GET);
	add_arg(xml, "interface", "s", "in");
	add_arg(xml, "propname", "s", "in");
	add_arg(xml, "value", "v", "out");
	wpabuf_put_str(xml, "</method>");

	wpabuf_printf(xml, "<method name=\"%s\">", WPA_DBUS_PROPERTIES_GETALL);
	add_arg(xml, "interface", "s", "in");
	add_arg(xml, "props", "a{sv}", "out");
	wpabuf_put_str(xml, "</method>");

	wpabuf_printf(xml, "<method name=\"%s\">", WPA_DBUS_PROPERTIES_SET);
	add_arg(xml, "interface", "s", "in");
	add_arg(xml, "propname", "s", "in");
	add_arg(xml, "value", "v", "in");
	wpabuf_put_str(xml, "</method>");

	wpabuf_put_str(xml, "</interface>");
}


static void add_wpas_interfaces(struct wpabuf *xml,
				struct wpa_dbus_object_desc *obj_dsc)
{
	struct dl_list ifaces;

	dl_list_init(&ifaces);
	extract_interfaces(&ifaces, obj_dsc);
	add_interfaces(&ifaces, xml);
}


/**
 * wpa_dbus_introspect - Responds for Introspect calls on object
 * @message: Message with Introspect call
 * @obj_dsc: Object description on which Introspect was called
 * Returns: Message with introspection result XML string as only argument
 *
 * Iterates over all methods, signals and properties registered with
 * object and generates introspection data for the object as XML string.
 */
DBusMessage * wpa_dbus_introspect(DBusMessage *message,
				  struct wpa_dbus_object_desc *obj_dsc)
{

	DBusMessage *reply;
	struct wpabuf *xml;

	xml = wpabuf_alloc(20000);
	if (xml == NULL)
		return NULL;

	wpabuf_put_str(xml, "<?xml version=\"1.0\"?>\n");
	wpabuf_put_str(xml, DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE);
	wpabuf_put_str(xml, "<node>");

	add_introspectable_interface(xml);
	add_properties_interface(xml);
	add_wpas_interfaces(xml, obj_dsc);
	add_child_nodes(xml, obj_dsc->connection,
			dbus_message_get_path(message));

	wpabuf_put_str(xml, "</node>\n");

	reply = dbus_message_new_method_return(message);
	if (reply) {
		const char *intro_str = wpabuf_head(xml);

		dbus_message_append_args(reply, DBUS_TYPE_STRING, &intro_str,
					 DBUS_TYPE_INVALID);
	}
	wpabuf_free(xml);

	return reply;
}
