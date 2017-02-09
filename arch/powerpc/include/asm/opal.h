/*
 * PowerNV OPAL definitions.
 *
 * Copyright 2011 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_POWERPC_OPAL_H
#define _ASM_POWERPC_OPAL_H

#include <asm/opal-api.h>

#ifndef __ASSEMBLY__

#include <linux/notifier.h>

/* We calculate number of sg entries based on PAGE_SIZE */
#define SG_ENTRIES_PER_NODE ((PAGE_SIZE - 16) / sizeof(struct opal_sg_entry))

/* /sys/firmware/opal */
extern struct kobject *opal_kobj;

/* /ibm,opal */
extern struct device_node *opal_node;

/* API functions */
int64_t opal_invalid_call(void);
int64_t opal_console_write(int64_t term_number, __be64 *length,
			   const uint8_t *buffer);
int64_t opal_console_read(int64_t term_number, __be64 *length,
			  uint8_t *buffer);
int64_t opal_console_write_buffer_space(int64_t term_number,
					__be64 *length);
int64_t opal_console_flush(int64_t term_number);
int64_t opal_rtc_read(__be32 *year_month_day,
		      __be64 *hour_minute_second_millisecond);
int64_t opal_rtc_write(uint32_t year_month_day,
		       uint64_t hour_minute_second_millisecond);
int64_t opal_tpo_read(uint64_t token, __be32 *year_mon_day, __be32 *hour_min);
int64_t opal_tpo_write(uint64_t token, uint32_t year_mon_day,
		       uint32_t hour_min);
int64_t opal_cec_power_down(uint64_t request);
int64_t opal_cec_reboot(void);
int64_t opal_cec_reboot2(uint32_t reboot_type, char *diag);
int64_t opal_read_nvram(uint64_t buffer, uint64_t size, uint64_t offset);
int64_t opal_write_nvram(uint64_t buffer, uint64_t size, uint64_t offset);
int64_t opal_handle_interrupt(uint64_t isn, __be64 *outstanding_event_mask);
int64_t opal_poll_events(__be64 *outstanding_event_mask);
int64_t opal_pci_set_hub_tce_memory(uint64_t hub_id, uint64_t tce_mem_addr,
				    uint64_t tce_mem_size);
int64_t opal_pci_set_phb_tce_memory(uint64_t phb_id, uint64_t tce_mem_addr,
				    uint64_t tce_mem_size);
int64_t opal_pci_config_read_byte(uint64_t phb_id, uint64_t bus_dev_func,
				  uint64_t offset, uint8_t *data);
int64_t opal_pci_config_read_half_word(uint64_t phb_id, uint64_t bus_dev_func,
				       uint64_t offset, __be16 *data);
int64_t opal_pci_config_read_word(uint64_t phb_id, uint64_t bus_dev_func,
				  uint64_t offset, __be32 *data);
int64_t opal_pci_config_write_byte(uint64_t phb_id, uint64_t bus_dev_func,
				   uint64_t offset, uint8_t data);
int64_t opal_pci_config_write_half_word(uint64_t phb_id, uint64_t bus_dev_func,
					uint64_t offset, uint16_t data);
int64_t opal_pci_config_write_word(uint64_t phb_id, uint64_t bus_dev_func,
				   uint64_t offset, uint32_t data);
int64_t opal_set_xive(uint32_t isn, uint16_t server, uint8_t priority);
int64_t opal_get_xive(uint32_t isn, __be16 *server, uint8_t *priority);
int64_t opal_register_exception_handler(uint64_t opal_exception,
					uint64_t handler_address,
					uint64_t glue_cache_line);
int64_t opal_pci_eeh_freeze_status(uint64_t phb_id, uint64_t pe_number,
				   uint8_t *freeze_state,
				   __be16 *pci_error_type,
				   __be64 *phb_status);
int64_t opal_pci_eeh_freeze_clear(uint64_t phb_id, uint64_t pe_number,
				  uint64_t eeh_action_token);
int64_t opal_pci_eeh_freeze_set(uint64_t phb_id, uint64_t pe_number,
				uint64_t eeh_action_token);
int64_t opal_pci_err_inject(uint64_t phb_id, uint32_t pe_no, uint32_t type,
			    uint32_t func, uint64_t addr, uint64_t mask);
