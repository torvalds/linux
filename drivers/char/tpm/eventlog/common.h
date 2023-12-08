#ifndef __TPM_EVENTLOG_COMMON_H__
#define __TPM_EVENTLOG_COMMON_H__

#include "../tpm.h"

extern const struct seq_operations tpm1_ascii_b_measurements_seqops;
extern const struct seq_operations tpm1_binary_b_measurements_seqops;
extern const struct seq_operations tpm2_binary_b_measurements_seqops;

#if defined(CONFIG_ACPI)
int tpm_read_log_acpi(struct tpm_chip *chip);
#else
static inline int tpm_read_log_acpi(struct tpm_chip *chip)
{
	return -ENODEV;
}
#endif
#if defined(CONFIG_OF)
int tpm_read_log_of(struct tpm_chip *chip);
#else
static inline int tpm_read_log_of(struct tpm_chip *chip)
{
	return -ENODEV;
}
#endif
#if defined(CONFIG_EFI)
int tpm_read_log_efi(struct tpm_chip *chip);
#else
static inline int tpm_read_log_efi(struct tpm_chip *chip)
{
	return -ENODEV;
}
#endif

#endif
