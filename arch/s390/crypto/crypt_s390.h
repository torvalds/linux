/*
 * Cryptographic API.
 *
 * Support for s390 cryptographic instructions.
 *
 *   Copyright IBM Corp. 2003,2007
 *   Author(s): Thomas Spatzier
 *		Jan Glauber (jan.glauber@de.ibm.com)
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

#define CRYPT_S390_PRIORITY 300
#define CRYPT_S390_COMPOSITE_PRIORITY 400

/* s390 cryptographic operations */
enum crypt_s390_operations {
	CRYPT_S390_KM   = 0x0100,
	CRYPT_S390_KMC  = 0x0200,
	CRYPT_S390_KIMD = 0x0300,
	CRYPT_S390_KLMD = 0x0400,
	CRYPT_S390_KMAC = 0x0500
};

/*
 * function codes for KM (CIPHER MESSAGE) instruction
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

/*
 * function codes for KMC (CIPHER MESSAGE WITH CHAINING)
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
	KMC_PRNG	     = CRYPT_S390_KMC | 0x43,
};

/*
 * function codes for KIMD (COMPUTE INTERMEDIATE MESSAGE DIGEST)
 * instruction
 */
enum crypt_s390_kimd_func {
	KIMD_QUERY   = CRYPT_S390_KIMD | 0,
	KIMD_SHA_1   = CRYPT_S390_KIMD | 1,
	KIMD_SHA_256 = CRYPT_S390_KIMD | 2,
	KIMD_SHA_512 = CRYPT_S390_KIMD | 3,
};

/*
 * function codes for KLMD (COMPUTE LAST MESSAGE DIGEST)
 * instruction
 */
enum crypt_s390_klmd_func {
	KLMD_QUERY   = CRYPT_S390_KLMD | 0,
	KLMD_SHA_1   = CRYPT_S390_KLMD | 1,
	KLMD_SHA_256 = CRYPT_S390_KLMD | 2,
	KLMD_SHA_512 = CRYPT_S390_KLMD | 3,
};

/*
 * function codes for KMAC (COMPUTE MESSAGE AUTHENTICATION CODE)
 * instruction
 */
enum crypt_s390_kmac_func {
	KMAC_QUERY    = CRYPT_S390_KMAC | 0,
	KMAC_DEA      = CRYPT_S390_KMAC | 1,
	KMAC_TDEA_128 = CRYPT_S390_KMAC | 2,
	KMAC_TDEA_192 = CRYPT_S390_KMAC | 3
};

/**
 * crypt_s390_km:
 * @func: the function code passed to KM; see crypt_s390_km_func
 * @param: address of parameter block; see POP for details on each func
 * @dest: address of destination memory area
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Executes the KM (CIPHER MESSAGE) operation of the CPU.
 *
 * Returns -1 for failure, 0 for the query func, number of processed
 * bytes for encryption/decryption funcs
 */
static inline int crypt_s390_km(long func, void *param,
				u8 *dest, const u8 *src, long src_len)
{
	register long __func asm("0") = func & CRYPT_S390_FUNC_MASK;
	register void *__param asm("1") = param;
	register const u8 *__src asm("2") = src;
	register long __src_len asm("3") = src_len;
	register u8 *__dest asm("4") = dest;
	int ret;

	asm volatile(
		"0:	.insn	rre,0xb92e0000,%3,%1 \n" /* KM opcode */
		"1:	brc	1,0b \n" /* handle partial completion */
		"	la	%0,0\n"
		"2:\n"
		EX_TABLE(0b,2b) EX_TABLE(1b,2b)
		: "=d" (ret), "+a" (__src), "+d" (__src_len), "+a" (__dest)
		: "d" (__func), "a" (__param), "0" (-1) : "cc", "memory");
	if (ret < 0)
		return ret;
	return (func & CRYPT_S390_FUNC_MASK) ? src_len - __src_len : __src_len;
}

/**
 * crypt_s390_kmc:
 * @func: the function code passed to KM; see crypt_s390_kmc_func
 * @param: address of parameter block; see POP for details on each func
 * @dest: address of destination memory area
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Executes the KMC (CIPHER MESSAGE WITH CHAINING) operation of the CPU.
 *
 * Returns -1 for failure, 0 for the query func, number of processed
 * bytes for encryption/decryption funcs
 */
static inline int crypt_s390_kmc(long func, void *param,
				 u8 *dest, const u8 *src, long src_len)
{
	register long __func asm("0") = func & CRYPT_S390_FUNC_MASK;
	register void *__param asm("1") = param;
	register const u8 *__src asm("2") = src;
	register long __src_len asm("3") = src_len;
	register u8 *__dest asm("4") = dest;
	int ret;

	asm volatile(
		"0:	.insn	rre,0xb92f0000,%3,%1 \n" /* KMC opcode */
		"1:	brc	1,0b \n" /* handle partial completion */
		"	la	%0,0\n"
		"2:\n"
		EX_TABLE(0b,2b) EX_TABLE(1b,2b)
		: "=d" (ret), "+a" (__src), "+d" (__src_len), "+a" (__dest)
		: "d" (__func), "a" (__param), "0" (-1) : "cc", "memory");
	if (ret < 0)
		return ret;
	return (func & CRYPT_S390_FUNC_MASK) ? src_len - __src_len : __src_len;
}

