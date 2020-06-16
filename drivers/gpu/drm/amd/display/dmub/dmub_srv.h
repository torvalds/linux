/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef _DMUB_SRV_H_
#define _DMUB_SRV_H_

/**
 * DOC: DMUB interface and operation
 *
 * DMUB is the interface to the display DMCUB microcontroller on DCN hardware.
 * It delegates hardware initialization and command submission to the
 * microcontroller. DMUB is the shortname for DMCUB.
 *
 * This interface is not thread-safe. Ensure that all access to the interface
 * is properly synchronized by the caller.
 *
 * Initialization and usage of the DMUB service should be done in the
 * steps given below:
 *
 * 1. dmub_srv_create()
 * 2. dmub_srv_has_hw_support()
 * 3. dmub_srv_calc_region_info()
 * 4. dmub_srv_hw_init()
 *
 * The call to dmub_srv_create() is required to use the server.
 *
 * The calls to dmub_srv_has_hw_support() and dmub_srv_calc_region_info()
 * are helpers to query cache window size and allocate framebuffer(s)
 * for the cache windows.
 *
 * The call to dmub_srv_hw_init() programs the DMCUB registers to prepare
 * for command submission. Commands can be queued via dmub_srv_cmd_queue()
 * and executed via dmub_srv_cmd_execute().
 *
 * If the queue is full the dmub_srv_wait_for_idle() call can be used to
 * wait until the queue has been cleared.
 *
 * Destroying the DMUB service can be done by calling dmub_srv_destroy().
 * This does not clear DMUB hardware state, only software state.
 *
 * The interface is intended to be standalone and should not depend on any
 * other component within DAL.
 */

#include "inc/dmub_types.h"
#include "inc/dmub_cmd.h"
#include "inc/dmub_gpint_cmd.h"
#include "inc/dmub_cmd_dal.h"
#include "inc/dmub_rb.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Forward declarations */
struct dmub_srv;
struct dmub_srv_common_regs;

/* enum dmub_status - return code for dmcub functions */
enum dmub_status {
	DMUB_STATUS_OK = 0,
	DMUB_STATUS_NO_CTX,
	DMUB_STATUS_QUEUE_FULL,
	DMUB_STATUS_TIMEOUT,
	DMUB_STATUS_INVALID,
};

/* enum dmub_asic - dmub asic identifier */
enum dmub_asic {
	DMUB_ASIC_NONE = 0,
	DMUB_ASIC_DCN20,
	DMUB_ASIC_DCN21,
	DMUB_ASIC_MAX,
};

/* enum dmub_window_id - dmub window identifier */
enum dmub_window_id {
	DMUB_WINDOW_0_INST_CONST = 0,
	DMUB_WINDOW_1_STACK,
	DMUB_WINDOW_2_BSS_DATA,
	DMUB_WINDOW_3_VBIOS,
	DMUB_WINDOW_4_MAILBOX,
	DMUB_WINDOW_5_TRACEBUFF,
	DMUB_WINDOW_6_FW_STATE,
	DMUB_WINDOW_7_SCRATCH_MEM,
	DMUB_WINDOW_TOTAL,
};

/**
 * struct dmub_region - dmub hw memory region
 * @base: base address for region, must be 256 byte aligned
 * @top: top address for region
 */
struct dmub_region {
	uint32_t base;
	uint32_t top;
};

/**
 * struct dmub_window - dmub hw cache window
 * @off: offset to the fb memory in gpu address space
 * @r: region in uc address space for cache window
 */
struct dmub_window {
	union dmub_addr offset;
	struct dmub_region region;
};

/**
 * struct dmub_fb - defines a dmub framebuffer memory region
 * @cpu_addr: cpu virtual address for the region, NULL if invalid
 * @gpu_addr: gpu virtual address for the region, NULL if invalid
 * @size: size of the region in bytes, zero if invalid
 */
struct dmub_fb {
	void *cpu_addr;
	uint64_t gpu_addr;
	uint32_t size;
};

/**
 * struct dmub_srv_region_params - params used for calculating dmub regions
 * @inst_const_size: size of the fw inst const section
 * @bss_data_size: size of the fw bss data section
 * @vbios_size: size of the vbios data
 * @fw_bss_data: raw firmware bss data section
 */
struct dmub_srv_region_params {
	uint32_t inst_const_size;
	uint32_t bss_data_size;
	uint32_t vbios_size;
	const uint8_t *fw_inst_const;
	const uint8_t *fw_bss_data;
};

/**
 * struct dmub_srv_region_info - output region info from the dmub service
 * @fb_size: required minimum fb size for all regions, aligned to 4096 bytes
 * @num_regions: number of regions used by the dmub service
 * @regions: region info
 *
 * The regions are aligned such that they can be all placed within the
 * same framebuffer but they can also be placed into different framebuffers.
 *
 * The size of each region can be calculated by the caller:
 * size = reg.top - reg.base
 *
 * Care must be taken when performing custom allocations to ensure that each
 * region base address is 256 byte aligned.
 */
