/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/******************************************************************************
 *
 * Name: achware.h -- hardware specific interfaces
 *
 * Copyright (C) 2000 - 2020, Intel Corp.
 *
 *****************************************************************************/

#ifndef __ACHWARE_H__
#define __ACHWARE_H__

/* Values for the _SST predefined method */

#define ACPI_SST_INDICATOR_OFF  0
#define ACPI_SST_WORKING        1
#define ACPI_SST_WAKING         2
#define ACPI_SST_SLEEPING       3
#define ACPI_SST_SLEEP_CONTEXT  4

/*
 * hwacpi - high level functions
 */
acpi_status acpi_hw_set_mode(u32 mode);

u32 acpi_hw_get_mode(void);

/*
 * hwregs - ACPI Register I/O
 */
acpi_status
acpi_hw_validate_register(struct acpi_generic_address *reg,
			  u8 max_bit_width, u64 *address);

acpi_status acpi_hw_read(u64 *value, struct acpi_generic_address *reg);

acpi_status acpi_hw_write(u64 value, struct acpi_generic_address *reg);

struct acpi_bit_register_info *acpi_hw_get_bit_register_info(u32 register_id);

acpi_status acpi_hw_write_pm1_control(u32 pm1a_control, u32 pm1b_control);

acpi_status acpi_hw_register_read(u32 register_id, u32 *return_value);

acpi_status acpi_hw_register_write(u32 register_id, u32 value);

acpi_status acpi_hw_clear_acpi_status(void);

/*
 * hwsleep - sleep/wake support (Legacy sleep registers)
 */
acpi_status acpi_hw_legacy_sleep(u8 sleep_state);

acpi_status acpi_hw_legacy_wake_prep(u8 sleep_state);

acpi_status acpi_hw_legacy_wake(u8 sleep_state);

/*
 * hwesleep - sleep/wake support (Extended FADT-V5 sleep registers)
 */
void acpi_hw_execute_sleep_method(char *method_name, u32 integer_argument);

acpi_status acpi_hw_extended_sleep(u8 sleep_state);

acpi_status acpi_hw_extended_wake_prep(u8 sleep_state);

acpi_status acpi_hw_extended_wake(u8 sleep_state);

/*
 * hwvalid - Port I/O with validation
 */
acpi_status acpi_hw_read_port(acpi_io_address address, u32 *value, u32 width);

acpi_status acpi_hw_write_port(acpi_io_address address, u32 value, u32 width);

acpi_status acpi_hw_validate_io_block(u64 address, u32 bit_width, u32 count);

/*
 * hwgpe - GPE support
 */
acpi_status acpi_hw_gpe_read(u64 *value, struct acpi_gpe_address *reg);

acpi_status acpi_hw_gpe_write(u64 value, struct acpi_gpe_address *reg);

u32 acpi_hw_get_gpe_register_bit(struct acpi_gpe_event_info *gpe_event_info);

acpi_status
acpi_hw_low_set_gpe(struct acpi_gpe_event_info *gpe_event_info, u32 action);

acpi_status
acpi_hw_disable_gpe_block(struct acpi_gpe_xrupt_info *gpe_xrupt_info,
			  struct acpi_gpe_block_info *gpe_block, void *context);

acpi_status acpi_hw_clear_gpe(struct acpi_gpe_event_info *gpe_event_info);

acpi_status
acpi_hw_clear_gpe_block(struct acpi_gpe_xrupt_info *gpe_xrupt_info,
			struct acpi_gpe_block_info *gpe_block, void *context);

acpi_status
acpi_hw_get_gpe_status(struct acpi_gpe_event_info *gpe_event_info,
		       acpi_event_status *event_status);

acpi_status acpi_hw_disable_all_gpes(void);

acpi_status acpi_hw_enable_all_runtime_gpes(void);

acpi_status acpi_hw_enable_all_wakeup_gpes(void);

u8 acpi_hw_check_all_gpes(acpi_handle gpe_skip_device, u32 gpe_skip_number);

acpi_status
acpi_hw_enable_runtime_gpe_block(struct acpi_gpe_xrupt_info *gpe_xrupt_info,
				 struct acpi_gpe_block_info *gpe_block,
				 void *context);

#ifdef ACPI_PCI_CONFIGURED
/*
 * hwpci - PCI configuration support
 */
acpi_status
acpi_hw_derive_pci_id(struct acpi_pci_id *pci_id,
		      acpi_handle root_pci_device, acpi_handle pci_region);
#else
static inline acpi_status
acpi_hw_derive_pci_id(struct acpi_pci_id *pci_id, acpi_handle root_pci_device,
		      acpi_handle pci_region)
{
	return AE_SUPPORT;
}
#endif

#endif				/* __ACHWARE_H__ */
