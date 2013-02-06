/**
 * @addtogroup MC_SUID mcSuid - SoC unique ID.
 *
 * <!-- Copyright Giesecke & Devrient GmbH 2011-2012 -->
 * @ingroup  MC_DATA_TYPES
 * @{
 */

#ifndef MC_SUID_H_
#define MC_SUID_H_

/** Length of SUID. */
#define MC_SUID_LEN    16

/** Platform specific device identifier (serial number of the chip). */
typedef struct {
    uint8_t data[MC_SUID_LEN - sizeof(uint32_t)];
} suidData_t;

/** Soc unique identifier type. */
typedef struct {
    uint32_t    sipId;  /**< Silicon Provider ID to be set during build. */
    suidData_t  suidData;
} mcSuid_t;

#endif // MC_SUID_H_

/** @} */
