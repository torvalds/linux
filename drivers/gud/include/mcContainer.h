/** @addtogroup MC_CONTAINER mcContainer - Containers for MobiCore Content Management.
 * @ingroup  MC_DATA_TYPES
 * @{
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2009-2012 -->
 */
#ifndef MC_CONTAINER_H_
#define MC_CONTAINER_H_

#include <stdint.h>

#include "mcRootid.h"
#include "mcSpid.h"
#include "mcUuid.h"
#include "mcSo.h"
#include "mcSuid.h"

#define CONTAINER_VERSION_MAJOR   2
#define CONTAINER_VERSION_MINOR   0

#define MC_CONT_SYMMETRIC_KEY_SIZE      32
#define MC_CONT_PUBLIC_KEY_SIZE         320
#define MC_CONT_CHILDREN_COUNT          16
#define MC_DATA_CONT_MAX_DATA_SIZE      2048
#define MC_TLT_CODE_HASH_SIZE           32

#define MC_BYTES_TO_WORDS(bytes)       ( (bytes) / sizeof(uint32_t) )
#define MC_ENUM_32BIT_SPACER           ((int32_t)-1)

typedef uint32_t mcContVersion_t;

/** Personalization Data ID. */
typedef struct {
    uint32_t data;
} mcPid_t;

typedef struct {
    uint32_t keydata[MC_BYTES_TO_WORDS(MC_CONT_SYMMETRIC_KEY_SIZE)];
} mcSymmetricKey_t;

typedef struct {
    uint32_t keydata[MC_BYTES_TO_WORDS(MC_CONT_PUBLIC_KEY_SIZE)];
} mcPublicKey_t;

typedef mcSpid_t spChild_t[MC_CONT_CHILDREN_COUNT];

typedef mcUuid_t mcUuidChild_t[MC_CONT_CHILDREN_COUNT];

/** Content management container states.
 */
typedef enum {
     /** Container state unregistered. */
     MC_CONT_STATE_UNREGISTERED = 0,
     /** Container is registered. */
     MC_CONT_STATE_REGISTERED = 1,
     /** Container  is activated. */
     MC_CONT_STATE_ACTIVATED = 2,
     /** Container is locked by root. */
     MC_CONT_STATE_ROOT_LOCKED = 3,
     /** Container is locked by service provider. */
     MC_CONT_STATE_SP_LOCKED = 4,
     /** Container is locked by root and service provider. */
     MC_CONT_STATE_ROOT_SP_LOCKED = 5,
     /** Dummy: ensure that enum is 32 bits wide. */
     MC_CONT_ATTRIB_SPACER = MC_ENUM_32BIT_SPACER
} mcContainerState_t;

/** Content management container attributes.
 */
typedef struct {
    mcContainerState_t state;
} mcContainerAttribs_t;

/** Container types. */
typedef enum {
    /** SOC container. */
    CONT_TYPE_SOC = 0,
    /** Root container. */
    CONT_TYPE_ROOT,
    /** Service provider container. */
    CONT_TYPE_SP,
    /** Trustlet container. */
    CONT_TYPE_TLCON,
    /** Service provider data. */
    CONT_TYPE_SPDATA,
    /** Trustlet data. */
    CONT_TYPE_TLDATA
} contType_t;


/** @defgroup MC_CONTAINER_CRYPTO_OBJECTS Container secrets.
 * Data that is stored encrypted within the container.
 * @{ */

/** SoC secret */
typedef struct {
    mcSymmetricKey_t kSocAuth;
} mcCoSocCont_t;

/** */
typedef struct {
    mcSymmetricKey_t kRootAuth;
} mcCoRootCont_t;

/** */
typedef struct {
    mcSymmetricKey_t kSpAuth;
} mcCoSpCont_t;

/** */
typedef struct {
    mcSymmetricKey_t kTl;
} mcCoTltCont_t;

/** */
typedef struct {
    uint8_t data[MC_DATA_CONT_MAX_DATA_SIZE];
} mcCoDataCont_t;

