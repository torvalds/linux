#ifndef __WILC_LOG_H__
#define __WILC_LOG_H__

/* Errors will always get printed */
#define WILC_ERROR(...) do {  WILC_PRINTF("(ERR)(%s:%d) ", __WILC_FUNCTION__, __WILC_LINE__); \
			      WILC_PRINTF(__VA_ARGS__); \
				} while (0)

/* Wraning only printed if verbosity is 1 or more */
#if (WILC_LOG_VERBOSITY_LEVEL > 0)
#define WILC_WARN(...) do { WILC_PRINTF("(WRN)"); \
			    WILC_PRINTF(__VA_ARGS__); \
				} while (0)
#else
#define WILC_WARN(...) (0)
#endif

/* Info only printed if verbosity is 2 or more */
#if (WILC_LOG_VERBOSITY_LEVEL > 1)
#define WILC_INFO(...) do {  WILC_PRINTF("(INF)"); \
			     WILC_PRINTF(__VA_ARGS__); \
				} while (0)
#else
#define WILC_INFO(...) (0)
#endif

/* Debug is only printed if verbosity is 3 or more */
#if (WILC_LOG_VERBOSITY_LEVEL > 2)
#define WILC_DBG(...) do { WILC_PRINTF("(DBG)(%s:%d) ", __WILC_FUNCTION__, __WILC_LINE__); \
			   WILC_PRINTF(__VA_ARGS__); \
			} while (0)

#else
#define WILC_DBG(...) (0)
#endif

/* Function In/Out is only printed if verbosity is 4 or more */
#if (WILC_LOG_VERBOSITY_LEVEL > 3)
#define WILC_FN_IN do { WILC_PRINTF("(FIN) (%s:%d) \n", __WILC_FUNCTION__, __WILC_LINE__);  } while (0)
#define WILC_FN_OUT(ret) do { WILC_PRINTF("(FOUT) (%s:%d) %d.\n", __WILC_FUNCTION__, __WILC_LINE__, (ret));  } while (0)
#else
#define WILC_FN_IN (0)
#define WILC_FN_OUT(ret) (0)
#endif


#endif