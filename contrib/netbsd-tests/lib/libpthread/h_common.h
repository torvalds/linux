#ifndef H_COMMON_H
#define H_COMMON_H

#include <string.h>

#define PTHREAD_REQUIRE(x) \
    do { \
        int _ret = (x); \
        ATF_REQUIRE_MSG(_ret == 0, "%s: %s", #x, strerror(_ret)); \
    } while (0)

#define PTHREAD_REQUIRE_STATUS(x, v) \
    do { \
        int _ret = (x); \
        ATF_REQUIRE_MSG(_ret == (v), "%s: %s", #x, strerror(_ret)); \
    } while (0)

#endif // H_COMMON_H
