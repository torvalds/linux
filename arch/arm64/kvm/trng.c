// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Arm Ltd.

#include <linux/arm-smccc.h>
#include <linux/kvm_host.h>

#include <asm/kvm_emulate.h>

#include <kvm/arm_hypercalls.h>

#define ARM_SMCCC_TRNG_VERSION_1_0	0x10000UL

/* Those values are deliberately separate from the generic SMCCC definitions. */
#define TRNG_SUCCESS			0UL
#define TRNG_NOT_SUPPORTED		((unsigned long)-1)
#define TRNG_INVALID_PARAMETER		((unsigned long)-2)
#define TRNG_NO_ENTROPY			((unsigned long)-3)

#define TRNG_MAX_BITS64			192

static const uuid_t arm_smc_trng_uuid __aligned(4) = UUID_INIT(
	0x0d21e000, 0x4384, 0x11eb, 0x80, 0x70, 0x52, 0x44, 0x55, 0x4e, 0x5a, 0x4c);

static int kvm_trng_do_rnd(struct kvm_vcpu *vcpu, int size)
{
	DECLARE_BITMAP(bits, TRNG_MAX_BITS64);
	u32 num_bits = smccc_get_arg1(vcpu);
	int i;

	if (num_bits > 3 * size) {
		smccc_set_retval(vcpu, TRNG_INVALID_PARAMETER, 0, 0, 0);
		return 1;
	}

	/* get as many bits as we need to fulfil the request */
	for (i = 0; i < DIV_ROUND_UP(num_bits, BITS_PER_LONG); i++)
		bits[i] = get_random_long();

	bitmap_clear(bits, num_bits, TRNG_MAX_BITS64 - num_bits);

	if (size == 32)
		smccc_set_retval(vcpu, TRNG_SUCCESS, lower_32_bits(bits[1]),
				 upper_32_bits(bits[0]), lower_32_bits(bits[0]));
	else
		smccc_set_retval(vcpu, TRNG_SUCCESS, bits[2], bits[1], bits[0]);

	memzero_explicit(bits, sizeof(bits));
	return 1;
}

int kvm_trng_call(struct kvm_vcpu *vcpu)
{
	const __le32 *u = (__le32 *)arm_smc_trng_uuid.b;
	u32 func_id = smccc_get_function(vcpu);
	unsigned long val = TRNG_NOT_SUPPORTED;
	int size = 64;

	switch (func_id) {
	case ARM_SMCCC_TRNG_VERSION:
		val = ARM_SMCCC_TRNG_VERSION_1_0;
		break;
	case ARM_SMCCC_TRNG_FEATURES:
		switch (smccc_get_arg1(vcpu)) {
		case ARM_SMCCC_TRNG_VERSION:
		case ARM_SMCCC_TRNG_FEATURES:
		case ARM_SMCCC_TRNG_GET_UUID:
		case ARM_SMCCC_TRNG_RND32:
		case ARM_SMCCC_TRNG_RND64:
			val = TRNG_SUCCESS;
		}
		break;
	case ARM_SMCCC_TRNG_GET_UUID:
		smccc_set_retval(vcpu, le32_to_cpu(u[0]), le32_to_cpu(u[1]),
				 le32_to_cpu(u[2]), le32_to_cpu(u[3]));
		return 1;
	case ARM_SMCCC_TRNG_RND32:
		size = 32;
		fallthrough;
	case ARM_SMCCC_TRNG_RND64:
		return kvm_trng_do_rnd(vcpu, size);
	}

	smccc_set_retval(vcpu, val, 0, 0, 0);
	return 1;
}
