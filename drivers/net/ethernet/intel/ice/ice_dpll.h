/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022, Intel Corporation. */

#ifndef _ICE_DPLL_H_
#define _ICE_DPLL_H_

#include "ice.h"

#define ICE_DPLL_RCLK_NUM_MAX	4
#define ICE_DPLL_TXCLK_NUM_MAX	2
#define E825_EXT_EREF_PIN_IDX	0
#define E825_EXT_SYNCE_PIN_IDX	1

#define ICE_CGU_R10			0x28
#define ICE_CGU_R10_SYNCE_CLKO_SEL	GENMASK(8, 5)
#define ICE_CGU_R10_SYNCE_CLKODIV_M1	GENMASK(13, 9)
#define ICE_CGU_R10_SYNCE_CLKODIV_LOAD	BIT(14)
#define ICE_CGU_R10_SYNCE_DCK_RST	BIT(15)
#define ICE_CGU_R10_SYNCE_ETHCLKO_SEL	GENMASK(18, 16)
#define ICE_CGU_R10_SYNCE_ETHDIV_M1	GENMASK(23, 19)
#define ICE_CGU_R10_SYNCE_ETHDIV_LOAD	BIT(24)
#define ICE_CGU_R10_SYNCE_DCK2_RST	BIT(25)
#define ICE_CGU_R10_SYNCE_S_REF_CLK	GENMASK(31, 27)

#define ICE_CGU_R11			0x2C
#define ICE_CGU_R11_SYNCE_S_BYP_CLK	GENMASK(6, 1)

#define ICE_CGU_BYPASS_MUX_OFFSET_E825C	3

/**
 * enum ice_dpll_pin_sw - enumerate ice software pin indices:
 * @ICE_DPLL_PIN_SW_1_IDX: index of first SW pin
 * @ICE_DPLL_PIN_SW_2_IDX: index of second SW pin
 * @ICE_DPLL_PIN_SW_NUM: number of SW pins in pair
 */
enum ice_dpll_pin_sw {
	ICE_DPLL_PIN_SW_1_IDX,
	ICE_DPLL_PIN_SW_2_IDX,
	ICE_DPLL_PIN_SW_NUM
};

struct ice_dpll_pin_work {
	struct work_struct work;
	unsigned long action;
	struct ice_dpll_pin *pin;
};

/** ice_dpll_pin - store info about pins
 * @pin: dpll pin structure
 * @pf: pointer to pf, which has registered the dpll_pin
 * @tracker: reference count tracker
 * @idx: ice pin private idx
 * @num_parents: hols number of parent pins
 * @parent_idx: hold indexes of parent pins
 * @flags: pin flags returned from HW
 * @state: state of a pin
 * @prop: pin properties
 * @freq: current frequency of a pin
 * @phase_adjust: current phase adjust value
 * @phase_offset: monitored phase offset value
 * @ref_sync: store id of reference sync pin
 */
struct ice_dpll_pin {
	struct dpll_pin *pin;
	struct ice_pf *pf;
	dpll_tracker tracker;
	struct fwnode_handle *fwnode;
	struct notifier_block nb;
	u8 idx;
	u8 num_parents;
	u8 parent_idx[ICE_DPLL_RCLK_NUM_MAX];
	u8 flags[ICE_DPLL_RCLK_NUM_MAX];
	u8 state[ICE_DPLL_RCLK_NUM_MAX];
	struct dpll_pin_properties prop;
	u32 freq;
	s32 phase_adjust;
	struct ice_dpll_pin *input;
	struct ice_dpll_pin *output;
	enum dpll_pin_direction direction;
	s64 phase_offset;
	u8 status;
	u8 ref_sync;
	bool active;
	bool hidden;
	enum ice_e825c_ref_clk tx_ref_src;
};

/** ice_dpll - store info required for DPLL control
 * @dpll: pointer to dpll dev
 * @pf: pointer to pf, which has registered the dpll_device
 * @tracker: reference count tracker
 * @dpll_idx: index of dpll on the NIC
 * @input_idx: currently selected input index
 * @prev_input_idx: previously selected input index
 * @ref_state: state of dpll reference signals
 * @eec_mode: eec_mode dpll is configured for
 * @phase_offset: phase offset of active pin vs dpll signal
 * @prev_phase_offset: previous phase offset of active pin vs dpll signal
 * @input_prio: priorities of each input
 * @dpll_state: current dpll sync state
 * @prev_dpll_state: last dpll sync state
 * @phase_offset_monitor_period: period for phase offset monitor read frequency
 * @active_input: pointer to active input pin
 * @prev_input: pointer to previous active input pin
 * @ops: holds the registered ops
 */