struct dmub_srv_region_info {
	uint32_t fb_size;
	uint8_t num_regions;
	struct dmub_region regions[DMUB_WINDOW_TOTAL];
};

/**
 * struct dmub_srv_fb_params - parameters used for driver fb setup
 * @region_info: region info calculated by dmub service
 * @cpu_addr: base cpu address for the framebuffer
 * @gpu_addr: base gpu virtual address for the framebuffer
 */
struct dmub_srv_fb_params {
	const struct dmub_srv_region_info *region_info;
	void *cpu_addr;
	uint64_t gpu_addr;
};

/**
 * struct dmub_srv_fb_info - output fb info from the dmub service
 * @num_fbs: number of required dmub framebuffers
 * @fbs: fb data for each region
 *
 * Output from the dmub service helper that can be used by the
 * driver to prepare dmub_fb that can be passed into the dmub
 * hw init service.
 *
 * Assumes that all regions are within the same framebuffer
 * and have been setup according to the region_info generated
 * by the dmub service.
 */
struct dmub_srv_fb_info {
	uint8_t num_fb;
	struct dmub_fb fb[DMUB_WINDOW_TOTAL];
};

/**
 * struct dmub_srv_base_funcs - Driver specific base callbacks
 */
struct dmub_srv_base_funcs {
	/**
	 * @reg_read:
	 *
	 * Hook for reading a register.
	 *
	 * Return: The 32-bit register value from the given address.
	 */
	uint32_t (*reg_read)(void *ctx, uint32_t address);

	/**
	 * @reg_write:
	 *
	 * Hook for writing a value to the register specified by address.
	 */
	void (*reg_write)(void *ctx, uint32_t address, uint32_t value);
};

/**
 * struct dmub_srv_hw_funcs - hardware sequencer funcs for dmub
 */
struct dmub_srv_hw_funcs {
	/* private: internal use only */

	void (*init)(struct dmub_srv *dmub);

	void (*reset)(struct dmub_srv *dmub);

	void (*reset_release)(struct dmub_srv *dmub);

	void (*backdoor_load)(struct dmub_srv *dmub,
			      const struct dmub_window *cw0,
			      const struct dmub_window *cw1);

	void (*setup_windows)(struct dmub_srv *dmub,
			      const struct dmub_window *cw2,
			      const struct dmub_window *cw3,
			      const struct dmub_window *cw4,
			      const struct dmub_window *cw5,
			      const struct dmub_window *cw6);

	void (*setup_mailbox)(struct dmub_srv *dmub,
			      const struct dmub_region *inbox1);

	uint32_t (*get_inbox1_rptr)(struct dmub_srv *dmub);

	void (*set_inbox1_wptr)(struct dmub_srv *dmub, uint32_t wptr_offset);

	bool (*is_supported)(struct dmub_srv *dmub);

	bool (*is_hw_init)(struct dmub_srv *dmub);

	bool (*is_phy_init)(struct dmub_srv *dmub);

	bool (*is_auto_load_done)(struct dmub_srv *dmub);

	void (*set_gpint)(struct dmub_srv *dmub,
			  union dmub_gpint_data_register reg);

	bool (*is_gpint_acked)(struct dmub_srv *dmub,
			       union dmub_gpint_data_register reg);

	uint32_t (*get_gpint_response)(struct dmub_srv *dmub);
};

/**
 * struct dmub_srv_create_params - params for dmub service creation
 * @base_funcs: driver supplied base routines
 * @hw_funcs: optional overrides for hw funcs
 * @user_ctx: context data for callback funcs
 * @asic: driver supplied asic
 * @is_virtual: false for hw support only
 */
struct dmub_srv_create_params {
	struct dmub_srv_base_funcs funcs;
	struct dmub_srv_hw_funcs *hw_funcs;
	void *user_ctx;
	enum dmub_asic asic;
	bool is_virtual;
};

/*
 * struct dmub_srv_hw_params - params for dmub hardware initialization
 * @fb: framebuffer info for each region
 * @fb_base: base of the framebuffer aperture
 * @fb_offset: offset of the framebuffer aperture
 * @psp_version: psp version to pass for DMCU init
 * @load_inst_const: true if DMUB should load inst const fw
 */
struct dmub_srv_hw_params {
	struct dmub_fb *fb[DMUB_WINDOW_TOTAL];
	uint64_t fb_base;
	uint64_t fb_offset;
	uint32_t psp_version;
	bool load_inst_const;
};