/**
 * crypt_s390_kimd:
 * @func: the function code passed to KM; see crypt_s390_kimd_func
 * @param: address of parameter block; see POP for details on each func
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Executes the KIMD (COMPUTE INTERMEDIATE MESSAGE DIGEST) operation
 * of the CPU.
 *
 * Returns -1 for failure, 0 for the query func, number of processed
 * bytes for digest funcs
 */
static inline int crypt_s390_kimd(long func, void *param,
				  const u8 *src, long src_len)
{
	register long __func asm("0") = func & CRYPT_S390_FUNC_MASK;
	register void *__param asm("1") = param;
	register const u8 *__src asm("2") = src;
	register long __src_len asm("3") = src_len;
	int ret;

	asm volatile(
		"0:	.insn	rre,0xb93e0000,%1,%1 \n" /* KIMD opcode */
		"1:	brc	1,0b \n" /* handle partial completion */
		"	la	%0,0\n"
		"2:\n"
		EX_TABLE(0b,2b) EX_TABLE(1b,2b)
		: "=d" (ret), "+a" (__src), "+d" (__src_len)
		: "d" (__func), "a" (__param), "0" (-1) : "cc", "memory");
	if (ret < 0)
		return ret;
	return (func & CRYPT_S390_FUNC_MASK) ? src_len - __src_len : __src_len;
}

/**
 * crypt_s390_klmd:
 * @func: the function code passed to KM; see crypt_s390_klmd_func
 * @param: address of parameter block; see POP for details on each func
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Executes the KLMD (COMPUTE LAST MESSAGE DIGEST) operation of the CPU.
 *
 * Returns -1 for failure, 0 for the query func, number of processed
 * bytes for digest funcs
 */
static inline int crypt_s390_klmd(long func, void *param,
				  const u8 *src, long src_len)
{
	register long __func asm("0") = func & CRYPT_S390_FUNC_MASK;
	register void *__param asm("1") = param;
	register const u8 *__src asm("2") = src;
	register long __src_len asm("3") = src_len;
	int ret;

	asm volatile(
		"0:	.insn	rre,0xb93f0000,%1,%1 \n" /* KLMD opcode */
		"1:	brc	1,0b \n" /* handle partial completion */
		"	la	%0,0\n"
		"2:\n"
		EX_TABLE(0b,2b) EX_TABLE(1b,2b)
		: "=d" (ret), "+a" (__src), "+d" (__src_len)
		: "d" (__func), "a" (__param), "0" (-1) : "cc", "memory");
	if (ret < 0)
		return ret;
	return (func & CRYPT_S390_FUNC_MASK) ? src_len - __src_len : __src_len;
}

/**
 * crypt_s390_kmac:
 * @func: the function code passed to KM; see crypt_s390_klmd_func
 * @param: address of parameter block; see POP for details on each func
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Executes the KMAC (COMPUTE MESSAGE AUTHENTICATION CODE) operation
 * of the CPU.
 *
 * Returns -1 for failure, 0 for the query func, number of processed
 * bytes for digest funcs
 */
static inline int crypt_s390_kmac(long func, void *param,
				  const u8 *src, long src_len)
{
	register long __func asm("0") = func & CRYPT_S390_FUNC_MASK;
	register void *__param asm("1") = param;
	register const u8 *__src asm("2") = src;
	register long __src_len asm("3") = src_len;
	int ret;

	asm volatile(
		"0:	.insn	rre,0xb91e0000,%1,%1 \n" /* KLAC opcode */
		"1:	brc	1,0b \n" /* handle partial completion */
		"	la	%0,0\n"
		"2:\n"
		EX_TABLE(0b,2b) EX_TABLE(1b,2b)
		: "=d" (ret), "+a" (__src), "+d" (__src_len)
		: "d" (__func), "a" (__param), "0" (-1) : "cc", "memory");
	if (ret < 0)
		return ret;
	return (func & CRYPT_S390_FUNC_MASK) ? src_len - __src_len : __src_len;
}

/**
 * crypt_s390_func_available:
 * @func: the function code of the specific function; 0 if op in general
 *
 * Tests if a specific crypto function is implemented on the machine.
 *
 * Returns 1 if func available; 0 if func or op in general not available
 */
static inline int crypt_s390_func_available(int func)
{
	unsigned char status[16];
	int ret;

	switch (func & CRYPT_S390_OP_MASK) {
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
		return 0;
	}
	if (ret < 0)
		return 0;
	func &= CRYPT_S390_FUNC_MASK;
	func &= 0x7f;		/* mask modifier bit */
	return (status[func >> 3] & (0x80 >> (func & 7))) != 0;
}

#endif	/* _CRYPTO_ARCH_S390_CRYPT_S390_H */