int64_t opal_pci_shpc(uint64_t phb_id, uint64_t shpc_action, uint8_t *state);



int64_t opal_pci_phb_mmio_enable(uint64_t phb_id, uint16_t window_type,
				 uint16_t window_num, uint16_t enable);
int64_t opal_pci_set_phb_mem_window(uint64_t phb_id, uint16_t window_type,
				    uint16_t window_num,
				    uint64_t starting_real_address,
				    uint64_t starting_pci_address,
				    uint64_t size);
int64_t opal_pci_map_pe_mmio_window(uint64_t phb_id, uint16_t pe_number,
				    uint16_t window_type, uint16_t window_num,
				    uint16_t segment_num);
int64_t opal_pci_set_phb_table_memory(uint64_t phb_id, uint64_t rtt_addr,
				      uint64_t ivt_addr, uint64_t ivt_len,
				      uint64_t reject_array_addr,
				      uint64_t peltv_addr);
int64_t opal_pci_set_pe(uint64_t phb_id, uint64_t pe_number, uint64_t bus_dev_func,
			uint8_t bus_compare, uint8_t dev_compare, uint8_t func_compare,
			uint8_t pe_action);
int64_t opal_pci_set_peltv(uint64_t phb_id, uint32_t parent_pe, uint32_t child_pe,
			   uint8_t state);
int64_t opal_pci_set_mve(uint64_t phb_id, uint32_t mve_number, uint32_t pe_number);
int64_t opal_pci_set_mve_enable(uint64_t phb_id, uint32_t mve_number,
				uint32_t state);
int64_t opal_pci_get_xive_reissue(uint64_t phb_id, uint32_t xive_number,
				  uint8_t *p_bit, uint8_t *q_bit);
int64_t opal_pci_set_xive_reissue(uint64_t phb_id, uint32_t xive_number,
				  uint8_t p_bit, uint8_t q_bit);
int64_t opal_pci_msi_eoi(uint64_t phb_id, uint32_t hw_irq);
int64_t opal_pci_set_xive_pe(uint64_t phb_id, uint32_t pe_number,
			     uint32_t xive_num);
int64_t opal_get_xive_source(uint64_t phb_id, uint32_t xive_num,
			     __be32 *interrupt_source_number);
int64_t opal_get_msi_32(uint64_t phb_id, uint32_t mve_number, uint32_t xive_num,
			uint8_t msi_range, __be32 *msi_address,
			__be32 *message_data);
int64_t opal_get_msi_64(uint64_t phb_id, uint32_t mve_number,
			uint32_t xive_num, uint8_t msi_range,
			__be64 *msi_address, __be32 *message_data);
int64_t opal_start_cpu(uint64_t thread_number, uint64_t start_address);
int64_t opal_query_cpu_status(uint64_t thread_number, uint8_t *thread_status);
int64_t opal_write_oppanel(oppanel_line_t *lines, uint64_t num_lines);
int64_t opal_pci_map_pe_dma_window(uint64_t phb_id, uint16_t pe_number, uint16_t window_id,
				   uint16_t tce_levels, uint64_t tce_table_addr,
				   uint64_t tce_table_size, uint64_t tce_page_size);
int64_t opal_pci_map_pe_dma_window_real(uint64_t phb_id, uint16_t pe_number,
					uint16_t dma_window_number, uint64_t pci_start_addr,
					uint64_t pci_mem_size);
int64_t opal_pci_reset(uint64_t id, uint8_t reset_scope, uint8_t assert_state);

int64_t opal_pci_get_hub_diag_data(uint64_t hub_id, void *diag_buffer,
				   uint64_t diag_buffer_len);
int64_t opal_pci_get_phb_diag_data(uint64_t phb_id, void *diag_buffer,
				   uint64_t diag_buffer_len);
int64_t opal_pci_get_phb_diag_data2(uint64_t phb_id, void *diag_buffer,
				    uint64_t diag_buffer_len);