/**
 * struct dmub_srv - software state for dmcub
 * @asic: dmub asic identifier
 * @user_ctx: user provided context for the dmub_srv
 * @is_virtual: false if hardware support only
 * @fw_state: dmub firmware state pointer
 */
struct dmub_srv {
	enum dmub_asic asic;
	void *user_ctx;
	bool is_virtual;
	struct dmub_fb scratch_mem_fb;
	volatile const struct dmub_fw_state *fw_state;

	/* private: internal use only */
	const struct dmub_srv_common_regs *regs;

	struct dmub_srv_base_funcs funcs;
	struct dmub_srv_hw_funcs hw_funcs;
	struct dmub_rb inbox1_rb;

	bool sw_init;
	bool hw_init;

	uint64_t fb_base;
	uint64_t fb_offset;
	uint32_t psp_version;
};

/**
 * dmub_srv_create() - creates the DMUB service.
 * @dmub: the dmub service
 * @params: creation parameters for the service
 *
 * Return:
 *   DMUB_STATUS_OK - success
 *   DMUB_STATUS_INVALID - unspecified error
 */
enum dmub_status dmub_srv_create(struct dmub_srv *dmub,
				 const struct dmub_srv_create_params *params);

/**
 * dmub_srv_destroy() - destroys the DMUB service.
 * @dmub: the dmub service
 */
void dmub_srv_destroy(struct dmub_srv *dmub);

/**
 * dmub_srv_calc_region_info() - retreives region info from the dmub service
 * @dmub: the dmub service
 * @params: parameters used to calculate region locations
 * @info_out: the output region info from dmub
 *
 * Calculates the base and top address for all relevant dmub regions
 * using the parameters given (if any).
 *
 * Return:
 *   DMUB_STATUS_OK - success
 *   DMUB_STATUS_INVALID - unspecified error
 */
enum dmub_status
dmub_srv_calc_region_info(struct dmub_srv *dmub,
			  const struct dmub_srv_region_params *params,
			  struct dmub_srv_region_info *out);

/**
 * dmub_srv_calc_region_info() - retreives fb info from the dmub service
 * @dmub: the dmub service
 * @params: parameters used to calculate fb locations
 * @info_out: the output fb info from dmub
 *
 * Calculates the base and top address for all relevant dmub regions
 * using the parameters given (if any).
 *
 * Return:
 *   DMUB_STATUS_OK - success
 *   DMUB_STATUS_INVALID - unspecified error
 */
enum dmub_status dmub_srv_calc_fb_info(struct dmub_srv *dmub,
				       const struct dmub_srv_fb_params *params,
				       struct dmub_srv_fb_info *out);

/**
 * dmub_srv_has_hw_support() - returns hw support state for dmcub
 * @dmub: the dmub service
 * @is_supported: hw support state
 *
 * Queries the hardware for DMCUB support and returns the result.
 *
 * Can be called before dmub_srv_hw_init().
 *
 * Return:
 *   DMUB_STATUS_OK - success
 *   DMUB_STATUS_INVALID - unspecified error
 */
enum dmub_status dmub_srv_has_hw_support(struct dmub_srv *dmub,
					 bool *is_supported);

/**
 * dmub_srv_is_hw_init() - returns hardware init state
 *
 * Return:
 *   DMUB_STATUS_OK - success
 *   DMUB_STATUS_INVALID - unspecified error
 */
enum dmub_status dmub_srv_is_hw_init(struct dmub_srv *dmub, bool *is_hw_init);

/**
 * dmub_srv_hw_init() - initializes the underlying DMUB hardware
 * @dmub: the dmub service
 * @params: params for hardware initialization
 *
 * Resets the DMUB hardware and performs backdoor loading of the
 * required cache regions based on the input framebuffer regions.
 *
 * Return:
 *   DMUB_STATUS_OK - success
 *   DMUB_STATUS_NO_CTX - dmcub context not initialized
 *   DMUB_STATUS_INVALID - unspecified error
 */
enum dmub_status dmub_srv_hw_init(struct dmub_srv *dmub,
				  const struct dmub_srv_hw_params *params);

/**
 * dmub_srv_hw_reset() - puts the DMUB hardware in reset state if initialized
 * @dmub: the dmub service
 *
 * Before destroying the DMUB service or releasing the backing framebuffer
 * memory we'll need to put the DMCUB into reset first.
 *
 * A subsequent call to dmub_srv_hw_init() will re-enable the DMCUB.
 *
 * Return:
 *   DMUB_STATUS_OK - success
 *   DMUB_STATUS_INVALID - unspecified error
 */
enum dmub_status dmub_srv_hw_reset(struct dmub_srv *dmub);

