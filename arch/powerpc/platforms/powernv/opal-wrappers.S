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

#include <linux/jump_label.h>
#include <asm/ppc_asm.h>
#include <asm/hvcall.h>
#include <asm/asm-offsets.h>
#include <asm/opal.h>

	.section	".text"

#ifdef CONFIG_TRACEPOINTS
#ifdef HAVE_JUMP_LABEL
#define OPAL_BRANCH(LABEL)					\
	ARCH_STATIC_BRANCH(LABEL, opal_tracepoint_key)
#else

	.section	".toc","aw"

	.globl opal_tracepoint_refcount
opal_tracepoint_refcount:
	.8byte	0

	.section	".text"

/*
 * We branch around this in early init by using an unconditional cpu
 * feature.
 */
#define OPAL_BRANCH(LABEL)					\
BEGIN_FTR_SECTION;						\
	b	1f;						\
END_FTR_SECTION(0, 1);						\
	ld	r11,opal_tracepoint_refcount@toc(r2);		\
	cmpdi	r11,0;						\
	bne-	LABEL;						\
1:

#endif

#else
#define OPAL_BRANCH(LABEL)
#endif

/*
 * DO_OPAL_CALL assumes:
 * r0  = opal call token
 * r12 = msr
 * LR has been saved
 */
#define DO_OPAL_CALL()			\
	mfcr	r11;			\
	stw	r11,8(r1);		\
	li	r11,0;			\
	ori	r11,r11,MSR_EE;		\
	std	r12,PACASAVEDMSR(r13);	\
	andc	r12,r12,r11;		\
	mtmsrd	r12,1;			\
	LOAD_REG_ADDR(r11,opal_return);	\
	mtlr	r11;			\
	li	r11,MSR_DR|MSR_IR|MSR_LE;\
	andc	r12,r12,r11;		\
	mtspr	SPRN_HSRR1,r12;		\
	LOAD_REG_ADDR(r11,opal);	\
	ld	r12,8(r11);		\
	ld	r2,0(r11);		\
	mtspr	SPRN_HSRR0,r12;		\
	hrfid

#define OPAL_CALL(name, token)		\
 _GLOBAL_TOC(name);			\
	mfmsr	r12;			\
	mflr	r0;			\
	andi.	r11,r12,MSR_IR|MSR_DR; 	\
	std	r0,PPC_LR_STKOFF(r1);	\
	li	r0,token;		\
	beq	opal_real_call;         \
	OPAL_BRANCH(opal_tracepoint_entry) \
	DO_OPAL_CALL()


opal_return:
	/*
	 * Fixup endian on OPAL return... we should be able to simplify
	 * this by instead converting the below trampoline to a set of
	 * bytes (always BE) since MSR:LE will end up fixed up as a side
	 * effect of the rfid.
	 */
	FIXUP_ENDIAN_HV
	ld	r2,PACATOC(r13);
	lwz	r4,8(r1);
	ld	r5,PPC_LR_STKOFF(r1);
	ld	r6,PACASAVEDMSR(r13);
	mtcr	r4;
	mtspr	SPRN_HSRR0,r5;
	mtspr	SPRN_HSRR1,r6;
	hrfid

opal_real_call:
	mfcr	r11
	stw	r11,8(r1)
	/* Set opal return address */
	LOAD_REG_ADDR(r11, opal_return_realmode)
	mtlr	r11
	li	r11,MSR_LE
	andc	r12,r12,r11
	mtspr	SPRN_HSRR1,r12
	LOAD_REG_ADDR(r11,opal)
	ld	r12,8(r11)
	ld	r2,0(r11)
	mtspr	SPRN_HSRR0,r12
	hrfid

opal_return_realmode:
	FIXUP_ENDIAN_HV
	ld	r2,PACATOC(r13);
	lwz	r11,8(r1);
	ld	r12,PPC_LR_STKOFF(r1)
	mtcr	r11;
	mtlr	r12
	blr

#ifdef CONFIG_TRACEPOINTS
opal_tracepoint_entry:
	stdu	r1,-STACKFRAMESIZE(r1)
	std	r0,STK_REG(R23)(r1)
	std	r3,STK_REG(R24)(r1)
	std	r4,STK_REG(R25)(r1)
	std	r5,STK_REG(R26)(r1)
	std	r6,STK_REG(R27)(r1)
	std	r7,STK_REG(R28)(r1)
	std	r8,STK_REG(R29)(r1)
	std	r9,STK_REG(R30)(r1)
	std	r10,STK_REG(R31)(r1)
	mr	r3,r0
	addi	r4,r1,STK_REG(R24)
	bl	__trace_opal_entry
	ld	r0,STK_REG(R23)(r1)
	ld	r3,STK_REG(R24)(r1)
	ld	r4,STK_REG(R25)(r1)
	ld	r5,STK_REG(R26)(r1)
	ld	r6,STK_REG(R27)(r1)
	ld	r7,STK_REG(R28)(r1)
	ld	r8,STK_REG(R29)(r1)
	ld	r9,STK_REG(R30)(r1)
	ld	r10,STK_REG(R31)(r1)

	/* setup LR so we return via tracepoint_return */
	LOAD_REG_ADDR(r11,opal_tracepoint_return)
	std	r11,16(r1)

	mfmsr	r12
	DO_OPAL_CALL()

