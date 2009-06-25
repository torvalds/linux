#define ERROR_CODE(x)			((x) & 0xffff)
#define EXT_ERROR_CODE(x)		(((x) >> 16) & 0x1f)
#define LOW_SYNDROME(x)			(((x) >> 15) & 0xff)
#define HIGH_SYNDROME(x)		(((x) >> 24) & 0xff)

#define TLB_ERROR(x)			(((x) & 0xFFF0) == 0x0010)
#define MEM_ERROR(x)			(((x) & 0xFF00) == 0x0100)
#define BUS_ERROR(x)			(((x) & 0xF800) == 0x0800)

#define TT(x)				(((x) >> 2) & 0x3)
#define TT_MSG(x)			tt_msgs[TT(x)]
#define II(x)				(((x) >> 2) & 0x3)
#define II_MSG(x)			ii_msgs[II(x)]
#define LL(x)				(((x) >> 0) & 0x3)
#define LL_MSG(x)			ll_msgs[LL(x)]
#define RRRR(x)				(((x) >> 4) & 0xf)
#define RRRR_MSG(x)			rrrr_msgs[RRRR(x)]
#define TO(x)				(((x) >> 8) & 0x1)
#define TO_MSG(x)			to_msgs[TO(x)]
#define PP(x)				(((x) >> 9) & 0x3)
#define PP_MSG(x)			pp_msgs[PP(x)]

extern const char *tt_msgs[];
extern const char *ll_msgs[];
extern const char *rrrr_msgs[];
extern const char *pp_msgs[];
extern const char *to_msgs[];
extern const char *ii_msgs[];
extern const char *ext_msgs[];
