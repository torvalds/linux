/*
 * Cryptographic API.
 *
 * Support for s390 cryptographic instructions.
 *
 *   Copyright (C) 2003 IBM Deutschland GmbH, IBM Corporation
 *   Author(s): Thomas Spatzier (tspat@de.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#ifndef _CRYPTO_ARCH_S390_CRYPT_S390_H
#define _CRYPTO_ARCH_S390_CRYPT_S390_H

#include <asm/errno.h>

#define CRYPT_S390_OP_MASK 0xFF00
#define CRYPT_S390_FUNC_MASK 0x00FF

/* s930 cryptographic operations */
enum crypt_s390_operations {
	CRYPT_S390_KM   = 0x0100,
	CRYPT_S390_KMC  = 0x0200,
	CRYPT_S390_KIMD = 0x0300,
	CRYPT_S390_KLMD = 0x0400,
	CRYPT_S390_KMAC = 0x0500
};

/* function codes for KM (CIPHER MESSAGE) instruction
 * 0x80 is the decipher modifier bit
 */
enum crypt_s390_km_func {
	KM_QUERY	    = CRYPT_S390_KM | 0x0,
	KM_DEA_ENCRYPT      = CRYPT_S390_KM | 0x1,
	KM_DEA_DECRYPT      = CRYPT_S390_KM | 0x1 | 0x80,
	KM_TDEA_128_ENCRYPT = CRYPT_S390_KM | 0x2,
	KM_TDEA_128_DECRYPT = CRYPT_S390_KM | 0x2 | 0x80,
	KM_TDEA_192_ENCRYPT = CRYPT_S390_KM | 0x3,
	KM_TDEA_192_DECRYPT = CRYPT_S390_KM | 0x3 | 0x80,
	KM_AES_128_ENCRYPT  = CRYPT_S390_KM | 0x12,
	KM_AES_128_DECRYPT  = CRYPT_S390_KM | 0x12 | 0x80,
	KM_AES_192_ENCRYPT  = CRYPT_S390_KM | 0x13,
	KM_AES_192_DECRYPT  = CRYPT_S390_KM | 0x13 | 0x80,
	KM_AES_256_ENCRYPT  = CRYPT_S390_KM | 0x14,
	KM_AES_256_DECRYPT  = CRYPT_S390_KM | 0x14 | 0x80,
};

/* function codes for KMC (CIPHER MESSAGE WITH CHAINING)
 * instruction
 */
enum crypt_s390_kmc_func {
	KMC_QUERY            = CRYPT_S390_KMC | 0x0,
	KMC_DEA_ENCRYPT      = CRYPT_S390_KMC | 0x1,
	KMC_DEA_DECRYPT      = CRYPT_S390_KMC | 0x1 | 0x80,
	KMC_TDEA_128_ENCRYPT = CRYPT_S390_KMC | 0x2,
	KMC_TDEA_128_DECRYPT = CRYPT_S390_KMC | 0x2 | 0x80,
	KMC_TDEA_192_ENCRYPT = CRYPT_S390_KMC | 0x3,
	KMC_TDEA_192_DECRYPT = CRYPT_S390_KMC | 0x3 | 0x80,
	KMC_AES_128_ENCRYPT  = CRYPT_S390_KMC | 0x12,
	KMC_AES_128_DECRYPT  = CRYPT_S390_KMC | 0x12 | 0x80,
	KMC_AES_192_ENCRYPT  = CRYPT_S390_KMC | 0x13,
	KMC_AES_192_DECRYPT  = CRYPT_S390_KMC | 0x13 | 0x80,
	KMC_AES_256_ENCRYPT  = CRYPT_S390_KMC | 0x14,
	KMC_AES_256_DECRYPT  = CRYPT_S390_KMC | 0x14 | 0x80,
};

/* function codes for KIMD (COMPUTE INTERMEDIATE MESSAGE DIGEST)
 * instruction
 */
enum crypt_s390_kimd_func {
	KIMD_QUERY   = CRYPT_S390_KIMD | 0,
	KIMD_SHA_1   = CRYPT_S390_KIMD | 1,
	KIMD_SHA_256 = CRYPT_S390_KIMD | 2,
};