/** */
typedef union {
    mcSpid_t spid;
    mcUuid_t uuid;
} mcCid_t;

/** @} */

/** @defgroup MC_CONTAINER_CONTAINER_OBJECTS Container definitions.
 * Container type definitions.
 * @{ */

/** SoC Container */
typedef struct {
    contType_t type;
    uint32_t version;
    mcContainerAttribs_t attribs;
    mcSuid_t suid;
    /* Secrets. */
    mcCoSocCont_t co;
} mcSocCont_t;

/** */
typedef struct {
    contType_t type;
    uint32_t version;
    mcContainerAttribs_t attribs;
    mcSuid_t suid;
    mcRootid_t rootid;
    spChild_t children;
    /* Secrets. */
    mcCoRootCont_t co;
} mcRootCont_t;

/** */
typedef struct {
    contType_t type;
    uint32_t version;
    mcContainerAttribs_t attribs;
    mcSpid_t spid;
    mcUuidChild_t children;
    /* Secrets. */
    mcCoSpCont_t co;
} mcSpCont_t;

/** */
typedef struct {
    contType_t type;
    uint32_t version;
    mcContainerAttribs_t attribs;
    mcSpid_t parent;
    mcUuid_t uuid;
    /* Secrets. */
    mcCoTltCont_t co;
} mcTltCont_t;

/** */
typedef struct {
    contType_t type;
    uint32_t version;
    mcUuid_t uuid;
    mcPid_t pid;
    /* Secrets. */
    mcCoDataCont_t co;
} mcDataCont_t;

/** @} */

/** Calculates the total size of the secure object hash and padding for a given
 * container.
 * @param contTotalSize Total size of the container (sum of plain and encrypted
 * parts).
 * @param contCoSize Size/length of the encrypted container part ("crypto
 * object").
 * @return Total size of hash and padding for given container.
 */
#define SO_CONT_HASH_AND_PAD_SIZE(contTotalSize, contCoSize) ( \
    MC_SO_SIZE((contTotalSize) - (contCoSize), (contCoSize)) \
        - sizeof(mcSoHeader_t) \
        - (contTotalSize) )

/** @defgroup MC_CONTAINER_SECURE_OBJECTS Containers in secure objects.
 * Secure objects wrapping different containers.
 * @{ */

/** Authentication token */
typedef struct {
    mcSoHeader_t soHeader;
    mcSocCont_t coSoc;
    uint8_t hashAndPad[SO_CONT_HASH_AND_PAD_SIZE(sizeof(mcSocCont_t), sizeof(mcCoSocCont_t))];
} mcSoAuthTokenCont_t;

/** Root container */
typedef struct {
    mcSoHeader_t soHeader;
    mcRootCont_t cont;
    uint8_t hashAndPad[SO_CONT_HASH_AND_PAD_SIZE(sizeof(mcRootCont_t), sizeof(mcCoRootCont_t))];
} mcSoRootCont_t;

/** */
typedef struct {
    mcSoHeader_t soHeader;
    mcSpCont_t cont;
    uint8_t hashAndPad[SO_CONT_HASH_AND_PAD_SIZE(sizeof(mcSpCont_t), sizeof(mcCoSpCont_t))];
} mcSoSpCont_t;

/** */
typedef struct {
    mcSoHeader_t soHeader;
    mcTltCont_t cont;
    uint8_t hashAndPad[SO_CONT_HASH_AND_PAD_SIZE(sizeof(mcTltCont_t), sizeof(mcCoTltCont_t))];
} mcSoTltCont_t;

/** */
typedef struct {
    mcSoHeader_t soHeader;
    mcDataCont_t cont;
    uint8_t hashAndPad[SO_CONT_HASH_AND_PAD_SIZE(sizeof(mcDataCont_t), sizeof(mcCoDataCont_t))];
} mcSoDataCont_t;

/** */
typedef struct {
    mcSoRootCont_t soRoot;
    mcSoSpCont_t soSp;
    mcSoTltCont_t soTlt;
} mcSoContainerPath_t;

/** @} */

#endif // MC_CONTAINER_H_

/** @} */
