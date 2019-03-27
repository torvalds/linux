/*
 * WPA Supplicant / PC/SC smartcard interface for USIM, GSM SIM
 * Copyright (c) 2004-2006, 2012, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef PCSC_FUNCS_H
#define PCSC_FUNCS_H

#ifdef PCSC_FUNCS
struct scard_data * scard_init(const char *reader);
void scard_deinit(struct scard_data *scard);

int scard_set_pin(struct scard_data *scard, const char *pin);
int scard_get_imsi(struct scard_data *scard, char *imsi, size_t *len);
int scard_get_mnc_len(struct scard_data *scard);
int scard_gsm_auth(struct scard_data *scard, const unsigned char *_rand,
		   unsigned char *sres, unsigned char *kc);
int scard_umts_auth(struct scard_data *scard, const unsigned char *_rand,
		    const unsigned char *autn,
		    unsigned char *res, size_t *res_len,
		    unsigned char *ik, unsigned char *ck, unsigned char *auts);
int scard_get_pin_retry_counter(struct scard_data *scard);
int scard_supports_umts(struct scard_data *scard);

#else /* PCSC_FUNCS */

#define scard_init(r) NULL
#define scard_deinit(s) do { } while (0)
#define scard_set_pin(s, p) -1
#define scard_get_imsi(s, i, l) -1
#define scard_get_mnc_len(s) -1
#define scard_gsm_auth(s, r, s2, k) -1
#define scard_umts_auth(s, r, a, r2, rl, i, c, a2) -1
#define scard_get_pin_retry_counter(s) -1
#define scard_supports_umts(s) 0

#endif /* PCSC_FUNCS */

#endif /* PCSC_FUNCS_H */
