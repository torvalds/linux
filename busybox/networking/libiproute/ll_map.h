/* vi: set sw=4 ts=4: */
#ifndef LL_MAP_H
#define LL_MAP_H 1

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

int ll_remember_index(const struct sockaddr_nl *who, struct nlmsghdr *n, void *arg) FAST_FUNC;
int ll_init_map(struct rtnl_handle *rth) FAST_FUNC;
int xll_name_to_index(const char *name) FAST_FUNC;
//static: const char *ll_idx_n2a(int idx, char *buf) FAST_FUNC;
const char *ll_index_to_name(int idx) FAST_FUNC;
/* int ll_index_to_type(int idx); */
unsigned ll_index_to_flags(int idx) FAST_FUNC;

POP_SAVED_FUNCTION_VISIBILITY

#endif
