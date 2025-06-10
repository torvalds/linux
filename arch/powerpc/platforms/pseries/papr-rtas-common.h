/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_POWERPC_PAPR_RTAS_COMMON_H
#define _ASM_POWERPC_PAPR_RTAS_COMMON_H

#include <linux/types.h>

/*
 * Return codes for sequence based RTAS calls.
 * Not listed under PAPR+ v2.13 7.2.8: "Return Codes".
 * But defined in the specific section of each RTAS call.
 */
#define RTAS_SEQ_COMPLETE	0 /* All data has been retrieved. */
#define RTAS_SEQ_MORE_DATA	1 /* More data is available */
#define RTAS_SEQ_START_OVER	-4 /* Data changed, restart call sequence. */

/*
 * Internal "blob" APIs for accumulating RTAS call results into
 * an immutable buffer to be attached to a file descriptor.
 */
struct papr_rtas_blob {
	const char *data;
	size_t len;
};

/**
 * struct papr_sequence - State for managing a sequence of RTAS calls.
 * @error:  Shall be zero as long as the sequence has not encountered an error,
 *          -ve errno otherwise. Use papr_rtas_sequence_set_err() to update.
 * @params: Parameter block to pass to rtas_*() calls.
 * @begin: Work area allocation and initialize the needed parameter
 *         values passed to RTAS call
 * @end: Free the allocated work area
 * @work: Obtain data with RTAS call and invoke it until the sequence is
 *        completed.
 *
 */
struct papr_rtas_sequence {
	int error;
	void *params;
	void (*begin)(struct papr_rtas_sequence *seq);
	void (*end)(struct papr_rtas_sequence *seq);
	const char *(*work)(struct papr_rtas_sequence *seq, size_t *len);
};

extern bool papr_rtas_blob_has_data(const struct papr_rtas_blob *blob);
extern void papr_rtas_blob_free(const struct papr_rtas_blob *blob);
extern int papr_rtas_sequence_set_err(struct papr_rtas_sequence *seq,
		int err);
extern const struct papr_rtas_blob *papr_rtas_retrieve(struct papr_rtas_sequence *seq);
extern long papr_rtas_setup_file_interface(struct papr_rtas_sequence *seq,
			const struct file_operations *fops, char *name);
extern bool papr_rtas_sequence_should_stop(const struct papr_rtas_sequence *seq,
				s32 status, bool init_state);
extern ssize_t papr_rtas_common_handle_read(struct file *file,
			char __user *buf, size_t size, loff_t *off);
extern int papr_rtas_common_handle_release(struct inode *inode,
					struct file *file);
extern loff_t papr_rtas_common_handle_seek(struct file *file, loff_t off,
					int whence);
#endif /* _ASM_POWERPC_PAPR_RTAS_COMMON_H */

