/*
 *  ntp_keyacc.h - key access stuff
 */
#ifndef NTP_KEYACC_H
#define NTP_KEYACC_H

typedef struct keyaccess KeyAccT;
struct keyaccess {
	KeyAccT *	next;
	sockaddr_u	addr;
	unsigned int	subnetbits;
};

extern KeyAccT* keyacc_new_push(KeyAccT *head, const sockaddr_u *addr,
				unsigned int subnetbits);
extern KeyAccT* keyacc_pop_free(KeyAccT *head);
extern KeyAccT* keyacc_all_free(KeyAccT *head);
extern int      keyacc_contains(const KeyAccT *head, const sockaddr_u *addr,
				int res_on_empty_list);
/* public for testability: */
extern int	keyacc_amatch(const sockaddr_u *,const sockaddr_u *,
			      unsigned int mbits);


#endif	/* NTP_KEYACC_H */