opal_tracepoint_return:
	std	r3,STK_REG(R31)(r1)
	mr	r4,r3
	ld	r3,STK_REG(R23)(r1)
	bl	__trace_opal_exit
	ld	r3,STK_REG(R31)(r1)
	addi	r1,r1,STACKFRAMESIZE
	ld	r0,16(r1)
	mtlr	r0
	blr
#endif


OPAL_CALL(opal_invalid_call,			OPAL_INVALID_CALL);
OPAL_CALL(opal_console_write,			OPAL_CONSOLE_WRITE);
OPAL_CALL(opal_console_read,			OPAL_CONSOLE_READ);
OPAL_CALL(opal_console_write_buffer_space,	OPAL_CONSOLE_WRITE_BUFFER_SPACE);
OPAL_CALL(opal_rtc_read,			OPAL_RTC_READ);
OPAL_CALL(opal_rtc_write,			OPAL_RTC_WRITE);
OPAL_CALL(opal_cec_power_down,			OPAL_CEC_POWER_DOWN);
OPAL_CALL(opal_cec_reboot,			OPAL_CEC_REBOOT);
OPAL_CALL(opal_cec_reboot2,			OPAL_CEC_REBOOT2);
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
OPAL_CALL(opal_pci_eeh_freeze_set,		OPAL_PCI_EEH_FREEZE_SET);
OPAL_CALL(opal_pci_err_inject,			OPAL_PCI_ERR_INJECT);
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
OPAL_CALL(opal_get_dpo_status,			OPAL_GET_DPO_STATUS);
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
OPAL_CALL(opal_reinit_cpus,			OPAL_REINIT_CPUS);
OPAL_CALL(opal_read_elog,			OPAL_ELOG_READ);
OPAL_CALL(opal_send_ack_elog,			OPAL_ELOG_ACK);
OPAL_CALL(opal_get_elog_size,			OPAL_ELOG_SIZE);
OPAL_CALL(opal_resend_pending_logs,		OPAL_ELOG_RESEND);
OPAL_CALL(opal_write_elog,			OPAL_ELOG_WRITE);
OPAL_CALL(opal_validate_flash,			OPAL_FLASH_VALIDATE);
OPAL_CALL(opal_manage_flash,			OPAL_FLASH_MANAGE);
OPAL_CALL(opal_update_flash,			OPAL_FLASH_UPDATE);
OPAL_CALL(opal_resync_timebase,			OPAL_RESYNC_TIMEBASE);
OPAL_CALL(opal_check_token,			OPAL_CHECK_TOKEN);
OPAL_CALL(opal_dump_init,			OPAL_DUMP_INIT);
OPAL_CALL(opal_dump_info,			OPAL_DUMP_INFO);
OPAL_CALL(opal_dump_info2,			OPAL_DUMP_INFO2);
OPAL_CALL(opal_dump_read,			OPAL_DUMP_READ);
OPAL_CALL(opal_dump_ack,			OPAL_DUMP_ACK);
OPAL_CALL(opal_get_msg,				OPAL_GET_MSG);
OPAL_CALL(opal_write_oppanel_async,		OPAL_WRITE_OPPANEL_ASYNC);
OPAL_CALL(opal_check_completion,		OPAL_CHECK_ASYNC_COMPLETION);
OPAL_CALL(opal_dump_resend_notification,	OPAL_DUMP_RESEND);
OPAL_CALL(opal_sync_host_reboot,		OPAL_SYNC_HOST_REBOOT);
OPAL_CALL(opal_sensor_read,			OPAL_SENSOR_READ);
OPAL_CALL(opal_get_param,			OPAL_GET_PARAM);
OPAL_CALL(opal_set_param,			OPAL_SET_PARAM);
OPAL_CALL(opal_handle_hmi,			OPAL_HANDLE_HMI);
OPAL_CALL(opal_config_cpu_idle_state,		OPAL_CONFIG_CPU_IDLE_STATE);
OPAL_CALL(opal_slw_set_reg,			OPAL_SLW_SET_REG);
OPAL_CALL(opal_register_dump_region,		OPAL_REGISTER_DUMP_REGION);
OPAL_CALL(opal_unregister_dump_region,		OPAL_UNREGISTER_DUMP_REGION);
OPAL_CALL(opal_pci_set_phb_cxl_mode,		OPAL_PCI_SET_PHB_CAPI_MODE);
OPAL_CALL(opal_tpo_write,			OPAL_WRITE_TPO);
OPAL_CALL(opal_tpo_read,			OPAL_READ_TPO);
OPAL_CALL(opal_ipmi_send,			OPAL_IPMI_SEND);
OPAL_CALL(opal_ipmi_recv,			OPAL_IPMI_RECV);
OPAL_CALL(opal_i2c_request,			OPAL_I2C_REQUEST);
OPAL_CALL(opal_flash_read,			OPAL_FLASH_READ);
OPAL_CALL(opal_flash_write,			OPAL_FLASH_WRITE);
OPAL_CALL(opal_flash_erase,			OPAL_FLASH_ERASE);
OPAL_CALL(opal_prd_msg,				OPAL_PRD_MSG);
OPAL_CALL(opal_leds_get_ind,			OPAL_LEDS_GET_INDICATOR);
OPAL_CALL(opal_leds_set_ind,			OPAL_LEDS_SET_INDICATOR);
OPAL_CALL(opal_console_flush,			OPAL_CONSOLE_FLUSH);
OPAL_CALL(opal_get_device_tree,			OPAL_GET_DEVICE_TREE);
OPAL_CALL(opal_pci_get_presence_state,		OPAL_PCI_GET_PRESENCE_STATE);
OPAL_CALL(opal_pci_get_power_state,		OPAL_PCI_GET_POWER_STATE);
OPAL_CALL(opal_pci_set_power_state,		OPAL_PCI_SET_POWER_STATE);
OPAL_CALL(opal_int_get_xirr,			OPAL_INT_GET_XIRR);
OPAL_CALL(opal_int_set_cppr,			OPAL_INT_SET_CPPR);
OPAL_CALL(opal_int_eoi,				OPAL_INT_EOI);
OPAL_CALL(opal_int_set_mfrr,			OPAL_INT_SET_MFRR);
OPAL_CALL(opal_pci_tce_kill,			OPAL_PCI_TCE_KILL);
OPAL_CALL(opal_nmmu_set_ptcr,			OPAL_NMMU_SET_PTCR);
OPAL_CALL(opal_xive_reset,			OPAL_XIVE_RESET);
OPAL_CALL(opal_xive_get_irq_info,		OPAL_XIVE_GET_IRQ_INFO);
OPAL_CALL(opal_xive_get_irq_config,		OPAL_XIVE_GET_IRQ_CONFIG);
OPAL_CALL(opal_xive_set_irq_config,		OPAL_XIVE_SET_IRQ_CONFIG);
OPAL_CALL(opal_xive_get_queue_info,		OPAL_XIVE_GET_QUEUE_INFO);
OPAL_CALL(opal_xive_set_queue_info,		OPAL_XIVE_SET_QUEUE_INFO);
OPAL_CALL(opal_xive_donate_page,		OPAL_XIVE_DONATE_PAGE);
OPAL_CALL(opal_xive_alloc_vp_block,		OPAL_XIVE_ALLOCATE_VP_BLOCK);
OPAL_CALL(opal_xive_free_vp_block,		OPAL_XIVE_FREE_VP_BLOCK);
OPAL_CALL(opal_xive_allocate_irq,		OPAL_XIVE_ALLOCATE_IRQ);
OPAL_CALL(opal_xive_free_irq,			OPAL_XIVE_FREE_IRQ);
OPAL_CALL(opal_xive_get_vp_info,		OPAL_XIVE_GET_VP_INFO);
OPAL_CALL(opal_xive_set_vp_info,		OPAL_XIVE_SET_VP_INFO);
OPAL_CALL(opal_xive_sync,			OPAL_XIVE_SYNC);
OPAL_CALL(opal_xive_dump,			OPAL_XIVE_DUMP);
OPAL_CALL(opal_signal_system_reset,		OPAL_SIGNAL_SYSTEM_RESET);
OPAL_CALL(opal_npu_init_context,		OPAL_NPU_INIT_CONTEXT);
OPAL_CALL(opal_npu_destroy_context,		OPAL_NPU_DESTROY_CONTEXT);
OPAL_CALL(opal_npu_map_lpar,			OPAL_NPU_MAP_LPAR);
OPAL_CALL(opal_imc_counters_init,		OPAL_IMC_COUNTERS_INIT);
OPAL_CALL(opal_imc_counters_start,		OPAL_IMC_COUNTERS_START);
OPAL_CALL(opal_imc_counters_stop,		OPAL_IMC_COUNTERS_STOP);
OPAL_CALL(opal_pci_set_p2p,			OPAL_PCI_SET_P2P);
OPAL_CALL(opal_get_powercap,			OPAL_GET_POWERCAP);
OPAL_CALL(opal_set_powercap,			OPAL_SET_POWERCAP);
OPAL_CALL(opal_get_power_shift_ratio,		OPAL_GET_POWER_SHIFT_RATIO);
OPAL_CALL(opal_set_power_shift_ratio,		OPAL_SET_POWER_SHIFT_RATIO);
OPAL_CALL(opal_sensor_group_clear,		OPAL_SENSOR_GROUP_CLEAR);
OPAL_CALL(opal_npu_spa_setup,			OPAL_NPU_SPA_SETUP);
OPAL_CALL(opal_npu_spa_clear_cache,		OPAL_NPU_SPA_CLEAR_CACHE);
OPAL_CALL(opal_npu_tl_set,			OPAL_NPU_TL_SET);
OPAL_CALL(opal_pci_get_pbcq_tunnel_bar,		OPAL_PCI_GET_PBCQ_TUNNEL_BAR);
OPAL_CALL(opal_pci_set_pbcq_tunnel_bar,		OPAL_PCI_SET_PBCQ_TUNNEL_BAR);
