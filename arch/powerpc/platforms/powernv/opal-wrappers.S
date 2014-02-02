/*
 * PowerNV OPAL API wrappers
 *
 * Copyright 2011 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <asm/ppc_asm.h>
#include <asm/hvcall.h>
#include <asm/asm-offsets.h>
#include <asm/opal.h>

/* TODO:
 *
 * - Trace irqs in/off (needs saving/restoring all args, argh...)
 * - Get r11 feed up by Dave so I can have better register usage
 */
#define OPAL_CALL(name, token)		\
 _GLOBAL(name);				\
	mflr	r0;			\
	mfcr	r12;			\
	std	r0,16(r1);		\
	stw	r12,8(r1);		\
	std	r1,PACAR1(r13);		\
	li	r0,0;			\
	mfmsr	r12;			\
	ori	r0,r0,MSR_EE;		\
	std	r12,PACASAVEDMSR(r13);	\
	andc	r12,r12,r0;		\
	mtmsrd	r12,1;			\
	LOAD_REG_ADDR(r0,.opal_return);	\
	mtlr	r0;			\
	li	r0,MSR_DR|MSR_IR|MSR_LE;\
	andc	r12,r12,r0;		\
	li	r0,token;		\
	mtspr	SPRN_HSRR1,r12;		\
	LOAD_REG_ADDR(r11,opal);	\
	ld	r12,8(r11);		\
	ld	r2,0(r11);		\
	mtspr	SPRN_HSRR0,r12;		\
	hrfid

_STATIC(opal_return)
	/*
	 * Fixup endian on OPAL return... we should be able to simplify
	 * this by instead converting the below trampoline to a set of
	 * bytes (always BE) since MSR:LE will end up fixed up as a side
	 * effect of the rfid.
	 */
	FIXUP_ENDIAN
	ld	r2,PACATOC(r13);
	lwz	r4,8(r1);
	ld	r5,16(r1);
	ld	r6,PACASAVEDMSR(r13);
	mtspr	SPRN_SRR0,r5;
	mtspr	SPRN_SRR1,r6;
	mtcr	r4;
	rfid

