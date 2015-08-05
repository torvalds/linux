/*
 *
 * (C) COPYRIGHT 2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#ifndef _KBASE_VINSTR_H_
#define _KBASE_VINSTR_H_

enum {
	SHADER_HWCNT_BM,
	TILER_HWCNT_BM,
	MMU_L2_HWCNT_BM,
	JM_HWCNT_BM
};

struct kbase_vinstr_context;
struct kbase_vinstr_client;

/**
 * kbase_vinstr_init() - Initialize the vinstr core
 * @kbdev:	Kbase device
 *
 * Return:	A pointer to the vinstr context on success or NULL on failure
 */
struct kbase_vinstr_context *kbase_vinstr_init(struct kbase_device *kbdev);

/**
 * kbase_vinstr_term() - Terminate the vinstr core
 * @ctx:	Vinstr context
 */
void kbase_vinstr_term(struct kbase_vinstr_context *ctx);

/**
 * kbase_vinstr_attach_client - Attach a client to the vinstr core
 * @ctx:		Vinstr context
 * @kernel:		True if this client is a kernel-side client, false
 *                      otherwise
 * @dump_buffer:	Client's dump buffer
 * @bitmap:		Bitmaps describing which counters should be enabled
 *
 * Return:		A vinstr opaque client handle or NULL or failure
 */
struct kbase_vinstr_client *kbase_vinstr_attach_client(struct kbase_vinstr_context *ctx,
		bool kernel,
		u64 dump_buffer,
		u32 bitmap[4]);

/**
 * kbase_vinstr_detach_client - Detach a client from the vinstr core
 * @ctx:	Vinstr context
 * @cli:	Pointer to vinstr client
 */
void kbase_vinstr_detach_client(struct kbase_vinstr_context *ctx,
		struct kbase_vinstr_client *cli);

/**
 * kbase_vinstr_dump_size - Get the size of the dump buffer
 * @ctx:	Vinstr context
 *
 * This is only useful for kernel-side clients to know how much
 * memory they need to allocate to receive the performance counter
 * memory block.
 *
 * Return:	Returns the size of the client side buffer
 */
size_t kbase_vinstr_dump_size(struct kbase_vinstr_context *ctx);

/**
 * kbase_vinstr_dump - Performs a synchronous hardware counter dump for a given
 *                     kbase context
 * @ctx:	Vinstr context
 * @cli:	Pointer to vinstr client
 *
 * Return:	0 on success
 */
int kbase_vinstr_dump(struct kbase_vinstr_context *ctx,
		struct kbase_vinstr_client *cli);

/**
 * kbase_vinstr_clear - Performs a reset of the hardware counters for a given
 *                      kbase context
 * @ctx:	Vinstr context
 * @cli:	Pointer to vinstr client
 *
 * Return:	0 on success
 */
int kbase_vinstr_clear(struct kbase_vinstr_context *ctx,
		struct kbase_vinstr_client *cli);

#endif /* _KBASE_VINSTR_H_ */
