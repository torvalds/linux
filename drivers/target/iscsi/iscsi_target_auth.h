/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ISCSI_CHAP_H_
#define _ISCSI_CHAP_H_

#include <linux/types.h>

#define CHAP_DIGEST_UNKNOWN	0
#define CHAP_DIGEST_MD5		5
#define CHAP_DIGEST_SHA1	6
#define CHAP_DIGEST_SHA256	7
#define CHAP_DIGEST_SHA3_256	8

#define MAX_CHAP_CHALLENGE_LEN	32
#define CHAP_CHALLENGE_STR_LEN	4096
#define MAX_RESPONSE_LENGTH	128	/* sufficient for SHA3 256 */
#define	MAX_CHAP_N_SIZE		512

#define CHAP_STAGE_CLIENT_A	1
#define CHAP_STAGE_SERVER_AIC	2
#define CHAP_STAGE_CLIENT_NR	3
#define CHAP_STAGE_CLIENT_NRIC	4
#define CHAP_STAGE_SERVER_NR	5

struct iscsi_node_auth;
struct iscsit_conn;

extern u32 chap_main_loop(struct iscsit_conn *, struct iscsi_node_auth *, char *, char *,
				int *, int *);

struct iscsi_chap {
	unsigned char	id;
	unsigned char	challenge[MAX_CHAP_CHALLENGE_LEN];
	unsigned int	challenge_len;
	const char	*digest_name;
	int		digest_type;
	unsigned int	digest_size;
	unsigned int	authenticate_target;
	unsigned int	chap_state;
} ____cacheline_aligned;

#endif   /*** _ISCSI_CHAP_H_ ***/
