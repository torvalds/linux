/*
 * EAP server method registration
 * Copyright (c) 2004-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_SERVER_METHODS_H
#define EAP_SERVER_METHODS_H

#include "eap_common/eap_defs.h"

const struct eap_method * eap_server_get_eap_method(int vendor,
						    EapType method);
struct eap_method * eap_server_method_alloc(int version, int vendor,
					    EapType method, const char *name);
int eap_server_method_register(struct eap_method *method);

EapType eap_server_get_type(const char *name, int *vendor);
void eap_server_unregister_methods(void);
const char * eap_server_get_name(int vendor, EapType type);

/* EAP server method registration calls for statically linked in methods */
int eap_server_identity_register(void);
int eap_server_md5_register(void);
int eap_server_tls_register(void);
int eap_server_unauth_tls_register(void);
int eap_server_wfa_unauth_tls_register(void);
int eap_server_mschapv2_register(void);
int eap_server_peap_register(void);
int eap_server_tlv_register(void);
int eap_server_gtc_register(void);
int eap_server_ttls_register(void);
int eap_server_sim_register(void);
int eap_server_aka_register(void);
int eap_server_aka_prime_register(void);
int eap_server_pax_register(void);
int eap_server_psk_register(void);
int eap_server_sake_register(void);
int eap_server_gpsk_register(void);
int eap_server_vendor_test_register(void);
int eap_server_fast_register(void);
int eap_server_wsc_register(void);
int eap_server_ikev2_register(void);
int eap_server_tnc_register(void);
int eap_server_pwd_register(void);
int eap_server_eke_register(void);

#endif /* EAP_SERVER_METHODS_H */