/**
 * dmub_srv_cmd_queue() - queues a command to the DMUB
 * @dmub: the dmub service
 * @cmd: the command to queue
 *
 * Queues a command to the DMUB service but does not begin execution
 * immediately.
 *
 * Return:
 *   DMUB_STATUS_OK - success
 *   DMUB_STATUS_QUEUE_FULL - no remaining room in queue
 *   DMUB_STATUS_INVALID - unspecified error
 */
enum dmub_status dmub_srv_cmd_queue(struct dmub_srv *dmub,
				    const union dmub_rb_cmd *cmd);

/**
 * dmub_srv_cmd_execute() - Executes a queued sequence to the dmub
 * @dmub: the dmub service
 *
 * Begins execution of queued commands on the dmub.
 *
 * Return:
 *   DMUB_STATUS_OK - success
 *   DMUB_STATUS_INVALID - unspecified error
 */
enum dmub_status dmub_srv_cmd_execute(struct dmub_srv *dmub);

/**
 * dmub_srv_wait_for_auto_load() - Waits for firmware auto load to complete
 * @dmub: the dmub service
 * @timeout_us: the maximum number of microseconds to wait
 *
 * Waits until firmware has been autoloaded by the DMCUB. The maximum
 * wait time is given in microseconds to prevent spinning forever.
 *
 * On ASICs without firmware autoload support this function will return
 * immediately.
 *
 * Return:
 *   DMUB_STATUS_OK - success
 *   DMUB_STATUS_TIMEOUT - wait for phy init timed out
 *   DMUB_STATUS_INVALID - unspecified error
 */
enum dmub_status dmub_srv_wait_for_auto_load(struct dmub_srv *dmub,
					     uint32_t timeout_us);

/**
 * dmub_srv_wait_for_phy_init() - Waits for DMUB PHY init to complete
 * @dmub: the dmub service
 * @timeout_us: the maximum number of microseconds to wait
 *
 * Waits until the PHY has been initialized by the DMUB. The maximum
 * wait time is given in microseconds to prevent spinning forever.
 *
 * On ASICs without PHY init support this function will return
 * immediately.
 *
 * Return:
 *   DMUB_STATUS_OK - success
 *   DMUB_STATUS_TIMEOUT - wait for phy init timed out
 *   DMUB_STATUS_INVALID - unspecified error
 */
enum dmub_status dmub_srv_wait_for_phy_init(struct dmub_srv *dmub,
					    uint32_t timeout_us);

/**
 * dmub_srv_wait_for_idle() - Waits for the DMUB to be idle
 * @dmub: the dmub service
 * @timeout_us: the maximum number of microseconds to wait
 *
 * Waits until the DMUB buffer is empty and all commands have
 * finished processing. The maximum wait time is given in
 * microseconds to prevent spinning forever.
 *
 * Return:
 *   DMUB_STATUS_OK - success
 *   DMUB_STATUS_TIMEOUT - wait for buffer to flush timed out
 *   DMUB_STATUS_INVALID - unspecified error
 */
enum dmub_status dmub_srv_wait_for_idle(struct dmub_srv *dmub,
					uint32_t timeout_us);

/**
 * dmub_srv_send_gpint_command() - Sends a GPINT based command.
 * @dmub: the dmub service
 * @command_code: the command code to send
 * @param: the command parameter to send
 * @timeout_us: the maximum number of microseconds to wait
 *
 * Sends a command via the general purpose interrupt (GPINT).
 * Waits for the number of microseconds specified by timeout_us
 * for the command ACK before returning.
 *
 * Can be called after software initialization.
 *
 * Return:
 *   DMUB_STATUS_OK - success
 *   DMUB_STATUS_TIMEOUT - wait for ACK timed out
 *   DMUB_STATUS_INVALID - unspecified error
 */
enum dmub_status
dmub_srv_send_gpint_command(struct dmub_srv *dmub,
			    enum dmub_gpint_command command_code,
			    uint16_t param, uint32_t timeout_us);

/**
 * dmub_srv_get_gpint_response() - Queries the GPINT response.
 * @dmub: the dmub service
 * @response: the response for the last GPINT
 *
 * Returns the response code for the last GPINT interrupt.
 *
 * Can be called after software initialization.
 *
 * Return:
 *   DMUB_STATUS_OK - success
 *   DMUB_STATUS_INVALID - unspecified error
 */
enum dmub_status dmub_srv_get_gpint_response(struct dmub_srv *dmub,
					     uint32_t *response);

/**
 * dmub_flush_buffer_mem() - Read back entire frame buffer region.
 * This ensures that the write from x86 has been flushed and will not
 * hang the DMCUB.
 * @fb: frame buffer to flush
 *
 * Can be called after software initialization.
 */
void dmub_flush_buffer_mem(const struct dmub_fb *fb);

#if defined(__cplusplus)
}
#endif

#endif /* _DMUB_SRV_H_ */
