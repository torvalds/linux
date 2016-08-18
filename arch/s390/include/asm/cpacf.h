/*
 * CP Assist for Cryptographic Functions (CPACF)
 *
 * Copyright IBM Corp. 2003, 2016
 * Author(s): Thomas Spatzier
 *	      Jan Glauber
 *	      Harald Freudenberger (freude@de.ibm.com)
 *	      Martin Schwidefsky <schwidefsky@de.ibm.com>
 */
#ifndef _ASM_S390_CPACF_H
#define _ASM_S390_CPACF_H

#include <asm/facility.h>

/*
 * Instruction opcodes for the CPACF instructions
 */
#define CPACF_KMAC		0xb91e		/* MSA	*/
#define CPACF_KM		0xb92e		/* MSA	*/
#define CPACF_KMC		0xb92f		/* MSA	*/
#define CPACF_KIMD		0xb93e		/* MSA	*/
#define CPACF_KLMD		0xb93f		/* MSA	*/
#define CPACF_PCKMO		0xb928		/* MSA3 */
#define CPACF_KMF		0xb92a		/* MSA4 */
#define CPACF_KMO		0xb92b		/* MSA4 */
#define CPACF_PCC		0xb92c		/* MSA4 */
#define CPACF_KMCTR		0xb92d		/* MSA4 */
#define CPACF_PPNO		0xb93c		/* MSA5 */

/*
 * Function codes for the KM (CIPHER MESSAGE)
 * instruction (0x80 is the decipher modifier bit)
 */
#define CPACF_KM_QUERY		0x00
#define CPACF_KM_DEA_ENC	0x01
#define CPACF_KM_DEA_DEC	0x81
#define CPACF_KM_TDEA_128_ENC	0x02
#define CPACF_KM_TDEA_128_DEC	0x82
#define CPACF_KM_TDEA_192_ENC	0x03
#define CPACF_KM_TDEA_192_DEC	0x83
#define CPACF_KM_AES_128_ENC	0x12
#define CPACF_KM_AES_128_DEC	0x92
#define CPACF_KM_AES_192_ENC	0x13
#define CPACF_KM_AES_192_DEC	0x93
#define CPACF_KM_AES_256_ENC	0x14
#define CPACF_KM_AES_256_DEC	0x94
#define CPACF_KM_XTS_128_ENC	0x32
#define CPACF_KM_XTS_128_DEC	0xb2
#define CPACF_KM_XTS_256_ENC	0x34
#define CPACF_KM_XTS_256_DEC	0xb4

/*
 * Function codes for the KMC (CIPHER MESSAGE WITH CHAINING)
 * instruction (0x80 is the decipher modifier bit)
 */
#define CPACF_KMC_QUERY		0x00
#define CPACF_KMC_DEA_ENC	0x01
#define CPACF_KMC_DEA_DEC	0x81
#define CPACF_KMC_TDEA_128_ENC	0x02
#define CPACF_KMC_TDEA_128_DEC	0x82
#define CPACF_KMC_TDEA_192_ENC	0x03
#define CPACF_KMC_TDEA_192_DEC	0x83
#define CPACF_KMC_AES_128_ENC	0x12
#define CPACF_KMC_AES_128_DEC	0x92
#define CPACF_KMC_AES_192_ENC	0x13
#define CPACF_KMC_AES_192_DEC	0x93
#define CPACF_KMC_AES_256_ENC	0x14
#define CPACF_KMC_AES_256_DEC	0x94
#define CPACF_KMC_PRNG		0x43

/*
 * Function codes for the KMCTR (CIPHER MESSAGE WITH COUNTER)
 * instruction (0x80 is the decipher modifier bit)
 */
#define CPACF_KMCTR_QUERY	 0x00
#define CPACF_KMCTR_DEA_ENC	 0x01
#define CPACF_KMCTR_DEA_DEC	 0x81
#define CPACF_KMCTR_TDEA_128_ENC 0x02
#define CPACF_KMCTR_TDEA_128_DEC 0x82
#define CPACF_KMCTR_TDEA_192_ENC 0x03
#define CPACF_KMCTR_TDEA_192_DEC 0x83
#define CPACF_KMCTR_AES_128_ENC	 0x12
#define CPACF_KMCTR_AES_128_DEC	 0x92
#define CPACF_KMCTR_AES_192_ENC	 0x13
#define CPACF_KMCTR_AES_192_DEC	 0x93
#define CPACF_KMCTR_AES_256_ENC	 0x14
#define CPACF_KMCTR_AES_256_DEC	 0x94

