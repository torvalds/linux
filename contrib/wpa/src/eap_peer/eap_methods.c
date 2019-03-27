/*
 * EAP peer: Method registration
 * Copyright (c) 2004-2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#ifdef CONFIG_DYNAMIC_EAP_METHODS
#include <dlfcn.h>
#endif /* CONFIG_DYNAMIC_EAP_METHODS */

#include "common.h"
#include "eap_i.h"
#include "eap_methods.h"


static struct eap_method *eap_methods = NULL;

static void eap_peer_method_free(struct eap_method *method);


/**
 * eap_peer_get_eap_method - Get EAP method based on type number
 * @vendor: EAP Vendor-Id (0 = IETF)
 * @method: EAP type number
 * Returns: Pointer to EAP method or %NULL if not found
 */
const struct eap_method * eap_peer_get_eap_method(int vendor, EapType method)
{
	struct eap_method *m;
	for (m = eap_methods; m; m = m->next) {
		if (m->vendor == vendor && m->method == method)
			return m;
	}
	return NULL;
}


/**
 * eap_peer_get_type - Get EAP type for the given EAP method name
 * @name: EAP method name, e.g., TLS
 * @vendor: Buffer for returning EAP Vendor-Id
 * Returns: EAP method type or %EAP_TYPE_NONE if not found
 *
 * This function maps EAP type names into EAP type numbers based on the list of
 * EAP methods included in the build.
 */
EapType eap_peer_get_type(const char *name, int *vendor)
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
 * eap_get_name - Get EAP method name for the given EAP type
 * @vendor: EAP Vendor-Id (0 = IETF)
 * @type: EAP method type
 * Returns: EAP method name, e.g., TLS, or %NULL if not found
 *
 * This function maps EAP type numbers into EAP type names based on the list of
 * EAP methods included in the build.
 */
const char * eap_get_name(int vendor, EapType type)
{
	struct eap_method *m;
	if (vendor == EAP_VENDOR_IETF && type == EAP_TYPE_EXPANDED)
		return "expanded";
	for (m = eap_methods; m; m = m->next) {
		if (m->vendor == vendor && m->method == type)
			return m->name;
	}
	return NULL;
}


/**
 * eap_get_names - Get space separated list of names for supported EAP methods
 * @buf: Buffer for names
 * @buflen: Buffer length
 * Returns: Number of characters written into buf (not including nul
 * termination)
 */
size_t eap_get_names(char *buf, size_t buflen)
{
	char *pos, *end;
	struct eap_method *m;
	int ret;

	if (buflen == 0)
		return 0;

	pos = buf;
	end = pos + buflen;

	for (m = eap_methods; m; m = m->next) {
		ret = os_snprintf(pos, end - pos, "%s%s",
				  m == eap_methods ? "" : " ", m->name);
		if (os_snprintf_error(end - pos, ret))
			break;
		pos += ret;
	}
	buf[buflen - 1] = '\0';

	return pos - buf;
}


/**
 * eap_get_names_as_string_array - Get supported EAP methods as string array
 * @num: Buffer for returning the number of items in array, not including %NULL
 * terminator. This parameter can be %NULL if the length is not needed.
 * Returns: A %NULL-terminated array of strings, or %NULL on error.
 *
 * This function returns the list of names for all supported EAP methods as an
 * array of strings. The caller must free the returned array items and the
 * array.
 */
char ** eap_get_names_as_string_array(size_t *num)
{
	struct eap_method *m;
	size_t array_len = 0;
	char **array;
	int i = 0, j;

	for (m = eap_methods; m; m = m->next)
		array_len++;

	array = os_calloc(array_len + 1, sizeof(char *));
	if (array == NULL)
		return NULL;

	for (m = eap_methods; m; m = m->next) {
		array[i++] = os_strdup(m->name);
		if (array[i - 1] == NULL) {
			for (j = 0; j < i; j++)
				os_free(array[j]);
			os_free(array);
			return NULL;
		}
	}
	array[i] = NULL;

	if (num)
		*num = array_len;

	return array;
}


/**
 * eap_peer_get_methods - Get a list of enabled EAP peer methods
 * @count: Set to number of available methods
 * Returns: List of enabled EAP peer methods
 */
const struct eap_method * eap_peer_get_methods(size_t *count)
{
	int c = 0;
	struct eap_method *m;

	for (m = eap_methods; m; m = m->next)
		c++;
	
	*count = c;
	return eap_methods;
}


#ifdef CONFIG_DYNAMIC_EAP_METHODS
/**
 * eap_peer_method_load - Load a dynamic EAP method library (shared object)
 * @so: File path for the shared object file to load
 * Returns: 0 on success, -1 on failure
 */
