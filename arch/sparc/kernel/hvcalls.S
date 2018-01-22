/* SPDX-License-Identifier: GPL-2.0 */
	/* %o0: devhandle
	 * %o1:	devino
	 *
	 * returns %o0: sysino
	 */
ENTRY(sun4v_devino_to_sysino)
	mov	HV_FAST_INTR_DEVINO2SYSINO, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o1, %o0
ENDPROC(sun4v_devino_to_sysino)

	/* %o0: sysino
	 *
	 * returns %o0: intr_enabled (HV_INTR_{DISABLED,ENABLED})
	 */
ENTRY(sun4v_intr_getenabled)
	mov	HV_FAST_INTR_GETENABLED, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o1, %o0
ENDPROC(sun4v_intr_getenabled)

	/* %o0: sysino
	 * %o1: intr_enabled (HV_INTR_{DISABLED,ENABLED})
	 */
ENTRY(sun4v_intr_setenabled)
	mov	HV_FAST_INTR_SETENABLED, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_intr_setenabled)

	/* %o0: sysino
	 *
	 * returns %o0: intr_state (HV_INTR_STATE_*)
	 */
ENTRY(sun4v_intr_getstate)
	mov	HV_FAST_INTR_GETSTATE, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o1, %o0
ENDPROC(sun4v_intr_getstate)

	/* %o0: sysino
	 * %o1: intr_state (HV_INTR_STATE_*)
	 */
ENTRY(sun4v_intr_setstate)
	mov	HV_FAST_INTR_SETSTATE, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_intr_setstate)

	/* %o0: sysino
	 *
	 * returns %o0: cpuid
	 */
ENTRY(sun4v_intr_gettarget)
	mov	HV_FAST_INTR_GETTARGET, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o1, %o0
ENDPROC(sun4v_intr_gettarget)

	/* %o0: sysino
	 * %o1: cpuid
	 */
ENTRY(sun4v_intr_settarget)
	mov	HV_FAST_INTR_SETTARGET, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_intr_settarget)

	/* %o0:	cpuid
	 * %o1: pc
	 * %o2:	rtba
	 * %o3:	arg0
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_cpu_start)
	mov	HV_FAST_CPU_START, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_cpu_start)

	/* %o0:	cpuid
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_cpu_stop)
	mov	HV_FAST_CPU_STOP, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_cpu_stop)

	/* returns %o0:	status  */
ENTRY(sun4v_cpu_yield)
	mov	HV_FAST_CPU_YIELD, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_cpu_yield)

	/* %o0: cpuid
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_cpu_poke)
	mov     HV_FAST_CPU_POKE, %o5
	ta      HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_cpu_poke)

	/* %o0:	type
	 * %o1:	queue paddr
	 * %o2:	num queue entries
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_cpu_qconf)
	mov	HV_FAST_CPU_QCONF, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_cpu_qconf)

	/* %o0:	num cpus in cpu list
	 * %o1:	cpu list paddr
	 * %o2:	mondo block paddr
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_cpu_mondo_send)
	mov	HV_FAST_CPU_MONDO_SEND, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_cpu_mondo_send)

	/* %o0:	CPU ID
	 *
	 * returns %o0:	-status if status non-zero, else
	 *         %o0:	cpu state as HV_CPU_STATE_*
	 */
ENTRY(sun4v_cpu_state)
	mov	HV_FAST_CPU_STATE, %o5
	ta	HV_FAST_TRAP
	brnz,pn	%o0, 1f
	 sub	%g0, %o0, %o0
	mov	%o1, %o0
1:	retl
	 nop