/* function codes for KLMD (COMPUTE LAST MESSAGE DIGEST)
 * instruction
 */
enum crypt_s390_klmd_func {
	KLMD_QUERY   = CRYPT_S390_KLMD | 0,
	KLMD_SHA_1   = CRYPT_S390_KLMD | 1,
	KLMD_SHA_256 = CRYPT_S390_KLMD | 2,
};

/* function codes for KMAC (COMPUTE MESSAGE AUTHENTICATION CODE)
 * instruction
 */
enum crypt_s390_kmac_func {
	KMAC_QUERY    = CRYPT_S390_KMAC | 0,
	KMAC_DEA      = CRYPT_S390_KMAC | 1,
	KMAC_TDEA_128 = CRYPT_S390_KMAC | 2,
	KMAC_TDEA_192 = CRYPT_S390_KMAC | 3
};

/* status word for s390 crypto instructions' QUERY functions */
struct crypt_s390_query_status {
	u64 high;
	u64 low;
};

/*
 * Standard fixup and ex_table sections for crypt_s390 inline functions.
 * label 0: the s390 crypto operation
 * label 1: just after 1 to catch illegal operation exception
 *          (unsupported model)
 * label 6: the return point after fixup
 * label 7: set error value if exception _in_ crypto operation
 * label 8: set error value if illegal operation exception
 * [ret] is the variable to receive the error code
 * [ERR] is the error code value
 */
#ifndef CONFIG_64BIT
#define __crypt_s390_fixup \
	".section .fixup,\"ax\" \n"	\
	"7:	lhi	%0,%h[e1] \n"	\
	"	bras	1,9f \n"	\
	"	.long	6b \n"		\
	"8:	lhi	%0,%h[e2] \n"	\
	"	bras	1,9f \n"	\
	"	.long	6b \n"		\
	"9:	l	1,0(1) \n"	\
	"	br	1 \n"		\
	".previous \n"			\
	".section __ex_table,\"a\" \n"	\
	"	.align	4 \n"		\
	"	.long	0b,7b \n"	\
	"	.long	1b,8b \n"	\
	".previous"
#else /* CONFIG_64BIT */
#define __crypt_s390_fixup \
	".section .fixup,\"ax\" \n"	\
	"7:	lhi	%0,%h[e1] \n"	\
	"	jg	6b \n"		\
	"8:	lhi	%0,%h[e2] \n"	\
	"	jg	6b \n"		\
	".previous\n"			\
	".section __ex_table,\"a\" \n"	\
	"	.align	8 \n"		\
	"	.quad	0b,7b \n"	\
	"	.quad	1b,8b \n"	\
	".previous"
#endif /* CONFIG_64BIT */

/*
 * Standard code for setting the result of s390 crypto instructions.
 * %0: the register which will receive the result
 * [result]: the register containing the result (e.g. second operand length
 * to compute number of processed bytes].
 */
#ifndef CONFIG_64BIT
#define __crypt_s390_set_result \
	"	lr	%0,%[result] \n"
#else /* CONFIG_64BIT */
#define __crypt_s390_set_result \
	"	lgr	%0,%[result] \n"
#endif

/*
 * Executes the KM (CIPHER MESSAGE) operation of the CPU.
 * @param func: the function code passed to KM; see crypt_s390_km_func
 * @param param: address of parameter block; see POP for details on each func
 * @param dest: address of destination memory area
 * @param src: address of source memory area
 * @param src_len: length of src operand in bytes
 * @returns < zero for failure, 0 for the query func, number of processed bytes
 * 	for encryption/decryption funcs
 */