/*
 * Function codes for the KIMD (COMPUTE INTERMEDIATE MESSAGE DIGEST)
 * instruction (0x80 is the decipher modifier bit)
 */
#define CPACF_KIMD_QUERY	0x00
#define CPACF_KIMD_SHA_1	0x01
#define CPACF_KIMD_SHA_256	0x02
#define CPACF_KIMD_SHA_512	0x03
#define CPACF_KIMD_GHASH	0x41

/*
 * Function codes for the KLMD (COMPUTE LAST MESSAGE DIGEST)
 * instruction (0x80 is the decipher modifier bit)
 */
#define CPACF_KLMD_QUERY	0x00
#define CPACF_KLMD_SHA_1	0x01
#define CPACF_KLMD_SHA_256	0x02
#define CPACF_KLMD_SHA_512	0x03

/*
 * function codes for the KMAC (COMPUTE MESSAGE AUTHENTICATION CODE)
 * instruction (0x80 is the decipher modifier bit)
 */
#define CPACF_KMAC_QUERY	0x00
#define CPACF_KMAC_DEA		0x01
#define CPACF_KMAC_TDEA_128	0x02
#define CPACF_KMAC_TDEA_192	0x03

/*
 * Function codes for the PPNO (PERFORM PSEUDORANDOM NUMBER OPERATION)
 * instruction (0x80 is the decipher modifier bit)
 */
#define CPACF_PPNO_QUERY		0x00
#define CPACF_PPNO_SHA512_DRNG_GEN	0x03
#define CPACF_PPNO_SHA512_DRNG_SEED	0x83

/**
 * cpacf_query() - check if a specific CPACF function is available
 * @opcode: the opcode of the crypto instruction
 * @func: the function code to test for
 *
 * Executes the query function for the given crypto instruction @opcode
 * and checks if @func is available
 *
 * Returns 1 if @func is available for @opcode, 0 otherwise
 */
static inline void __cpacf_query(unsigned int opcode, unsigned char *status)
{
	typedef struct { unsigned char _[16]; } status_type;
	register unsigned long r0 asm("0") = 0;	/* query function */
	register unsigned long r1 asm("1") = (unsigned long) status;

	asm volatile(
		"	spm 0\n" /* pckmo doesn't change the cc */
		/* Parameter registers are ignored, but may not be 0 */
		"0:	.insn	rrf,%[opc] << 16,2,2,2,0\n"
		"	brc	1,0b\n"	/* handle partial completion */
		: "=m" (*(status_type *) status)
		: [fc] "d" (r0), [pba] "a" (r1), [opc] "i" (opcode)
		: "cc");
}

static inline int cpacf_query(unsigned int opcode, unsigned int func)
{
	unsigned char status[16];

	switch (opcode) {
	case CPACF_KMAC:
	case CPACF_KM:
	case CPACF_KMC:
	case CPACF_KIMD:
	case CPACF_KLMD:
		if (!test_facility(17))	/* check for MSA */
			return 0;
		break;
	case CPACF_PCKMO:
		if (!test_facility(76))	/* check for MSA3 */
			return 0;
		break;
	case CPACF_KMF:
	case CPACF_KMO:
	case CPACF_PCC:
	case CPACF_KMCTR:
		if (!test_facility(77))	/* check for MSA4 */
			return 0;
		break;
	case CPACF_PPNO:
		if (!test_facility(57))	/* check for MSA5 */
			return 0;
		break;
	default:
		BUG();
	}
	__cpacf_query(opcode, status);
	return (status[func >> 3] & (0x80 >> (func & 7))) != 0;
}

/**
 * cpacf_km() - executes the KM (CIPHER MESSAGE) instruction
 * @func: the function code passed to KM; see CPACF_KM_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @dest: address of destination memory area
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Returns 0 for the query func, number of processed bytes for
 * encryption/decryption funcs
 */