ENDPROC(sun4v_cpu_state)

	/* %o0: virtual address
	 * %o1: must be zero
	 * %o2: TTE
	 * %o3: HV_MMU_* flags
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_mmu_map_perm_addr)
	mov	HV_FAST_MMU_MAP_PERM_ADDR, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_mmu_map_perm_addr)

	/* %o0: number of TSB descriptions
	 * %o1: TSB descriptions real address
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_mmu_tsb_ctx0)
	mov	HV_FAST_MMU_TSB_CTX0, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_mmu_tsb_ctx0)

	/* %o0:	API group number
	 * %o1: pointer to unsigned long major number storage
	 * %o2: pointer to unsigned long minor number storage
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_get_version)
	mov	HV_CORE_GET_VER, %o5
	mov	%o1, %o3
	mov	%o2, %o4
	ta	HV_CORE_TRAP
	stx	%o1, [%o3]
	retl
	 stx	%o2, [%o4]
ENDPROC(sun4v_get_version)

	/* %o0: API group number
	 * %o1: desired major number
	 * %o2: desired minor number
	 * %o3: pointer to unsigned long actual minor number storage
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_set_version)
	mov	HV_CORE_SET_VER, %o5
	mov	%o3, %o4
	ta	HV_CORE_TRAP
	retl
	 stx	%o1, [%o4]
ENDPROC(sun4v_set_version)

	/* %o0: pointer to unsigned long time
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_tod_get)
	mov	%o0, %o4
	mov	HV_FAST_TOD_GET, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
ENDPROC(sun4v_tod_get)

	/* %o0: time
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_tod_set)
	mov	HV_FAST_TOD_SET, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_tod_set)

	/* %o0: pointer to unsigned long status
	 *
	 * returns %o0: signed character
	 */
ENTRY(sun4v_con_getchar)
	mov	%o0, %o4
	mov	HV_FAST_CONS_GETCHAR, %o5
	clr	%o0
	clr	%o1
	ta	HV_FAST_TRAP
	stx	%o0, [%o4]
	retl
	 sra	%o1, 0, %o0
ENDPROC(sun4v_con_getchar)

	/* %o0: signed long character
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_con_putchar)
	mov	HV_FAST_CONS_PUTCHAR, %o5
	ta	HV_FAST_TRAP
	retl
	 sra	%o0, 0, %o0
ENDPROC(sun4v_con_putchar)

	/* %o0: buffer real address
	 * %o1: buffer size
	 * %o2: pointer to unsigned long bytes_read
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_con_read)
	mov	%o2, %o4
	mov	HV_FAST_CONS_READ, %o5
	ta	HV_FAST_TRAP
	brnz	%o0, 1f
	 cmp	%o1, -1		/* break */
	be,a,pn	%icc, 1f
	 mov	%o1, %o0
	cmp	%o1, -2		/* hup */
	be,a,pn	%icc, 1f
	 mov	%o1, %o0
	stx	%o1, [%o4]
1:	retl
	 nop
ENDPROC(sun4v_con_read)

	/* %o0: buffer real address
	 * %o1: buffer size
	 * %o2: pointer to unsigned long bytes_written
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_con_write)
	mov	%o2, %o4
	mov	HV_FAST_CONS_WRITE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
ENDPROC(sun4v_con_write)

	/* %o0:	soft state
	 * %o1:	address of description string
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_mach_set_soft_state)
	mov	HV_FAST_MACH_SET_SOFT_STATE, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_mach_set_soft_state)

	/* %o0: exit code
	 *
	 * Does not return.
	 */
ENTRY(sun4v_mach_exit)
	mov	HV_FAST_MACH_EXIT, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_mach_exit)

	/* %o0: buffer real address
	 * %o1: buffer length
	 * %o2: pointer to unsigned long real_buf_len
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_mach_desc)
	mov	%o2, %o4
	mov	HV_FAST_MACH_DESC, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
ENDPROC(sun4v_mach_desc)

	/* %o0: new timeout in milliseconds
	 * %o1: pointer to unsigned long orig_timeout
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_mach_set_watchdog)
	mov	%o1, %o4
	mov	HV_FAST_MACH_SET_WATCHDOG, %o5
	ta	HV_FAST_TRAP
	brnz,a,pn %o4, 0f
	stx	%o1, [%o4]
0:	retl
	 nop
ENDPROC(sun4v_mach_set_watchdog)
EXPORT_SYMBOL(sun4v_mach_set_watchdog)

	/* No inputs and does not return.  */
ENTRY(sun4v_mach_sir)
	mov	%o1, %o4
	mov	HV_FAST_MACH_SIR, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
