#ifndef CRYPTO_H
#define CRYPTO_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <ntp_fp.h>
#include <ntp.h>
#include <ntp_stdlib.h>
#include <ntp_md5.h>	/* provides OpenSSL digest API */
#include "utilities.h"
#include "sntp-opts.h"

#define LEN_PKT_MAC	LEN_PKT_NOMAC + sizeof(u_int32)

/* #include "sntp-opts.h" */

struct key {
	struct key *	next;
	int		key_id;
	int		key_len;
	int		typei;
	char		typen[20];
	char		key_seq[64];
};

extern	int	auth_init(const char *keyfile, struct key **keys);
extern	void	get_key(int key_id, struct key **d_key);
extern	int	make_mac(const void *pkt_data, int pkt_size, int mac_size,
			 const struct key *cmp_key, void *digest);
extern	int	auth_md5(const void *pkt_data, int pkt_size, int mac_size,
			 const struct key *cmp_key);

#endif