static inline int
crypt_s390_km(long func, void* param, u8* dest, const u8* src, long src_len)
{
	register long __func asm("0") = func & CRYPT_S390_FUNC_MASK;
	register void* __param asm("1") = param;
	register u8* __dest asm("4") = dest;
	register const u8* __src asm("2") = src;
	register long __src_len asm("3") = src_len;
	int ret;

	ret = 0;
	__asm__ __volatile__ (
		"0:	.insn	rre,0xB92E0000,%1,%2 \n" /* KM opcode */
		"1:	brc	1,0b \n" /* handle partial completion */
		__crypt_s390_set_result
		"6:	\n"
		__crypt_s390_fixup
		: "+d" (ret), "+a" (__dest), "+a" (__src),
		  [result] "+d" (__src_len)
		: [e1] "K" (-EFAULT), [e2] "K" (-ENOSYS), "d" (__func),
		  "a" (__param)
		: "cc", "memory"
	);
	if (ret >= 0 && func & CRYPT_S390_FUNC_MASK){
		ret = src_len - ret;
	}
	return ret;
}

/*
 * Executes the KMC (CIPHER MESSAGE WITH CHAINING) operation of the CPU.
 * @param func: the function code passed to KM; see crypt_s390_kmc_func
 * @param param: address of parameter block; see POP for details on each func
 * @param dest: address of destination memory area
 * @param src: address of source memory area
 * @param src_len: length of src operand in bytes
 * @returns < zero for failure, 0 for the query func, number of processed bytes
 * 	for encryption/decryption funcs
 */
static inline int
crypt_s390_kmc(long func, void* param, u8* dest, const u8* src, long src_len)
{
	register long __func asm("0") = func & CRYPT_S390_FUNC_MASK;
	register void* __param asm("1") = param;
	register u8* __dest asm("4") = dest;
	register const u8* __src asm("2") = src;
	register long __src_len asm("3") = src_len;
	int ret;

	ret = 0;
	__asm__ __volatile__ (
		"0:	.insn	rre,0xB92F0000,%1,%2 \n" /* KMC opcode */
		"1:	brc	1,0b \n" /* handle partial completion */
		__crypt_s390_set_result
		"6:	\n"
		__crypt_s390_fixup
		: "+d" (ret), "+a" (__dest), "+a" (__src),
		  [result] "+d" (__src_len)
		: [e1] "K" (-EFAULT), [e2] "K" (-ENOSYS), "d" (__func),
		  "a" (__param)
		: "cc", "memory"
	);
	if (ret >= 0 && func & CRYPT_S390_FUNC_MASK){
		ret = src_len - ret;
	}
	return ret;
}

/*
 * Executes the KIMD (COMPUTE INTERMEDIATE MESSAGE DIGEST) operation
 * of the CPU.
 * @param func: the function code passed to KM; see crypt_s390_kimd_func
 * @param param: address of parameter block; see POP for details on each func
 * @param src: address of source memory area
 * @param src_len: length of src operand in bytes
 * @returns < zero for failure, 0 for the query func, number of processed bytes
 * 	for digest funcs
 */
static inline int
crypt_s390_kimd(long func, void* param, const u8* src, long src_len)
{
	register long __func asm("0") = func & CRYPT_S390_FUNC_MASK;
	register void* __param asm("1") = param;
	register const u8* __src asm("2") = src;
	register long __src_len asm("3") = src_len;
	int ret;

	ret = 0;
	__asm__ __volatile__ (
		"0:	.insn	rre,0xB93E0000,%1,%1 \n" /* KIMD opcode */
		"1:	brc	1,0b \n" /* handle partical completion */
		__crypt_s390_set_result
		"6:	\n"
		__crypt_s390_fixup
		: "+d" (ret), "+a" (__src), [result] "+d" (__src_len)
		: [e1] "K" (-EFAULT), [e2] "K" (-ENOSYS), "d" (__func),
		  "a" (__param)
		: "cc", "memory"
	);
	if (ret >= 0 && (func & CRYPT_S390_FUNC_MASK)){
		ret = src_len - ret;
	}
	return ret;
}

/*
 * Executes the KLMD (COMPUTE LAST MESSAGE DIGEST) operation of the CPU.
 * @param func: the function code passed to KM; see crypt_s390_klmd_func
 * @param param: address of parameter block; see POP for details on each func
 * @param src: address of source memory area
 * @param src_len: length of src operand in bytes
 * @returns < zero for failure, 0 for the query func, number of processed bytes
 * 	for digest funcs
 */