ENDPROC(sun4v_mach_sir)

	/* %o0: channel
	 * %o1:	ra
	 * %o2:	num_entries
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_ldc_tx_qconf)
	mov	HV_FAST_LDC_TX_QCONF, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_ldc_tx_qconf)

	/* %o0: channel
	 * %o1:	pointer to unsigned long ra
	 * %o2:	pointer to unsigned long num_entries
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_ldc_tx_qinfo)
	mov	%o1, %g1
	mov	%o2, %g2
	mov	HV_FAST_LDC_TX_QINFO, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	stx	%o2, [%g2]
	retl
	 nop
ENDPROC(sun4v_ldc_tx_qinfo)

	/* %o0: channel
	 * %o1:	pointer to unsigned long head_off
	 * %o2:	pointer to unsigned long tail_off
	 * %o2:	pointer to unsigned long chan_state
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_ldc_tx_get_state)
	mov	%o1, %g1
	mov	%o2, %g2
	mov	%o3, %g3
	mov	HV_FAST_LDC_TX_GET_STATE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	stx	%o2, [%g2]
	stx	%o3, [%g3]
	retl
	 nop
ENDPROC(sun4v_ldc_tx_get_state)

	/* %o0: channel
	 * %o1:	tail_off
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_ldc_tx_set_qtail)
	mov	HV_FAST_LDC_TX_SET_QTAIL, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_ldc_tx_set_qtail)

	/* %o0: channel
	 * %o1:	ra
	 * %o2:	num_entries
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_ldc_rx_qconf)
	mov	HV_FAST_LDC_RX_QCONF, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_ldc_rx_qconf)

	/* %o0: channel
	 * %o1:	pointer to unsigned long ra
	 * %o2:	pointer to unsigned long num_entries
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_ldc_rx_qinfo)
	mov	%o1, %g1
	mov	%o2, %g2
	mov	HV_FAST_LDC_RX_QINFO, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	stx	%o2, [%g2]
	retl
	 nop
ENDPROC(sun4v_ldc_rx_qinfo)

	/* %o0: channel
	 * %o1:	pointer to unsigned long head_off
	 * %o2:	pointer to unsigned long tail_off
	 * %o2:	pointer to unsigned long chan_state
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_ldc_rx_get_state)
	mov	%o1, %g1
	mov	%o2, %g2
	mov	%o3, %g3
	mov	HV_FAST_LDC_RX_GET_STATE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	stx	%o2, [%g2]
	stx	%o3, [%g3]
	retl
	 nop
ENDPROC(sun4v_ldc_rx_get_state)

	/* %o0: channel
	 * %o1:	head_off
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_ldc_rx_set_qhead)
	mov	HV_FAST_LDC_RX_SET_QHEAD, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_ldc_rx_set_qhead)

	/* %o0: channel
	 * %o1:	ra
	 * %o2:	num_entries
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_ldc_set_map_table)
	mov	HV_FAST_LDC_SET_MAP_TABLE, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_ldc_set_map_table)

	/* %o0: channel
	 * %o1:	pointer to unsigned long ra
	 * %o2:	pointer to unsigned long num_entries
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_ldc_get_map_table)
	mov	%o1, %g1
	mov	%o2, %g2
	mov	HV_FAST_LDC_GET_MAP_TABLE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	stx	%o2, [%g2]
	retl
	 nop
ENDPROC(sun4v_ldc_get_map_table)

	/* %o0:	channel
	 * %o1:	dir_code
	 * %o2:	tgt_raddr
	 * %o3:	lcl_raddr
	 * %o4:	len
	 * %o5:	pointer to unsigned long actual_len
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_ldc_copy)
	mov	%o5, %g1
	mov	HV_FAST_LDC_COPY, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	retl
	 nop
ENDPROC(sun4v_ldc_copy)

	/* %o0:	channel
	 * %o1:	cookie
	 * %o2:	pointer to unsigned long ra
	 * %o3:	pointer to unsigned long perm
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_ldc_mapin)
	mov	%o2, %g1
	mov	%o3, %g2
	mov	HV_FAST_LDC_MAPIN, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	stx	%o2, [%g2]
	retl
	 nop
ENDPROC(sun4v_ldc_mapin)

	/* %o0:	ra
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_ldc_unmap)
	mov	HV_FAST_LDC_UNMAP, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_ldc_unmap)

	/* %o0: channel
	 * %o1:	cookie
	 * %o2:	mte_cookie
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_ldc_revoke)
	mov	HV_FAST_LDC_REVOKE, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_ldc_revoke)

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	pointer to unsigned long cookie
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_vintr_get_cookie)
	mov	%o2, %g1
	mov	HV_FAST_VINTR_GET_COOKIE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	retl
	 nop
ENDPROC(sun4v_vintr_get_cookie)

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	cookie
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_vintr_set_cookie)
	mov	HV_FAST_VINTR_SET_COOKIE, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_vintr_set_cookie)

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	pointer to unsigned long valid_state
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_vintr_get_valid)
	mov	%o2, %g1
	mov	HV_FAST_VINTR_GET_VALID, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	retl
	 nop
ENDPROC(sun4v_vintr_get_valid)

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	valid_state
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_vintr_set_valid)
	mov	HV_FAST_VINTR_SET_VALID, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_vintr_set_valid)

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	pointer to unsigned long state
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_vintr_get_state)
	mov	%o2, %g1
	mov	HV_FAST_VINTR_GET_STATE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	retl
	 nop
ENDPROC(sun4v_vintr_get_state)

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	state
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_vintr_set_state)
	mov	HV_FAST_VINTR_SET_STATE, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_vintr_set_state)

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	pointer to unsigned long cpuid
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_vintr_get_target)
	mov	%o2, %g1
	mov	HV_FAST_VINTR_GET_TARGET, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	retl
	 nop
ENDPROC(sun4v_vintr_get_target)

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	cpuid
	 *
	 * returns %o0: status
	 */
