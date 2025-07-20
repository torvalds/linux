// SPDX-License-Identifier: GPL-2.0-only

#define pr_fmt(fmt) "papr-common: " fmt

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/sched/signal.h>
#include "papr-rtas-common.h"

/*
 * Sequence based RTAS HCALL has to issue multiple times to retrieve
 * complete data from the hypervisor. For some of these RTAS calls,
 * the OS should not interleave calls with different input until the
 * sequence is completed. So data is collected for these calls during
 * ioctl handle and export to user space with read() handle.
 * This file provides common functions needed for such sequence based
 * RTAS calls Ex: ibm,get-vpd and ibm,get-indices.
 */

bool papr_rtas_blob_has_data(const struct papr_rtas_blob *blob)
{
	return blob->data && blob->len;
}

void papr_rtas_blob_free(const struct papr_rtas_blob *blob)
{
	if (blob) {
		kvfree(blob->data);
		kfree(blob);
	}
}

/**
 * papr_rtas_blob_extend() - Append data to a &struct papr_rtas_blob.
 * @blob: The blob to extend.
 * @data: The new data to append to @blob.
 * @len:  The length of @data.
 *
 * Context: May sleep.
 * Return: -ENOMEM on allocation failure, 0 otherwise.
 */
static int papr_rtas_blob_extend(struct papr_rtas_blob *blob,
				const char *data, size_t len)
{
	const size_t new_len = blob->len + len;
	const size_t old_len = blob->len;
	const char *old_ptr = blob->data;
	char *new_ptr;

	new_ptr = kvrealloc(old_ptr, new_len, GFP_KERNEL_ACCOUNT);
	if (!new_ptr)
		return -ENOMEM;

	memcpy(&new_ptr[old_len], data, len);
	blob->data = new_ptr;
	blob->len = new_len;
	return 0;
}

/**
 * papr_rtas_blob_generate() - Construct a new &struct papr_rtas_blob.
 * @seq: work function of the caller that is called to obtain
 *       data with the caller RTAS call.
 *
 * The @work callback is invoked until it returns NULL. @seq is
 * passed to @work in its first argument on each call. When
 * @work returns data, it should store the data length in its
 * second argument.
 *
 * Context: May sleep.
 * Return: A completely populated &struct papr_rtas_blob, or NULL on error.
 */
static const struct papr_rtas_blob *
papr_rtas_blob_generate(struct papr_rtas_sequence *seq)
{
	struct papr_rtas_blob *blob;
	const char *buf;
	size_t len;
	int err = 0;

	blob  = kzalloc(sizeof(*blob), GFP_KERNEL_ACCOUNT);
	if (!blob)
		return NULL;

	if (!seq->work)
		return ERR_PTR(-EINVAL);


	while (err == 0 && (buf = seq->work(seq, &len)))
		err = papr_rtas_blob_extend(blob, buf, len);

	if (err != 0 || !papr_rtas_blob_has_data(blob))
		goto free_blob;

	return blob;
free_blob:
	papr_rtas_blob_free(blob);
	return NULL;
}

int papr_rtas_sequence_set_err(struct papr_rtas_sequence *seq, int err)
{
	/* Preserve the first error recorded. */
	if (seq->error == 0)
		seq->error = err;

	return seq->error;
}

/*
 * Higher-level retrieval code below. These functions use the
 * papr_rtas_blob_* and sequence_* APIs defined above to create fd-based
 * handles for consumption by user space.
 */

/**
 * papr_rtas_run_sequence() - Run a single retrieval sequence.
 * @seq:	Functions of the caller to complete the sequence
 *
 * Context: May sleep. Holds a mutex and an RTAS work area for its
 *          duration. Typically performs multiple sleepable slab
 *          allocations.
 *
 * Return: A populated &struct papr_rtas_blob on success. Encoded error
 * pointer otherwise.
 */
static const struct papr_rtas_blob *papr_rtas_run_sequence(struct papr_rtas_sequence *seq)
{
	const struct papr_rtas_blob *blob;

	if (seq->begin)
		seq->begin(seq);

	blob = papr_rtas_blob_generate(seq);
	if (!blob)
		papr_rtas_sequence_set_err(seq, -ENOMEM);

	if (seq->end)
		seq->end(seq);


	if (seq->error) {
		papr_rtas_blob_free(blob);
		return ERR_PTR(seq->error);
	}

	return blob;
}