OPAL_CALL(opal_console_write,			OPAL_CONSOLE_WRITE);
OPAL_CALL(opal_console_read,			OPAL_CONSOLE_READ);
OPAL_CALL(opal_console_write_buffer_space,	OPAL_CONSOLE_WRITE_BUFFER_SPACE);
OPAL_CALL(opal_rtc_read,			OPAL_RTC_READ);
OPAL_CALL(opal_rtc_write,			OPAL_RTC_WRITE);
OPAL_CALL(opal_cec_power_down,			OPAL_CEC_POWER_DOWN);
OPAL_CALL(opal_cec_reboot,			OPAL_CEC_REBOOT);
OPAL_CALL(opal_read_nvram,			OPAL_READ_NVRAM);
OPAL_CALL(opal_write_nvram,			OPAL_WRITE_NVRAM);
OPAL_CALL(opal_handle_interrupt,		OPAL_HANDLE_INTERRUPT);
OPAL_CALL(opal_poll_events,			OPAL_POLL_EVENTS);
OPAL_CALL(opal_pci_set_hub_tce_memory,		OPAL_PCI_SET_HUB_TCE_MEMORY);
OPAL_CALL(opal_pci_set_phb_tce_memory,		OPAL_PCI_SET_PHB_TCE_MEMORY);
OPAL_CALL(opal_pci_config_read_byte,		OPAL_PCI_CONFIG_READ_BYTE);
OPAL_CALL(opal_pci_config_read_half_word,	OPAL_PCI_CONFIG_READ_HALF_WORD);
OPAL_CALL(opal_pci_config_read_word,		OPAL_PCI_CONFIG_READ_WORD);
OPAL_CALL(opal_pci_config_write_byte,		OPAL_PCI_CONFIG_WRITE_BYTE);
OPAL_CALL(opal_pci_config_write_half_word,	OPAL_PCI_CONFIG_WRITE_HALF_WORD);
OPAL_CALL(opal_pci_config_write_word,		OPAL_PCI_CONFIG_WRITE_WORD);
OPAL_CALL(opal_set_xive,			OPAL_SET_XIVE);
OPAL_CALL(opal_get_xive,			OPAL_GET_XIVE);
OPAL_CALL(opal_register_exception_handler,	OPAL_REGISTER_OPAL_EXCEPTION_HANDLER);
OPAL_CALL(opal_pci_eeh_freeze_status,		OPAL_PCI_EEH_FREEZE_STATUS);
OPAL_CALL(opal_pci_eeh_freeze_clear,		OPAL_PCI_EEH_FREEZE_CLEAR);
OPAL_CALL(opal_pci_shpc,			OPAL_PCI_SHPC);
OPAL_CALL(opal_pci_phb_mmio_enable,		OPAL_PCI_PHB_MMIO_ENABLE);
OPAL_CALL(opal_pci_set_phb_mem_window,		OPAL_PCI_SET_PHB_MEM_WINDOW);
OPAL_CALL(opal_pci_map_pe_mmio_window,		OPAL_PCI_MAP_PE_MMIO_WINDOW);
OPAL_CALL(opal_pci_set_phb_table_memory,	OPAL_PCI_SET_PHB_TABLE_MEMORY);
OPAL_CALL(opal_pci_set_pe,			OPAL_PCI_SET_PE);
OPAL_CALL(opal_pci_set_peltv,			OPAL_PCI_SET_PELTV);
OPAL_CALL(opal_pci_set_mve,			OPAL_PCI_SET_MVE);
OPAL_CALL(opal_pci_set_mve_enable,		OPAL_PCI_SET_MVE_ENABLE);
OPAL_CALL(opal_pci_get_xive_reissue,		OPAL_PCI_GET_XIVE_REISSUE);
OPAL_CALL(opal_pci_set_xive_reissue,		OPAL_PCI_SET_XIVE_REISSUE);
OPAL_CALL(opal_pci_set_xive_pe,			OPAL_PCI_SET_XIVE_PE);
OPAL_CALL(opal_get_xive_source,			OPAL_GET_XIVE_SOURCE);
OPAL_CALL(opal_get_msi_32,			OPAL_GET_MSI_32);
OPAL_CALL(opal_get_msi_64,			OPAL_GET_MSI_64);
OPAL_CALL(opal_start_cpu,			OPAL_START_CPU);
OPAL_CALL(opal_query_cpu_status,		OPAL_QUERY_CPU_STATUS);
OPAL_CALL(opal_write_oppanel,			OPAL_WRITE_OPPANEL);
OPAL_CALL(opal_pci_map_pe_dma_window,		OPAL_PCI_MAP_PE_DMA_WINDOW);
OPAL_CALL(opal_pci_map_pe_dma_window_real,	OPAL_PCI_MAP_PE_DMA_WINDOW_REAL);
OPAL_CALL(opal_pci_reset,			OPAL_PCI_RESET);
OPAL_CALL(opal_pci_get_hub_diag_data,		OPAL_PCI_GET_HUB_DIAG_DATA);
OPAL_CALL(opal_pci_get_phb_diag_data,		OPAL_PCI_GET_PHB_DIAG_DATA);
OPAL_CALL(opal_pci_fence_phb,			OPAL_PCI_FENCE_PHB);
OPAL_CALL(opal_pci_reinit,			OPAL_PCI_REINIT);
OPAL_CALL(opal_pci_mask_pe_error,		OPAL_PCI_MASK_PE_ERROR);
OPAL_CALL(opal_set_slot_led_status,		OPAL_SET_SLOT_LED_STATUS);
OPAL_CALL(opal_get_epow_status,			OPAL_GET_EPOW_STATUS);
OPAL_CALL(opal_set_system_attention_led,	OPAL_SET_SYSTEM_ATTENTION_LED);
OPAL_CALL(opal_pci_next_error,			OPAL_PCI_NEXT_ERROR);
OPAL_CALL(opal_pci_poll,			OPAL_PCI_POLL);
OPAL_CALL(opal_pci_msi_eoi,			OPAL_PCI_MSI_EOI);
OPAL_CALL(opal_pci_get_phb_diag_data2,		OPAL_PCI_GET_PHB_DIAG_DATA2);
OPAL_CALL(opal_xscom_read,			OPAL_XSCOM_READ);
OPAL_CALL(opal_xscom_write,			OPAL_XSCOM_WRITE);
OPAL_CALL(opal_lpc_read,			OPAL_LPC_READ);
OPAL_CALL(opal_lpc_write,			OPAL_LPC_WRITE);
OPAL_CALL(opal_return_cpu,			OPAL_RETURN_CPU);
OPAL_CALL(opal_validate_flash,			OPAL_FLASH_VALIDATE);
OPAL_CALL(opal_manage_flash,			OPAL_FLASH_MANAGE);
OPAL_CALL(opal_update_flash,			OPAL_FLASH_UPDATE);
OPAL_CALL(opal_get_msg,				OPAL_GET_MSG);
OPAL_CALL(opal_check_completion,		OPAL_CHECK_ASYNC_COMPLETION);
OPAL_CALL(opal_sync_host_reboot,		OPAL_SYNC_HOST_REBOOT);