ENTRY(sun4v_vintr_set_target)
	mov	HV_FAST_VINTR_SET_TARGET, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_vintr_set_target)

	/* %o0: NCS sub-function
	 * %o1:	sub-function arg real-address
	 * %o2:	sub-function arg size
	 *
	 * returns %o0:	status
	 */
ENTRY(sun4v_ncs_request)
	mov	HV_FAST_NCS_REQUEST, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_ncs_request)

ENTRY(sun4v_svc_send)
	save	%sp, -192, %sp
	mov	%i0, %o0
	mov	%i1, %o1
	mov	%i2, %o2
	mov	HV_FAST_SVC_SEND, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%i3]
	ret
	restore
ENDPROC(sun4v_svc_send)

ENTRY(sun4v_svc_recv)
	save	%sp, -192, %sp
	mov	%i0, %o0
	mov	%i1, %o1
	mov	%i2, %o2
	mov	HV_FAST_SVC_RECV, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%i3]
	ret
	restore
ENDPROC(sun4v_svc_recv)

ENTRY(sun4v_svc_getstatus)
	mov	HV_FAST_SVC_GETSTATUS, %o5
	mov	%o1, %o4
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
ENDPROC(sun4v_svc_getstatus)

ENTRY(sun4v_svc_setstatus)
	mov	HV_FAST_SVC_SETSTATUS, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_svc_setstatus)

ENTRY(sun4v_svc_clrstatus)
	mov	HV_FAST_SVC_CLRSTATUS, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_svc_clrstatus)

ENTRY(sun4v_mmustat_conf)
	mov	%o1, %o4
	mov	HV_FAST_MMUSTAT_CONF, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
ENDPROC(sun4v_mmustat_conf)

ENTRY(sun4v_mmustat_info)
	mov	%o0, %o4
	mov	HV_FAST_MMUSTAT_INFO, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
ENDPROC(sun4v_mmustat_info)

ENTRY(sun4v_mmu_demap_all)
	clr	%o0
	clr	%o1
	mov	HV_MMU_ALL, %o2
	mov	HV_FAST_MMU_DEMAP_ALL, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_mmu_demap_all)

ENTRY(sun4v_niagara_getperf)
	mov	%o0, %o4
	mov	HV_FAST_GET_PERFREG, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
