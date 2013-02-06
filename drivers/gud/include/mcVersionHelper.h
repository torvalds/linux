/** @addtogroup MC_RTM
 * @{
 * MobiCore Version Helper Macros
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 */
#include <stdio.h>

//lint -emacro(*,MC_CHECK_VERSION) Disable all warnings for this macro.
//lint -emacro(*,MC_MAKE_VERSION) Disable all warnings for this macro.
//lint -emacro(*,MC_GET_MAJOR_VERSION) Disable all warnings for this macro.
//lint -emacro(*,MC_GET_MINOR_VERSION) Disable all warnings for this macro.
//lint -emacro(*,MC_GET_MINOR_VERSION) Disable all warnings for this macro.
//lint -emacro(*,ASSERT_VERSION_IMPLEMENTATION) Disable all warnings for this macro.
//lint -esym(*,Actual_*) Disable all warnings for these functions.

/** Create a version number given major and minor numbers. */
#define MC_MAKE_VERSION(major,minor) \
    (   (((major) & 0xffff) << 16) |\
        ((minor) & 0xffff))

/** Get major version number from complete version. */
#define MC_GET_MAJOR_VERSION(version) ((version) >> 16)

/** Get minor version number from complete version. */
#define MC_GET_MINOR_VERSION(version) ((version) & 0xffff)

// Asserts expression at compile-time (to be used outside a function body).
#define ASSERT_VERSION_IMPLEMENTATION(comp, versionpart, requiredV, actualV, expression) \
    extern int Actual_##comp##_##versionpart##_VERSION_##actualV##_does_not_match_required_version_##requiredV[(expression) ? 0:-1]

#define ASSERT_VERSION_EVALUATOR(comp, versionpart, requiredV, actualV, expression) \
        ASSERT_VERSION_IMPLEMENTATION(comp, versionpart, requiredV, actualV, expression)

