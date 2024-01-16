#ifndef	_ASM_X86_INTEL_PCONFIG_H
#define	_ASM_X86_INTEL_PCONFIG_H

#include <asm/asm.h>
#include <asm/processor.h>

enum pconfig_target {
	INVALID_TARGET	= 0,
	MKTME_TARGET	= 1,
	PCONFIG_TARGET_NR
};

int pconfig_target_supported(enum pconfig_target target);

enum pconfig_leaf {
	MKTME_KEY_PROGRAM	= 0,
	PCONFIG_LEAF_INVALID,
};

#define PCONFIG ".byte 0x0f, 0x01, 0xc5"

/* Defines and structure for MKTME_KEY_PROGRAM of PCONFIG instruction */

/* mktme_key_program::keyid_ctrl COMMAND, bits [7:0] */
#define MKTME_KEYID_SET_KEY_DIRECT	0
#define MKTME_KEYID_SET_KEY_RANDOM	1
#define MKTME_KEYID_CLEAR_KEY		2
#define MKTME_KEYID_NO_ENCRYPT		3

/* mktme_key_program::keyid_ctrl ENC_ALG, bits [23:8] */
#define MKTME_AES_XTS_128	(1 << 8)

/* Return codes from the PCONFIG MKTME_KEY_PROGRAM */
#define MKTME_PROG_SUCCESS	0
#define MKTME_INVALID_PROG_CMD	1
#define MKTME_ENTROPY_ERROR	2
#define MKTME_INVALID_KEYID	3
#define MKTME_INVALID_ENC_ALG	4
#define MKTME_DEVICE_BUSY	5

/* Hardware requires the structure to be 256 byte aligned. Otherwise #GP(0). */
struct mktme_key_program {
	u16 keyid;
	u32 keyid_ctrl;
	u8 __rsvd[58];
	u8 key_field_1[64];
	u8 key_field_2[64];
} __packed __aligned(256);

static inline int mktme_key_program(struct mktme_key_program *key_program)
{
	unsigned long rax = MKTME_KEY_PROGRAM;

	if (!pconfig_target_supported(MKTME_TARGET))
		return -ENXIO;

	asm volatile(PCONFIG
		: "=a" (rax), "=b" (key_program)
		: "0" (rax), "1" (key_program)
		: "memory", "cc");

	return rax;
}

#endif	/* _ASM_X86_INTEL_PCONFIG_H */
