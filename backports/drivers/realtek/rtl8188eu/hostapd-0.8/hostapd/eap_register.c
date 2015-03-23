/*
 * EAP method registration
 * Copyright (c) 2004-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "includes.h"

#include "common.h"
#include "eap_server/eap_methods.h"
#include "eap_register.h"


/**
 * eap_server_register_methods - Register statically linked EAP server methods
 * Returns: 0 on success, -1 or -2 on failure
 *
 * This function is called at program initialization to register all EAP
 * methods that were linked in statically.
 */
int eap_server_register_methods(void)
{
	int ret = 0;

#ifdef EAP_SERVER_IDENTITY
	if (ret == 0)
		ret = eap_server_identity_register();
#endif /* EAP_SERVER_IDENTITY */

#ifdef EAP_SERVER_MD5
	if (ret == 0)
		ret = eap_server_md5_register();
#endif /* EAP_SERVER_MD5 */

#ifdef EAP_SERVER_TLS
	if (ret == 0)
		ret = eap_server_tls_register();
#endif /* EAP_SERVER_TLS */

#ifdef EAP_SERVER_MSCHAPV2
	if (ret == 0)
		ret = eap_server_mschapv2_register();
#endif /* EAP_SERVER_MSCHAPV2 */

#ifdef EAP_SERVER_PEAP
	if (ret == 0)
		ret = eap_server_peap_register();
#endif /* EAP_SERVER_PEAP */

#ifdef EAP_SERVER_TLV
	if (ret == 0)
		ret = eap_server_tlv_register();
#endif /* EAP_SERVER_TLV */

#ifdef EAP_SERVER_GTC
	if (ret == 0)
		ret = eap_server_gtc_register();
#endif /* EAP_SERVER_GTC */

#ifdef EAP_SERVER_TTLS
	if (ret == 0)
		ret = eap_server_ttls_register();
#endif /* EAP_SERVER_TTLS */

#ifdef EAP_SERVER_SIM
	if (ret == 0)
		ret = eap_server_sim_register();
#endif /* EAP_SERVER_SIM */

#ifdef EAP_SERVER_AKA
	if (ret == 0)
		ret = eap_server_aka_register();
#endif /* EAP_SERVER_AKA */

#ifdef EAP_SERVER_AKA_PRIME
	if (ret == 0)
		ret = eap_server_aka_prime_register();
#endif /* EAP_SERVER_AKA_PRIME */

#ifdef EAP_SERVER_PAX
	if (ret == 0)
		ret = eap_server_pax_register();
#endif /* EAP_SERVER_PAX */

#ifdef EAP_SERVER_PSK
	if (ret == 0)
		ret = eap_server_psk_register();
#endif /* EAP_SERVER_PSK */

#ifdef EAP_SERVER_SAKE
	if (ret == 0)
		ret = eap_server_sake_register();
#endif /* EAP_SERVER_SAKE */

#ifdef EAP_SERVER_GPSK
	if (ret == 0)
		ret = eap_server_gpsk_register();
#endif /* EAP_SERVER_GPSK */

#ifdef EAP_SERVER_VENDOR_TEST
	if (ret == 0)
		ret = eap_server_vendor_test_register();
#endif /* EAP_SERVER_VENDOR_TEST */

#ifdef EAP_SERVER_FAST
	if (ret == 0)
		ret = eap_server_fast_register();
#endif /* EAP_SERVER_FAST */

#ifdef EAP_SERVER_WSC
	if (ret == 0)
		ret = eap_server_wsc_register();
#endif /* EAP_SERVER_WSC */

#ifdef EAP_SERVER_IKEV2
	if (ret == 0)
		ret = eap_server_ikev2_register();
#endif /* EAP_SERVER_IKEV2 */

#ifdef EAP_SERVER_TNC
	if (ret == 0)
		ret = eap_server_tnc_register();
#endif /* EAP_SERVER_TNC */

#ifdef EAP_SERVER_PWD
	if (ret == 0)
		ret = eap_server_pwd_register();
#endif /* EAP_SERVER_PWD */

	return ret;
}
