#ifndef _ISCSI_CHAP_H_
#define _ISCSI_CHAP_H_

#define CHAP_DIGEST_MD5		5
#define CHAP_DIGEST_SHA		6

#define CHAP_CHALLENGE_LENGTH	16
#define CHAP_CHALLENGE_STR_LEN	4096
#define MAX_RESPONSE_LENGTH	64	/* sufficient for MD5 */
#define	MAX_CHAP_N_SIZE		512

#define MD5_SIGNATURE_SIZE	16	/* 16 bytes in a MD5 message digest */

#define CHAP_STAGE_CLIENT_A	1
#define CHAP_STAGE_SERVER_AIC	2
#define CHAP_STAGE_CLIENT_NR	3
#define CHAP_STAGE_CLIENT_NRIC	4
#define CHAP_STAGE_SERVER_NR	5

extern u32 chap_main_loop(struct iscsi_conn *, struct iscsi_node_auth *, char *, char *,
				int *, int *);

struct iscsi_chap {
	unsigned char	digest_type;
	unsigned char	id;
	unsigned char	challenge[CHAP_CHALLENGE_LENGTH];
	unsigned int	authenticate_target;
	unsigned int	chap_state;
} ____cacheline_aligned;

#endif   /*** _ISCSI_CHAP_H_ ***/