ENDPROC(sun4v_niagara_getperf)
EXPORT_SYMBOL(sun4v_niagara_getperf)

ENTRY(sun4v_niagara_setperf)
	mov	HV_FAST_SET_PERFREG, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_niagara_setperf)
EXPORT_SYMBOL(sun4v_niagara_setperf)

ENTRY(sun4v_niagara2_getperf)
	mov	%o0, %o4
	mov	HV_FAST_N2_GET_PERFREG, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
ENDPROC(sun4v_niagara2_getperf)
EXPORT_SYMBOL(sun4v_niagara2_getperf)

ENTRY(sun4v_niagara2_setperf)
	mov	HV_FAST_N2_SET_PERFREG, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_niagara2_setperf)
EXPORT_SYMBOL(sun4v_niagara2_setperf)

ENTRY(sun4v_reboot_data_set)
	mov	HV_FAST_REBOOT_DATA_SET, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_reboot_data_set)

ENTRY(sun4v_vt_get_perfreg)
	mov	%o1, %o4
	mov	HV_FAST_VT_GET_PERFREG, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
ENDPROC(sun4v_vt_get_perfreg)

ENTRY(sun4v_vt_set_perfreg)
	mov	HV_FAST_VT_SET_PERFREG, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_vt_set_perfreg)

ENTRY(sun4v_t5_get_perfreg)
	mov	%o1, %o4
	mov	HV_FAST_T5_GET_PERFREG, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
ENDPROC(sun4v_t5_get_perfreg)

ENTRY(sun4v_t5_set_perfreg)
	mov	HV_FAST_T5_SET_PERFREG, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
ENDPROC(sun4v_t5_set_perfreg)

ENTRY(sun4v_m7_get_perfreg)
	mov	%o1, %o4
	mov	HV_FAST_M7_GET_PERFREG, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	nop
ENDPROC(sun4v_m7_get_perfreg)

ENTRY(sun4v_m7_set_perfreg)
	mov	HV_FAST_M7_SET_PERFREG, %o5
	ta	HV_FAST_TRAP
	retl
	nop
ENDPROC(sun4v_m7_set_perfreg)

	/* %o0: address of CCB array
	 * %o1: size (in bytes) of CCB array
	 * %o2: flags
	 * %o3: reserved
	 *
	 * returns:
	 * %o0: status
	 * %o1: size (in bytes) of the CCB array that was accepted
	 * %o2: status data
	 * %o3: reserved
	 */
ENTRY(sun4v_ccb_submit)
	mov	%o5, %g1
	mov	HV_CCB_SUBMIT, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 stx	%o2, [%g1]
ENDPROC(sun4v_ccb_submit)
EXPORT_SYMBOL(sun4v_ccb_submit)

	/* %o0: completion area ra for the ccb to get info
	 *
	 * returns:
	 * %o0: status
	 * %o1: CCB state
	 * %o2: position
	 * %o3: dax unit
	 * %o4: queue
	 */
ENTRY(sun4v_ccb_info)
	mov	%o1, %g1
	mov	HV_CCB_INFO, %o5
	ta	HV_FAST_TRAP
	sth	%o1, [%g1 + CCB_INFO_OFFSET_CCB_STATE]
	sth	%o2, [%g1 + CCB_INFO_OFFSET_QUEUE_POS]
	sth	%o3, [%g1 + CCB_INFO_OFFSET_DAX_UNIT]
	retl
	 sth	%o4, [%g1 + CCB_INFO_OFFSET_QUEUE_NUM]
ENDPROC(sun4v_ccb_info)
EXPORT_SYMBOL(sun4v_ccb_info)

	/* %o0: completion area ra for the ccb to kill
	 *
	 * returns:
	 * %o0: status
	 * %o1: result of the kill
	 */
ENTRY(sun4v_ccb_kill)
	mov	%o1, %g1
	mov	HV_CCB_KILL, %o5
	ta	HV_FAST_TRAP
	retl
	 sth	%o1, [%g1]
ENDPROC(sun4v_ccb_kill)
EXPORT_SYMBOL(sun4v_ccb_kill)
