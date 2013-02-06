/**
 * @addtogroup MC_ROOTID mcRootid - Root container id.
 *
 * Global definition of root ID.
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2011-2012 -->
 * @ingroup  MC_DATA_TYPES
 * @{
 */

#ifndef MC_ROOTID_H_
#define MC_ROOTID_H_

/** Root Identifier type. */
typedef uint32_t mcRootid_t;

/** Reserved root id value 1. */
static const mcRootid_t MC_ROOTID_RESERVED1 = 0;

/** Reserved root id value 2. */
static const mcRootid_t MC_ROOTID_RESERVED2 = 0xFFFFFFFF;

/** Root id for system applications. */
static const mcRootid_t MC_ROOTID_SYSTEM = 0xFFFFFFFE;

#endif // MC_ROOTID_H_

/** @} */
