/*
 * Beat hypervisor call numbers
 *
 * (C) Copyright 2004-2007 TOSHIBA CORPORATION
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef BEAT_BEAT_syscall_H
#define BEAT_BEAT_syscall_H

#ifdef	__ASSEMBLY__
#define	__BEAT_ADD_VENDOR_ID(__x, __v)	((__v)<<60|(__x))
#else
#define	__BEAT_ADD_VENDOR_ID(__x, __v)	((u64)(__v)<<60|(__x))
#endif
#define HV_allocate_memory __BEAT_ADD_VENDOR_ID(0, 0)
#define HV_construct_virtual_address_space __BEAT_ADD_VENDOR_ID(2, 0)
#define HV_destruct_virtual_address_space __BEAT_ADD_VENDOR_ID(10, 0)
#define HV_get_virtual_address_space_id_of_ppe __BEAT_ADD_VENDOR_ID(4, 0)
#define HV_query_logical_partition_address_region_info 			\
						__BEAT_ADD_VENDOR_ID(6, 0)
#define HV_release_memory __BEAT_ADD_VENDOR_ID(13, 0)
#define HV_select_virtual_address_space __BEAT_ADD_VENDOR_ID(7, 0)
#define HV_load_range_registers __BEAT_ADD_VENDOR_ID(68, 0)
#define HV_set_ppe_l2cache_rmt_entry __BEAT_ADD_VENDOR_ID(70, 0)
#define HV_set_ppe_tlb_rmt_entry __BEAT_ADD_VENDOR_ID(71, 0)
#define HV_set_spe_tlb_rmt_entry __BEAT_ADD_VENDOR_ID(72, 0)
#define HV_get_io_address_translation_fault_info __BEAT_ADD_VENDOR_ID(14, 0)
#define HV_get_iopte __BEAT_ADD_VENDOR_ID(16, 0)
#define HV_preload_iopt_cache __BEAT_ADD_VENDOR_ID(17, 0)
#define HV_put_iopte __BEAT_ADD_VENDOR_ID(15, 0)
#define HV_connect_event_ports __BEAT_ADD_VENDOR_ID(21, 0)
#define HV_construct_event_receive_port __BEAT_ADD_VENDOR_ID(18, 0)
#define HV_destruct_event_receive_port __BEAT_ADD_VENDOR_ID(19, 0)
#define HV_destruct_event_send_port __BEAT_ADD_VENDOR_ID(22, 0)
#define HV_get_state_of_event_send_port __BEAT_ADD_VENDOR_ID(25, 0)
#define HV_request_to_connect_event_ports __BEAT_ADD_VENDOR_ID(20, 0)
#define HV_send_event_externally __BEAT_ADD_VENDOR_ID(23, 0)
#define HV_send_event_locally __BEAT_ADD_VENDOR_ID(24, 0)
#define HV_construct_and_connect_irq_plug __BEAT_ADD_VENDOR_ID(28, 0)
#define HV_destruct_irq_plug __BEAT_ADD_VENDOR_ID(29, 0)
#define HV_detect_pending_interrupts __BEAT_ADD_VENDOR_ID(26, 0)
#define HV_end_of_interrupt __BEAT_ADD_VENDOR_ID(27, 0)
#define HV_assign_control_signal_notification_port __BEAT_ADD_VENDOR_ID(45, 0)
#define HV_end_of_control_signal_processing __BEAT_ADD_VENDOR_ID(48, 0)
#define HV_get_control_signal __BEAT_ADD_VENDOR_ID(46, 0)
#define HV_set_irq_mask_for_spe __BEAT_ADD_VENDOR_ID(61, 0)
#define HV_shutdown_logical_partition __BEAT_ADD_VENDOR_ID(44, 0)
#define HV_connect_message_ports __BEAT_ADD_VENDOR_ID(35, 0)
#define HV_destruct_message_port __BEAT_ADD_VENDOR_ID(36, 0)
#define HV_receive_message __BEAT_ADD_VENDOR_ID(37, 0)
#define HV_get_message_port_info __BEAT_ADD_VENDOR_ID(34, 0)
#define HV_request_to_connect_message_ports __BEAT_ADD_VENDOR_ID(33, 0)
#define HV_send_message __BEAT_ADD_VENDOR_ID(32, 0)
#define HV_get_logical_ppe_id __BEAT_ADD_VENDOR_ID(69, 0)
#define HV_pause __BEAT_ADD_VENDOR_ID(9, 0)
#define HV_destruct_shared_memory_handle __BEAT_ADD_VENDOR_ID(51, 0)
#define HV_get_shared_memory_info __BEAT_ADD_VENDOR_ID(52, 0)
#define HV_permit_sharing_memory __BEAT_ADD_VENDOR_ID(50, 0)
#define HV_request_to_attach_shared_memory __BEAT_ADD_VENDOR_ID(49, 0)
#define HV_enable_logical_spe_execution __BEAT_ADD_VENDOR_ID(55, 0)
#define HV_construct_logical_spe __BEAT_ADD_VENDOR_ID(53, 0)
#define HV_disable_logical_spe_execution __BEAT_ADD_VENDOR_ID(56, 0)
#define HV_destruct_logical_spe __BEAT_ADD_VENDOR_ID(54, 0)
#define HV_sense_spe_execution_status __BEAT_ADD_VENDOR_ID(58, 0)
#define HV_insert_htab_entry __BEAT_ADD_VENDOR_ID(101, 0)
#define HV_read_htab_entries __BEAT_ADD_VENDOR_ID(95, 0)
#define HV_write_htab_entry __BEAT_ADD_VENDOR_ID(94, 0)
#define HV_assign_io_address_translation_fault_port 			\
						__BEAT_ADD_VENDOR_ID(100, 0)
#define HV_set_interrupt_mask __BEAT_ADD_VENDOR_ID(73, 0)
#define HV_get_logical_partition_id __BEAT_ADD_VENDOR_ID(74, 0)
#define HV_create_repository_node2 __BEAT_ADD_VENDOR_ID(90, 0)
#define HV_create_repository_node __BEAT_ADD_VENDOR_ID(90, 0) /* alias */
#define HV_get_repository_node_value2 __BEAT_ADD_VENDOR_ID(91, 0)
#define HV_get_repository_node_value __BEAT_ADD_VENDOR_ID(91, 0) /* alias */
#define HV_modify_repository_node_value2 __BEAT_ADD_VENDOR_ID(92, 0)
#define HV_modify_repository_node_value __BEAT_ADD_VENDOR_ID(92, 0) /* alias */
#define HV_remove_repository_node2 __BEAT_ADD_VENDOR_ID(93, 0)
#define HV_remove_repository_node __BEAT_ADD_VENDOR_ID(93, 0) /* alias */
#define HV_cancel_shared_memory __BEAT_ADD_VENDOR_ID(104, 0)
#define HV_clear_interrupt_status_of_spe __BEAT_ADD_VENDOR_ID(206, 0)
#define HV_construct_spe_irq_outlet __BEAT_ADD_VENDOR_ID(80, 0)
#define HV_destruct_spe_irq_outlet __BEAT_ADD_VENDOR_ID(81, 0)
#define HV_disconnect_ipspc_service __BEAT_ADD_VENDOR_ID(88, 0)
#define HV_execute_ipspc_command __BEAT_ADD_VENDOR_ID(86, 0)
#define HV_get_interrupt_status_of_spe __BEAT_ADD_VENDOR_ID(205, 0)
#define HV_get_spe_privileged_state_1_registers __BEAT_ADD_VENDOR_ID(208, 0)
#define HV_permit_use_of_ipspc_service __BEAT_ADD_VENDOR_ID(85, 0)
#define HV_reinitialize_logical_spe __BEAT_ADD_VENDOR_ID(82, 0)
#define HV_request_ipspc_service __BEAT_ADD_VENDOR_ID(84, 0)
#define HV_stop_ipspc_command __BEAT_ADD_VENDOR_ID(87, 0)
#define HV_set_spe_privileged_state_1_registers __BEAT_ADD_VENDOR_ID(204, 0)
#define HV_get_status_of_ipspc_service __BEAT_ADD_VENDOR_ID(203, 0)
#define HV_put_characters_to_console __BEAT_ADD_VENDOR_ID(0x101, 1)
#define HV_get_characters_from_console __BEAT_ADD_VENDOR_ID(0x102, 1)
#define HV_get_base_clock __BEAT_ADD_VENDOR_ID(0x111, 1)
#define HV_set_base_clock __BEAT_ADD_VENDOR_ID(0x112, 1)
#define HV_get_frame_cycle __BEAT_ADD_VENDOR_ID(0x114, 1)
#define HV_disable_console __BEAT_ADD_VENDOR_ID(0x115, 1)
#define HV_disable_all_console __BEAT_ADD_VENDOR_ID(0x116, 1)
#define HV_oneshot_timer __BEAT_ADD_VENDOR_ID(0x117, 1)
#define HV_set_dabr __BEAT_ADD_VENDOR_ID(0x118, 1)
#define HV_get_dabr __BEAT_ADD_VENDOR_ID(0x119, 1)
#define HV_start_hv_stats __BEAT_ADD_VENDOR_ID(0x21c, 1)
#define HV_stop_hv_stats __BEAT_ADD_VENDOR_ID(0x21d, 1)
#define HV_get_hv_stats __BEAT_ADD_VENDOR_ID(0x21e, 1)
#define HV_get_hv_error_stats __BEAT_ADD_VENDOR_ID(0x221, 1)
#define HV_get_stats __BEAT_ADD_VENDOR_ID(0x224, 1)
#define HV_get_heap_stats __BEAT_ADD_VENDOR_ID(0x225, 1)
#define HV_get_memory_stats __BEAT_ADD_VENDOR_ID(0x227, 1)
#define HV_get_memory_detail __BEAT_ADD_VENDOR_ID(0x228, 1)
#define HV_set_priority_of_irq_outlet __BEAT_ADD_VENDOR_ID(0x122, 1)
#define HV_get_physical_spe_by_reservation_id __BEAT_ADD_VENDOR_ID(0x128, 1)
#define HV_get_spe_context __BEAT_ADD_VENDOR_ID(0x129, 1)
#define HV_set_spe_context __BEAT_ADD_VENDOR_ID(0x12a, 1)
#define HV_downcount_of_interrupt __BEAT_ADD_VENDOR_ID(0x12e, 1)
#define HV_peek_spe_context __BEAT_ADD_VENDOR_ID(0x12f, 1)
#define HV_read_bpa_register __BEAT_ADD_VENDOR_ID(0x131, 1)
#define HV_write_bpa_register __BEAT_ADD_VENDOR_ID(0x132, 1)
#define HV_map_context_table_of_spe __BEAT_ADD_VENDOR_ID(0x137, 1)
#define HV_get_slb_for_logical_spe __BEAT_ADD_VENDOR_ID(0x138, 1)
#define HV_set_slb_for_logical_spe __BEAT_ADD_VENDOR_ID(0x139, 1)
#define HV_init_pm __BEAT_ADD_VENDOR_ID(0x150, 1)
#define HV_set_pm_signal __BEAT_ADD_VENDOR_ID(0x151, 1)
#define HV_get_pm_signal __BEAT_ADD_VENDOR_ID(0x152, 1)
#define HV_set_pm_config __BEAT_ADD_VENDOR_ID(0x153, 1)
#define HV_get_pm_config __BEAT_ADD_VENDOR_ID(0x154, 1)
#define HV_get_inner_trace_data __BEAT_ADD_VENDOR_ID(0x155, 1)
#define HV_set_ext_trace_buffer __BEAT_ADD_VENDOR_ID(0x156, 1)
#define HV_get_ext_trace_buffer __BEAT_ADD_VENDOR_ID(0x157, 1)
#define HV_set_pm_interrupt __BEAT_ADD_VENDOR_ID(0x158, 1)
#define HV_get_pm_interrupt __BEAT_ADD_VENDOR_ID(0x159, 1)
#define HV_kick_pm __BEAT_ADD_VENDOR_ID(0x160, 1)
#define HV_construct_pm_context __BEAT_ADD_VENDOR_ID(0x164, 1)
#define HV_destruct_pm_context __BEAT_ADD_VENDOR_ID(0x165, 1)
#define HV_be_slow __BEAT_ADD_VENDOR_ID(0x170, 1)
#define HV_assign_ipspc_server_connection_status_notification_port 	\
						__BEAT_ADD_VENDOR_ID(0x173, 1)
#define HV_get_raid_of_physical_spe __BEAT_ADD_VENDOR_ID(0x174, 1)
#define HV_set_physical_spe_to_rag __BEAT_ADD_VENDOR_ID(0x175, 1)
#define HV_release_physical_spe_from_rag __BEAT_ADD_VENDOR_ID(0x176, 1)
#define HV_rtc_read __BEAT_ADD_VENDOR_ID(0x190, 1)
#define HV_rtc_write __BEAT_ADD_VENDOR_ID(0x191, 1)
#define HV_eeprom_read __BEAT_ADD_VENDOR_ID(0x192, 1)
#define HV_eeprom_write __BEAT_ADD_VENDOR_ID(0x193, 1)
#define HV_insert_htab_entry3 __BEAT_ADD_VENDOR_ID(0x104, 1)
#define HV_invalidate_htab_entry3 __BEAT_ADD_VENDOR_ID(0x105, 1)
#define HV_update_htab_permission3 __BEAT_ADD_VENDOR_ID(0x106, 1)
#define HV_clear_htab3 __BEAT_ADD_VENDOR_ID(0x107, 1)
#endif
