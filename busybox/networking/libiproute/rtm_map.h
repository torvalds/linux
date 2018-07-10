/* vi: set sw=4 ts=4: */
#ifndef RTM_MAP_H
#define RTM_MAP_H 1

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

const char *rtnl_rtntype_n2a(int id) FAST_FUNC;
int rtnl_rtntype_a2n(int *id, char *arg) FAST_FUNC;

int get_rt_realms(uint32_t *realms, char *arg) FAST_FUNC;

POP_SAVED_FUNCTION_VISIBILITY

#endif
