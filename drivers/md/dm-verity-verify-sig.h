// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Microsoft Corporation.
 *
 * Author:  Jaskaran Singh Khurana <jaskarankhurana@linux.microsoft.com>
 *
 */
#ifndef DM_VERITY_SIG_VERIFICATION_H
#define DM_VERITY_SIG_VERIFICATION_H

#define DM_VERITY_ROOT_HASH_VERIFICATION "DM Verity Sig Verification"
#define DM_VERITY_ROOT_HASH_VERIFICATION_OPT_SIG_KEY "root_hash_sig_key_desc"

struct dm_verity_sig_opts {
	unsigned int sig_size;
	u8 *sig;
};

#ifdef CONFIG_DM_VERITY_VERIFY_ROOTHASH_SIG

#define DM_VERITY_ROOT_HASH_VERIFICATION_OPTS 2

int verity_verify_root_hash(const void *data, size_t data_len,
			    const void *sig_data, size_t sig_len);
bool verity_verify_is_sig_opt_arg(const char *arg_name);

int verity_verify_sig_parse_opt_args(struct dm_arg_set *as, struct dm_verity *v,
				    struct dm_verity_sig_opts *sig_opts,
				    unsigned int *argc, const char *arg_name);

void verity_verify_sig_opts_cleanup(struct dm_verity_sig_opts *sig_opts);

#else

#define DM_VERITY_ROOT_HASH_VERIFICATION_OPTS 0

int verity_verify_root_hash(const void *data, size_t data_len,
			    const void *sig_data, size_t sig_len)
{
	return 0;
}

bool verity_verify_is_sig_opt_arg(const char *arg_name)
{
	return false;
}

int verity_verify_sig_parse_opt_args(struct dm_arg_set *as, struct dm_verity *v,
				    struct dm_verity_sig_opts *sig_opts,
				    unsigned int *argc, const char *arg_name)
{
	return -EINVAL;
}

void verity_verify_sig_opts_cleanup(struct dm_verity_sig_opts *sig_opts)
{
}

#endif /* CONFIG_DM_VERITY_VERIFY_ROOTHASH_SIG */
#endif /* DM_VERITY_SIG_VERIFICATION_H */
