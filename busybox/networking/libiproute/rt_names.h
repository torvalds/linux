/* vi: set sw=4 ts=4: */
#ifndef RT_NAMES_H
#define RT_NAMES_H 1

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

extern const char* rtnl_rtprot_n2a(int id) FAST_FUNC;
extern const char* rtnl_rtscope_n2a(int id) FAST_FUNC;
extern const char* rtnl_rtrealm_n2a(int id) FAST_FUNC;
extern const char* rtnl_dsfield_n2a(int id) FAST_FUNC;
extern const char* rtnl_rttable_n2a(int id) FAST_FUNC;
extern int rtnl_rtprot_a2n(uint32_t *id, char *arg) FAST_FUNC;
extern int rtnl_rtscope_a2n(uint32_t *id, char *arg) FAST_FUNC;
extern int rtnl_rtrealm_a2n(uint32_t *id, char *arg) FAST_FUNC;
extern int rtnl_dsfield_a2n(uint32_t *id, char *arg) FAST_FUNC;
extern int rtnl_rttable_a2n(uint32_t *id, char *arg) FAST_FUNC;

extern const char* ll_type_n2a(int type, char *buf) FAST_FUNC;

extern const char* ll_addr_n2a(unsigned char *addr, int alen, int type,
				char *buf, int blen) FAST_FUNC;
extern int ll_addr_a2n(unsigned char *lladdr, int len, char *arg) FAST_FUNC;

extern const char* ll_proto_n2a(unsigned short id, char *buf, int len) FAST_FUNC;
extern int ll_proto_a2n(unsigned short *id, char *buf) FAST_FUNC;

POP_SAVED_FUNCTION_VISIBILITY

#endif