int eap_peer_method_load(const char *so)
{
	void *handle;
	int (*dyn_init)(void);
	int ret;

	handle = dlopen(so, RTLD_LAZY);
	if (handle == NULL) {
		wpa_printf(MSG_ERROR, "EAP: Failed to open dynamic EAP method "
			   "'%s': %s", so, dlerror());
		return -1;
	}

	dyn_init = dlsym(handle, "eap_peer_method_dynamic_init");
	if (dyn_init == NULL) {
		dlclose(handle);
		wpa_printf(MSG_ERROR, "EAP: Invalid EAP method '%s' - no "
			   "eap_peer_method_dynamic_init()", so);
		return -1;
	}

	ret = dyn_init();
	if (ret) {
		dlclose(handle);
		wpa_printf(MSG_ERROR, "EAP: Failed to add EAP method '%s' - "
			   "ret %d", so, ret);
		return ret;
	}

	/* Store the handle for this shared object. It will be freed with
	 * dlclose() when the EAP method is unregistered. */
	eap_methods->dl_handle = handle;

	wpa_printf(MSG_DEBUG, "EAP: Loaded dynamic EAP method: '%s'", so);

	return 0;
}


/**
 * eap_peer_method_unload - Unload a dynamic EAP method library (shared object)
 * @method: Pointer to the dynamically loaded EAP method
 * Returns: 0 on success, -1 on failure
 *
 * This function can be used to unload EAP methods that have been previously
 * loaded with eap_peer_method_load(). Before unloading the method, all
 * references to the method must be removed to make sure that no dereferences
 * of freed memory will occur after unloading.
 */
int eap_peer_method_unload(struct eap_method *method)
{
	struct eap_method *m, *prev;
	void *handle;

	m = eap_methods;
	prev = NULL;
	while (m) {
		if (m == method)
			break;
		prev = m;
		m = m->next;
	}

	if (m == NULL || m->dl_handle == NULL)
		return -1;

	if (prev)
		prev->next = m->next;
	else
		eap_methods = m->next;

	handle = m->dl_handle;

	if (m->free)
		m->free(m);
	else
		eap_peer_method_free(m);

	dlclose(handle);

	return 0;
}
#endif /* CONFIG_DYNAMIC_EAP_METHODS */


/**
 * eap_peer_method_alloc - Allocate EAP peer method structure
 * @version: Version of the EAP peer method interface (set to
 * EAP_PEER_METHOD_INTERFACE_VERSION)
 * @vendor: EAP Vendor-ID (EAP_VENDOR_*) (0 = IETF)
 * @method: EAP type number (EAP_TYPE_*)
 * @name: Name of the method (e.g., "TLS")
 * Returns: Allocated EAP method structure or %NULL on failure
 *
 * The returned structure should be freed with eap_peer_method_free() when it
 * is not needed anymore.
 */
struct eap_method * eap_peer_method_alloc(int version, int vendor,
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
 * eap_peer_method_free - Free EAP peer method structure
 * @method: Method structure allocated with eap_peer_method_alloc()
 */
static void eap_peer_method_free(struct eap_method *method)
{
	os_free(method);
}


/**
 * eap_peer_method_register - Register an EAP peer method
 * @method: EAP method to register from eap_peer_method_alloc()
 * Returns: 0 on success, -1 on invalid method, or -2 if a matching EAP method
 * has already been registered
 *
 * Each EAP peer method needs to call this function to register itself as a
 * supported EAP method. The caller must not free the allocated method data
 * regardless of the return value.
 */
int eap_peer_method_register(struct eap_method *method)
{
	struct eap_method *m, *last = NULL;

	if (method == NULL || method->name == NULL ||
	    method->version != EAP_PEER_METHOD_INTERFACE_VERSION) {
		eap_peer_method_free(method);
		return -1;
	}

	for (m = eap_methods; m; m = m->next) {
		if ((m->vendor == method->vendor &&
		     m->method == method->method) ||
		    os_strcmp(m->name, method->name) == 0) {
			eap_peer_method_free(method);
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
 * eap_peer_unregister_methods - Unregister EAP peer methods
 *
 * This function is called at program termination to unregister all EAP peer
 * methods.
 */
void eap_peer_unregister_methods(void)
{
	struct eap_method *m;
#ifdef CONFIG_DYNAMIC_EAP_METHODS
	void *handle;
#endif /* CONFIG_DYNAMIC_EAP_METHODS */

	while (eap_methods) {
		m = eap_methods;
		eap_methods = eap_methods->next;

#ifdef CONFIG_DYNAMIC_EAP_METHODS
		handle = m->dl_handle;
#endif /* CONFIG_DYNAMIC_EAP_METHODS */

		if (m->free)
			m->free(m);
		else
			eap_peer_method_free(m);

#ifdef CONFIG_DYNAMIC_EAP_METHODS
		if (handle)
			dlclose(handle);
#endif /* CONFIG_DYNAMIC_EAP_METHODS */
	}
}
