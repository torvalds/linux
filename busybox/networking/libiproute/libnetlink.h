/* vi: set sw=4 ts=4: */
#ifndef LIBNETLINK_H
#define LIBNETLINK_H 1

#include <linux/types.h>
/* We need linux/types.h because older kernels use __u32 etc
 * in linux/[rt]netlink.h. 2.6.19 seems to be ok, though */
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

struct rtnl_handle {
	int                fd;
	struct sockaddr_nl local;
	struct sockaddr_nl peer;
	uint32_t           seq;
	uint32_t           dump;
};

extern void xrtnl_open(struct rtnl_handle *rth) FAST_FUNC;
#define rtnl_close(rth) (close((rth)->fd))
extern int xrtnl_wilddump_request(struct rtnl_handle *rth, int fam, int type) FAST_FUNC;
extern int rtnl_dump_request(struct rtnl_handle *rth, int type, void *req, int len) FAST_FUNC;
extern int xrtnl_dump_filter(struct rtnl_handle *rth,
		int (*filter)(const struct sockaddr_nl*, struct nlmsghdr *n, void*) FAST_FUNC,
		void *arg1) FAST_FUNC;

/* bbox doesn't use parameters no. 3, 4, 6, 7, stub them out */
#define rtnl_talk(rtnl, n, peer, groups, answer, junk, jarg) \
	rtnl_talk(rtnl, n, answer)
extern int rtnl_talk(struct rtnl_handle *rtnl, struct nlmsghdr *n, pid_t peer,
		unsigned groups, struct nlmsghdr *answer,
		int (*junk)(struct sockaddr_nl *,struct nlmsghdr *n, void *),
		void *jarg) FAST_FUNC;

extern int rtnl_send(struct rtnl_handle *rth, char *buf, int) FAST_FUNC;


extern int addattr32(struct nlmsghdr *n, int maxlen, int type, uint32_t data) FAST_FUNC;
extern int addattr_l(struct nlmsghdr *n, int maxlen, int type, void *data, int alen) FAST_FUNC;
extern int rta_addattr32(struct rtattr *rta, int maxlen, int type, uint32_t data) FAST_FUNC;
extern int rta_addattr_l(struct rtattr *rta, int maxlen, int type, void *data, int alen) FAST_FUNC;

extern void parse_rtattr(struct rtattr *tb[], int max, struct rtattr *rta, int len) FAST_FUNC;

POP_SAVED_FUNCTION_VISIBILITY

#endif
