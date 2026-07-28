/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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
 */

#ifndef __AMDGPU_DM_IRQ_H__
#define __AMDGPU_DM_IRQ_H__

#include "irq_types.h" /* DAL irq definitions */

struct amdgpu_device;
struct amdgpu_crtc;
struct amdgpu_display_manager;
struct dc_sink;
struct hpd_rx_irq_offload_work_queue;
struct work_struct;
enum dmub_notification_type;

/*
 * Display Manager IRQ-related interfaces (for use by DAL).
 */

/**
 * amdgpu_dm_irq_init - Initialize internal structures of 'amdgpu_dm_irq'.
 *
 * This function should be called exactly once - during DM initialization.
 *
 * Returns:
 *	0 - success
 *	non-zero - error
 */
int amdgpu_dm_irq_init(struct amdgpu_device *adev);

/**
 * amdgpu_dm_irq_fini - deallocate internal structures of 'amdgpu_dm_irq'.
 *
 * This function should be called exactly once - during DM destruction.
 *
 */
void amdgpu_dm_irq_fini(struct amdgpu_device *adev);

/**
 * amdgpu_dm_irq_register_interrupt - register irq handler for Display block.
 *
 * @adev: AMD DRM device
 * @int_params: parameters for the irq
 * @ih: pointer to the irq hander function
 * @handler_args: arguments which will be passed to ih
 *
 * Returns:
 * 	IRQ Handler Index on success.
 * 	NULL on failure.
 *
 * Cannot be called from an interrupt handler.
 */
void *amdgpu_dm_irq_register_interrupt(struct amdgpu_device *adev,
				       struct dc_interrupt_params *int_params,
				       void (*ih)(void *),
				       void *handler_args);

/**
 * amdgpu_dm_irq_unregister_interrupt - unregister handler which was registered
 *	by amdgpu_dm_irq_register_interrupt().
 *
 * @adev: AMD DRM device.
 * @ih_index: irq handler index which was returned by
 *	amdgpu_dm_irq_register_interrupt
 */
void amdgpu_dm_irq_unregister_interrupt(struct amdgpu_device *adev,
					enum dc_irq_source irq_source,
					void *ih_index);

void amdgpu_dm_set_irq_funcs(struct amdgpu_device *adev);

void amdgpu_dm_outbox_init(struct amdgpu_device *adev);
void amdgpu_dm_hpd_init(struct amdgpu_device *adev);
void amdgpu_dm_hpd_fini(struct amdgpu_device *adev);

/**
 * amdgpu_dm_irq_suspend - disable ASIC interrupt during suspend.
 *
 */
void amdgpu_dm_irq_suspend(struct amdgpu_device *adev);

/**
 * amdgpu_dm_irq_resume_early - enable HPDRX ASIC interrupts during resume.
 * amdgpu_dm_irq_resume - enable ASIC interrupt during resume.
 *
 */
void amdgpu_dm_irq_resume_early(struct amdgpu_device *adev);
void amdgpu_dm_irq_resume_late(struct amdgpu_device *adev);

/* HPD handling */
struct hpd_rx_irq_offload_work_queue *amdgpu_dm_hpd_rx_irq_create_workqueue(struct amdgpu_device *adev);
void amdgpu_dm_hpd_rx_irq_work_suspend(struct amdgpu_display_manager *dm);
int amdgpu_dm_register_hpd_handlers(struct amdgpu_device *adev);
void amdgpu_dm_hdmi_hpd_debounce_work(struct work_struct *work);

/* IRQ handlers */
struct amdgpu_crtc *amdgpu_dm_get_crtc_by_otg_inst(struct amdgpu_device *adev,
						    int otg_inst);
int amdgpu_dm_dce110_register_irq_handlers(struct amdgpu_device *adev);
int amdgpu_dm_dcn10_register_irq_handlers(struct amdgpu_device *adev);
int amdgpu_dm_register_outbox_irq_handlers(struct amdgpu_device *adev);

#if IS_ENABLED(CONFIG_DRM_AMD_DC_KUNIT_TEST)
struct amdgpu_irq_src;
struct amdgpu_iv_entry;
enum amdgpu_interrupt_state;

enum dc_irq_source amdgpu_dm_hpd_to_dal_irq_source(unsigned int type);
bool are_sinks_equal(const struct dc_sink *sink1, const struct dc_sink *sink2);
const char *dmub_notification_type_str(enum dmub_notification_type e);
int amdgpu_dm_set_hpd_irq_state(struct amdgpu_device *adev,
				struct amdgpu_irq_src *source,
				unsigned int type,
				enum amdgpu_interrupt_state state);
int amdgpu_dm_set_dmub_outbox_irq_state(struct amdgpu_device *adev,
					struct amdgpu_irq_src *source,
					unsigned int crtc_id,
					enum amdgpu_interrupt_state state);
int amdgpu_dm_set_dmub_trace_irq_state(struct amdgpu_device *adev,
				       struct amdgpu_irq_src *source,
				       unsigned int type,
				       enum amdgpu_interrupt_state state);
int amdgpu_dm_set_pflip_irq_state(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  unsigned int crtc_id,
				  enum amdgpu_interrupt_state state);
int amdgpu_dm_set_crtc_irq_state(struct amdgpu_device *adev,
				 struct amdgpu_irq_src *source,
				 unsigned int crtc_id,
				 enum amdgpu_interrupt_state state);
int amdgpu_dm_set_vline0_irq_state(struct amdgpu_device *adev,
				   struct amdgpu_irq_src *source,
				   unsigned int crtc_id,
				   enum amdgpu_interrupt_state state);
int amdgpu_dm_set_vupdate_irq_state(struct amdgpu_device *adev,
				    struct amdgpu_irq_src *source,
				    unsigned int crtc_id,
				    enum amdgpu_interrupt_state state);
void amdgpu_dm_irq_schedule_work(struct amdgpu_device *adev,
				 enum dc_irq_source irq_source);
void amdgpu_dm_irq_immediate_work(struct amdgpu_device *adev,
				  enum dc_irq_source irq_source);
void dm_handle_hpd_rx_offload_work(struct work_struct *work);
void handle_hpd_irq_helper(struct amdgpu_dm_connector *aconnector,
			   enum dc_detect_reason reason);
void handle_hpd_irq(void *param);
void schedule_hpd_rx_offload_work(struct amdgpu_device *adev,
				  struct hpd_rx_irq_offload_work_queue *offload_wq,
				  union hpd_irq_data hpd_irq_data);
void handle_hpd_rx_irq(void *param);
void dmub_hpd_callback(struct amdgpu_device *adev,
		       struct dmub_notification *notify);
void dmub_hpd_sense_callback(struct amdgpu_device *adev,
			     struct dmub_notification *notify);
void dm_pflip_high_irq(void *interrupt_params);
void dm_vupdate_high_irq(void *interrupt_params);
void dm_crtc_high_irq(void *interrupt_params);
void dm_handle_hpd_work(struct work_struct *work);
void dm_dmub_outbox1_low_irq(void *interrupt_params);
int amdgpu_dm_irq_handler(struct amdgpu_device *adev,
			  struct amdgpu_irq_src *source,
			  struct amdgpu_iv_entry *entry);
void dm_handle_vmin_vmax_update(struct work_struct *offload_work);
#endif

#endif /* __AMDGPU_DM_IRQ_H__ */
