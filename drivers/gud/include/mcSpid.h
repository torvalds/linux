/**
 * @addtogroup MC_SPID mcSpid - service provider ID.
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2011-2012 -->
 * @ingroup  MC_DATA_TYPES
 * @{
 */

#ifndef MC_SPID_H_
#define MC_SPID_H_

/** Service provider Identifier type. */
typedef uint32_t mcSpid_t;

/** SPID value used as free marker in root containers. */
static const mcSpid_t MC_SPID_FREE = 0xFFFFFFFF;

/** Reserved SPID value. */
static const mcSpid_t MC_SPID_RESERVED = 0;

/** SPID for system applications. */
static const mcSpid_t MC_SPID_SYSTEM = 0xFFFFFFFE;

#endif // MC_SPID_H_

/** @} */