static inline int cpacf_km(long func, void *param,
			   u8 *dest, const u8 *src, long src_len)
{
	register unsigned long r0 asm("0") = (unsigned long) func;
	register unsigned long r1 asm("1") = (unsigned long) param;
	register unsigned long r2 asm("2") = (unsigned long) src;
	register unsigned long r3 asm("3") = (unsigned long) src_len;
	register unsigned long r4 asm("4") = (unsigned long) dest;

	asm volatile(
		"0:	.insn	rre,%[opc] << 16,%[dst],%[src]\n"
		"	brc	1,0b\n" /* handle partial completion */
		: [src] "+a" (r2), [len] "+d" (r3), [dst] "+a" (r4)
		: [fc] "d" (r0), [pba] "a" (r1), [opc] "i" (CPACF_KM)
		: "cc", "memory");

	return src_len - r3;
}

/**
 * cpacf_kmc() - executes the KMC (CIPHER MESSAGE WITH CHAINING) instruction
 * @func: the function code passed to KM; see CPACF_KMC_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @dest: address of destination memory area
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Returns 0 for the query func, number of processed bytes for
 * encryption/decryption funcs
 */
static inline int cpacf_kmc(long func, void *param,
			    u8 *dest, const u8 *src, long src_len)
{
	register unsigned long r0 asm("0") = (unsigned long) func;
	register unsigned long r1 asm("1") = (unsigned long) param;
	register unsigned long r2 asm("2") = (unsigned long) src;
	register unsigned long r3 asm("3") = (unsigned long) src_len;
	register unsigned long r4 asm("4") = (unsigned long) dest;

	asm volatile(
		"0:	.insn	rre,%[opc] << 16,%[dst],%[src]\n"
		"	brc	1,0b\n" /* handle partial completion */
		: [src] "+a" (r2), [len] "+d" (r3), [dst] "+a" (r4)
		: [fc] "d" (r0), [pba] "a" (r1), [opc] "i" (CPACF_KMC)
		: "cc", "memory");

	return src_len - r3;
}

/**
 * cpacf_kimd() - executes the KIMD (COMPUTE INTERMEDIATE MESSAGE DIGEST)
 *		  instruction
 * @func: the function code passed to KM; see CPACF_KIMD_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Returns 0 for the query func, number of processed bytes for digest funcs
 */
static inline int cpacf_kimd(long func, void *param,
			     const u8 *src, long src_len)
{
	register unsigned long r0 asm("0") = (unsigned long) func;
	register unsigned long r1 asm("1") = (unsigned long) param;
	register unsigned long r2 asm("2") = (unsigned long) src;
	register unsigned long r3 asm("3") = (unsigned long) src_len;

	asm volatile(
		"0:	.insn	rre,%[opc] << 16,0,%[src]\n"
		"	brc	1,0b\n" /* handle partial completion */
		: [src] "+a" (r2), [len] "+d" (r3)
		: [fc] "d" (r0), [pba] "a" (r1), [opc] "i" (CPACF_KIMD)
		: "cc", "memory");

	return src_len - r3;
}

/**
 * cpacf_klmd() - executes the KLMD (COMPUTE LAST MESSAGE DIGEST) instruction
 * @func: the function code passed to KM; see CPACF_KLMD_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Returns 0 for the query func, number of processed bytes for digest funcs
 */
static inline int cpacf_klmd(long func, void *param,
			     const u8 *src, long src_len)
{
	register unsigned long r0 asm("0") = (unsigned long) func;
	register unsigned long r1 asm("1") = (unsigned long) param;
	register unsigned long r2 asm("2") = (unsigned long) src;
	register unsigned long r3 asm("3") = (unsigned long) src_len;

	asm volatile(
		"0:	.insn	rre,%[opc] << 16,0,%[src]\n"
		"	brc	1,0b\n" /* handle partial completion */
		: [src] "+a" (r2), [len] "+d" (r3)
		: [fc] "d" (r0), [pba] "a" (r1), [opc] "i" (CPACF_KLMD)
		: "cc", "memory");

	return src_len - r3;
}

/**
 * cpacf_kmac() - executes the KMAC (COMPUTE MESSAGE AUTHENTICATION CODE)
 *		  instruction
 * @func: the function code passed to KM; see CPACF_KMAC_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 *
 * Returns 0 for the query func, number of processed bytes for digest funcs
 */
