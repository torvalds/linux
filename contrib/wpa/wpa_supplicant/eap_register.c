/*
 * EAP method registration
 * Copyright (c) 2004-2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "eap_peer/eap_methods.h"
#include "eap_server/eap_methods.h"
#include "wpa_supplicant_i.h"


/**
 * eap_register_methods - Register statically linked EAP methods
 * Returns: 0 on success, -1 or -2 on failure
 *
 * This function is called at program initialization to register all EAP
 * methods that were linked in statically.
 */
int eap_register_methods(void)
{
	int ret = 0;

#ifdef EAP_MD5
	if (ret == 0)
		ret = eap_peer_md5_register();
#endif /* EAP_MD5 */

#ifdef EAP_TLS
	if (ret == 0)
		ret = eap_peer_tls_register();
#endif /* EAP_TLS */

#ifdef EAP_UNAUTH_TLS
	if (ret == 0)
		ret = eap_peer_unauth_tls_register();
#endif /* EAP_UNAUTH_TLS */

#ifdef EAP_TLS
#ifdef CONFIG_HS20
	if (ret == 0)
		ret = eap_peer_wfa_unauth_tls_register();
#endif /* CONFIG_HS20 */
#endif /* EAP_TLS */

#ifdef EAP_MSCHAPv2
	if (ret == 0)
		ret = eap_peer_mschapv2_register();
#endif /* EAP_MSCHAPv2 */

#ifdef EAP_PEAP
	if (ret == 0)
		ret = eap_peer_peap_register();
#endif /* EAP_PEAP */

#ifdef EAP_TTLS
	if (ret == 0)
		ret = eap_peer_ttls_register();
#endif /* EAP_TTLS */

#ifdef EAP_GTC
	if (ret == 0)
		ret = eap_peer_gtc_register();
#endif /* EAP_GTC */

#ifdef EAP_OTP
	if (ret == 0)
		ret = eap_peer_otp_register();
#endif /* EAP_OTP */

#ifdef EAP_SIM
	if (ret == 0)
		ret = eap_peer_sim_register();
#endif /* EAP_SIM */

#ifdef EAP_LEAP
	if (ret == 0)
		ret = eap_peer_leap_register();
#endif /* EAP_LEAP */

#ifdef EAP_PSK
	if (ret == 0)
		ret = eap_peer_psk_register();
#endif /* EAP_PSK */

#ifdef EAP_AKA
	if (ret == 0)
		ret = eap_peer_aka_register();
#endif /* EAP_AKA */

#ifdef EAP_AKA_PRIME
	if (ret == 0)
		ret = eap_peer_aka_prime_register();
#endif /* EAP_AKA_PRIME */

#ifdef EAP_FAST
	if (ret == 0)
		ret = eap_peer_fast_register();
#endif /* EAP_FAST */

#ifdef EAP_PAX
	if (ret == 0)
		ret = eap_peer_pax_register();
#endif /* EAP_PAX */

#ifdef EAP_SAKE
	if (ret == 0)
		ret = eap_peer_sake_register();
#endif /* EAP_SAKE */

#ifdef EAP_GPSK
	if (ret == 0)
		ret = eap_peer_gpsk_register();
#endif /* EAP_GPSK */

#ifdef EAP_WSC
	if (ret == 0)
		ret = eap_peer_wsc_register();
#endif /* EAP_WSC */

#ifdef EAP_IKEV2
	if (ret == 0)
		ret = eap_peer_ikev2_register();
#endif /* EAP_IKEV2 */

#ifdef EAP_VENDOR_TEST
	if (ret == 0)
		ret = eap_peer_vendor_test_register();
#endif /* EAP_VENDOR_TEST */

#ifdef EAP_TNC
	if (ret == 0)
		ret = eap_peer_tnc_register();
#endif /* EAP_TNC */

#ifdef EAP_PWD
	if (ret == 0)
		ret = eap_peer_pwd_register();
#endif /* EAP_PWD */

#ifdef EAP_EKE
	if (ret == 0)
		ret = eap_peer_eke_register();
#endif /* EAP_EKE */

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

#ifdef EAP_SERVER_UNAUTH_TLS
	if (ret == 0)
		ret = eap_server_unauth_tls_register();
#endif /* EAP_SERVER_UNAUTH_TLS */

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