struct ice_dpll {
	struct dpll_device *dpll;
	struct ice_pf *pf;
	dpll_tracker tracker;
	u8 dpll_idx;
	u8 input_idx;
	u8 prev_input_idx;
	u8 ref_state;
	u8 eec_mode;
	s64 phase_offset;
	s64 prev_phase_offset;
	u8 *input_prio;
	enum dpll_lock_status dpll_state;
	enum dpll_lock_status prev_dpll_state;
	enum dpll_mode mode;
	u32 phase_offset_monitor_period;
	struct dpll_pin *active_input;
	struct dpll_pin *prev_input;
	const struct dpll_device_ops *ops;
};

/** ice_dplls - store info required for CCU (clock controlling unit)
 * @kworker: periodic worker
 * @work: periodic work
 * @wq: workqueue used to schedule DPLL-related deferred work
 * @lock: protects DPLL configuration (see Locking below)
 * @eec: pointer to EEC dpll dev
 * @pps: pointer to PPS dpll dev
 * @txc: pointer to TXC dpll dev
 * @inputs: input pins pointer
 * @outputs: output pins pointer
 * @rclk: recovered pins pointer
 * @txclks: TX clock reference pins pointer
 * @num_inputs: number of input pins available on dpll
 * @num_outputs: number of output pins available on dpll
 * @cgu_state_acq_err_num: number of errors returned during periodic work
 * @base_rclk_idx: idx of first pin used for clock revocery pins
 * @clock_id: clock_id of dplls
 * @input_phase_adj_max: max phase adjust value for an input pins
 * @output_phase_adj_max: max phase adjust value for an output pins
 * @periodic_counter: counter of periodic work executions
 * @generic: true when generic DPLL ops are used
 * @txclk_work: deferred TX reference clock switch worker
 * @txclk_switch_requested: a TX ref clock switch is queued in @txclk_work
 * @txclk_notify_rwsem: drains in-flight TXCLK notifications on teardown
 *
 * Locking:
 *   Acquisition order (top to bottom):
 *
 *     txclk_notify_rwsem (read)
 *       -> pf->dplls.lock
 *         -> ctrl_pf->dplls.lock
 *
 *   - @lock serializes all DPLL state mutations on this PF. When the
 *     controlling PF's lock must also be taken (e.g. updating the shared
 *     tx_refclks usage map), acquire pf->dplls.lock first, then
 *     ctrl_pf->dplls.lock. Skip the second acquire when pf == ctrl_pf
 *     to avoid recursive locking.
 *   - @txclk_notify_rwsem is held for read across
 *     ice_txclk_update_and_notify(), including the out-of-lock
 *     dpll_*_change_ntf() calls. ice_dpll_deinit() takes the write side
 *     standalone (not nested under any other lock) to drain in-flight
 *     readers before pins and the TXC DPLL device are freed.
 */
struct ice_dplls {
	struct kthread_worker *kworker;
	struct kthread_delayed_work work;
	struct workqueue_struct *wq;
	struct mutex lock;
	struct completion dpll_init;
	struct ice_dpll eec;
	struct ice_dpll pps;
	struct ice_dpll txc;
	struct ice_dpll_pin *inputs;
	struct ice_dpll_pin *outputs;
	struct ice_dpll_pin sma[ICE_DPLL_PIN_SW_NUM];
	struct ice_dpll_pin ufl[ICE_DPLL_PIN_SW_NUM];
	struct ice_dpll_pin rclk;
	struct ice_dpll_pin txclks[ICE_DPLL_TXCLK_NUM_MAX];
	u8 num_inputs;
	u8 num_outputs;
	u8 sma_data;
	u8 base_rclk_idx;
	int cgu_state_acq_err_num;
	u64 clock_id;
	s32 input_phase_adj_max;
	s32 output_phase_adj_max;
	u32 periodic_counter;
	bool generic;
	struct work_struct txclk_work;
	bool txclk_switch_requested;
	struct rw_semaphore txclk_notify_rwsem;
};

#if IS_ENABLED(CONFIG_PTP_1588_CLOCK)
void ice_dpll_init(struct ice_pf *pf);
void ice_dpll_deinit(struct ice_pf *pf);
#else
static inline void ice_dpll_init(struct ice_pf *pf) { }
static inline void ice_dpll_deinit(struct ice_pf *pf) { }
#endif

#endif
