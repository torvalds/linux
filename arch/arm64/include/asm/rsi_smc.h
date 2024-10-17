/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2023 ARM Ltd.
 */

#ifndef __ASM_RSI_SMC_H_
#define __ASM_RSI_SMC_H_

#include <linux/arm-smccc.h>

/*
 * This file describes the Realm Services Interface (RSI) Application Binary
 * Interface (ABI) for SMC calls made from within the Realm to the RMM and
 * serviced by the RMM.
 */

/*
 * The major version number of the RSI implementation.  This is increased when
 * the binary format or semantics of the SMC calls change.
 */
#define RSI_ABI_VERSION_MAJOR		UL(1)

/*
 * The minor version number of the RSI implementation.  This is increased when
 * a bug is fixed, or a feature is added without breaking binary compatibility.
 */
#define RSI_ABI_VERSION_MINOR		UL(0)

#define RSI_ABI_VERSION			((RSI_ABI_VERSION_MAJOR << 16) | \
					 RSI_ABI_VERSION_MINOR)

#define RSI_ABI_VERSION_GET_MAJOR(_version) ((_version) >> 16)
#define RSI_ABI_VERSION_GET_MINOR(_version) ((_version) & 0xFFFF)

#define RSI_SUCCESS		UL(0)
#define RSI_ERROR_INPUT		UL(1)
#define RSI_ERROR_STATE		UL(2)
#define RSI_INCOMPLETE		UL(3)
#define RSI_ERROR_UNKNOWN	UL(4)

#define SMC_RSI_FID(n)		ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,      \
						   ARM_SMCCC_SMC_64,         \
						   ARM_SMCCC_OWNER_STANDARD, \
						   n)

/*
 * Returns RSI version.
 *
 * arg1 == Requested interface revision
 * ret0 == Status / error
 * ret1 == Lower implemented interface revision
 * ret2 == Higher implemented interface revision
 */
#define SMC_RSI_ABI_VERSION	SMC_RSI_FID(0x190)

/*
 * Read feature register.
 *
 * arg1 == Feature register index
 * ret0 == Status / error
 * ret1 == Feature register value
 */
#define SMC_RSI_FEATURES			SMC_RSI_FID(0x191)

/*
 * Read measurement for the current Realm.
 *
 * arg1 == Index, which measurements slot to read
 * ret0 == Status / error
 * ret1 == Measurement value, bytes:  0 -  7
 * ret2 == Measurement value, bytes:  8 - 15
 * ret3 == Measurement value, bytes: 16 - 23
 * ret4 == Measurement value, bytes: 24 - 31
 * ret5 == Measurement value, bytes: 32 - 39
 * ret6 == Measurement value, bytes: 40 - 47
 * ret7 == Measurement value, bytes: 48 - 55
 * ret8 == Measurement value, bytes: 56 - 63
 */
#define SMC_RSI_MEASUREMENT_READ		SMC_RSI_FID(0x192)

/*
 * Extend Realm Extensible Measurement (REM) value.
 *
 * arg1  == Index, which measurements slot to extend
 * arg2  == Size of realm measurement in bytes, max 64 bytes
 * arg3  == Measurement value, bytes:  0 -  7
 * arg4  == Measurement value, bytes:  8 - 15
 * arg5  == Measurement value, bytes: 16 - 23
 * arg6  == Measurement value, bytes: 24 - 31
 * arg7  == Measurement value, bytes: 32 - 39
 * arg8  == Measurement value, bytes: 40 - 47
 * arg9  == Measurement value, bytes: 48 - 55
 * arg10 == Measurement value, bytes: 56 - 63
 * ret0  == Status / error
 */
#define SMC_RSI_MEASUREMENT_EXTEND		SMC_RSI_FID(0x193)

/*
 * Initialize the operation to retrieve an attestation token.
 *
 * arg1 == Challenge value, bytes:  0 -  7
 * arg2 == Challenge value, bytes:  8 - 15
 * arg3 == Challenge value, bytes: 16 - 23
 * arg4 == Challenge value, bytes: 24 - 31
 * arg5 == Challenge value, bytes: 32 - 39
 * arg6 == Challenge value, bytes: 40 - 47
 * arg7 == Challenge value, bytes: 48 - 55
 * arg8 == Challenge value, bytes: 56 - 63
 * ret0 == Status / error
 * ret1 == Upper bound of token size in bytes
 */
#define SMC_RSI_ATTESTATION_TOKEN_INIT		SMC_RSI_FID(0x194)

/*
 * Continue the operation to retrieve an attestation token.
 *
 * arg1 == The IPA of token buffer
 * arg2 == Offset within the granule of the token buffer
 * arg3 == Size of the granule buffer
 * ret0 == Status / error
 * ret1 == Length of token bytes copied to the granule buffer
 */
#define SMC_RSI_ATTESTATION_TOKEN_CONTINUE	SMC_RSI_FID(0x195)

#ifndef __ASSEMBLY__

struct realm_config {
	union {
		struct {
			unsigned long ipa_bits; /* Width of IPA in bits */
			unsigned long hash_algo; /* Hash algorithm */
		};
		u8 pad[0x200];
	};
	union {
		u8 rpv[64]; /* Realm Personalization Value */
		u8 pad2[0xe00];
	};
	/*
	 * The RMM requires the configuration structure to be aligned to a 4k
	 * boundary, ensure this happens by aligning this structure.
	 */
} __aligned(0x1000);

#endif /* __ASSEMBLY__ */

/*
 * Read configuration for the current Realm.
 *
 * arg1 == struct realm_config addr
 * ret0 == Status / error
 */
#define SMC_RSI_REALM_CONFIG			SMC_RSI_FID(0x196)

/*
 * Request RIPAS of a target IPA range to be changed to a specified value.
 *
 * arg1 == Base IPA address of target region
 * arg2 == Top of the region
 * arg3 == RIPAS value
 * arg4 == flags
 * ret0 == Status / error
 * ret1 == Top of modified IPA range
 * ret2 == Whether the Host accepted or rejected the request
 */
#define SMC_RSI_IPA_STATE_SET			SMC_RSI_FID(0x197)

#define RSI_NO_CHANGE_DESTROYED			UL(0)
#define RSI_CHANGE_DESTROYED			UL(1)

#define RSI_ACCEPT				UL(0)
#define RSI_REJECT				UL(1)

/*
 * Get RIPAS of a target IPA range.
 *
 * arg1 == Base IPA of target region
 * arg2 == End of target IPA region
 * ret0 == Status / error
 * ret1 == Top of IPA region which has the reported RIPAS value
 * ret2 == RIPAS value
 */
#define SMC_RSI_IPA_STATE_GET			SMC_RSI_FID(0x198)

/*
 * Make a Host call.
 *
 * arg1 == IPA of host call structure
 * ret0 == Status / error
 */
#define SMC_RSI_HOST_CALL			SMC_RSI_FID(0x199)

#endif /* __ASM_RSI_SMC_H_ */