int64_t opal_pci_fence_phb(uint64_t phb_id);
int64_t opal_pci_reinit(uint64_t phb_id, uint64_t reinit_scope, uint64_t data);
int64_t opal_pci_mask_pe_error(uint64_t phb_id, uint16_t pe_number, uint8_t error_type, uint8_t mask_action);
int64_t opal_set_slot_led_status(uint64_t phb_id, uint64_t slot_id, uint8_t led_type, uint8_t led_action);
int64_t opal_get_epow_status(__be16 *epow_status, __be16 *num_epow_classes);
int64_t opal_get_dpo_status(__be64 *dpo_timeout);
int64_t opal_set_system_attention_led(uint8_t led_action);
int64_t opal_pci_next_error(uint64_t phb_id, __be64 *first_frozen_pe,
			    __be16 *pci_error_type, __be16 *severity);
int64_t opal_pci_poll(uint64_t id);
int64_t opal_return_cpu(void);
int64_t opal_check_token(uint64_t token);
int64_t opal_reinit_cpus(uint64_t flags);

int64_t opal_xscom_read(uint32_t gcid, uint64_t pcb_addr, __be64 *val);
int64_t opal_xscom_write(uint32_t gcid, uint64_t pcb_addr, uint64_t val);

int64_t opal_lpc_write(uint32_t chip_id, enum OpalLPCAddressType addr_type,
		       uint32_t addr, uint32_t data, uint32_t sz);
int64_t opal_lpc_read(uint32_t chip_id, enum OpalLPCAddressType addr_type,
		      uint32_t addr, __be32 *data, uint32_t sz);

int64_t opal_read_elog(uint64_t buffer, uint64_t size, uint64_t log_id);
int64_t opal_get_elog_size(__be64 *log_id, __be64 *size, __be64 *elog_type);
int64_t opal_write_elog(uint64_t buffer, uint64_t size, uint64_t offset);
int64_t opal_send_ack_elog(uint64_t log_id);
void opal_resend_pending_logs(void);

int64_t opal_validate_flash(uint64_t buffer, uint32_t *size, uint32_t *result);
int64_t opal_manage_flash(uint8_t op);
int64_t opal_update_flash(uint64_t blk_list);
int64_t opal_dump_init(uint8_t dump_type);
int64_t opal_dump_info(__be32 *dump_id, __be32 *dump_size);
int64_t opal_dump_info2(__be32 *dump_id, __be32 *dump_size, __be32 *dump_type);
int64_t opal_dump_read(uint32_t dump_id, uint64_t buffer);
int64_t opal_dump_ack(uint32_t dump_id);
int64_t opal_dump_resend_notification(void);

int64_t opal_get_msg(uint64_t buffer, uint64_t size);
int64_t opal_write_oppanel_async(uint64_t token, oppanel_line_t *lines,
					uint64_t num_lines);
int64_t opal_check_completion(uint64_t buffer, uint64_t size, uint64_t token);
int64_t opal_sync_host_reboot(void);
int64_t opal_get_param(uint64_t token, uint32_t param_id, uint64_t buffer,
		uint64_t length);
int64_t opal_set_param(uint64_t token, uint32_t param_id, uint64_t buffer,
		uint64_t length);
int64_t opal_sensor_read(uint32_t sensor_hndl, int token, __be32 *sensor_data);
int64_t opal_handle_hmi(void);
int64_t opal_register_dump_region(uint32_t id, uint64_t start, uint64_t end);
int64_t opal_unregister_dump_region(uint32_t id);
int64_t opal_slw_set_reg(uint64_t cpu_pir, uint64_t sprn, uint64_t val);
int64_t opal_config_cpu_idle_state(uint64_t state, uint64_t flag);
int64_t opal_pci_set_phb_cxl_mode(uint64_t phb_id, uint64_t mode, uint64_t pe_number);
int64_t opal_ipmi_send(uint64_t interface, struct opal_ipmi_msg *msg,
		uint64_t msg_len);
int64_t opal_ipmi_recv(uint64_t interface, struct opal_ipmi_msg *msg,
		uint64_t *msg_len);
int64_t opal_i2c_request(uint64_t async_token, uint32_t bus_id,
			 struct opal_i2c_request *oreq);
int64_t opal_prd_msg(struct opal_prd_msg *msg);
int64_t opal_leds_get_ind(char *loc_code, __be64 *led_mask,
			  __be64 *led_value, __be64 *max_led_type);
int64_t opal_leds_set_ind(uint64_t token, char *loc_code, const u64 led_mask,
			  const u64 led_value, __be64 *max_led_type);