static inline int cpacf_kmac(long func, void *param,
			     const u8 *src, long src_len)
{
	register unsigned long r0 asm("0") = (unsigned long) func;
	register unsigned long r1 asm("1") = (unsigned long) param;
	register unsigned long r2 asm("2") = (unsigned long) src;
	register unsigned long r3 asm("3") = (unsigned long) src_len;

	asm volatile(
		"0:	.insn	rre,%[opc] << 16,0,%[src]\n"
		"	brc	1,0b\n" /* handle partial completion */
		: [src] "+a" (r2), [len] "+d" (r3)
		: [fc] "d" (r0), [pba] "a" (r1), [opc] "i" (CPACF_KMAC)
		: "cc", "memory");

	return src_len - r3;
}

/**
 * cpacf_kmctr() - executes the KMCTR (CIPHER MESSAGE WITH COUNTER) instruction
 * @func: the function code passed to KMCTR; see CPACF_KMCTR_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @dest: address of destination memory area
 * @src: address of source memory area
 * @src_len: length of src operand in bytes
 * @counter: address of counter value
 *
 * Returns 0 for the query func, number of processed bytes for
 * encryption/decryption funcs
 */
static inline int cpacf_kmctr(long func, void *param, u8 *dest,
			      const u8 *src, long src_len, u8 *counter)
{
	register unsigned long r0 asm("0") = (unsigned long) func;
	register unsigned long r1 asm("1") = (unsigned long) param;
	register unsigned long r2 asm("2") = (unsigned long) src;
	register unsigned long r3 asm("3") = (unsigned long) src_len;
	register unsigned long r4 asm("4") = (unsigned long) dest;
	register unsigned long r6 asm("6") = (unsigned long) counter;

	asm volatile(
		"0:	.insn	rrf,%[opc] << 16,%[dst],%[src],%[ctr],0\n"
		"	brc	1,0b\n" /* handle partial completion */
		: [src] "+a" (r2), [len] "+d" (r3),
		  [dst] "+a" (r4), [ctr] "+a" (r6)
		: [fc] "d" (r0), [pba] "a" (r1), [opc] "i" (CPACF_KMCTR)
		: "cc", "memory");

	return src_len - r3;
}

/**
 * cpacf_ppno() - executes the PPNO (PERFORM PSEUDORANDOM NUMBER OPERATION)
 *		  instruction
 * @func: the function code passed to PPNO; see CPACF_PPNO_xxx defines
 * @param: address of parameter block; see POP for details on each func
 * @dest: address of destination memory area
 * @dest_len: size of destination memory area in bytes
 * @seed: address of seed data
 * @seed_len: size of seed data in bytes
 *
 * Returns 0 for the query func, number of random bytes stored in
 * dest buffer for generate function
 */
static inline int cpacf_ppno(long func, void *param,
			     u8 *dest, long dest_len,
			     const u8 *seed, long seed_len)
{
	register unsigned long r0 asm("0") = (unsigned long) func;
	register unsigned long r1 asm("1") = (unsigned long) param;
	register unsigned long r2 asm("2") = (unsigned long) dest;
	register unsigned long r3 asm("3") = (unsigned long) dest_len;
	register unsigned long r4 asm("4") = (unsigned long) seed;
	register unsigned long r5 asm("5") = (unsigned long) seed_len;

	asm volatile (
		"0:	.insn	rre,%[opc] << 16,%[dst],%[seed]\n"
		"	brc	1,0b\n"	  /* handle partial completion */
		: [dst] "+a" (r2), [dlen] "+d" (r3)
		: [fc] "d" (r0), [pba] "a" (r1),
		  [seed] "a" (r4), [slen] "d" (r5), [opc] "i" (CPACF_PPNO)
		: "cc", "memory");

	return dest_len - r3;
}

/**
 * cpacf_pcc() - executes the PCC (PERFORM CRYPTOGRAPHIC COMPUTATION)
 *		 instruction
 * @func: the function code passed to PCC; see CPACF_KM_xxx defines
 * @param: address of parameter block; see POP for details on each func
 *
 * Returns 0.
 */
static inline int cpacf_pcc(long func, void *param)
{
	register unsigned long r0 asm("0") = (unsigned long) func;
	register unsigned long r1 asm("1") = (unsigned long) param;

	asm volatile(
		"0:	.insn	rre,%[opc] << 16,0,0\n" /* PCC opcode */
		"	brc	1,0b\n" /* handle partial completion */
		:
		: [fc] "d" (r0), [pba] "a" (r1), [opc] "i" (CPACF_PCC)
		: "cc", "memory");

	return 0;
}

#endif	/* _ASM_S390_CPACF_H */
