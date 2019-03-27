/* XXX: Depends on m_hdr */
#ifdef _LP64
#define M_NEXT 0
#define M_DATA 16
#define M_LEN 32
#else
#define M_NEXT 0
#define M_DATA 8
#define M_LEN 16
#endif
