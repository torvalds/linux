/**
 * @defgroup MC_DATA_TYPES MobiCore generic data types
 *
 * @addtogroup MC_SO mcSo - Secure objects definitions.
 * <!-- Copyright Giesecke & Devrient GmbH 2011-2012 -->
 * @ingroup  MC_DATA_TYPES
 * @{
 *
 */

#ifndef MC_SO_H_
#define MC_SO_H_

#include "mcUuid.h"
#include "mcSpid.h"

#define SO_VERSION_MAJOR   2
#define SO_VERSION_MINOR   0

#define MC_ENUM_32BIT_SPACER           ((int32_t)-1)

/** Secure object type. */
typedef enum {
    /** Regular secure object. */
    MC_SO_TYPE_REGULAR = 0x00000001,
    /** Dummy to ensure that enum is 32 bit wide. */
    MC_SO_TYPE_DUMMY = MC_ENUM_32BIT_SPACER,
} mcSoType_t;


/** Secure object context.
 * A context defines which key to use to encrypt/decrypt a secure object.
 */
typedef enum {
    /** Trustlet context. */
    MC_SO_CONTEXT_TLT = 0x00000001,
     /** Service provider context. */
    MC_SO_CONTEXT_SP = 0x00000002,
     /** Device context. */
    MC_SO_CONTEXT_DEVICE = 0x00000003,
    /** Dummy to ensure that enum is 32 bit wide. */
    MC_SO_CONTEXT_DUMMY = MC_ENUM_32BIT_SPACER,
} mcSoContext_t;

/** Secure object lifetime.
 * A lifetime defines how long a secure object is valid.
 */
typedef enum {
    /** SO does not expire. */
    MC_SO_LIFETIME_PERMANENT = 0x00000000,
    /** SO expires on reboot (coldboot). */
    MC_SO_LIFETIME_POWERCYCLE = 0x00000001,
    /** SO expires when Trustlet is closed. */
    MC_SO_LIFETIME_SESSION = 0x00000002,
    /** Dummy to ensure that enum is 32 bit wide. */
    MC_SO_LIFETIME_DUMMY = MC_ENUM_32BIT_SPACER,
} mcSoLifeTime_t;

/** Service provider Trustlet id.
 * The combination of service provider id and Trustlet UUID forms a unique
 * Trustlet identifier.
 */
typedef struct {
    /** Service provider id. */
    mcSpid_t spid;
    /** Trustlet UUID. */
    mcUuid_t uuid;
} tlApiSpTrustletId_t;

/** Secure object header.
 * A secure object header introduces a secure object.
 * Layout of a secure object:
 * <pre>
 * <code>
 *
 *     +--------+------------------+------------------+--------+---------+
 *     | Header |   plain-data     |  encrypted-data  |  hash  | padding |
 *     +--------+------------------+------------------+--------+---------+
 *
 *     /--------/---- plainLen ----/-- encryptedLen --/-- 32 --/- 1..16 -/
 *
 *     /----------------- toBeHashedLen --------------/
 *
 *                                 /---------- toBeEncryptedLen ---------/
 *
 *     /--------------------------- totalSoSize -------------------------/
 *
 * </code>
 * </pre>
 */
typedef struct {
    /** Type of secure object. */
    uint32_t type;
    /** Secure object version. */
    uint32_t version;
    /** Secure object context. */
    mcSoContext_t context;
    /** Secure object lifetime. */
    mcSoLifeTime_t lifetime;
    /** Producer Trustlet id. */
    tlApiSpTrustletId_t producer;
    /** Length of unencrypted user data (after the header). */
    uint32_t plainLen;
    /** Length of encrypted user data (after unencrypted data, excl. checksum
     * and excl. padding bytes). */
    uint32_t encryptedLen;
} mcSoHeader_t;

#endif // MC_SO_H_

/** Maximum size of the payload (plain length + encrypted length) of a secure object. */
#define MC_SO_PAYLOAD_MAX_SIZE      1000000

/** Block size of encryption algorithm used for secure objects. */
#define MC_SO_ENCRYPT_BLOCK_SIZE    16

/** Maximum number of ISO padding bytes. */
#define MC_SO_MAX_PADDING_SIZE (MC_SO_ENCRYPT_BLOCK_SIZE)

/** Size of hash used for secure objects. */
#define MC_SO_HASH_SIZE             32

/** Calculates gross size of cryptogram within secure object including ISO padding bytes. */
#define MC_SO_ENCRYPT_PADDED_SIZE(netsize) ( (netsize) + \
    MC_SO_MAX_PADDING_SIZE - (netsize) % MC_SO_MAX_PADDING_SIZE )

/** Calculates the total size of a secure object.
 * @param plainLen Length of plain text part within secure object.
 * @param encryptedLen Length of encrypted part within secure object (excl.
 * hash, padding).
 * @return Total (gross) size of the secure object or 0 if given parameters are
 * illegal or would lead to a secure object of invalid size.
 */
#define MC_SO_SIZE(plainLen, encryptedLen) ( \
    ((plainLen) + (encryptedLen) < (encryptedLen) || (plainLen) + (encryptedLen) > MC_SO_PAYLOAD_MAX_SIZE) ? 0 : \
    sizeof(mcSoHeader_t) + (plainLen) + MC_SO_ENCRYPT_PADDED_SIZE((encryptedLen) + MC_SO_HASH_SIZE) \
)

/** @} */
