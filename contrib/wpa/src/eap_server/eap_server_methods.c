/*
 * EAP server method registration
 * Copyright (c) 2004-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "eap_i.h"
#include "eap_methods.h"


static struct eap_method *eap_methods;


/**
 * eap_server_get_eap_method - Get EAP method based on type number
 * @vendor: EAP Vendor-Id (0 = IETF)
 * @method: EAP type number
 * Returns: Pointer to EAP method or %NULL if not found
 */
const struct eap_method * eap_server_get_eap_method(int vendor, EapType method)
{
	struct eap_method *m;
	for (m = eap_methods; m; m = m->next) {
		if (m->vendor == vendor && m->method == method)
			return m;
	}
	return NULL;
}


/**
 * eap_server_get_type - Get EAP type for the given EAP method name
 * @name: EAP method name, e.g., TLS
 * @vendor: Buffer for returning EAP Vendor-Id
 * Returns: EAP method type or %EAP_TYPE_NONE if not found
 *
 * This function maps EAP type names into EAP type numbers based on the list of
 * EAP methods included in the build.
 */
EapType eap_server_get_type(const char *name, int *vendor)
{
	struct eap_method *m;
	for (m = eap_methods; m; m = m->next) {
		if (os_strcmp(m->name, name) == 0) {
			*vendor = m->vendor;
			return m->method;
		}
	}
	*vendor = EAP_VENDOR_IETF;
	return EAP_TYPE_NONE;
}


/**
 * eap_server_method_alloc - Allocate EAP server method structure
 * @version: Version of the EAP server method interface (set to
 * EAP_SERVER_METHOD_INTERFACE_VERSION)
 * @vendor: EAP Vendor-ID (EAP_VENDOR_*) (0 = IETF)
 * @method: EAP type number (EAP_TYPE_*)
 * @name: Name of the method (e.g., "TLS")
 * Returns: Allocated EAP method structure or %NULL on failure
 *
 * The returned structure should be freed with eap_server_method_free() when it
 * is not needed anymore.
 */
struct eap_method * eap_server_method_alloc(int version, int vendor,
					    EapType method, const char *name)
{
	struct eap_method *eap;
	eap = os_zalloc(sizeof(*eap));
	if (eap == NULL)
		return NULL;
	eap->version = version;
	eap->vendor = vendor;
	eap->method = method;
	eap->name = name;
	return eap;
}


/**
 * eap_server_method_free - Free EAP server method structure
 * @method: Method structure allocated with eap_server_method_alloc()
 */
static void eap_server_method_free(struct eap_method *method)
{
	os_free(method);
}


/**
 * eap_server_method_register - Register an EAP server method
 * @method: EAP method to register from eap_server_method_alloc()
 * Returns: 0 on success, -1 on invalid method, or -2 if a matching EAP method
 * has already been registered
 *
 * Each EAP server method needs to call this function to register itself as a
 * supported EAP method. The caller must not free the allocated method data
 * regardless of the return value.
 */
int eap_server_method_register(struct eap_method *method)
{
	struct eap_method *m, *last = NULL;

	if (method == NULL || method->name == NULL ||
	    method->version != EAP_SERVER_METHOD_INTERFACE_VERSION) {
		eap_server_method_free(method);
		return -1;
	}

	for (m = eap_methods; m; m = m->next) {
		if ((m->vendor == method->vendor &&
		     m->method == method->method) ||
		    os_strcmp(m->name, method->name) == 0) {
			eap_server_method_free(method);
			return -2;
		}
		last = m;
	}

	if (last)
		last->next = method;
	else
		eap_methods = method;

	return 0;
}


/**
 * eap_server_unregister_methods - Unregister EAP server methods
 *
 * This function is called at program termination to unregister all EAP server
 * methods.
 */
void eap_server_unregister_methods(void)
{
	struct eap_method *m;

	while (eap_methods) {
		m = eap_methods;
		eap_methods = eap_methods->next;

		if (m->free)
			m->free(m);
		else
			eap_server_method_free(m);
	}
}


/**
 * eap_server_get_name - Get EAP method name for the given EAP type
 * @vendor: EAP Vendor-Id (0 = IETF)
 * @type: EAP method type
 * Returns: EAP method name, e.g., TLS, or "unknown" if not found
 *
 * This function maps EAP type numbers into EAP type names based on the list of
 * EAP methods included in the build.
 */
const char * eap_server_get_name(int vendor, EapType type)
{
	struct eap_method *m;
	if (vendor == EAP_VENDOR_IETF && type == EAP_TYPE_EXPANDED)
		return "expanded";
	for (m = eap_methods; m; m = m->next) {
		if (m->vendor == vendor && m->method == type)
			return m->name;
	}
	return "unknown";
}