#define ASSERT_VERSION(required, comparator, comp, versionpart) \
    ASSERT_VERSION_EVALUATOR(comp, versionpart, required, comp ##_VERSION_## versionpart, required comparator comp ##_VERSION_## versionpart)

/** Checks at compile-time that an interface version provided by component
 * 'comp' is identical to the required version of a component using this interface.
 * Note! This check is useful for components that IMPLEMENT a particular
 * interface to be alerted of changes to the interface which are likely to
 * require adaptations in the implementation. */
#define MC_CHECK_VERSION_EQUALS(comp, major, minor) \
    ASSERT_VERSION(major, ==, comp, MAJOR); \
    ASSERT_VERSION(minor, ==, comp, MINOR);

/** Checks at compile-time that an interface version provided by component 'comp' meets the
 * required version of a component using this interface. */
#define MC_CHECK_VERSION_STATIC(comp, majorRequired, minorRequired) \
    ASSERT_VERSION(majorRequired, ==, comp, MAJOR); \
    ASSERT_VERSION(minorRequired, <=, comp, MINOR);

/** Version check helper macro for an interface consumer against an interface
 * provider.
 * @param comp          Name of Interface to check.
 * @param majorRequired Required major version of interface provider.
 * @param minorRequired Required minor version of interface provider.
 * Performs a compile-time interface version check that comp_VERSION_MAJOR
 * equals majorRequired and that comp_VERSION_MINOR is at least minorRequired.
 * On success, compilation goes through.
 * On error, compilation breaks, telling the component that did not match in the
 * error message.
 *
 * Additionally, a function is created:
 *
 * checkVersionOk##component(uint32_t version, char** errmsg)
 *
 * Compares version against majorRequired and minorRequired.
 * Additionally, it creates a message string that can be printed out using printf("%s", errmsg).
 * It returns either only the actual version, or on mismatch, actual and required version.
 *
 * @param version[in] component version as returned by layer-specific getVersion.
 * @param errmsg[out] a message string that contains a log.
 *
 */
#if !defined(NDEBUG)
#define MC_CHECK_VERSION(comp, majorRequired, minorRequired) \
    MC_CHECK_VERSION_STATIC(comp, majorRequired, minorRequired) \
    static uint32_t checkVersionOk##comp(uint32_t version, char** errmsg) { \
        static char msgBuf[100]; \
        uint32_t major = MC_GET_MAJOR_VERSION(version); \
        uint32_t minor = MC_GET_MINOR_VERSION(version); \
        uint32_t ret = 0; \
        *errmsg = msgBuf; \
        if ((major == majorRequired) && (minor >= minorRequired)) { \
            snprintf(msgBuf, sizeof(msgBuf), \
                #comp " version is %u.%u", major, minor); \
            ret = 1; \
        } else { \
            snprintf(msgBuf, sizeof(msgBuf), \
                #comp " version error. Got: %u.%u, want >= %u.%u", major, minor, majorRequired, minorRequired); \
        } \
        msgBuf[sizeof(msgBuf) - 1] = '\0'; \
        return ret; \
    }
#else
#define MC_CHECK_VERSION(comp, majorRequired, minorRequired) \
    MC_CHECK_VERSION_STATIC(comp, majorRequired, minorRequired) \
    static uint32_t checkVersionOk##comp(uint32_t version, char** errmsg) { \
        uint32_t major = MC_GET_MAJOR_VERSION(version); \
        uint32_t minor = MC_GET_MINOR_VERSION(version); \
        *errmsg = NULL; \
        if ((major == majorRequired) && (minor >= minorRequired)) { \
            return 1; \
        }; \
        return 0; \
    }
#endif

/** Version check helper macro for version checks of a data object version
 * against an data object consumer.
 *
 * @param comp           Name of Interface to check.
 * @param majorRequired Major data object version supported by component.
 * @param minorRequired Minor data object version supported by component.
 * Performs a compile-time interface version check that comp_VERSION_MAJOR
 * equals majorRequired and that comp_VERSION_MINOR is at least minorRequired.
 * On success, compilation goes through.
 * On error, compilation breaks, telling the component that did not match in the
 * error message.
 *
 * Additionally, the following function is created:
 *
 * checkVersionOkDataObject##component(uint32_t version, char** errmsg)
 *
 * This function checks that the data object version is compatible with the
 * interface version; that is, the major version of the data object must match
 * exactly and the minor version of the data object MUST BE LESS OR EQUAL to the
 * required interface version.
 * Additionally, it creates a message string that can be printed out using printf("%s", errmsg).
 * It returns either only the actual version, or on mismatch, actual and
 * provided version.
 *
 * @param version[in] Data object version of data object.
 * @param errmsg[out] a message string that contains a log.
 *
 */
#if !defined(NDEBUG)
#define MC_CHECK_DATA_OBJECT_VERSION(comp, majorRequired, minorRequired) \
    MC_CHECK_VERSION_STATIC(comp, majorRequired, minorRequired) \
    static uint32_t checkVersionOkDataObject##comp(uint32_t version, char** errmsg) { \
        static char msgBuf[100]; \
        uint32_t major = MC_GET_MAJOR_VERSION(version); \
        uint32_t minor = MC_GET_MINOR_VERSION(version); \
        uint32_t ret = 0; \
        *errmsg = msgBuf; \
        if ((major == majorRequired) && (minor <= minorRequired)) { \
            snprintf(msgBuf, sizeof(msgBuf), \
                #comp " version is %u.%u", major, minor); \
            ret = 1; \
        } else { \
            snprintf(msgBuf, sizeof(msgBuf), \
                #comp " version error. Got: %u.%u, want <= %u.%u", major, minor, majorRequired, minorRequired); \
        } \
        msgBuf[sizeof(msgBuf) - 1] = '\0'; \
        return ret; \
    }
#else
#define MC_CHECK_DATA_OBJECT_VERSION(comp, majorRequired, minorRequired) \
    MC_CHECK_VERSION_STATIC(comp, majorRequired, minorRequired) \
    static uint32_t checkVersionOkDataObject##comp(uint32_t version, char** errmsg) { \
        uint32_t major = MC_GET_MAJOR_VERSION(version); \
        uint32_t minor = MC_GET_MINOR_VERSION(version); \
        *errmsg = NULL; \
        if ((major == majorRequired) && (minor <= minorRequired)) { \
            return 1; \
        }; \
        return 0; \
    }
#endif