int64_t opal_flash_read(uint64_t id, uint64_t offset, uint64_t buf,
		uint64_t size, uint64_t token);
int64_t opal_flash_write(uint64_t id, uint64_t offset, uint64_t buf,
		uint64_t size, uint64_t token);
int64_t opal_flash_erase(uint64_t id, uint64_t offset, uint64_t size,
		uint64_t token);
int64_t opal_get_device_tree(uint32_t phandle, uint64_t buf, uint64_t len);
int64_t opal_pci_get_presence_state(uint64_t id, uint64_t data);
int64_t opal_pci_get_power_state(uint64_t id, uint64_t data);
int64_t opal_pci_set_power_state(uint64_t async_token, uint64_t id,
				 uint64_t data);
int64_t opal_pci_poll2(uint64_t id, uint64_t data);

int64_t opal_int_get_xirr(uint32_t *out_xirr, bool just_poll);
int64_t opal_int_set_cppr(uint8_t cppr);
int64_t opal_int_eoi(uint32_t xirr);
int64_t opal_int_set_mfrr(uint32_t cpu, uint8_t mfrr);
int64_t opal_pci_tce_kill(uint64_t phb_id, uint32_t kill_type,
			  uint32_t pe_num, uint32_t tce_size,
			  uint64_t dma_addr, uint32_t npages);

/* Internal functions */
extern int early_init_dt_scan_opal(unsigned long node, const char *uname,
				   int depth, void *data);
extern int early_init_dt_scan_recoverable_ranges(unsigned long node,
				 const char *uname, int depth, void *data);
extern void opal_configure_cores(void);

extern int opal_get_chars(uint32_t vtermno, char *buf, int count);
extern int opal_put_chars(uint32_t vtermno, const char *buf, int total_len);

extern void hvc_opal_init_early(void);

extern int opal_notifier_register(struct notifier_block *nb);
extern int opal_notifier_unregister(struct notifier_block *nb);

extern int opal_message_notifier_register(enum opal_msg_type msg_type,
						struct notifier_block *nb);
extern int opal_message_notifier_unregister(enum opal_msg_type msg_type,
					    struct notifier_block *nb);
extern void opal_notifier_enable(void);
extern void opal_notifier_disable(void);
extern void opal_notifier_update_evt(uint64_t evt_mask, uint64_t evt_val);

extern int __opal_async_get_token(void);
extern int opal_async_get_token_interruptible(void);
extern int __opal_async_release_token(int token);
extern int opal_async_release_token(int token);
extern int opal_async_wait_response(uint64_t token, struct opal_msg *msg);
extern int opal_get_sensor_data(u32 sensor_hndl, u32 *sensor_data);

struct rtc_time;
extern unsigned long opal_get_boot_time(void);
extern void opal_nvram_init(void);
extern void opal_flash_update_init(void);
extern void opal_flash_term_callback(void);
extern int opal_elog_init(void);
extern void opal_platform_dump_init(void);
extern void opal_sys_param_init(void);
extern void opal_msglog_init(void);
extern void opal_msglog_sysfs_init(void);
extern int opal_async_comp_init(void);
extern int opal_sensor_init(void);
extern int opal_hmi_handler_init(void);
extern int opal_event_init(void);

extern int opal_machine_check(struct pt_regs *regs);
extern bool opal_mce_check_early_recovery(struct pt_regs *regs);
extern int opal_hmi_exception_early(struct pt_regs *regs);
extern int opal_handle_hmi_exception(struct pt_regs *regs);

extern void opal_shutdown(void);
extern int opal_resync_timebase(void);

extern void opal_lpc_init(void);

extern void opal_kmsg_init(void);

extern int opal_event_request(unsigned int opal_event_nr);

struct opal_sg_list *opal_vmalloc_to_sg_list(void *vmalloc_addr,
					     unsigned long vmalloc_size);
void opal_free_sg_list(struct opal_sg_list *sg);

extern int opal_error_code(int rc);

ssize_t opal_msglog_copy(char *to, loff_t pos, size_t count);

static inline int opal_get_async_rc(struct opal_msg msg)
{
	if (msg.msg_type != OPAL_MSG_ASYNC_COMP)
		return OPAL_PARAMETER;
	else
		return be64_to_cpu(msg.params[1]);
}

void opal_wake_poller(void);

#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_OPAL_H */