/**
 * papr_rtas_retrieve() - Return the data blob that is exposed to
 * user space.
 * @seq: RTAS call specific functions to be invoked until the
 *       sequence is completed.
 *
 * Run sequences against @param until a blob is successfully
 * instantiated, or a hard error is encountered, or a fatal signal is
 * pending.
 *
 * Context: May sleep.
 * Return: A fully populated data blob when successful. Encoded error
 * pointer otherwise.
 */
const struct papr_rtas_blob *papr_rtas_retrieve(struct papr_rtas_sequence *seq)
{
	const struct papr_rtas_blob *blob;

	/*
	 * EAGAIN means the sequence returns error with a -4 (data
	 * changed and need to start the sequence) status from RTAS calls
	 * and we should attempt a new sequence. PAPR+ (v2.13 R1–7.3.20–5
	 * - ibm,get-vpd, R1–7.3.17–6 - ibm,get-indices) indicates that
	 * this should be a transient condition, not something that
	 * happens continuously. But we'll stop trying on a fatal signal.
	 */
	do {
		blob = papr_rtas_run_sequence(seq);
		if (!IS_ERR(blob)) /* Success. */
			break;
		if (PTR_ERR(blob) != -EAGAIN) /* Hard error. */
			break;
		cond_resched();
	} while (!fatal_signal_pending(current));

	return blob;
}

/**
 * papr_rtas_setup_file_interface - Complete the sequence and obtain
 * the data and export to user space with fd-based handles. Then the
 * user spave gets the data with read() handle.
 * @seq: RTAS call specific functions to get the data.
 * @fops: RTAS call specific file operations such as read().
 * @name: RTAS call specific char device node.
 *
 * Return: FD handle for consumption by user space
 */
long papr_rtas_setup_file_interface(struct papr_rtas_sequence *seq,
				const struct file_operations *fops,
				char *name)
{
	const struct papr_rtas_blob *blob;
	struct file *file;
	long ret;
	int fd;

	blob = papr_rtas_retrieve(seq);
	if (IS_ERR(blob))
		return PTR_ERR(blob);

	fd = get_unused_fd_flags(O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		ret = fd;
		goto free_blob;
	}

	file = anon_inode_getfile_fmode(name, fops, (void *)blob,
			O_RDONLY, FMODE_LSEEK | FMODE_PREAD);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto put_fd;
	}

	fd_install(fd, file);
	return fd;

put_fd:
	put_unused_fd(fd);
free_blob:
	papr_rtas_blob_free(blob);
	return ret;
}

/*
 * papr_rtas_sequence_should_stop() - Determine whether RTAS retrieval
 *                                    sequence should continue.
 *
 * Examines the sequence error state and outputs of the last call to
 * the specific RTAS to determine whether the sequence in progress
 * should continue or stop.
 *
 * Return: True if the sequence has encountered an error or if all data
 *         for this sequence has been retrieved. False otherwise.
 */
bool papr_rtas_sequence_should_stop(const struct papr_rtas_sequence *seq,
				s32 status, bool init_state)
{
	bool done;

	if (seq->error)
		return true;

	switch (status) {
	case RTAS_SEQ_COMPLETE:
		if (init_state)
			done = false; /* Initial state. */
		else
			done = true; /* All data consumed. */
		break;
	case RTAS_SEQ_MORE_DATA:
		done = false; /* More data available. */
		break;
	default:
		done = true; /* Error encountered. */
		break;
	}

	return done;
}

/*
 * User space read to retrieve data for the corresponding RTAS call.
 * papr_rtas_blob is filled with the data using the corresponding RTAS
 * call sequence API.
 */
ssize_t papr_rtas_common_handle_read(struct file *file,
	       char __user *buf, size_t size, loff_t *off)
{
	const struct papr_rtas_blob *blob = file->private_data;

	/* We should not instantiate a handle without any data attached. */
	if (!papr_rtas_blob_has_data(blob)) {
		pr_err_once("handle without data\n");
		return -EIO;
	}

	return simple_read_from_buffer(buf, size, off, blob->data, blob->len);
}

int papr_rtas_common_handle_release(struct inode *inode,
		struct file *file)
{
	const struct papr_rtas_blob *blob = file->private_data;

	papr_rtas_blob_free(blob);

	return 0;
}

loff_t papr_rtas_common_handle_seek(struct file *file, loff_t off,
					int whence)
{
	const struct papr_rtas_blob *blob = file->private_data;

	return fixed_size_llseek(file, off, whence, blob->len);
}