static inline int
crypt_s390_klmd(long func, void* param, const u8* src, long src_len)
{
	register long __func asm("0") = func & CRYPT_S390_FUNC_MASK;
	register void* __param asm("1") = param;
	register const u8* __src asm("2") = src;
	register long __src_len asm("3") = src_len;
	int ret;

	ret = 0;
	__asm__ __volatile__ (
		"0:	.insn	rre,0xB93F0000,%1,%1 \n" /* KLMD opcode */
		"1:	brc	1,0b \n" /* handle partical completion */
		__crypt_s390_set_result
		"6:	\n"
		__crypt_s390_fixup
		: "+d" (ret), "+a" (__src), [result] "+d" (__src_len)
		: [e1] "K" (-EFAULT), [e2] "K" (-ENOSYS), "d" (__func),
		  "a" (__param)
		: "cc", "memory"
	);
	if (ret >= 0 && func & CRYPT_S390_FUNC_MASK){
		ret = src_len - ret;
	}
	return ret;
}

/*
 * Executes the KMAC (COMPUTE MESSAGE AUTHENTICATION CODE) operation
 * of the CPU.
 * @param func: the function code passed to KM; see crypt_s390_klmd_func
 * @param param: address of parameter block; see POP for details on each func
 * @param src: address of source memory area
 * @param src_len: length of src operand in bytes
 * @returns < zero for failure, 0 for the query func, number of processed bytes
 * 	for digest funcs
 */
static inline int
crypt_s390_kmac(long func, void* param, const u8* src, long src_len)
{
	register long __func asm("0") = func & CRYPT_S390_FUNC_MASK;
	register void* __param asm("1") = param;
	register const u8* __src asm("2") = src;
	register long __src_len asm("3") = src_len;
	int ret;

	ret = 0;
	__asm__ __volatile__ (
		"0:	.insn	rre,0xB91E0000,%5,%5 \n" /* KMAC opcode */
		"1:	brc	1,0b \n" /* handle partical completion */
		__crypt_s390_set_result
		"6:	\n"
		__crypt_s390_fixup
		: "+d" (ret), "+a" (__src), [result] "+d" (__src_len)
		: [e1] "K" (-EFAULT), [e2] "K" (-ENOSYS), "d" (__func),
		  "a" (__param)
		: "cc", "memory"
	);
	if (ret >= 0 && func & CRYPT_S390_FUNC_MASK){
		ret = src_len - ret;
	}
	return ret;
}

/**
 * Tests if a specific crypto function is implemented on the machine.
 * @param func:	the function code of the specific function; 0 if op in general
 * @return	1 if func available; 0 if func or op in general not available
 */
static inline int
crypt_s390_func_available(int func)
{
	int ret;

	struct crypt_s390_query_status status = {
		.high = 0,
		.low = 0
	};
	switch (func & CRYPT_S390_OP_MASK){
		case CRYPT_S390_KM:
			ret = crypt_s390_km(KM_QUERY, &status, NULL, NULL, 0);
			break;
		case CRYPT_S390_KMC:
			ret = crypt_s390_kmc(KMC_QUERY, &status, NULL, NULL, 0);
			break;
		case CRYPT_S390_KIMD:
			ret = crypt_s390_kimd(KIMD_QUERY, &status, NULL, 0);
			break;
		case CRYPT_S390_KLMD:
			ret = crypt_s390_klmd(KLMD_QUERY, &status, NULL, 0);
			break;
		case CRYPT_S390_KMAC:
			ret = crypt_s390_kmac(KMAC_QUERY, &status, NULL, 0);
			break;
		default:
			ret = 0;
			return ret;
	}
	if (ret >= 0){
		func &= CRYPT_S390_FUNC_MASK;
		func &= 0x7f; //mask modifier bit
		if (func < 64){
			ret = (status.high >> (64 - func - 1)) & 0x1;
		} else {
			ret = (status.low >> (128 - func - 1)) & 0x1;
		}
	} else {
		ret = 0;
	}
	return ret;
}

#endif // _CRYPTO_ARCH_S390_CRYPT_S390_H
